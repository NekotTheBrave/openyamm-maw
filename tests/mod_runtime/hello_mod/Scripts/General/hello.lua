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
