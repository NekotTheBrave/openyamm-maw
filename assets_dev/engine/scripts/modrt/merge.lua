-- Parts of MMExtension and of the Merge base that mods call as plain functions or read as shared state, and that
-- have no engine counterpart to bind: game-time timers, Sleep, the continent lookup, state the Merge base keeps in
-- the save.

local ModRT = ModRT
local modrt = modrt
local Merge = {}
ModRT.Merge = Merge

local floor = math.floor

---------------------------------------------------------------------------------------------------------------------
-- timers: Timer(func, period) calls func every `period` of game time (MMExtension ticks, const.Minute = 256).
-- They belong to the map they were started on and are dropped when the party leaves it, which is why scripts
-- start them again in LoadMap.

local timers = {}

local function gameTime()
    local minutes = modrt.gameGet("GameMinutes")
    return minutes and floor(minutes * 256) or nil
end

local function addTimer(func, period, start, refill)
    if type(func) ~= "function" then
        return
    end

    period = tonumber(period) or 256

    if period <= 0 then
        period = 1
    end

    local now = gameTime() or 0
    timers[#timers + 1] = {
        func = func,
        period = period,
        nextTime = tonumber(start) or (now + period),
        refill = refill,
    }
end

function Timer(func, period, start)
    addTimer(func, period, start, false)
end

-- As Timer, for things that refill over time: after a long absence it fires once, not once per missed period.
function RefillTimer(func, period, start)
    addTimer(func, period, start, true)
end

function RemoveTimer(func)
    for index = #timers, 1, -1 do
        if timers[index].func == func then
            table.remove(timers, index)
        end
    end
end

function Merge.clearTimers()
    timers = {}
end

function Merge.runTimers()
    local now = gameTime()

    if not now then
        return
    end

    for index = 1, #timers do
        local timer = timers[index]

        if timer and now >= timer.nextTime then
            -- Time can jump (resting, travelling); the timer fires once and counts on from now.
            timer.nextTime = now + timer.period
            ModRT.protectedCall(timer.func, "Timer", now, timer.period)
        end
    end
end

---------------------------------------------------------------------------------------------------------------------
-- Sleep(milliseconds) suspends the calling event handler in MMExtension, which runs handlers as coroutines.
-- Handlers are plain calls here, so there is nothing to suspend: Sleep returns at once and the handler carries on.
-- Mods mostly use Sleep(1) to let one frame pass before touching the screen.

function Sleep()
end

-- DismissCharacter(slot): the Merge base's way to send party member `slot` to the Adventurer's Inn (MAW's Road to
-- Insanity "disintegrates" a character so). Whether it went.
function DismissCharacter(slot)
    return modrt.innDismiss(math.floor(tonumber(slot) or -1))
end

-- MMExtension console housekeeping; there is no such console here.
function ClearConsoleEvents()
end

-- MMExtension's XYZ (Structs/After/Functions.lua): XYZ(t) gives t's X, Y and Z; XYZ(t, x, y, z) sets them; with
-- neither it enumerates "X", "Y", "Z" (`for X in XYZ do`). MAW's monster fixes and spells take positions so.
function XYZ(t, x, y, z)
    if z then
        t.X, t.Y, t.Z = x, y, z
    elseif t then
        return t.X, t.Y, t.Z
    elseif not x then
        return "X"
    elseif x == "X" then
        return "Y"
    elseif x == "Y" then
        return "Z"
    end
end

-- The Merge's console helpers (Global/Editor Console.lua): to map `name` (without its extension), at its party start
-- (evt.MoveToMap with no place). MAW's Dimension Maps take the party into their dungeon this way.
function blv(name)
    evt.MoveToMap{Name = name .. ".blv"}
end

function odm(name)
    evt.MoveToMap{Name = name .. ".odm"}
end

-- The Merge's way of catching the objects a piece of engine code creates (a monster's drops, an item dropped from the
-- hand): BeginGrabObjects() before, GrabObjects() after returns them (Map.Objects), in the order they came. The items
-- lying on the map are only ever added at the end, so those from the place counted at the start on are new ones, also
-- when others were taken off meanwhile (that only moves the new ones down, and the first may then be missed).
-- Missiles are not objects here: they are not caught (MAW aims a spell cast at no monster at the closest one, which
-- the game does itself).
local grabStart = nil

function BeginGrabObjects()
    grabStart = modrt.worldItemGet(0, "#") or 0
end

function GrabObjects()
    if grabStart == nil then
        return
    end

    local grabbed = {}

    for index = grabStart, (modrt.worldItemGet(0, "#") or 0) - 1 do
        grabbed[#grabbed + 1] = Map.Objects[index]
    end

    return table.unpack(grabbed)
end

---------------------------------------------------------------------------------------------------------------------
-- continents: Merge numbers them 1 Jadame (MM8), 2 Antagarich (MM7), 3 Enroth (MM6), 4 everything in between.

local continentByWorld = {mm8 = 1, mm7 = 2, mm6 = 3}

TownPortalControls = TownPortalControls or {}

function TownPortalControls.MapOfContinent(mapStatsIndex)
    local world = modrt.mapStatsGet(mapStatsIndex or modrt.gameGet("MapStatsIndex") or 0, "WorldId")
    return continentByWorld[world] or 4
end

function TownPortalControls.GetCurrentSwitch()
    return TownPortalControls.MapOfContinent(modrt.gameGet("MapStatsIndex"))
end

---------------------------------------------------------------------------------------------------------------------
-- state the Merge base scripts keep in `vars`, which mods read without checking

function Merge.ensureSessionState()
    vars.ExtraSettings = vars.ExtraSettings or {}
    vars.NPCFollowers = vars.NPCFollowers or {}
end
