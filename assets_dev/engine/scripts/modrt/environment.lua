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

-- Mods address files relative to the original game folder; here that means a mod root or the Merge base.
function Env.resolvePath(path)
    path = normalizeSlashes(path)

    if isAbsolute(path) then
        return modrt.fileExists(path) and path or nil
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
-- files: reads resolve against the mod roots, writes are refused for now and reported

local realOpen = io.open
local realLines = io.lines

function io.open(path, mode)
    mode = mode or "r"

    if mode:find("[wa+]") then
        noteFileAccess(path, mode, "refused: writes are disabled")
        return nil, tostring(path) .. ": writes are disabled in the mod runtime"
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

local function refuseFileChange(name)
    return function(path)
        noteFileAccess(path, name, "refused: writes are disabled")
        return nil, tostring(path) .. ": file changes are disabled in the mod runtime"
    end
end

os.remove = refuseFileChange("os.remove")
os.rename = refuseFileChange("os.rename")

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

Stubs.installRoot("Game")
Stubs.installRoot("Party")
Stubs.installRoot("Map")
Stubs.installRoot("Mouse")
Stubs.installRoot("Keys")
Stubs.installRoot("Screens")
Stubs.installRoot("structs")
Stubs.installRoot("mem")
Stubs.installRoot("internal")
Stubs.installRoot("CustomUI")
Stubs.installRoot("Merge")
Stubs.installRoot("Editor")
Stubs.installRoot("Multiplayer", {in_game = false})
Stubs.installRoot("evt", {
    global = Events.new("evt.global"),
    map = Events.new("evt.map"),
    hint = {},
    house = {},
    str = {},
})

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

    Stubs.watchUndefinedGlobals()
end
