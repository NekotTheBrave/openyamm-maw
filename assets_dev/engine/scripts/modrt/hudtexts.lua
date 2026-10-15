-- CustomUI.CreateText: a text a mod puts on a screen of the original's (MAW's map completion percentage, its class
-- stacks). The texts of the game view (Screen 0) are drawn over it at the original's 640x480 positions; the other
-- screens' (settings menus) are kept but not drawn. Writing a field (Text, X, Y, Active...) updates the text.

local ModRT = ModRT
local modrt = modrt
local nextId = 0

local function createText(spec)
    spec = spec or {}
    nextId = nextId + 1
    local id = nextId
    local fields = {
        Text = spec.Text or "", X = spec.X or 0, Y = spec.Y or 0, Screen = spec.Screen or 0, Layer = spec.Layer or 1,
        AlignLeft = spec.AlignLeft ~= false, Width = spec.Width or 0, Height = spec.Height or 0,
        Active = spec.Active ~= false, Condition = spec.Condition,
    }

    local function push()
        modrt.hudTextSet(id, tostring(fields.Text or ""), tonumber(fields.X) or 0, tonumber(fields.Y) or 0,
            tonumber(fields.Screen) or 0, fields.AlignLeft and true or false, tonumber(fields.Width) or 0,
            fields.Active ~= false)
    end

    local object = setmetatable({}, {
        __index = function(_, key)
            if key == "Wt" then
                return #tostring(fields.Text or "") * 7
            elseif key == "Ht" then
                return fields.Height ~= 0 and fields.Height or 16
            elseif key == "UpdateSize" then
                return function() end
            end

            return fields[key]
        end,
        __newindex = function(_, key, value)
            if fields[key] ~= value then
                fields[key] = value
                push()
            end
        end,
    })

    push()
    return object
end

rawset(CustomUI, "CreateText", createText)
ModRT.HudTexts = {create = createText}
