-- Persistent mod state: `vars` belongs to the save, `mapvars` to one map within the save.
--
-- The host stores what captureState returns inside the save file and hands it back to restoreState on load.
-- Only plain data survives: tables, numbers, strings and booleans. Functions and stand-ins are dropped and counted.

local ModRT = ModRT
local modrt = modrt
local State = {}
ModRT.State = State

State.mapVarsByMap = {}
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

-- Keeps `mapvars` pointing at the table of the map the party is on.
function State.syncMap()
    local mapName = modrt.gameGet("MapFileName")

    if mapName == nil or mapName == "" then
        return
    end

    mapName = mapName:lower()

    if mapName == State.currentMap then
        return
    end

    if State.currentMap then
        State.mapVarsByMap[State.currentMap] = mapvars
    end

    State.currentMap = mapName
    mapvars = State.mapVarsByMap[mapName] or {}
    State.mapVarsByMap[mapName] = mapvars
end

function State.capture()
    State.syncMap()
    State.dropped = 0
    local text = State.serialize({version = 1, vars = vars, mapvars = State.mapVarsByMap})

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
    State.currentMap = nil
    mapvars = {}
    State.syncMap()
end
