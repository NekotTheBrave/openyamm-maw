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

-- The Merge's way of catching the objects a piece of engine code creates (a monster's drops, a spell's missiles):
-- BeginGrabObjects() before, GrabObjects() after returns them. Objects on the map are not bound yet, so nothing is
-- caught and scripts that go on to adjust the objects find none.
function BeginGrabObjects()
end

function GrabObjects()
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
