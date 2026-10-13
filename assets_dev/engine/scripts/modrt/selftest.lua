-- Self-test for the mod runtime's own pieces: the Lua 5.1 compatibility layer, the events object and the stand-ins.
-- Run with:  openyamm --headless-mod-report 0 "return ModRT.selfTest()"   or   lua ModRT.selfTest()   in the console.

local ModRT = ModRT

function ModRT.selfTest()
    local passed, failures = 0, {}

    local function check(name, condition, detail)
        if condition then
            passed = passed + 1
        else
            failures[#failures + 1] = name .. (detail and (" -- " .. tostring(detail)) or "")
        end
    end

    local function equal(name, actual, expected)
        check(name, actual == expected, "expected " .. tostring(expected) .. ", got " .. tostring(actual))
    end

    -- number formatting (native patch)
    equal("whole float prints without .0", tostring(10 / 2), "5")
    equal("whole float concatenates without .0", "x" .. 3.0, "x3")
    equal("fraction keeps its digits", tostring(5 / 2), "2.5")
    equal("negative whole float", tostring(-8 / 2), "-4")
    equal("integer unchanged", tostring(7), "7")

    -- integer coercion
    equal("format %d truncates a fraction", string.format("%d", 3.7), "3")
    equal("format %d truncates toward zero", string.format("%d", -3.7), "-3")
    equal("format mixed conversions", string.format("%s=%d (%.1f) %x", "a", 10.9, 1.25, 255.2), "a=10 (1.2) ff")
    equal("format literal percent", string.format("100%% of %d", 4.2), "100% of 4")
    equal("format accepts numeric string for %d", string.format("%d", "12"), "12")
    equal("string.sub with fractional bounds", ("abcdef"):sub(2.9, 4.2), "bcd")
    equal("string.rep with fractional count", ("ab"):rep(2.5), "abab")
    equal("string.char with fraction", string.char(65.9), "A")

    local inRange = true

    for _ = 1, 200 do
        local value = math.random(1.5, 6.9)
        inRange = inRange and value >= 1 and value <= 6 and math.type(value) == "integer"
    end

    check("math.random accepts fractional bounds", inRange)
    check("math.random single fractional bound", math.random(3.9) <= 3)

    local list = {"a", "c"}
    table.insert(list, 2.0, "b")
    equal("table.insert with float position", table.concat(list), "abc")
    equal("table.remove with float position", table.remove(list, 1.0), "a")

    -- removed 5.1 names
    equal("unpack", select("#", unpack({1, 2, 3})), 3)
    equal("loadstring", loadstring("return 6 * 7")(), 42)
    equal("table.getn", table.getn({1, 2, 3, 4}), 4)
    equal("math.pow", math.pow(2, 10), 1024)
    equal("math.mod", math.mod(7, 4), 3)

    -- bit library
    equal("bit.band", bit.band(0xFF, 0x0F), 0x0F)
    equal("bit.bor multi", bit.bor(1, 2, 4), 7)
    equal("bit.bxor", bit.bxor(5, 1), 4)
    equal("bit.lshift", bit.lshift(1, 4), 16)
    equal("bit.rshift", bit.rshift(256, 4), 16)
    equal("bit results are signed 32-bit", bit.bor(0x80000000, 0), -2147483648)
    equal("bit.bnot", bit.bnot(0), -1)
    equal("bit.arshift keeps sign", bit.arshift(-16, 2), -4)
    equal("bit.band with float", bit.band(7.0, 3), 3)

    -- function environments
    local function reader() return marker end
    setfenv(reader, {marker = "sandboxed"})
    equal("setfenv redirects globals", reader(), "sandboxed")
    equal("getfenv returns the environment", getfenv(reader).marker, "sandboxed")
    equal("getfenv(0) is the global table", getfenv(0), _G)
    equal("coroutine.running is nil on the main thread", coroutine.running(), nil)

    -- events
    local ev = ModRT.Events.new("selftest")
    local order = {}
    function ev.Alpha(t) order[#order + 1] = "first"; t.value = t.value + 1 end
    function ev.Alpha(t) order[#order + 1] = "second"; t.value = t.value * 10; return "last" end
    local payload = {value = 1}
    equal("handlers accumulate and the last result wins", ev.call("Alpha", payload), "last")
    equal("handlers run in registration order", table.concat(order, ","), "first,second")
    equal("handlers share the event table", payload.value, 20)
    equal("unknown event returns nil", ev.call("Missing"), nil)
    check("reading an event name does not create one", ev.Alpha == nil)

    local removed
    local function removable() removed = (removed or 0) + 1 end
    ev.add("Beta", removable)
    ev.call("Beta")
    equal("remove returns the handler", ev.remove("Beta", removable), removable)
    ev.call("Beta")
    equal("removed handler no longer runs", removed, 1)
    check("exists reflects removal", not ev.exists("Beta"))

    local ran = {}
    ev.add("Gamma", function() ran[#ran + 1] = "a"; ev.add("Gamma", function() ran[#ran + 1] = "late" end) end)
    ev.add("Gamma", function() ran[#ran + 1] = "b" end)
    ev.call("Gamma")
    equal("a handler added during a run waits for the next one", table.concat(ran, ","), "a,b")

    local errorsBefore = ModRT.totalErrorCount
    local survived = false
    ev.add("Delta", function() error("selftest: deliberate failure") end)
    ev.add("Delta", function() survived = true end)
    ev.cocalls("Delta")
    check("cocalls continues past a failing handler", survived)
    equal("the failure is recorded once", ModRT.totalErrorCount, errorsBefore + 1)
    ModRT.totalErrorCount = errorsBefore
    ModRT.errors["selftest: deliberate failure"] = nil

    for index = #ModRT.errorOrder, 1, -1 do
        if ModRT.errorOrder[index].message:find("selftest: deliberate failure", 1, true) then
            ModRT.errors[ModRT.errorOrder[index].message] = nil
            table.remove(ModRT.errorOrder, index)
        end
    end

    -- watchdog
    local previousBudget = ModRT.instructionBudget
    ModRT.instructionBudget = 100000
    local finished = ModRT.protectedCall(function() while true do end end, "selftest watchdog")
    ModRT.instructionBudget = previousBudget
    check("endless loops are stopped", not finished)

    for index = #ModRT.errorOrder, 1, -1 do
        if ModRT.errorOrder[index].context:find("selftest watchdog", 1, true) then
            ModRT.totalErrorCount = ModRT.totalErrorCount - ModRT.errorOrder[index].count
            ModRT.errors[ModRT.errorOrder[index].message] = nil
            table.remove(ModRT.errorOrder, index)
        end
    end

    -- stand-ins
    local stub = ModRT.Stubs.new("SelfTestRoot")
    local total = 0

    for index = 0, stub.Items.High do
        total = total + 1
    end

    equal("stand-in arrays look empty to loops", total, 0)
    check("stand-ins survive chained use", stub.A.B(1, 2).C[3] ~= nil)
    equal("stand-in arithmetic yields a number", stub.Value + 5, 0)
    check("stand-in access is recorded", ModRT.Stubs.accessed["SelfTestRoot.A.B()"] ~= nil)

    for path in pairs(ModRT.Stubs.accessed) do
        if path:find("^SelfTestRoot") then
            ModRT.Stubs.accessed[path] = nil
        end
    end

    local summary = ("selftest: %d passed, %d failed"):format(passed, #failures)

    if #failures > 0 then
        summary = summary .. "\n  FAILED: " .. table.concat(failures, "\n  FAILED: ")
    end

    return summary
end
