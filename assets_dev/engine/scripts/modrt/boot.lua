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
    local ok, value = xpcall(func, debug.traceback, ...)
    callDepth = callDepth - 1

    if outermost then
        debug.sethook()
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

-- Session scripts are loaded when a game starts or is loaded (State.restore); this covers paths that reach
-- gameplay without either.
function ModRT.ensureSession()
    if not ModRT.Loader.sessionLoaded then
        ModRT.Loader.loadSessionScripts()
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
    ModRT.State.syncMap()
    local errorsBefore = ModRT.totalErrorCount
    ModRT.fire("Tick")

    if ModRT.totalErrorCount ~= errorsBefore then
        pruneFailingHandlers("Tick")
    end
end

function ModRT.key(pressed, virtualKey, isRepeat)
    local eventName = pressed and "KeyDown" or "KeyUp"
    ModRT.fire(eventName, {Key = virtualKey, Repeat = isRepeat, Handled = false})
end

function ModRT.console(code)
    ModRT.State.syncMap()
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

function ModRT.dispatch(kind, ...)
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
        ModRT.State.restore(...)
        return ModRT.Loader.loadSessionScripts()
    elseif kind == "damageToMonster" then
        return ModRT.Combat.damageToMonster(...)
    end

    error("unknown dispatch kind " .. tostring(kind))
end
