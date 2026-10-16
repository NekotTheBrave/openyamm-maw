-- The `events` object mods register handlers on, with the calling conventions MMExtension scripts rely on:
--   function events.Name(t) ... end     adds a handler (assignment appends, it does not replace)
--   events.call("Name", ...)            runs the handlers; the last non-nil return value wins
--   events.Remove("Name", handler)      removes one handler (a number: the function at that stack level, 1 the caller)
-- Handlers added or removed while an event is running do not disturb that run.

local ModRT = ModRT
local Events = {}
ModRT.Events = Events

-- Per-event bookkeeping for the compatibility report.
Events.registered = {}
Events.fired = {}

-- Handlers belong to the load phase that registered them ("startup" for Structs and General, "session" for Global).
-- MMExtension re-runs Global scripts for every new game or load, dropping what the previous run registered.
Events.scope = "startup"
local handlerScope = setmetatable({}, {__mode = "k"})
local objects = setmetatable({}, {__mode = "k"})

function Events.removeScope(scope)
    for object in pairs(objects) do
        object.removeScope(scope)
    end
end

local function copyList(list)
    local copy = {}

    for index = 1, #list do
        copy[index] = list[index]
    end

    return copy
end

function Events.new(label)
    local handlers = {}
    local object = {}
    local isMain = label == "events"

    local function add(name, handler)
        if not handler then
            return
        end

        local list = copyList(handlers[name] or {})
        list[#list + 1] = handler
        handlers[name] = list
        handlerScope[handler] = handlerScope[handler] or Events.scope

        if isMain then
            Events.registered[name] = (Events.registered[name] or 0) + 1
        end

        ModRT.noteHandler(handler, name)
    end

    local function addFirst(name, handler)
        if not handler then
            return
        end

        local list = copyList(handlers[name] or {})
        table.insert(list, 1, handler)
        handlers[name] = list
        handlerScope[handler] = handlerScope[handler] or Events.scope

        if isMain then
            Events.registered[name] = (Events.registered[name] or 0) + 1
        end

        ModRT.noteHandler(handler, name)
    end

    local function remove(name, handler)
        local list = handlers[name]

        if not list then
            return nil
        end

        -- MMExtension's events.Remove(name, level): a number is the function at that level of the call stack, 1 the
        -- one calling Remove; a handler removes itself so (MAW's one-shot Tick handlers). Without this they stayed
        -- registered and ran on every tick.
        if type(handler) == "number" then
            local info = debug.getinfo(math.floor(handler) + 1, "f")
            handler = info and info.func

            if not handler then
                return nil
            end
        end

        if handler == nil then
            handlers[name] = nil
            return list[1]
        end

        for index = #list, 1, -1 do
            if list[index] == handler then
                local copy = copyList(list)
                table.remove(copy, index)
                handlers[name] = #copy > 0 and copy or nil
                return handler
            end
        end

        return nil
    end

    local function stillRegistered(name, handler)
        local list = handlers[name]

        if list then
            for index = 1, #list do
                if list[index] == handler then
                    return true
                end
            end
        end

        return false
    end

    -- mode: "raise" propagates the first error, "continue" logs it and keeps going, "stop" logs it and stops.
    local function run(mode, name, ...)
        local list = handlers[name]

        if not list then
            return nil
        end

        local result = nil

        for index = 1, #list do
            local handler = list[index]

            if index == 1 or stillRegistered(name, handler) then
                if mode == "raise" then
                    local value = handler(...)

                    if value ~= nil then
                        result = value
                    end
                else
                    local ok, value = ModRT.protectedCall(handler, name, ...)

                    if ok then
                        if value ~= nil then
                            result = value
                        end
                    elseif mode == "stop" then
                        break
                    end
                end
            end
        end

        return result
    end

    local function call(name, ...) return run("raise", name, ...) end
    local function cocalls(name, ...) return run("continue", name, ...) end
    local function cocall(name, ...) return run("stop", name, ...) end

    local function exists(name)
        return handlers[name] ~= nil
    end

    object.call, object.Call = call, call
    object.pcalls, object.cocalls = cocalls, cocalls
    object.pcall, object.cocall = cocall, cocall
    object.add, object.Add = add, add
    object.addfirst, object.AddFirst = addFirst, addFirst
    object.remove, object.Remove = remove, remove
    object.exists, object.Exists = exists, exists
    object.new = Events.new

    function object.clear()
        handlers = {}
    end

    function object.removeScope(scope)
        for name, list in pairs(handlers) do
            local kept = {}

            for index = 1, #list do
                if handlerScope[list[index]] ~= scope then
                    kept[#kept + 1] = list[index]
                end
            end

            handlers[name] = #kept > 0 and kept or nil
        end
    end

    objects[object] = true

    function object.handlerCount(name)
        local list = handlers[name]
        return list and #list or 0
    end

    -- Not MMExtension's: a copy of an event's handlers, in the order they run (the port runs one mod file's handler
    -- of an event alone).
    function object.handlers(name)
        return copyList(handlers[name] or {})
    end

    return setmetatable(object, {
        __newindex = function(_, name, handler)
            add(name, handler)
        end,
        -- evt.map[id] (map events by number; a name reads as nil): the event's handlers, `last` the newest, and
        -- remove(handler) to take one off (MMExtension's evt.Global[id].Last and .remove).
        __index = not isMain and function(_, name)
            local list = type(name) == "number" and handlers[name] or nil

            if list then
                local function removeHandler(handler) return remove(name, handler) end
                return {first = list[1], last = list[#list], count = #list, First = list[1], Last = list[#list],
                    Count = #list, remove = removeHandler, Remove = removeHandler}
            end
        end or nil,
    })
end
