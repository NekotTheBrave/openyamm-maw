-- The global environment mods see: the MMExtension names they expect, backed by real implementations where the
-- runtime has one and by recording stand-ins (stubs.lua) everywhere else.

local ModRT = ModRT
local modrt = modrt
local Stubs = ModRT.Stubs
local Events = ModRT.Events
local Env = {}
ModRT.Env = Env

Env.fileAccess = {}

local config = modrt.config
local searchRoots = {}

for _, mod in ipairs(config.mods) do
    searchRoots[#searchRoots + 1] = mod.path
end

if config.mergeBase ~= "" then
    searchRoots[#searchRoots + 1] = config.mergeBase
end

local function normalizeSlashes(path)
    return (tostring(path):gsub("\\", "/"))
end

local function isAbsolute(path)
    return path:match("^%a:/") ~= nil or path:sub(1, 1) == "/"
end

-- Where a file a mod wants to create or change goes: the mods' data folder, never the mod or the game itself.
-- Only paths relative to the game folder are accepted.
function Env.writablePath(path)
    path = normalizeSlashes(path)

    if isAbsolute(path) then
        local appRoot = normalizeSlashes(AppPath):lower()

        if path:sub(1, #appRoot):lower() ~= appRoot then
            return nil
        end

        path = path:sub(#appRoot + 1)
    end

    return modrt.dataPath(path)
end

-- Mods address files relative to the original game folder; here that means the data folder (files the mod wrote
-- itself), then a mod root, then the Merge base.
function Env.resolvePath(path)
    path = normalizeSlashes(path)

    if isAbsolute(path) then
        local port = ModRT.Loader and ModRT.Loader.portPathForFile(path)

        if port then
            return port
        end

        return modrt.fileExists(path) and path or nil
    end

    local written = modrt.dataPath(path)

    if written and modrt.fileExists(written) then
        return written
    end

    for _, root in ipairs(searchRoots) do
        local candidate = root .. "/" .. path

        if modrt.fileExists(candidate) then
            return candidate
        end
    end

    return nil
end

local function noteFileAccess(path, mode, outcome)
    local key = normalizeSlashes(path) .. " [" .. mode .. "] " .. outcome
    Env.fileAccess[key] = (Env.fileAccess[key] or 0) + 1
end

---------------------------------------------------------------------------------------------------------------------
-- output

function print(...)
    local parts = {}

    for index = 1, select("#", ...) do
        parts[index] = tostring((select(index, ...)))
    end

    local line = table.concat(parts, "\t")
    ModRT.printLines[#ModRT.printLines + 1] = line
    ModRT.log("print", line)
end

debug.Message = print

---------------------------------------------------------------------------------------------------------------------
-- files: reads resolve against the data folder and the mod roots; writes go to the data folder only

local realOpen = io.open
local realLines = io.lines
local realRemove = os.remove
local realRename = os.rename

function io.open(path, mode)
    mode = mode or "r"

    if mode:find("[wa+]") then
        local target = Env.writablePath(path)

        if not target then
            noteFileAccess(path, mode, "refused: outside the game folder")
            return nil, tostring(path) .. ": mods may only write inside the game folder"
        end

        noteFileAccess(path, mode, "ok (data folder)")
        return realOpen(target, mode)
    end

    local resolved = Env.resolvePath(path)

    if not resolved then
        noteFileAccess(path, mode, "not found")
        return nil, tostring(path) .. ": No such file or directory", 2
    end

    noteFileAccess(path, mode, "ok")
    return realOpen(resolved, mode)
end

function io.lines(path, ...)
    if path == nil then
        return realLines()
    end

    local resolved = Env.resolvePath(path)

    if not resolved then
        noteFileAccess(path, "lines", "not found")
        error(tostring(path) .. ": No such file or directory", 2)
    end

    noteFileAccess(path, "lines", "ok")
    return realLines(resolved, ...)
end

function os.remove(path)
    local target = Env.writablePath(path)

    if not target then
        noteFileAccess(path, "os.remove", "refused: outside the game folder")
        return nil, tostring(path) .. ": mods may only change files inside the game folder"
    end

    noteFileAccess(path, "os.remove", "ok (data folder)")
    return realRemove(target)
end

function os.rename(from, to)
    local source, target = Env.writablePath(from), Env.writablePath(to)

    if not source or not target then
        noteFileAccess(from, "os.rename", "refused: outside the game folder")
        return nil, tostring(from) .. ": mods may only change files inside the game folder"
    end

    noteFileAccess(from, "os.rename", "ok (data folder)")
    realRemove(target)
    return realRename(source, target)
end

function loadfile(path)
    local resolved = Env.resolvePath(path)

    if not resolved then
        noteFileAccess(path, "loadfile", "not found")
        return nil, "cannot open " .. tostring(path)
    end

    local source, readError = modrt.readFile(resolved)

    if not source then
        return nil, readError
    end

    noteFileAccess(path, "loadfile", "ok")
    return load(ModRT.Loader.prepareSource(source), "@" .. ModRT.Loader.displayName(resolved))
end

function dofile(path, ...)
    local chunk, loadError = loadfile(path)

    if not chunk then
        error(loadError, 2)
    end

    ModRT.Loader.noteLoaded(path, "dofile")
    return chunk(...)
end

---------------------------------------------------------------------------------------------------------------------
-- MMExtension globals

local firstRoot = config.mods[1] and config.mods[1].path or ""
AppPath = firstRoot:gsub("/", "\\") .. "\\"
DevPath = AppPath
path = path or {}
offsets = {MMVersion = 8}

events = Events.new("events")
vars = {}
mapvars = {}

-- The `settings:` of every enabled mod in mods.yml, as strings.
ModSettings = {}

for _, mod in ipairs(config.mods) do
    for name, value in pairs(mod.settings or {}) do
        ModSettings[name] = value
    end
end

Stubs.installRoot("Game")
Stubs.installRoot("Party")
Stubs.installRoot("Map")
Stubs.installRoot("Mouse")
Stubs.installRoot("Keys")
Stubs.installRoot("Screens")
Stubs.installRoot("structs")
Stubs.installRoot("internal")
-- Helpers for patching the original executable: callable, recorded, without effect (Merge's quests are
-- modrt/quests.lua).
Stubs.installRoot("HookManager")

-- Milliseconds of real time, as the Windows call of the same name.
function timeGetTime()
    return math.floor(modrt.clock() * 1000)
end

-- Text markup of the original engine: 16-bit colour, column alignment.
function RGB(r, g, b)
    -- 16-bit colour, five bits of red, six of green, five of blue: the low bits of each go first.
    r, g, b = math.floor(r) & 0xF8, math.floor(g) & 0xFC, math.floor(b) & 0xF8
    return (b + 0x40 * (g + 0x20 * r)) // 8
end

function StrLeft(value)
    return ("\t%.3d"):format(value)
end

function StrRight(value)
    return ("\r%.3d"):format(value)
end

function StrColor(r, g, b, text)
    local color = b and RGB(r, g, b) or r
    local body = text or (not b and g) or nil
    return ("\f%.5d"):format(color) .. (body and (body .. StrColor(0)) or "")
end
Stubs.installRoot("CustomUI")
Stubs.installRoot("Merge")
Stubs.installRoot("Editor")
Stubs.installRoot("Multiplayer", {in_game = false})
do
    local globalEvents = Events.new("evt.global")
    local mapEvents = Events.new("evt.map")

    Stubs.installRoot("evt", {
        global = globalEvents,
        Global = globalEvents,
        map = mapEvents,
        Map = mapEvents,
        hint = {},
        house = {},
        str = {},
        -- evt.PotionEffects[potion] = function(isDrunk, target, power, ...): a potion's own effect, by its number in
        -- the potion settings table (MAW's gems and crafting items, used on an item in the backpack).
        PotionEffects = {},
    })
end

-- MMExtension's debug console is a Lua function whose code compiler scripts reach through its upvalues (to wrap
-- or log what is typed). The same shape is provided so that code finds what it looks for.
do
    local loadstring = loadstring

    local function DoDebug(code, ...)
        local chunk, loadError = loadstring(code, ...)

        if not chunk then
            return nil, loadError
        end

        return chunk()
    end

    function debug.debug(code, ...)
        return DoDebug(code, ...)
    end
end

-- Helper library from MMExtension's core (table.copy, string.split, path.*): plain Lua, so it is loaded as is from
-- the user's own Merge install rather than reimplemented here.
function Env.loadCoreHelper(relativePath)
    if config.mergeBase == "" then
        return false
    end

    local fullPath = config.mergeBase .. "/Scripts/Core/" .. relativePath
    local source = modrt.readFile(fullPath)

    if not source then
        ModRT.log("warn", "core helper not found: " .. fullPath)
        return false
    end

    local chunk, loadError = load(ModRT.Loader.prepareSource(source), "@mmext:Core/" .. relativePath)

    if not chunk then
        ModRT.recordError("core helper " .. relativePath, loadError)
        return false
    end

    return (ModRT.protectedCall(chunk, "core helper " .. relativePath))
end

function Env.finish()
    Env.loadCoreHelper("RSFunctions.lua")
    math.round = math.round or function(value) return math.floor(value + 0.5) end

    if not Env.loadCoreHelper("ConstAndBits.lua") or rawget(_G, "const") == nil then
        Stubs.installRoot("const")
    end

    ModRT.Bindings.ensureConstants()
    Stubs.watchUndefinedGlobals()
end
