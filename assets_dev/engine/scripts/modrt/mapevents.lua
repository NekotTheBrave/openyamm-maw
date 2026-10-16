-- Map events the MMExtension way: evt.map[id] = function ... end adds a handler to event `id` of the current map,
-- evt.hint[id] names it, Game.MapEvtLines:RemoveEvent(id) drops the map's own lines for it, and the evt commands
-- (evt.MoveToMap{...}, evt.Cmp{"QBits", Value = 527}, ...) do what the map's events do.
--
-- The game runs a map event and asks first for the mod's handlers (dispatch "hasMapEvent", "mapEvent"); they run
-- inside the event, and their commands are the engine's own event commands (modrt.eventCall), given the arguments
-- the engine takes. A command given outside an event (a LoadMap handler, a timer) is run as an event of its own
-- (modrt.runCommandEvent), so what it does is applied as any event's is.
--
-- The game's global events (NPC topics, mostly) the same way: evt.global[id] handlers run inside the event before its
-- own lines, Game.GlobalEvtLines:RemoveEvent(id) drops those lines, and events.ClickNPCTopic(id) (an NPC's topic
-- clicked, before) and events.EvtGlobal(id) (after the event, its own lines included) are raised around it
-- (dispatch "hasGlobalEvent", "globalEvent", "globalEventDone").

local ModRT = ModRT
local modrt = modrt
local MapEvents = {}
ModRT.MapEvents = MapEvents

local CommandEventId = 0xFFFF
local mapEvents = rawget(evt, "map")
local globalEvents = rawget(evt, "global")
local removed = {}
local removedAll = false
local removedGlobal = {}
local queue = {}
local inEvent = false
-- Inside a global event a message's number is an NPC text's (the original's global events have no strings of their
-- own), not one of the map's strings.
local inGlobalEvent = false
-- The player the commands are for (evt.ForPlayer), given again before each command run outside an event.
local forPlayer = nil
local varTag = nil
local stringsMap = nil

local playerSelectors = {All = 5, Random = 6, Current = 7}
-- Variables whose value names the thing (a quest bit's number, an item's): the engine takes them packed with it.
local packedVariables = {QBits = true, Inventory = true, Awards = true, AutonotesBits = true, Players = true,
    PlayerBits = true}
-- MMExtension's other names for variables, and the engine's numbers (laid out as MM7's: EvtEnums.h) for those its
-- table has no name for.
local variableAliases = {Items = "Inventory"}
local extraVariables = {MainCondition = 0x7A, ReputationIs = 0xE9, Reputation = 0x113, Deaths = 0x133,
    MontersHunted = 0x134}

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
    if type(value) == "number" and inGlobalEvent then
        return Game.NPCText[value] or ""
    elseif type(value) == "number" then
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
simpleCommand("RemoveItems", {"MinItemIndex", "MaxItemIndex", "Count"})
simpleCommand("CheckItemsCount", {"MinItemIndex", "MaxItemIndex", "Count"})
simpleCommand("GiveItem", {"Strength", "Type", "Id"})
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

-- The answer the NPC gives in the dialog (a topic's text).
function commands.SetMessage(...)
    local v = parameters({"Str"}, ...)
    return call("SetMessage", text(v[1]))
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

commands.Sub = commands.Subtract

for name, command in pairs(commands) do
    rawset(evt, name, command)
end

-- evt.All.Add(...), evt[0].Cmp(...), evt.Current.Add(...): a command for the players chosen, as evt.ForPlayer(...)
-- followed by it; the players chosen before are chosen again afterwards.
local function forPlayers(who)
    return setmetatable({}, {
        __index = function(_, name)
            local command = commands[name]

            if not command then
                return nil
            end

            return function(...)
                local before = forPlayer
                commands.ForPlayer(who)
                local results = table.pack(command(...))
                forPlayer = before

                if inEvent and before ~= nil then
                    modrt.eventCall("evt.ForPlayer", before)
                end

                return table.unpack(results, 1, results.n)
            end
        end,
    })
end

for name in pairs(playerSelectors) do
    rawset(evt, name, forPlayers(name))
end

for index = 0, 4 do
    rawset(evt, index, forPlayers(index))
end

-- The original's message box (Message, Game.EscMessage): the game's event message box, as a sign's. With no map to
-- show it on (the main menu), the text goes to the status line. Either way it is in the log in full.
function Message(text, keepSound, global)
    text = tostring(text or "")
    print("[message] " .. text)

    if inEvent or (Map and Map.Name) then
        local shown = withinEvent(function()
            -- In an NPC's topic (a global event) the text is the NPC's answer, as the original shows it there.
            modrt.eventCall(inGlobalEvent and "evt.SetMessage" or "evt.SimpleMessage", text)
            return true
        end)

        if shown then
            return
        end
    end

    modrt.showStatusText((text:gsub("\f%d%d%d%d%d", ""):gsub("%s*\n%s*", " ")))
end

SimpleMessage = Message
rawset(Game, "EscMessage", function(text)
    Message(text)
end)

rawset(evt, "str", strTable)
rawset(evt, "Players", playerSelectors)

-- evt.Player: the member the event's commands act on, a party slot from 0 (MAW's Kriegspire well gives experience
-- to Party[evt.Player]): the one evt.ForPlayer chose, else the selected member. A write chooses one, as
-- evt.ForPlayer does. evt is otherwise the stand-in root every unknown field of which reads as a stand-in.
do
    local function eventPlayer()
        if type(forPlayer) == "number" and forPlayer >= 0 and forPlayer <= Party.High then
            return forPlayer
        end

        local current = Game.CurrentPlayer
        return type(current) == "number" and current >= 0 and current or 0
    end

    local stubMeta = getmetatable(evt)
    local evtMeta = {}

    for key, value in pairs(stubMeta) do
        evtMeta[key] = value
    end

    evtMeta.__index = function(self, key)
        if key == "Player" then
            return eventPlayer()
        end

        return stubMeta.__index(self, key)
    end
    evtMeta.__newindex = function(self, key, value)
        if key == "Player" then
            forPlayer = player(value)
            return
        end

        return stubMeta.__newindex(self, key, value)
    end

    rawset(evt, "Player", nil)
    setmetatable(evt, evtMeta)
end

-- evt.HouseDoor(event, house): Merge's door of a house (Core/evt.lua): the map's event names the house and enters it.
rawset(evt, "HouseDoor", function(eventId, houseId)
    evt.house[eventId] = houseId
    evt.map[eventId] = function()
        evt.EnterHouse(houseId)
    end
end)

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

-- Game.GlobalEvtLines: the game's own global event lines; RemoveEvent(id) drops an event's.
rawset(Game, "GlobalEvtLines", {
    RemoveEvent = function(_, eventId)
        removedGlobal[eventId] = true
    end,
})

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

-- A question a mod asks (Question): the game's answer prompt, asked as the mod's command event; the coroutine that
-- asked waits for the answer (questionAnswered).
local questionAsker = nil

function MapEvents.askQuestion(text)
    if questionAsker or not (Map and Map.Name) then
        return false
    end

    local asked = withinEvent(function()
        modrt.eventCall("evt.AskQuestion", CommandEventId, 0, 0, text)
        return true
    end)

    if asked then
        questionAsker = coroutine.running()
    end

    return asked == true
end

function MapEvents.questionAnswered(answer)
    local asker = questionAsker
    questionAsker = nil

    if asker and coroutine.status(asker) == "suspended" then
        local ok, message = coroutine.resume(asker, answer)

        if not ok then
            ModRT.log("error", "after a question: " .. tostring(message))
        end
    end
end

-- Whether coroutine `co` still waits for an answer; one whose prompt was closed without an answer gets an empty one.
function MapEvents.questionWaits(co)
    if questionAsker ~= co then
        return false
    end

    if modrt.questionOpen() then
        return true
    end

    MapEvents.questionAnswered("")
    return questionAsker == co
end

-- ModRT.async(fn): fn as a function that runs in a coroutine, so that it may wait for a Question; while it waits,
-- calls to it do nothing (a timer's asking again).
function ModRT.async(fn)
    local waiting = nil

    return function(...)
        if waiting and coroutine.status(waiting) == "suspended" and MapEvents.questionWaits(waiting) then
            return
        end

        local co = coroutine.create(fn)
        local ok, message = coroutine.resume(co, ...)

        if not ok then
            ModRT.log("error", tostring(message))
        end

        waiting = coroutine.status(co) == "suspended" and co or nil
    end
end

-- What the scripts of a playthrough set for the global events goes with them (Loader.loadSessionScripts).
function MapEvents.resetGlobal()
    removedGlobal = {}
end

function MapEvents.hasGlobal(eventId)
    -- The playthrough's scripts (Global/) set the global events' handlers.
    ModRT.ensureSession()
    return globalEvents.exists(eventId) or removedGlobal[eventId] == true or events.exists("ClickNPCTopic")
        or events.exists("EvtGlobal") or ModRT.Quests.owns(eventId)
end

function MapEvents.runGlobal(eventId, topicClick)
    local wasInEvent, wasForPlayer, wasInGlobal = inEvent, forPlayer, inGlobalEvent
    inEvent, inGlobalEvent = true, true
    forPlayer = nil

    if topicClick then
        events.cocalls("ClickNPCTopic", eventId)
    end

    local ran = globalEvents.exists(eventId)

    if ran then
        globalEvents.cocalls(eventId)
    end

    inEvent, forPlayer, inGlobalEvent = wasInEvent, wasForPlayer, wasInGlobal
    return (ran and 1 or 0) + (removedGlobal[eventId] and 2 or 0)
end

-- Returns whether the event was a quest's topic, which the quest took.
function MapEvents.globalDone(eventId)
    local wasInEvent, wasForPlayer, wasInGlobal = inEvent, forPlayer, inGlobalEvent
    inEvent, inGlobalEvent = true, true
    forPlayer = nil
    -- Merge's quests answer EvtGlobal before the mod's own handlers (MAW's pays a quest's gold by party level).
    local taken = ModRT.Quests.execute(eventId)
    events.cocalls("EvtGlobal", eventId)
    inEvent, forPlayer, inGlobalEvent = wasInEvent, wasForPlayer, wasInGlobal
    return taken
end

function MapEvents.removedEvent(eventId)
    return removed[eventId] == true or removedAll
end

function MapEvents.hint(eventId)
    local hint = rawget(evt, "hint")[eventId]
    return type(hint) == "string" and hint or nil
end

MapEvents.reset()
