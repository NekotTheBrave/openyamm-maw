-- Recording stand-ins for the parts of the MMExtension API that have no OpenYAMM binding yet.
--
-- A stand-in accepts any index, call or arithmetic, records the access path, and returns something harmless so the
-- calling script keeps going. That lets a whole mod load and report everything it reaches for in one run, instead
-- of stopping at the first missing name. Real bindings replace these one root at a time; whatever a binding does not
-- cover keeps falling back to a stand-in, so the report always shows what is left.

local ModRT = ModRT
local Stubs = {}
ModRT.Stubs = Stubs

Stubs.accessed = {}
Stubs.undefinedGlobals = {}

local accessed = Stubs.accessed
local rawget = rawget
local rawset = rawset
local type = type
local tostring = tostring

local PathKey = {}
local stubMeta = {}

-- Size-like fields answer with "empty" so loops over stand-in arrays end immediately.
local emptySizes = {
    High = -1, high = -1, Count = 0, count = 0, Length = 0, length = 0, Limit = 0, limit = 0, Size = 0, size = 0,
}

local function record(path)
    accessed[path] = (accessed[path] or 0) + 1
end

local function newStub(path)
    return setmetatable({[PathKey] = path}, stubMeta)
end

Stubs.new = newStub

function Stubs.isStub(value)
    return type(value) == "table" and rawget(value, PathKey) ~= nil
end

function Stubs.pathOf(value)
    return rawget(value, PathKey)
end

stubMeta.__index = function(self, key)
    local path = rawget(self, PathKey)

    if type(key) == "number" then
        local childPath = path .. "[]"
        record(childPath)
        local child = rawget(self, "[]")

        if child == nil then
            child = newStub(childPath)
            rawset(self, "[]", child)
        end

        return child
    end

    local childPath = path .. "." .. tostring(key)
    record(childPath)

    if emptySizes[key] ~= nil then
        return emptySizes[key]
    end

    local child = newStub(childPath)
    rawset(self, key, child)
    return child
end

stubMeta.__newindex = function(self, key, value)
    local path = rawget(self, PathKey)

    if type(key) == "number" then
        record(path .. "[] (write)")
        return
    end

    record(path .. "." .. tostring(key) .. " (write)")
    rawset(self, key, value)
end

stubMeta.__call = function(self, ...)
    -- MMExtension arrays are iterated directly: `for i, v in Map.Sprites do`. The loop calls the array with two
    -- nils to start; a stand-in for an array is empty, so the loop ends there.
    if select("#", ...) == 2 and (...) == nil and select(2, ...) == nil then
        record(rawget(self, PathKey) .. " (iterated)")
        return nil
    end

    local path = rawget(self, PathKey) .. "()"
    record(path)
    local result = rawget(self, "()")

    if result == nil then
        result = newStub(path)
        rawset(self, "()", result)
    end

    return result
end

local function arithmetic()
    return 0
end

stubMeta.__add = arithmetic
stubMeta.__sub = arithmetic
stubMeta.__mul = arithmetic
stubMeta.__div = arithmetic
stubMeta.__mod = arithmetic
stubMeta.__pow = arithmetic
stubMeta.__unm = arithmetic
stubMeta.__idiv = arithmetic
stubMeta.__band = arithmetic
stubMeta.__bor = arithmetic
stubMeta.__bxor = arithmetic
stubMeta.__shl = arithmetic
stubMeta.__shr = arithmetic
stubMeta.__bnot = arithmetic
stubMeta.__len = function() return 0 end
stubMeta.__lt = function() return false end
stubMeta.__le = function() return false end
stubMeta.__concat = function(left, right)
    local function text(value)
        if Stubs.isStub(value) then
            return "<" .. rawget(value, PathKey) .. ">"
        end

        return tostring(value)
    end

    return text(left) .. text(right)
end
stubMeta.__tostring = function(self)
    return "<" .. rawget(self, PathKey) .. ">"
end
stubMeta.__pairs = function()
    return function() return nil end
end

-- A stand-in handed to string.format would raise ("number expected, got table") and take the whole script down
-- with it; it formats as 0, or as its path for %s.
local plainFormat = string.format

function string.format(format, ...)
    local count = select("#", ...)
    local arguments = nil

    for index = 1, count do
        local value = select(index, ...)

        if type(value) == "table" and rawget(value, PathKey) ~= nil then
            arguments = arguments or {...}
            arguments[index] = false
        end
    end

    if not arguments then
        return plainFormat(format, ...)
    end

    local index = 0

    for conversion in format:gmatch("%%[%-+ #0]*%d*%.?%d*([%a%%])") do
        if conversion ~= "%" then
            index = index + 1

            if arguments[index] == false then
                arguments[index] = (conversion == "s" or conversion == "q") and tostring((select(index, ...))) or 0
            end
        end
    end

    return plainFormat(format, table.unpack(arguments, 1, count))
end

-- Installs a stand-in as a global root, with optional preset fields that behave normally.
function Stubs.installRoot(name, presets)
    local stub = newStub(name)
    -- A root of its own metatable: scripts rewrite a root's (MAW wraps getmetatable(Editor).__call to block the
    -- original's editor), which must not reach every other stand-in through the one they share.
    local meta = {}

    for key, value in pairs(stubMeta) do
        meta[key] = value
    end

    setmetatable(stub, meta)

    if presets then
        for key, value in pairs(presets) do
            rawset(stub, key, value)
        end
    end

    rawset(_G, name, stub)
    return stub
end

-- Reads of globals nobody defined still return nil, as scripts depend on that, but they are counted.
function Stubs.watchUndefinedGlobals()
    local undefined = Stubs.undefinedGlobals

    setmetatable(_G, {
        __index = function(_, name)
            if type(name) == "string" then
                undefined[name] = (undefined[name] or 0) + 1
            end

            return nil
        end,
    })
end
