-- Minimal mod used to check the mod runtime: load-time code, a module, engine events and a key handler.
local helper = require "HelloHelper"

HelloMod = {ticks = 0, keys = {}, initialized = {}}
print("hello_mod: General script loaded, helper says " .. helper.greeting())

function events.GameInitialized1() HelloMod.initialized[#HelloMod.initialized + 1] = 1 end
function events.GameInitialized2() HelloMod.initialized[#HelloMod.initialized + 1] = 2 end

function events.Tick()
    HelloMod.ticks = HelloMod.ticks + 1
end

function events.KeyDown(t)
    HelloMod.keys[#HelloMod.keys + 1] = t.Key

    if t.Key == 82 then -- R
        print("hello_mod: R pressed after " .. HelloMod.ticks .. " ticks")
    end
end

-- Map lifecycle, recorded for tests/mod_runtime/scenarios/map_events.yml.
HelloMod.mapEvents = {}

local function recordMapEvent(name)
    events[name] = function(wasInGame)
        HelloMod.mapEvents[#HelloMod.mapEvents + 1] =
            name .. "@" .. tostring(Map.Name) .. (wasInGame == nil and "" or (wasInGame and "+" or "-"))
    end
end

recordMapEvent("BeforeLoadMap")
recordMapEvent("LoadMap")
recordMapEvent("AfterLoadMap")
recordMapEvent("LeaveMap")

-- A flag the mod keeps on Game, as mods do. It is the mod's own field: unset, it must read as nil and not as a
-- stand-in, or the test below would take it for "on". A field the mod never assigns (Game.HelloEngineField) is
-- taken for the engine's and stays a stand-in. Checked by tests/mod_runtime/scenarios/bindings.yml.
HelloMod.modeAtLoad = Game.HelloModMode
HelloMod.modeTakenForOn = Game.HelloModMode and true or false
HelloMod.engineFieldAtLoad = Game.HelloEngineField

function HelloMod.setMode(value)
    Game.HelloModMode = value
end
