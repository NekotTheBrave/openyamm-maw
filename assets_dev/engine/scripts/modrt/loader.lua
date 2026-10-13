-- Loads mod scripts in the order MMExtension does, so a mod's file-name ordering keeps its meaning:
--   startup:  Structs/*.lua, Structs/After/*.lua, General/*.lua
--   session:  Global/*.lua (each new game or load)
--   map:      Maps/<map>.lua (not wired yet)
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

local function runFile(mod, relativePath)
    local displayName = mod.id .. ":" .. relativePath
    local skipReason = isSkipped(mod, relativePath)

    if skipReason then
        noteFile(displayName, "skipped", skipReason)
        return
    end

    local source, readError = modrt.readFile(mod.path .. "/Scripts/" .. relativePath)

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
        for _, name in ipairs(listLuaFiles(mod.path .. "/Scripts/" .. folder)) do
            runFile(mod, folder .. "/" .. name)
        end
    end
end

-- `require "Name"` looks in each mod's Scripts/Modules, as MMExtension's package.path does.
local function installModuleSearcher()
    table.insert(package.searchers, 2, function(moduleName)
        local relativePath = "Modules/" .. moduleName:gsub("%.", "/") .. ".lua"

        for _, mod in ipairs(config.mods) do
            local fullPath = mod.path .. "/Scripts/" .. relativePath

            if modrt.fileExists(fullPath) then
                local displayName = mod.id .. ":" .. relativePath
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

function Loader.loadStartupScripts()
    installModuleSearcher()
    ModRT.Env.finish()
    runFolder("Structs")
    events.cocalls("StructsLoaded")
    runFolder("Structs/After")
    runFolder("General")
    ModRT.log("info", "startup scripts loaded: " .. #Loader.files .. " files seen")
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
