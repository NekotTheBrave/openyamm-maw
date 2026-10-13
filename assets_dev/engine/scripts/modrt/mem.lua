-- `mem`, as far as it can mean anything outside the original executable.
--
-- Two kinds of use reach this object. Engine access (patching code, reading game structures at fixed addresses)
-- has nothing to act on here: it is recorded through a stand-in and has no effect. Self-contained storage is
-- different: a script that allocates a block and reads and writes its own bytes is just using memory as an array,
-- and that is honoured with a simulated heap, so such code runs unmodified.

local ModRT = ModRT
local Stubs = ModRT.Stubs
local Mem = {}
ModRT.Mem = Mem

local floor = math.floor
local mathType = math.type
local type = type

-- Far above anything the original 32-bit process used for code or game data, so the two cannot be confused.
local HeapBase = 0x70000000
local heapEnd = HeapBase
local bytes = {}
local fallback = Stubs.new("mem")

local function toAddress(value)
    if mathType(value) == "float" then
        return floor(value)
    end

    return value
end

local function inHeap(address)
    return type(address) == "number" and address >= HeapBase and address < heapEnd
end

Mem.inHeap = inHeap

function Mem.alloc(size)
    size = toAddress(size or 0)

    if type(size) ~= "number" or size < 0 then
        size = 0
    end

    local address = heapEnd
    heapEnd = heapEnd + (size + 15) // 16 * 16 + 16
    return address
end

local function readUnsigned(address, size)
    local value = 0

    for offset = size - 1, 0, -1 do
        value = value * 256 + (bytes[address + offset] or 0)
    end

    return value
end

local function writeUnsigned(address, size, value)
    if mathType(value) == "float" then
        value = floor(value)
    end

    for offset = 0, size - 1 do
        bytes[address + offset] = value & 0xFF
        value = value >> 8
    end
end

local function accessor(name, size, signed)
    local stand = fallback[name]
    local limit = 1 << (size * 8)

    return setmetatable({}, {
        __index = function(_, address)
            address = toAddress(address)

            if not inHeap(address) then
                return stand[address]
            end

            local value = readUnsigned(address, size)

            if signed and value >= limit // 2 then
                return value - limit
            end

            return value
        end,
        __newindex = function(_, address, value)
            address = toAddress(address)

            if not inHeap(address) then
                stand[address] = value
                return
            end

            if type(value) == "boolean" then
                value = value and 1 or 0
            elseif type(value) ~= "number" then
                value = 0
            end

            writeUnsigned(address, size, value)
        end,
    })
end

local known = {
    u1 = accessor("u1", 1, false),
    u2 = accessor("u2", 2, false),
    u4 = accessor("u4", 4, false),
    u8 = accessor("u8", 8, false),
    i1 = accessor("i1", 1, true),
    i2 = accessor("i2", 2, true),
    i4 = accessor("i4", 4, true),
    i8 = accessor("i8", 8, true),
}

known.StaticAlloc = Mem.alloc
known.allocMM = Mem.alloc
known.malloc = Mem.alloc
known.alloc = Mem.alloc
known.hookalloc = Mem.alloc
known.new = Mem.alloc

function known.freeMM()
end

known.free = known.freeMM
known.hookfree = known.freeMM

-- Copies a Lua string into a fresh block and returns its address.
function known.topointer(text)
    if type(text) ~= "string" then
        return fallback.topointer(text)
    end

    local address = Mem.alloc(#text + 1)

    for index = 1, #text do
        bytes[address + index - 1] = text:byte(index)
    end

    bytes[address + #text] = 0
    return address
end

function known.string(address, size, exact)
    address = toAddress(address)

    if not inHeap(address) then
        fallback.string(address, size, exact)
        return ""
    end

    local characters = {}
    local limit = type(size) == "number" and toAddress(size) or math.huge
    local offset = 0

    while offset < limit and inHeap(address + offset) do
        local value = bytes[address + offset] or 0

        if value == 0 and not exact then
            break
        end

        characters[#characters + 1] = string.char(value)
        offset = offset + 1
    end

    return table.concat(characters)
end

function known.copy(destination, source, size)
    destination = toAddress(destination)

    if not inHeap(destination) then
        fallback.copy(destination, source, size)
        return
    end

    if type(source) == "string" then
        local count = type(size) == "number" and toAddress(size) or #source + 1

        for index = 1, count do
            bytes[destination + index - 1] = source:byte(index) or 0
        end

        return
    end

    source = toAddress(source)

    if inHeap(source) and type(size) == "number" then
        local buffer = {}

        for offset = 0, toAddress(size) - 1 do
            buffer[offset] = bytes[source + offset] or 0
        end

        for offset = 0, toAddress(size) - 1 do
            bytes[destination + offset] = buffer[offset]
        end
    end
end

function known.fill(address, size, value)
    address = toAddress(address)

    if not inHeap(address) then
        fallback.fill(address, size, value)
        return
    end

    for offset = 0, toAddress(size) - 1 do
        bytes[address + offset] = (toAddress(value or 0)) & 0xFF
    end
end

mem = setmetatable({}, {
    __index = function(_, key)
        local value = known[key]

        if value ~= nil then
            return value
        end

        return fallback[key]
    end,
    __newindex = function(_, key, value)
        fallback[key] = value
    end,
})
