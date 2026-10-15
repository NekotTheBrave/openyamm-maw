-- OpenYAMM addition to MAW's randomizer (zzzzMalekith-Randomizer.lua), which runs as MAW has it.
--
-- The randomizer is a choice for a new game (MAW's switch on its character creation screen, Game.RandomizerMode;
-- here the "Randomizer: Off/On" button in the footer of character creation). A game with it on:
--   * shuffles about a hundred quest items and artifacts of the three continents (vars.OriginalItemOrder, keys left
--     out) into vars.Randomizer: the first time one of them would reach the party it is its counterpart instead. MAW
--     changes them in the map's chests and on its ground on arriving at a map, and in the party's backpacks and hand
--     after an event or a dialog gave items (the original's actions 404 and 14);
--   * shuffles the monsters of every map (Game.MapStats' three encounters, with their difficulty and numbers) among
--     all the maps but a few.
--
-- The game does not raise those two actions, so the party's backpacks and hand are looked at twice a second instead,
-- with MAW's own steps: a shuffled item not met before is taken (evt.Subtract) and its counterpart given (evt.Add).

ModRT.NewGameOptions = ModRT.NewGameOptions or {}
table.insert(ModRT.NewGameOptions, {
    Name = "Randomizer",
    Choices = {"Off", "On"},
    Get = function()
        return Game.RandomizerMode and "On" or "Off"
    end,
    -- MAW's BeforeNewGameAutosave turns Game.RandomizerMode into vars.RandomizerMode for the new game.
    Set = function(value)
        Game.RandomizerMode = value == "On"
    end,
})

local function swapReceivedItems()
    if not (vars and vars.RandomizerMode and vars.OriginalItemOrder and vars.Randomizer) then
        return
    end

    vars.ItemFound = vars.ItemFound or {}
    local found, position = {}, {}

    for _, itemId in ipairs(vars.ItemFound) do
        found[itemId] = true
    end

    for index, itemId in ipairs(vars.OriginalItemOrder) do
        position[itemId] = index
    end

    for slot = 0, Party.High do
        for item = 0, (modrt.playerGet(slot, "ItemCount") or 0) - 1 do
            local itemId = modrt.itemGet(slot, "inv", item, "Number")
            local index = itemId and position[itemId]

            if index and not found[itemId] then
                found[itemId] = true
                vars.ItemFound[#vars.ItemFound + 1] = itemId
                evt.Subtract("Items", itemId)
                evt.Add("Items", vars.Randomizer[index])
            end
        end
    end

    local held = Mouse.Item.Number
    local index = position[held]

    if index and not found[held] then
        vars.ItemFound[#vars.ItemFound + 1] = vars.Randomizer[index]
        Mouse.Item.Number = vars.Randomizer[index]
    end
end

-- For the checks: the same step on demand.
RandomizerSwapReceivedItems = swapReceivedItems

function events.GameInitialized2()
    MawCore.Scheduler.every("randomizer/received-items", 500, swapReceivedItems)
end
