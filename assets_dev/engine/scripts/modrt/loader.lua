-- Loads mod scripts in the order MMExtension does, so a mod's file-name ordering keeps its meaning:
--   startup:  Structs/*.lua, Structs/After/*.lua, General/*.lua
--   session:  Global/*.lua (each new game or load)
--   map:      Maps/<map>.lua (each time the party enters the map)
-- Files a mod ships only to override the Merge base are skipped by default: OpenYAMM implements the base natively.

local ModRT = ModRT
local modrt = modrt
local Loader = {}
ModRT.Loader = Loader

Loader.files = {}

local config = modrt.config
local fileIndex = {}

local function normalizeSlashes(path)
    return (tostring(path):gsub("\\", "/"))
end

function Loader.prepareSource(source)
    -- UTF-8 byte order mark
    if source:sub(1, 3) == "\239\187\191" then
        source = source:sub(4)
    end

    return source
end

function Loader.displayName(fullPath)
    fullPath = normalizeSlashes(fullPath)

    for _, mod in ipairs(config.mods) do
        local prefix = mod.path .. "/Scripts/"

        if fullPath:sub(1, #prefix):lower() == prefix:lower() then
            return mod.id .. ":" .. fullPath:sub(#prefix + 1)
        end
    end

    return fullPath
end

local function noteFile(displayName, status, detail)
    local entry = fileIndex[displayName]

    if not entry then
        entry = {name = displayName}
        fileIndex[displayName] = entry
        Loader.files[#Loader.files + 1] = entry
    end

    entry.status = status
    entry.detail = detail
    return entry
end

function Loader.noteLoaded(path, how)
    local resolved = ModRT.Env.resolvePath(path)
    noteFile(Loader.displayName(resolved or path), "ok", how)
end

local function listLuaFiles(directory)
    local names = {}

    for _, entry in ipairs(modrt.listDirectory(directory)) do
        if not entry.isDirectory and entry.name:lower():sub(-4) == ".lua" then
            names[#names + 1] = entry.name
        end
    end

    -- MMExtension runs a folder's scripts sorted by lower-cased name, and mods rely on it: lower-cased, '_' sorts
    -- before every letter, so "zz_Maw-initialize.lua" runs ahead of "zzAlchemy.lua". (Upper-cased it would run last.)
    table.sort(names, function(left, right)
        local lowerLeft, lowerRight = left:lower(), right:lower()

        if lowerLeft == lowerRight then
            return left < right
        end

        return lowerLeft < lowerRight
    end)

    return names
end

local function isSkipped(mod, relativePath)
    local lowered = relativePath:lower()

    for _, pattern in ipairs(mod.skip) do
        local loweredPattern = normalizeSlashes(pattern):lower()

        if lowered == loweredPattern or (loweredPattern:sub(-1) == "/" and lowered:sub(1, #loweredPattern) == loweredPattern) then
            return "listed under skip in mods.yml"
        end
    end

    if mod.baseOverrides ~= "load" and config.mergeBase ~= "" then
        if modrt.fileExists(config.mergeBase .. "/Scripts/" .. relativePath) then
            return "overrides a Merge base file (native in OpenYAMM)"
        end
    end

    return nil
end

-- The port folder's version of a mod script, if there is one.
function Loader.portPath(mod, relativePath)
    if mod.port and mod.port ~= "" then
        local candidate = mod.port .. "/Scripts/" .. relativePath

        if modrt.fileExists(candidate) then
            return candidate
        end
    end

    return nil
end

-- Maps a path inside a mod's Scripts folder to its port version, so dofile and require pick ports up too.
function Loader.portPathForFile(fullPath)
    fullPath = normalizeSlashes(fullPath)

    for _, mod in ipairs(config.mods) do
        local prefix = mod.path .. "/Scripts/"

        if fullPath:sub(1, #prefix):lower() == prefix:lower() then
            return Loader.portPath(mod, fullPath:sub(#prefix + 1))
        end
    end

    return nil
end

local function runFile(mod, relativePath)
    local displayName = mod.id .. ":" .. relativePath
    local portPath = Loader.portPath(mod, relativePath)
    local skipReason = not portPath and isSkipped(mod, relativePath) or nil

    if skipReason then
        noteFile(displayName, "skipped", skipReason)
        return
    end

    local source, readError = modrt.readFile(portPath or (mod.path .. "/Scripts/" .. relativePath))

    if portPath then
        displayName = mod.id .. "+port:" .. relativePath
    end

    if not source then
        noteFile(displayName, "error", "cannot read: " .. tostring(readError))
        return
    end

    local chunk, loadError = load(Loader.prepareSource(source), "@" .. displayName)

    if not chunk then
        noteFile(displayName, "error", "does not compile: " .. tostring(loadError))
        ModRT.recordError("compile " .. displayName, loadError)
        return
    end

    local errorsBefore = ModRT.totalErrorCount
    local ok = ModRT.protectedCall(chunk, "load " .. displayName)

    if ok then
        noteFile(displayName, "ok")
    else
        local last = ModRT.errorOrder[#ModRT.errorOrder]
        local detail = ModRT.totalErrorCount ~= errorsBefore and last and last.message or "failed"
        noteFile(displayName, "error", "stopped while loading: " .. detail)
    end
end

local function runFolder(folder)
    for _, mod in ipairs(config.mods) do
        local names = listLuaFiles(mod.path .. "/Scripts/" .. folder)

        -- Scripts that exist only in the port folder join the same sorted order.
        if mod.port and mod.port ~= "" then
            local present = {}

            for _, name in ipairs(names) do
                present[name:lower()] = true
            end

            for _, name in ipairs(listLuaFiles(mod.port .. "/Scripts/" .. folder)) do
                if not present[name:lower()] then
                    names[#names + 1] = name
                end
            end

            table.sort(names, function(left, right)
                local lowerLeft, lowerRight = left:lower(), right:lower()
                return lowerLeft == lowerRight and left < right or lowerLeft < lowerRight
            end)
        end

        for _, name in ipairs(names) do
            runFile(mod, folder .. "/" .. name)
        end
    end
end

-- `require "Name"` looks in each mod's Scripts/Modules, as MMExtension's package.path does.
local function installModuleSearcher()
    table.insert(package.searchers, 2, function(moduleName)
        local relativePath = "Modules/" .. moduleName:gsub("%.", "/") .. ".lua"

        for _, mod in ipairs(config.mods) do
            local portPath = Loader.portPath(mod, relativePath)
            local fullPath = portPath or (mod.path .. "/Scripts/" .. relativePath)

            if modrt.fileExists(fullPath) then
                local displayName = mod.id .. (portPath and "+port:" or ":") .. relativePath
                local source = modrt.readFile(fullPath)
                local chunk, loadError = load(Loader.prepareSource(source), "@" .. displayName)

                if not chunk then
                    noteFile(displayName, "error", "does not compile: " .. tostring(loadError))
                    error(loadError, 0)
                end

                noteFile(displayName, "ok", "require")
                return chunk, fullPath
            end
        end

        return "\n\tno file '" .. relativePath .. "' in any mod"
    end)
end

-- Works out which fields of the engine's root objects are the mods' own (see Bindings.ownedFields): those a mod's
-- script assigns somewhere, unless the field is one of the original engine's. The original's fields are read
-- from the structure definitions of the Merge base and of the mods (function structs.f.GameStructure and so on);
-- a field of the original that is not bound here stays a stand-in. Runs before any script does.
local structureOfRoot = {Game = "GameStructure", Party = "GameParty", Map = "GameMap", Mouse = "GameMouse"}

local function eachLuaFile(directory, depth, visit)
    for _, entry in ipairs(modrt.listDirectory(directory)) do
        local path = directory .. "/" .. entry.name

        if entry.isDirectory then
            if depth < 6 then
                eachLuaFile(path, depth + 1, visit)
            end
        elseif entry.name:lower():sub(-4) == ".lua" then
            visit(path)
        end
    end
end

local function scanOwnedFields()
    local ownedFields = ModRT.Bindings and ModRT.Bindings.ownedFields

    if not ownedFields then
        return
    end

    -- the original engine's fields, by root
    local engineFields = {}

    for root in pairs(ownedFields) do
        engineFields[root] = {}
    end

    local function scanStructures(path)
        local source = modrt.readFile(path)

        if not source then
            return
        end

        for root, structure in pairs(structureOfRoot) do
            local position = 1

            while true do
                local _, bodyStart = source:find("function structs%.f%." .. structure .. "%s*%(", position)

                if not bodyStart then
                    break
                end

                -- the definition ends at the first "end" in the first column
                local bodyEnd = source:find("\nend", bodyStart, true) or #source
                local body = source:sub(bodyStart, bodyEnd)

                for name in body:gmatch("'([%a_][%w_]*)'") do
                    engineFields[root][name] = true
                end

                for name in body:gmatch('"([%a_][%w_]*)"') do
                    engineFields[root][name] = true
                end

                position = bodyEnd
            end
        end
    end

    if config.mergeBase ~= "" then
        eachLuaFile(config.mergeBase .. "/Scripts/Structs", 0, scanStructures)
    end

    for _, mod in ipairs(config.mods) do
        eachLuaFile(mod.path .. "/Scripts/Structs", 0, scanStructures)
    end

    local count = 0

    local function scanAssignments(path)
        local source = modrt.readFile(path)

        if not source then
            return
        end

        for root, names in pairs(ownedFields) do
            -- "Game.Name =" but neither "Game.Name ==" nor "Game.Table.Field ="
            for name in source:gmatch("%f[%w_]" .. root .. "%.([%a_][%w_]*)%s*=[^=]") do
                if not names[name] and not engineFields[root][name] then
                    names[name] = true
                    count = count + 1
                end
            end
        end
    end

    for _, mod in ipairs(config.mods) do
        local scriptsRoot = mod.path .. "/Scripts/"

        -- A file that is not going to run (a Merge base file the mod overrides, a file under `skip`) sets nothing.
        eachLuaFile(mod.path .. "/Scripts", 0, function(path)
            if not isSkipped(mod, path:sub(#scriptsRoot + 1)) then
                scanAssignments(path)
            end
        end)

        if mod.port and mod.port ~= "" then
            eachLuaFile(mod.port .. "/Scripts", 0, scanAssignments)
        end
    end

    local listed = {}

    for root, names in pairs(ownedFields) do
        for name in pairs(names) do
            listed[#listed + 1] = root .. "." .. name
        end
    end

    table.sort(listed)
    ModRT.log("info", "fields the mods keep on the engine's objects (" .. count .. "): " .. table.concat(listed, " "))
end

function Loader.loadStartupScripts()
    installModuleSearcher()
    ModRT.Env.finish()
    scanOwnedFields()
    runFolder("Structs")
    events.cocalls("StructsLoaded")
    runFolder("Structs/After")
    runFolder("General")
    ModRT.log("info", "startup scripts loaded: " .. #Loader.files .. " files seen")
end

-- Runs the scripts of the map the party enters (Maps/<map name>.lua, any case). What they register belongs to the
-- map and goes when the party leaves it.
function Loader.loadMapScripts(mapName)
    local wanted = (mapName:gsub("%.%w+$", "") .. ".lua"):lower()
    ModRT.Events.scope = "map"

    for _, mod in ipairs(config.mods) do
        local found = nil

        for _, name in ipairs(listLuaFiles(mod.path .. "/Scripts/Maps")) do
            if name:lower() == wanted then
                found = name
            end
        end

        if not found and mod.port and mod.port ~= "" then
            for _, name in ipairs(listLuaFiles(mod.port .. "/Scripts/Maps")) do
                if name:lower() == wanted then
                    found = name
                end
            end
        end

        if found then
            runFile(mod, "Maps/" .. found)
        end
    end

    ModRT.Events.scope = "startup"
end

-- Runs the per-playthrough scripts. Called for every new game and every load: what the previous run registered is
-- dropped first, as MMExtension does.
function Loader.loadSessionScripts()
    ModRT.Events.removeScope("session")
    ModRT.Events.scope = "session"
    runFolder("Global")
    ModRT.Events.scope = "startup"
    Loader.sessionLoaded = true
    ModRT.log("info", "session scripts loaded")
end
