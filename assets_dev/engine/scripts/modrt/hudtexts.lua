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
        Active = spec.Active ~= false, Condition = spec.Condition, ColorStd = spec.ColorStd,
    }

    local function push()
        -- ColorStd, the text's colour (the original's 16-bit RGB), as the game's colour code before it.
        local text = tostring(fields.Text or "")
        local color = tonumber(fields.ColorStd)

        if color and text ~= "" then
            text = ("\f%.5d"):format(color) .. text
        end

        modrt.hudTextSet(id, text, tonumber(fields.X) or 0, tonumber(fields.Y) or 0,
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

-- CustomUI.CreateButton: a button a mod puts on a screen of the original's (MAW's bag buttons). The game draws a
-- screen's buttons where the original has them (the character screen's at the original's 640x480 positions, those
-- of the bottom strip on the game's bottom frame) with the icon IUpSrc (IconUp; MAW swaps it to show the bag
-- chosen), and calls Action when one is clicked. Screen is a screen number or a list of them (const.Screens);
-- Condition, when given, must agree; Active false hides the button.
local buttons = {}

local function createButton(spec)
    spec = spec or {}
    local button = {
        IUpSrc = spec.IconUp, IDwSrc = spec.IconDown, X = spec.X or 0, Y = spec.Y or 0, Screen = spec.Screen or 0,
        Layer = spec.Layer or 1, Condition = spec.Condition, Action = spec.Action, Active = spec.Active ~= false,
    }
    buttons[#buttons + 1] = button
    button.Id = #buttons
    return button
end

local function onScreen(button, screen)
    if type(button.Screen) == "table" then
        for _, s in ipairs(button.Screen) do
            if s == screen then
                return true
            end
        end

        return false
    end

    return button.Screen == screen
end

rawset(CustomUI, "CreateButton", createButton)

ModRT.HudButtons = {
    -- The buttons to draw on the screen open now (Game.CurrentScreen; or `screen`): {Id, Icon, X, Y}.
    list = function(screen)
        local list = {}
        screen = screen or Game.CurrentScreen

        for _, button in ipairs(buttons) do
            if button.Active ~= false and button.IUpSrc and onScreen(button, screen) then
                local ok, shown = true, true

                if type(button.Condition) == "function" then
                    ok, shown = ModRT.protectedCall(button.Condition, "CustomUI button")
                end

                if ok and shown then
                    list[#list + 1] = {Id = button.Id, Icon = tostring(button.IUpSrc), X = tonumber(button.X) or 0,
                        Y = tonumber(button.Y) or 0}
                end
            end
        end

        return list
    end,
    click = function(id)
        local button = buttons[id]

        if button and type(button.Action) == "function" then
            ModRT.protectedCall(button.Action, "CustomUI button", button)
        end
    end,
}
