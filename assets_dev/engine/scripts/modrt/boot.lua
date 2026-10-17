-- Entry point of the mod runtime's Lua side. The host (game/mod/ModRuntime.cpp) runs this once in a fresh Lua
-- state, then talks to it only through ModRT.dispatch.

ModRT = {}

local ModRT = ModRT
local modrt = modrt

ModRT.instructionBudget = 20000000
ModRT.errors = {}
ModRT.errorOrder = {}
ModRT.totalErrorCount = 0
ModRT.disabledHandlers = {}
ModRT.handlerSources = setmetatable({}, {__mode = "k"})
ModRT.printLines = {}

local handlerFailures = setmetatable({}, {__mode = "k"})
local callDepth = 0
local MaxLoggedRepeats = 3
local MaxHandlerFailures = 3
local slowHandlerReports = {}
ModRT.slowHandlerMilliseconds = 100

function ModRT.log(level, text)
    modrt.log(level, tostring(text))
end

function ModRT.noteHandler(handler, eventName)
    if type(handler) == "function" then
        local info = debug.getinfo(handler, "S")
        ModRT.handlerSources[handler] = (info.short_src or "?") .. ":" .. (info.linedefined or 0)
    end
end

-- Errors are grouped by their first line so a handler failing every frame costs one report entry.
function ModRT.recordError(context, message)
    local text = tostring(message)
    local firstLine = text:match("^[^\n]*") or text
    local entry = ModRT.errors[firstLine]

    if not entry then
        entry = {message = firstLine, count = 0, context = context, trace = text}
        ModRT.errors[firstLine] = entry
        ModRT.errorOrder[#ModRT.errorOrder + 1] = entry
    end

    entry.count = entry.count + 1
    ModRT.totalErrorCount = ModRT.totalErrorCount + 1

    if entry.count <= MaxLoggedRepeats then
        ModRT.log("error", "[" .. tostring(context) .. "] " .. text)
    end
end

local function watchdog()
    error("watchdog: script exceeded its instruction budget (possible endless loop)", 2)
end

-- Runs a mod function with a traceback and an instruction budget. Returns ok, firstResult.
function ModRT.protectedCall(func, context, ...)
    local outermost = callDepth == 0

    if outermost then
        debug.sethook(watchdog, "", ModRT.instructionBudget)
    end

    callDepth = callDepth + 1
    local started = modrt.clock()
    local ok, value = xpcall(func, debug.traceback, ...)
    local milliseconds = (modrt.clock() - started) * 1000
    callDepth = callDepth - 1

    if outermost then
        debug.sethook()
    end

    -- A handler that holds the game up is noted in the log (the first few times each), with where it is.
    if milliseconds >= ModRT.slowHandlerMilliseconds then
        local source = ModRT.handlerSources[func] or "?"
        local key = tostring(context) .. " @ " .. source
        slowHandlerReports[key] = (slowHandlerReports[key] or 0) + 1

        if slowHandlerReports[key] <= MaxLoggedRepeats then
            ModRT.log("info", string.format("[slow] %s: %d ms", key, math.floor(milliseconds + 0.5)))
        end
    end

    if not ok then
        local source = ModRT.handlerSources[func]
        ModRT.recordError(source and (context .. " @ " .. source) or context, value)
        handlerFailures[func] = (handlerFailures[func] or 0) + 1
        return false, nil
    end

    return true, value
end

function ModRT.handlerFailureCount(func)
    return handlerFailures[func] or 0
end

local function loadRuntimeScript(name)
    local source = modrt.readAsset("scripts/modrt/" .. name)

    if not source then
        error("missing runtime script " .. name)
    end

    local chunk, loadError = load(source, "@modrt/" .. name)

    if not chunk then
        error(loadError)
    end

    chunk()
end

loadRuntimeScript("compat51.lua")
loadRuntimeScript("events.lua")
loadRuntimeScript("stubs.lua")
loadRuntimeScript("environment.lua")
loadRuntimeScript("mem.lua")
loadRuntimeScript("bindings.lua")
loadRuntimeScript("state.lua")
loadRuntimeScript("combat.lua")
loadRuntimeScript("stats.lua")
loadRuntimeScript("merge.lua")
loadRuntimeScript("mapevents.lua")
loadRuntimeScript("hudtexts.lua")
loadRuntimeScript("quests.lua")
loadRuntimeScript("loader.lua")
loadRuntimeScript("report.lua")
loadRuntimeScript("selftest.lua")

local Events = ModRT.Events

-- Per-frame handlers that keep failing are switched off so one broken handler cannot stall the game.
local function pruneFailingHandlers(eventName)
    for handler, source in pairs(ModRT.handlerSources) do
        if ModRT.handlerFailureCount(handler) >= MaxHandlerFailures and not ModRT.disabledHandlers[handler] then
            if events.remove(eventName, handler) then
                ModRT.disabledHandlers[handler] = eventName .. " @ " .. source
                ModRT.log("warn", "disabled failing " .. eventName .. " handler at " .. source)
            end
        end
    end
end

-- Brings the scripts up to date with the game before anything is asked of them: session scripts are loaded if
-- this game has none yet, and a change of map is noticed.
function ModRT.ensureSession()
    if not ModRT.Loader.sessionLoaded then
        ModRT.Loader.loadSessionScripts()
    end

    local State = ModRT.State

    -- A game that starts without saved mod state is a new game as far as the scripts are concerned. They hear
    -- about it once the first map is ready, before that map's own events.
    if State.newGamePending and modrt.gameGet("MapReady") then
        State.newGamePending = false
        ModRT.Merge.ensureSessionState()
        ModRT.fire("BeforeNewGameAutosave")
    end

    State.syncMap()
end

-- Functions queued by RunNextTick, run once at the start of the next tick.
local nextTickQueue = {}

function RunNextTick(func)
    if type(func) == "function" then
        nextTickQueue[#nextTickQueue + 1] = func
    end
end

function ModRT.fire(eventName, ...)
    Events.fired[eventName] = (Events.fired[eventName] or 0) + 1

    if eventName == "SessionStarted" then
        ModRT.ensureSession()
        return nil
    end

    return events.cocalls(eventName, ...)
end

function ModRT.tick(deltaSeconds)
    ModRT.ensureSession()

    if #nextTickQueue > 0 then
        local queue = nextTickQueue
        nextTickQueue = {}

        for index = 1, #queue do
            ModRT.protectedCall(queue[index], "RunNextTick")
        end
    end

    ModRT.Merge.runTimers()
    local errorsBefore = ModRT.totalErrorCount
    ModRT.fire("Tick")

    if ModRT.totalErrorCount ~= errorsBefore then
        pruneFailingHandlers("Tick")
    end
end

-- Keys held, by the original's key numbers (Windows virtual keys), for Keys.IsPressed.
ModRT.keysDown = {}

function ModRT.key(pressed, virtualKey, isRepeat, alt, ctrl, shift)
    ModRT.keysDown[virtualKey] = pressed or nil
    -- The modifiers come from the engine, which knows them even when their own key events were missed (focus lost
    -- with Alt held).
    ModRT.keysDown[0x12] = alt and (virtualKey ~= 0x12 or pressed) or nil
    ModRT.keysDown[0x11] = ctrl and (virtualKey ~= 0x11 or pressed) or nil
    ModRT.keysDown[0x10] = shift and (virtualKey ~= 0x10 or pressed) or nil
    local eventName = pressed and "KeyDown" or "KeyUp"
    ModRT.fire(eventName, {
        Key = virtualKey, Repeat = isRepeat, Handled = false,
        Alt = alt == true, Ctrl = ctrl == true, Shift = shift == true,
    })
end

function ModRT.console(code)
    ModRT.ensureSession()
    ModRT.printLines = {}
    local chunk, loadError = load("return " .. code, "=console")

    if not chunk then
        chunk, loadError = load(code, "=console")
    end

    if not chunk then
        return false, tostring(loadError)
    end

    local results = table.pack(xpcall(chunk, debug.traceback))
    local lines = ModRT.printLines
    ModRT.printLines = {}

    if not results[1] then
        lines[#lines + 1] = tostring(results[2])
        return false, table.concat(lines, "\n")
    end

    if results.n > 1 then
        local values = {}

        for index = 2, results.n do
            values[#values + 1] = tostring(results[index])
        end

        lines[#lines + 1] = table.concat(values, "\t")
    end

    return true, table.concat(lines, "\n")
end

function ModRT.start()
    ModRT.Loader.loadStartupScripts()
end

local function dispatchKind(kind, ...)
    if kind == "tick" then
        return ModRT.tick(...)
    elseif kind == "event" then
        return ModRT.fire(...)
    elseif kind == "key" then
        return ModRT.key(...)
    elseif kind == "console" then
        return ModRT.console(...)
    elseif kind == "start" then
        return ModRT.start()
    elseif kind == "captureState" then
        return ModRT.State.capture()
    elseif kind == "restoreState" then
        -- The map events follow later, from ensureSession, once the map's world is ready.
        ModRT.State.restore(...)
        return ModRT.Loader.loadSessionScripts()
    elseif kind == "damageToMonster" then
        return ModRT.Combat.damageToMonster(...)
    elseif kind == "damageToPlayer" then
        return ModRT.Combat.damageToPlayer(...)
    elseif kind == "monsterKilled" then
        return ModRT.Combat.monsterKilled(...)
    elseif kind == "monsterKillExperience" then
        return ModRT.Combat.monsterKillExperience(...)
    elseif kind == "playerHitOrMiss" then
        return ModRT.Combat.playerHitOrMiss(...)
    elseif kind == "monsterHitOrMiss" then
        return ModRT.Combat.monsterHitOrMiss(...)
    elseif kind == "playerAttacked" then
        return ModRT.Combat.playerAttacked(...)
    elseif kind == "badThingToPlayer" then
        return ModRT.Combat.badThingToPlayer(...)
    elseif kind == "playerCastSpell" then
        return ModRT.Combat.playerCastSpell(...)
    elseif kind == "characterBonuses" then
        return ModRT.Stats.characterBonuses(...)
    elseif kind == "characterSkills" then
        return ModRT.Stats.characterSkills(...)
    elseif kind == "statisticEffect" then
        return ModRT.Stats.statisticEffect(...)
    elseif kind == "attackDelay" then
        return ModRT.Stats.attackDelay(...)
    elseif kind == "itemValue" then
        return ModRT.Stats.itemValue(...)
    elseif kind == "itemTooltip" then
        return ModRT.Stats.itemTooltip(...)
    elseif kind == "playerSkillTotal" then
        return ModRT.Stats.playerSkillTotal(...)
    elseif kind == "canHandleItem" then
        return ModRT.Stats.canHandleItem(...)
    elseif kind == "action" then
        return ModRT.Stats.action(...)
    elseif kind == "canTeachSkillMastery" then
        return ModRT.Stats.canTeachSkillMastery(...)
    elseif kind == "teacherText" then
        return ModRT.Stats.teacherText(...)
    elseif kind == "trainingTime" then
        return ModRT.Stats.trainingTime(...)
    elseif kind == "statInformationBox" then
        return ModRT.Stats.statInformationBox(...)
    elseif kind == "skillInformationBox" then
        return ModRT.Stats.skillInformationBox(...)
    elseif kind == "canLearnSpell" then
        return ModRT.Stats.canLearnSpell(...)
    elseif kind == "useMouseItem" then
        return ModRT.Stats.useMouseItem(...)
    elseif kind == "regeneration" then
        return ModRT.Stats.regeneration(...)
    elseif kind == "potionEffect" then
        return ModRT.Stats.potionEffect(...)
    elseif kind == "townHallBounty" then
        return ModRT.Stats.townHallBounty(...)
    elseif kind == "mercenaryItemGenerated" then
        return ModRT.Stats.mercenaryItemGenerated(...)
    elseif kind == "monsterDropItem" then
        return ModRT.Stats.monsterDropItem(...)
    elseif kind == "hudButtons" then
        return ModRT.HudButtons.list(...)
    elseif kind == "hudButtonClicked" then
        return ModRT.HudButtons.click(...)
    elseif kind == "questionAnswered" then
        return ModRT.MapEvents.questionAnswered(...)
    elseif kind == "newGameOptions" or kind == "newGameOptionSet" then
        -- A mod's further options for a new game: ModRT.NewGameOptions, a list of {Name, Choices, Get = f, Set = f}.
        ModRT.ensureSession()
        local options = ModRT.NewGameOptions or {}

        if kind == "newGameOptions" then
            local list = {}

            for _, option in ipairs(options) do
                list[#list + 1] = {Name = option.Name, Choices = option.Choices, Value = tostring(option.Get())}
            end

            return list
        end

        local name, value = ...

        for _, option in ipairs(options) do
            if option.Name == name then
                option.Set(value)
            end
        end

        return nil
    elseif kind == "settingsOptions" or kind == "settingsOptionSet" then
        -- A mod's own tab of the settings window: ModRT.SettingsTitle, and ModRT.SettingsOptions, a list of
        -- {Name, Group, Description, Choices (none: an on/off switch, Get and Set with a boolean), Get = f, Set = f,
        -- Enabled = f (optional)}.
        ModRT.ensureSession()
        local options = ModRT.SettingsOptions or {}

        if kind == "settingsOptions" then
            local list = {Title = ModRT.SettingsTitle or "Mod"}

            for _, option in ipairs(options) do
                local value = option.Get()

                if option.Choices == nil then
                    value = value and "true" or "false"
                end

                list[#list + 1] = {
                    Name = option.Name, Group = option.Group or "", Description = option.Description or "",
                    Choices = option.Choices, Value = tostring(value),
                    Enabled = option.Enabled == nil or option.Enabled() and true or false,
                }
            end

            return list
        end

        local name, value = ...

        for _, option in ipairs(options) do
            if option.Name == name then
                if option.Choices == nil then
                    option.Set(value == "true")
                else
                    option.Set(value)
                end
            end
        end

        return nil
    elseif kind == "difficultyChoices" or kind == "difficultyGet" or kind == "difficultySet"
        or kind == "gameModeChoices" or kind == "gameModeGet" or kind == "gameModeSet" then
        -- A mod that has difficulties registers them as ModRT.Difficulty = {choices = f, get = f, set = f}, and its
        -- game modes the same way as ModRT.GameMode.
        local isMode = kind:sub(1, 8) == "gameMode"
        local provider = isMode and ModRT.GameMode or ModRT.Difficulty

        if not provider then
            return nil
        end

        ModRT.ensureSession()
        local action = kind:sub(isMode and 9 or 11)

        if action == "Choices" then
            return provider.choices()
        elseif action == "Get" then
            return provider.get()
        end

        return provider.set(...)
    elseif kind == "npcTopics" then
        return ModRT.Quests.topics(...)
    elseif kind == "npcGreeting" then
        return ModRT.Quests.greeting(...)
    elseif kind == "npcDialogClosed" then
        return ModRT.Quests.dialogClosed(...)
    elseif kind == "questLog" then
        return ModRT.Quests.questLog(...)
    elseif kind == "hasGlobalEvent" then
        return ModRT.MapEvents.hasGlobal(...)
    elseif kind == "globalEvent" then
        return ModRT.MapEvents.runGlobal(...)
    elseif kind == "globalEventDone" then
        return ModRT.MapEvents.globalDone(...)
    elseif kind == "mapRefilled" then
        return ModRT.State.mapRefilled(...)
    elseif kind == "mapLoading" then
        local index, fileName = ...
        return ModRT.fire("MapLoading", {MapStatsIndex = index, FileName = fileName})
    elseif kind == "monsterSpriteScale" then
        return ModRT.Stats.monsterSpriteScale(...)
    elseif kind == "canIdentifyMonster" then
        return ModRT.Stats.canIdentifyMonster(...)
    elseif kind == "monsterInformationBox" then
        return ModRT.Stats.monsterInformationBox(...)
    elseif kind == "canWearItem" then
        return ModRT.Stats.canWearItem(...)
    elseif kind == "canOpenChest" then
        return ModRT.Stats.canOpenChest(...)
    elseif kind == "pickObject" then
        return ModRT.Stats.pickObject(...)
    elseif kind == "shopItemsGenerated" then
        ModRT.ensureSession()
        -- The original's ShopItemsGenerated, as a shop's goods page opens: Game.HouseScreen reads as that page.
        return ModRT.fire("ShopItemsGenerated", {HouseId = ...})
    elseif kind == "partyDefeated" then
        ModRT.ensureSession()
        -- The original's DeathMap: the party fell on this map and is about to wake up in a temple.
        return ModRT.fire("DeathMap", {Name = Map.Name})
    elseif kind == "canHireNpcFollower" then
        return ModRT.Stats.canHireNpcFollower(...)
    elseif kind == "shopItemTreatment" then
        return ModRT.Stats.shopItemTreatment(...)
    elseif kind == "hasMapEvent" then
        return ModRT.MapEvents.has(...)
    elseif kind == "mapEvent" then
        return ModRT.MapEvents.run(...)
    elseif kind == "mapEventHint" then
        return ModRT.MapEvents.hint(...)
    elseif kind == "mapEventRemoved" then
        return ModRT.MapEvents.removedEvent(...)
    elseif kind == "pickCorpse" then
        return ModRT.Stats.pickCorpse(...)
    elseif kind == "generateItem" then
        return ModRT.Stats.generateItem(...)
    elseif kind == "itemGenerated" then
        return ModRT.Stats.itemGenerated(...)
    elseif kind == "skillStatBonus" then
        return ModRT.Stats.skillStatBonus(...)
    end

    error("unknown dispatch kind " .. tostring(kind))
end

-- The calls after which the scripts have handed control back to the game, as opposed to those the engine makes in
-- the middle of computing something (a statistic, a recovery time, the bonuses themselves).
local settlingKinds = {
    tick = true, event = true, key = true, console = true,
    damageToMonster = true, damageToPlayer = true, monsterKilled = true, playerAttacked = true,
    badThingToPlayer = true, playerCastSpell = true,
}
local dispatchDepth = 0

function ModRT.dispatch(kind, ...)
    dispatchDepth = dispatchDepth + 1
    local results = table.pack(xpcall(dispatchKind, debug.traceback, kind, ...))
    dispatchDepth = dispatchDepth - 1

    if dispatchDepth == 0 and settlingKinds[kind] and ModRT.Bindings then
        ModRT.Bindings.flushBonuses()
    end

    if not results[1] then
        error(results[2], 0)
    end

    return table.unpack(results, 2, results.n)
end
