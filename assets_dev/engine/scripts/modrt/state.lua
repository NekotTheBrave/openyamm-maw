-- Persistent mod state: `vars` belongs to the save, `mapvars` to one map within the save.
--
-- The host stores what captureState returns inside the save file and hands it back to restoreState on load.
-- Only plain data survives: tables, numbers, strings and booleans. Functions and stand-ins are dropped and counted.

local ModRT = ModRT
local modrt = modrt
local State = {}
ModRT.State = State

State.mapVarsByMap = {}
State.runtime = {}
State.currentMap = nil
State.dropped = 0

local type = type
local mathType = math.type
local format = string.format

local function serializeValue(value, out, seen, depth)
    local kind = type(value)

    if kind == "number" then
        if mathType(value) == "integer" then
            out[#out + 1] = format("%d", value)
        elseif value ~= value then
            out[#out + 1] = "(0/0)"
        elseif value == math.huge then
            out[#out + 1] = "math.huge"
        elseif value == -math.huge then
            out[#out + 1] = "-math.huge"
        else
            out[#out + 1] = format("%.17g", value)
        end
    elseif kind == "string" then
        out[#out + 1] = format("%q", value)
    elseif kind == "boolean" then
        out[#out + 1] = value and "true" or "false"
    elseif kind == "table" and not ModRT.Stubs.isStub(value) and not seen[value] and depth < 64 then
        seen[value] = true
        out[#out + 1] = "{"

        for key, item in pairs(value) do
            local keyKind = type(key)
            local itemKind = type(item)
            local keepItem = itemKind == "number" or itemKind == "string" or itemKind == "boolean"
                or (itemKind == "table" and not ModRT.Stubs.isStub(item) and not seen[item])

            if (keyKind == "number" or keyKind == "string" or keyKind == "boolean") and keepItem then
                out[#out + 1] = "["
                serializeValue(key, out, seen, depth + 1)
                out[#out + 1] = "]="
                serializeValue(item, out, seen, depth + 1)
                out[#out + 1] = ","
            else
                State.dropped = State.dropped + 1
            end
        end

        out[#out + 1] = "}"
        seen[value] = nil
    else
        State.dropped = State.dropped + 1
        out[#out + 1] = "nil"
    end
end

function State.serialize(value)
    local out = {}
    serializeValue(value, out, {}, 0)
    return table.concat(out)
end

function State.deserialize(text)
    -- Loaded with an environment that offers nothing but math.huge, so a save file cannot run code.
    local chunk, loadError = load("return " .. text, "=modstate", "t", {math = {huge = math.huge}})

    if not chunk then
        return nil, loadError
    end

    local ok, value = pcall(chunk)

    if not ok then
        return nil, value
    end

    return value
end

local function replaceContents(target, source)
    for key in pairs(target) do
        target[key] = nil
    end

    for key, value in pairs(source) do
        target[key] = value
    end
end

-- True between a new game or a load and the first map the scripts are told about.
State.freshSession = true
-- True until the scripts have been told that a new game began (BeforeNewGameAutosave).
State.newGamePending = true
local syncing = false

-- Notices that the party is on a different map than the scripts last knew: switches `mapvars` to that map's
-- table and raises the map events. The engine has already loaded the map by then, so "before" and "after" both
-- run after the fact, in MMExtension's order. Nothing happens until the map's world is ready.
function State.syncMap()
    if syncing or not modrt.gameGet("MapReady") then
        return
    end

    local mapName = modrt.gameGet("MapFileName")

    if mapName == nil or mapName == "" then
        return
    end

    mapName = mapName:lower()

    if mapName == State.currentMap then
        return
    end

    syncing = true
    local wasInGame = not State.freshSession

    if State.currentMap then
        ModRT.fire("LeaveMap")
        State.mapVarsByMap[State.currentMap] = mapvars
    end

    ModRT.Merge.clearTimers()
    ModRT.Merge.ensureSessionState()

    if ModRT.Bindings and ModRT.Bindings.resetMapMonsters then
        ModRT.Bindings.resetMapMonsters()
        ModRT.Bindings.restoreMonsterSpecials(mapName)
    end

    ModRT.Events.removeScope("map")
    ModRT.MapEvents.reset()
    State.currentMap = mapName
    State.freshSession = false
    mapvars = State.mapVarsByMap[mapName] or {}
    State.mapVarsByMap[mapName] = mapvars

    -- WasInGame: the party arrived from another map of the same game. WasLoaded: the map already had saved state.
    local wasLoaded = next(mapvars) ~= nil
    ModRT.fire("BeforeLoadMap", wasInGame, wasLoaded)
    ModRT.Loader.loadMapScripts(mapName)
    ModRT.fire("LoadMapScripts", wasInGame)
    ModRT.fire("LoadMap", wasInGame, wasLoaded)
    ModRT.fire("AfterLoadMap", wasInGame, wasLoaded)
    syncing = false

    -- Map handlers set up what the stat events read (MAW builds its item tables in AfterLoadMap).
    modrt.partyRefreshBonuses()
end

function State.capture()
    ModRT.fire("BeforeSaveGame")
    State.dropped = 0

    if State.currentMap then
        State.mapVarsByMap[State.currentMap] = mapvars
    end
    local text = State.serialize({version = 1, vars = vars, mapvars = State.mapVarsByMap, runtime = State.runtime})

    if State.dropped > 0 then
        ModRT.log("warn", State.dropped .. " values in vars/mapvars are not plain data and were not saved")
    end

    return text
end

-- An empty string starts from scratch (new game, or a save written without mods).
function State.restore(text)
    local data = nil

    if text ~= nil and text ~= "" then
        local loadError
        data, loadError = State.deserialize(text)

        if type(data) ~= "table" then
            ModRT.log("error", "saved mod state could not be read: " .. tostring(loadError))
            data = nil
        end
    end

    replaceContents(vars, data and type(data.vars) == "table" and data.vars or {})
    State.mapVarsByMap = data and type(data.mapvars) == "table" and data.mapvars or {}
    -- What the runtime itself keeps in the save, apart from the mods' vars: records the original engine has in its
    -- own save data and OpenYAMM has no place for (see spell buffs in bindings.lua).
    State.runtime = data and type(data.runtime) == "table" and data.runtime or {}
    State.currentMap = nil
    State.freshSession = true
    State.newGamePending = data == nil
    mapvars = {}
end
