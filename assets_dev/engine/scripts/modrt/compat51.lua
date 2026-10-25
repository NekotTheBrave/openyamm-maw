-- Lua 5.1 / LuaJIT compatibility for scripts written for MMExtension, running on Lua 5.4.
-- Covers the differences that change behaviour rather than every removed function:
-- integer coercion of fractional arguments, removed globals, the bit library and function environments.
-- Whole-valued floats printing as "5" instead of "5.0" is handled natively (luaO_floatsuffix).

local floor = math.floor
local mathType = math.type
local select = select
local type = type

-- Lua 5.1 truncated fractional numbers where an integer was expected; Lua 5.4 raises an error instead.
local function toInteger(value)
    if mathType(value) == "float" then
        if value >= 0 then
            return floor(value)
        end

        return -floor(-value)
    end

    return value
end

---------------------------------------------------------------------------------------------------------------------
-- removed or renamed functions

unpack = table.unpack
loadstring = load
table.getn = function(t) return #t end

function table.maxn(t)
    local highest = 0

    for key in pairs(t) do
        if type(key) == "number" and key > highest then
            highest = key
        end
    end

    return highest
end

function table.foreach(t, callback)
    for key, value in pairs(t) do
        local result = callback(key, value)

        if result ~= nil then
            return result
        end
    end
end

function table.foreachi(t, callback)
    for index = 1, #t do
        local result = callback(index, t[index])

        if result ~= nil then
            return result
        end
    end
end

math.pow = function(base, exponent) return base ^ exponent end
-- MAW's Puller bosses aim their pull with it (calculateDirection).
math.atan2 = function(y, x) return math.atan(y, x) end
math.mod = math.fmod
math.log10 = function(value) return math.log(value, 10) end
math.ldexp = function(mantissa, exponent) return mantissa * 2.0 ^ exponent end
math.cosh = function(x) return (math.exp(x) + math.exp(-x)) / 2 end
math.sinh = function(x) return (math.exp(x) - math.exp(-x)) / 2 end
math.tanh = function(x) return (math.exp(x) - math.exp(-x)) / (math.exp(x) + math.exp(-x)) end
string.gfind = string.gmatch

-- LuaJIT's math.max and math.min compare with the processor's maxsd/minsd, which give the later argument when
-- either is NaN: math.max(0/0, 0) is 0 there and NaN in Lua 5.4. MAW relies on it (Austerity's bolster level is
-- math.max((level - 4)^1.015, 0), NaN below level 4). Without NaN the results are Lua 5.4's.
local rawMax, rawMin = math.max, math.min

local function luajitMinMax(raw, isBetter)
    return function(...)
        local count = select("#", ...)

        if count == 2 then
            local a, b = ...

            if a == a and b == b then
                return raw(a, b)
            end

            raw(a, b)
            return b
        end

        local result = raw((...))

        for index = 2, count do
            local value = select(index, ...)
            raw(value)

            if result ~= result or value ~= value or isBetter(value, result) then
                result = value
            end
        end

        return result
    end
end

math.max = luajitMinMax(rawMax, function(value, best) return value > best end)
math.min = luajitMinMax(rawMin, function(value, best) return value < best end)

---------------------------------------------------------------------------------------------------------------------
-- integer coercion

local random = math.random
local randomSeed = math.randomseed

function math.random(low, high)
    if low == nil then
        return random()
    end

    low = floor(low)

    if high == nil then
        return random(low)
    end

    return random(low, floor(high))
end

function math.randomseed(seed)
    if seed == nil then
        return randomSeed()
    end

    return randomSeed(floor(seed))
end

local stringSub = string.sub
local stringRep = string.rep
local stringByte = string.byte
local stringChar = string.char
local stringFormat = string.format

function string.sub(text, first, last)
    return stringSub(text, toInteger(first or 1), toInteger(last or -1))
end

function string.rep(text, count, separator)
    return stringRep(text, toInteger(count), separator)
end

function string.byte(text, first, last)
    first = toInteger(first or 1)
    return stringByte(text, first, toInteger(last or first))
end

function string.char(...)
    local count = select("#", ...)

    if count == 0 then
        return stringChar()
    end

    local codes = {...}

    for index = 1, count do
        codes[index] = toInteger(codes[index])
    end

    return stringChar(table.unpack(codes, 1, count))
end

local integerConversions = {c = true, d = true, i = true, o = true, u = true, x = true, X = true}

function string.format(format, ...)
    local count = select("#", ...)

    if count == 0 then
        return stringFormat(format)
    end

    local arguments = {...}
    local index = 0

    for conversion in format:gmatch("%%[%-+ #0]*%d*%.?%d*([%a%%])") do
        if conversion ~= "%" then
            index = index + 1

            if integerConversions[conversion] then
                local value = arguments[index]

                if type(value) == "string" then
                    value = tonumber(value) or value
                end

                if mathType(value) == "float" then
                    arguments[index] = toInteger(value)
                end
            end
        end
    end

    return stringFormat(format, table.unpack(arguments, 1, count))
end

local tableInsert = table.insert
local tableRemove = table.remove

function table.insert(t, ...)
    if select("#", ...) == 1 then
        return tableInsert(t, (...))
    end

    local position, value = ...
    return tableInsert(t, toInteger(position), value)
end

function table.remove(t, position)
    if position == nil then
        return tableRemove(t)
    end

    return tableRemove(t, toInteger(position))
end

---------------------------------------------------------------------------------------------------------------------
-- coroutine.running returned nil on the main thread in 5.1

local coroutineRunning = coroutine.running

function coroutine.running()
    local thread, isMain = coroutineRunning()

    if isMain then
        return nil
    end

    return thread
end

---------------------------------------------------------------------------------------------------------------------
-- LuaJIT bit library: operands are taken modulo 2^32 and results are signed 32-bit.

local function toBit(value)
    value = toInteger(value) & 0xFFFFFFFF

    if value >= 0x80000000 then
        return value - 0x100000000
    end

    return value
end

local function foldBits(operation)
    return function(first, ...)
        local result = toInteger(first) & 0xFFFFFFFF

        for index = 1, select("#", ...) do
            result = operation(result, toInteger((select(index, ...))) & 0xFFFFFFFF)
        end

        return toBit(result)
    end
end

bit = {
    tobit = toBit,
    band = foldBits(function(a, b) return a & b end),
    bor = foldBits(function(a, b) return a | b end),
    bxor = foldBits(function(a, b) return a ~ b end),
    bnot = function(value) return toBit(~toInteger(value)) end,
    lshift = function(value, count) return toBit((toInteger(value) & 0xFFFFFFFF) << (toInteger(count) & 31)) end,
    rshift = function(value, count) return toBit((toInteger(value) & 0xFFFFFFFF) >> (toInteger(count) & 31)) end,
    rol = function(value, count)
        value = toInteger(value) & 0xFFFFFFFF
        count = toInteger(count) & 31
        return toBit((value << count) | (value >> (32 - count)))
    end,
    ror = function(value, count)
        value = toInteger(value) & 0xFFFFFFFF
        count = toInteger(count) & 31
        return toBit((value >> count) | (value << (32 - count)))
    end,
    bswap = function(value)
        value = toInteger(value) & 0xFFFFFFFF
        return toBit(((value & 0xFF) << 24) | ((value & 0xFF00) << 8) | ((value >> 8) & 0xFF00) | (value >> 24))
    end,
    tohex = function(value, digits)
        digits = toInteger(digits or 8)
        local format = digits < 0 and "%0" .. -digits .. "X" or "%0" .. digits .. "x"
        local text = stringFormat(format, toInteger(value) & 0xFFFFFFFF)
        return stringSub(text, -math.abs(digits))
    end,
}

-- Arithmetic right shift needs the sign bit replicated; Lua 5.4 shifts are logical.
bit.arshift = function(value, count)
    value = toBit(value)
    count = toInteger(count) & 31

    if value >= 0 then
        return value >> count
    end

    return toBit(~((~value) >> count))
end

---------------------------------------------------------------------------------------------------------------------
-- MMExtension: bit operations under capitalised names, and as methods of every number (flags:And(2))

bit.Or = bit.bor
bit.And = bit.band
bit.Xor = bit.bxor
bit.Not = bit.bnot

function bit.AndNot(value, mask)
    return bit.band(value, bit.bnot(mask))
end

do
    local numberMethods = {}

    for name, func in pairs(bit) do
        numberMethods[name] = func
    end

    function numberMethods.ToHex(value)
        return stringFormat("%X", toInteger(value))
    end

    function numberMethods.Div(value, divisor)
        return floor(value / divisor)
    end

    numberMethods.tohex = numberMethods.ToHex
    numberMethods.div = numberMethods.Div
    numberMethods.floor = math.floor
    numberMethods.ceil = math.ceil
    numberMethods.round = function(value) return floor(value + 0.5) end

    debug.setmetatable(0, {__index = numberMethods})
end

---------------------------------------------------------------------------------------------------------------------
-- function environments, emulated through the _ENV upvalue

local function resolveFunction(target, level)
    if type(target) == "function" then
        return target
    end

    local info = debug.getinfo((target or 1) + level, "f")
    return info and info.func
end

function getfenv(target)
    if target == 0 then
        return _G
    end

    local func = resolveFunction(target, 2)

    if func then
        local index = 1

        while true do
            local name, value = debug.getupvalue(func, index)

            if name == "_ENV" then
                return value
            elseif not name then
                break
            end

            index = index + 1
        end
    end

    return _G
end

function setfenv(target, environment)
    local func = resolveFunction(target, 2)

    if func then
        local index = 1

        while true do
            local name = debug.getupvalue(func, index)

            if name == "_ENV" then
                debug.upvaluejoin(func, index, function() return environment end, 1)
                break
            elseif not name then
                break
            end

            index = index + 1
        end
    end

    return target
end
