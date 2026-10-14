-- Map events the MMExtension way: evt.map[id] = function ... end adds a handler to event `id` of the current map,
-- evt.hint[id] names it, Game.MapEvtLines:RemoveEvent(id) drops the map's own lines for it, and the evt commands
-- (evt.MoveToMap{...}, evt.Cmp{"QBits", Value = 527}, ...) do what the map's events do.
--
-- The game runs a map event and asks first for the mod's handlers (dispatch "hasMapEvent", "mapEvent"); they run
-- inside the event, and their commands are the engine's own event commands (modrt.eventCall), given the arguments
-- the engine takes. A command given outside an event (a LoadMap handler, a timer) is run as an event of its own
-- (modrt.runCommandEvent), so what it does is applied as any event's is.

local ModRT = ModRT
local modrt = modrt
local MapEvents = {}
ModRT.MapEvents = MapEvents

local CommandEventId = 0xFFFF
local mapEvents = rawget(evt, "map")
local removed = {}
local removedAll = false
local queue = {}
local inEvent = false
-- The player the commands are for (evt.ForPlayer), given again before each command run outside an event.
local forPlayer = nil
local varTag = nil
local stringsMap = nil

local playerSelectors = {All = 5, Random = 6, Current = 7}
-- Variables whose value names the thing (a quest bit's number, an item's): the engine takes them packed with it.
local packedVariables = {QBits = true, Inventory = true, Awards = true, AutonotesBits = true, Players = true,
    PlayerBits = true}
-- MMExtension's other names for variables, and its MM8 numbers for ones the engine's table has no name for.
local variableAliases = {Items = "Inventory"}
local extraVariables = {MainCondition = 0x7C, ReputationIs = 0xEB}

local function engineVarTag()
    if not varTag then
        varTag = modrt.eventCall("eventSupport.varTag") or {}
    end

    return varTag
end

local function variable(name, value)
    if type(name) == "number" then
        return name
    end

    local mapVar = type(name) == "string" and name:match("^MapVar(%d+)$")

    -- MMExtension numbers the Merge's variables as MM8 does, its map variables from 0x7D (the engine's MapVar(n)
    -- counts from 0x7B, so MMExtension's MapVar19 is the engine's MapVar(21)).
    if mapVar then
        return 0x7D + tonumber(mapVar)
    end

    local history = type(name) == "string" and name:match("^History(%d+)$")

    if history then
        return engineVarTag().HistoryBegin + tonumber(history) - 1
    end

    name = variableAliases[name] or name
    local tag = engineVarTag()[name] or extraVariables[name]

    if tag == nil then
        error("unknown event variable " .. tostring(name), 3)
    end

    if packedVariables[name] then
        return math.floor(value or 0) * 65536 + tag
    end

    return tag
end

local function player(value)
    if type(value) == "string" then
        return playerSelectors[value] or tonumber(value:match("(%d+)$")) or playerSelectors.Current
    end

    return value or playerSelectors.Current
end

-- Runs fn within an event: directly in one, else as the game's command event.
local function withinEvent(fn)
    if inEvent then
        return fn()
    end

    local results = nil
    local chosen = forPlayer

    queue[#queue + 1] = function()
        if chosen ~= nil then
            modrt.eventCall("evt.ForPlayer", chosen)
        end

        results = table.pack(fn())
    end

    if not modrt.runCommandEvent() then
        queue = {}
        ModRT.log("warn", "an event command was given with no map to run it on")
        return nil
    end

    if results then
        return table.unpack(results, 1, results.n)
    end
end

local function call(name, ...)
    local arguments = table.pack(...)

    return withinEvent(function()
        return modrt.eventCall("evt." .. name, table.unpack(arguments, 1, arguments.n))
    end)
end

-- MMExtension's commands take a table of named or positional parameters, or the parameters themselves.
local function parameters(names, ...)
    local first = ...
    local values = {}

    if select("#", ...) == 1 and type(first) == "table" then
        for index, name in ipairs(names) do
            local value = first[name]

            if value == nil then
                value = first[index]
            end

            values[index] = value
        end
    else
        for index = 1, #names do
            values[index] = (select(index, ...))
        end
    end

    return values
end

local function number(value, default)
    if value == nil then
        return default or 0
    end

    if type(value) == "boolean" then
        return value and 1 or 0
    end

    return tonumber(value) or default or 0
end

local function mapStrings()
    local map = Map and Map.Name or nil

    if stringsMap ~= map then
        stringsMap = map
        local strings = {}
        local text = nil

        if map then
            local stem = map:gsub("%.%w+$", "")
            local world = nil

            for index = 0, Game.MapStats.High do
                local fileName = modrt.mapStatsGet(index, "FileName")

                if fileName and fileName:lower() == map:lower() then
                    world = modrt.mapStatsGet(index, "WorldId")
                    break
                end
            end

            if world and world ~= "" then
                text = modrt.readAsset("worlds/" .. world .. "/_legacy/events/" .. stem .. ".str")
            end
        end

        if text then
            local index = 0

            for line in (text .. "\n"):gmatch("([^\n]*)\n") do
                strings[index] = (line:gsub("\r$", ""))
                index = index + 1
            end
        end

        MapEvents.strings = strings
    end

    return MapEvents.strings
end

-- evt.str[n]: string n of the map's .str file; scripts may add their own.
local strTable = setmetatable({}, {
    __index = function(_, index)
        return mapStrings()[index]
    end,
    __newindex = function(_, index, value)
        mapStrings()[index] = value
    end,
})

local function text(value)
    if type(value) == "number" then
        return strTable[value] or ""
    end

    return tostring(value or "")
end

local commands = {}

local function variableCommand(name)
    commands[name] = function(...)
        local values = parameters({"VarNum", "Value"}, ...)
        local value = number(values[2])

        return withinEvent(function()
            return modrt.eventCall("evt." .. name, variable(values[1], value), value)
        end)
    end
end

for _, name in ipairs({"Cmp", "Add", "Subtract", "Set"}) do
    variableCommand(name)
end

local function simpleCommand(name, names, convert)
    commands[name] = function(...)
        local values = parameters(names, ...)

        for index = 1, #names do
            values[index] = (convert and convert[index] or number)(values[index])
        end

        return call(name, table.unpack(values, 1, #names))
    end
end

local function boolean(value)
    return value == true or (type(value) == "number" and value ~= 0)
end

local function passString(value)
    return value ~= nil and tostring(value) or nil
end

simpleCommand("SetDoorState", {"Id", "State"})
simpleCommand("StopDoor", {"Id"})
simpleCommand("EnterHouse", {"Id"})
simpleCommand("SpeakNPC", {"NPC"})
simpleCommand("SummonMonsters", {"TypeIndexInMapStats", "Level", "Count", "X", "Y", "Z", "NPCGroup", "unk"})
simpleCommand("CastSpell", {"Spell", "Mastery", "Skill", "FromX", "FromY", "FromZ", "ToX", "ToY", "ToZ"})
simpleCommand("SetMonGroupBit", {"NPCGroup", "Bit", "On"}, {number, number, boolean})
simpleCommand("SetFacetBit", {"Id", "Bit", "On"}, {number, number, boolean})
simpleCommand("CheckMonstersKilled", {"CheckType", "Id", "Count", "InvisibleAsDead"}, {number, number, number, boolean})
simpleCommand("SetNPCGreeting", {"NPC", "Greeting"})
simpleCommand("SetNPCTopic", {"NPC", "Index", "Event"})
simpleCommand("MoveNPC", {"NPC", "HouseId"})
simpleCommand("SetTexture", {"Facet", "Name"}, {number, passString})
simpleCommand("SetSnow", {"EffectId", "On"}, {number, boolean})
simpleCommand("PlaySound", {"Id", "X", "Y"})
simpleCommand("CanPlayerAct", {"Id"})

function commands.MoveToMap(...)
    local v = parameters({"X", "Y", "Z", "Direction", "LookAngle", "SpeedZ", "HouseId", "Icon", "Name"}, ...)
    return call("MoveToMap", number(v[1]), number(v[2]), number(v[3]), number(v[4]), number(v[5]), number(v[6]),
        number(v[7]), number(v[8]), v[9] ~= nil and tostring(v[9]) or nil)
end

function commands.ForPlayer(...)
    local v = parameters({"Player"}, ...)
    forPlayer = player(v[1])

    if inEvent then
        modrt.eventCall("evt.ForPlayer", forPlayer)
    end

    return evt
end

function commands.StatusText(...)
    local v = parameters({"Str"}, ...)
    return call("StatusText", text(v[1]))
end

function commands.ShowMovie(...)
    local v = parameters({"DoubleSize", "ExitCurrentScreen", "Name"}, ...)
    return call("ShowMovie", tostring(v[3] or ""))
end

-- These act on the player chosen as the event's (Player, else the one evt.ForPlayer chose).
local function playerCommand(name, names)
    commands[name] = function(...)
        local v = parameters(names, ...)
        local who = v[1] ~= nil and player(v[1]) or forPlayer

        return withinEvent(function()
            if who ~= nil then
                modrt.eventCall("evt.ForPlayer", who)
            end

            return modrt.eventCall("evt." .. name, number(v[2]))
        end)
    end
end

playerCommand("FaceAnimation", {"Player", "Animation"})
playerCommand("FaceExpression", {"Player", "Frame"})

function commands.DamagePlayer(...)
    local v = parameters({"Player", "DamageType", "Damage"}, ...)
    return call("DamagePlayer", player(v[1]), number(v[2]), number(v[3]))
end

for name, command in pairs(commands) do
    rawset(evt, name, command)
end

rawset(evt, "str", strTable)
rawset(evt, "Players", playerSelectors)

-- evt.map.clear(n): MMExtension keeps event n's handlers at n*256.
local clearAll = mapEvents.clear

rawset(mapEvents, "clear", function(key)
    if key == nil then
        return clearAll()
    end

    local eventId = key % 256 == 0 and key // 256 or key
    mapEvents.remove(eventId)
end)

-- Game.MapEvtLines: the map's own event lines. RemoveEvent(id) drops an event's; Count = 0 drops them all.
local mapEvtLines = setmetatable({
    RemoveEvent = function(_, eventId)
        removed[eventId] = true
    end,
}, {
    __index = function(_, key)
        if key == "Count" then
            return removedAll and 0 or 1
        end
    end,
    __newindex = function(_, key, value)
        if key == "Count" then
            removedAll = value == 0
        end
    end,
})

rawset(Game, "MapEvtLines", mapEvtLines)

-- The current map's events start empty: what the last map's scripts set goes when the party leaves it.
function MapEvents.reset()
    mapEvents.clear()
    removed = {}
    removedAll = false
    queue = {}
    forPlayer = nil
    rawset(evt, "hint", {})
    rawset(evt, "house", {})
end

function MapEvents.has(eventId)
    if eventId == CommandEventId then
        return #queue > 0
    end

    return mapEvents.exists(eventId) or removed[eventId] == true or removedAll or events.exists("EvtMap")
end

function MapEvents.run(eventId)
    local wasInEvent, wasForPlayer = inEvent, forPlayer
    inEvent = true

    if eventId == CommandEventId then
        local pending = queue
        queue = {}

        for _, fn in ipairs(pending) do
            ModRT.protectedCall(fn, "event command")
        end

        inEvent, forPlayer = wasInEvent, wasForPlayer
        return 1
    end

    forPlayer = nil
    events.cocalls("EvtMap", eventId)
    local ran = mapEvents.exists(eventId)

    if ran then
        mapEvents.cocalls(eventId)
    end

    inEvent, forPlayer = wasInEvent, wasForPlayer
    return (ran and 1 or 0) + ((removed[eventId] or removedAll) and 2 or 0)
end

function MapEvents.removedEvent(eventId)
    return removed[eventId] == true or removedAll
end

function MapEvents.hint(eventId)
    local hint = rawget(evt, "hint")[eventId]
    return type(hint) == "string" and hint or nil
end

MapEvents.reset()
