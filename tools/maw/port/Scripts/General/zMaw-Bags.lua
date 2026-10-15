-- OpenYAMM version of MAW's bag tools: sorting backpacks and its five bags per character.
--
-- MAW's item sorter (zzMaw-Item-Sorter.lua) and multi-bag (zzMaw-MultiBag.lua) work on the original's backpack
-- memory: the 14 by 9 cell grid (pl.Inventory), the item records (pl.Items), and evt.Add("Items") to hand an item to
-- the mouse and drop it into a backpack. The port does not emulate that memory. This replaces their four working
-- functions with ones on the game's own backpack (modrt.inventoryTake / inventoryPut), and keeps everything around
-- them as MAW has it: its keys (R the current bag, T the party's current bags, C all of this character's bags, G
-- everyone's bags, E the character who takes alchemy items, Shift+1..5 to switch bags), MAW's sort order (largest
-- first, then by skill, then by item; alchemy items first and to the chosen character), and its bags in
-- vars.mawbags[character] = {CurrentBag = n, [bag] = {items}}.
--
-- Nothing is lost when the items do not fit: a sort that cannot place everything puts every backpack it touched back
-- as it was.

local function slotOf(pl)
    local index = pl:GetIndex()

    for slot = 0, Party.High do
        if Party[slot]:GetIndex() == index then
            return slot
        end
    end
end

local function isAlchemy(number)
    return (number >= 220 and number < 300)
        or (alchemyItemsOrder ~= nil and table.find(alchemyItemsOrder, number) ~= nil)
end

-- Rows an item takes, with a one-row item wider than a cell counted between one and two rows (MAW's "1.5").
local function size(item)
    if item.Height == 1 and item.Width > 1 then
        return 1.5
    end

    return item.Height
end

local function skillOf(number)
    local row = Game.ItemsTxt[number]
    return row and row.Skill or 0
end

-- MAW's order (sortInventory's comparison).
local function before(a, b)
    if vars.alchemyPlayer and vars.alchemyPlayer >= 0 then
        local potionA = a.Number >= 220 and a.Number < 300
        local potionB = b.Number >= 220 and b.Number < 300

        if potionA or potionB then
            if potionA and potionB then
                return a.Number > b.Number
            end

            return potionA
        end

        local indexA = alchemyItemsOrder and table.find(alchemyItemsOrder, a.Number)
        local indexB = alchemyItemsOrder and table.find(alchemyItemsOrder, b.Number)

        if indexA and a.Number <= 1060 and a.Number > 1050 and (a.EnchantPower or 0) == 0 then
            indexA = indexA + 10
        end

        if indexB and b.Number <= 1060 and b.Number > 1050 and (b.EnchantPower or 0) == 0 then
            indexB = indexB + 10
        end

        if indexA and indexB then
            return indexA < indexB
        elseif indexA or indexB then
            return indexA ~= nil
        end
    end

    if size(a) == size(b) then
        local skillA, skillB = skillOf(a.Number), skillOf(b.Number)

        if skillA == skillB then
            return a.Number < b.Number
        end

        return skillA < skillB
    end

    return size(a) > size(b)
end

-- The backpacks a sort touches, as they were, so that a sort that cannot place everything undoes itself; and the
-- stored bags it put crafting materials into.
local function newSnapshot()
    return {lists = {}, storedBags = {}}
end

-- Empties a backpack the snapshot does not hold yet; returns its items.
local function takeInto(snapshot, slot)
    if snapshot.lists[slot] then
        return {}
    end

    local items = modrt.inventoryTake(slot) or {}
    snapshot.lists[slot] = items
    return items
end

-- A backpack the sort only adds to keeps its items where they are.
local function keepInto(snapshot, slot)
    if snapshot.lists[slot] then
        return
    end

    local items = modrt.inventoryTake(slot) or {}
    snapshot.lists[slot] = items

    for _, item in ipairs(items) do
        modrt.inventoryPut(slot, item, item.X, item.Y)
    end
end

local function restore(snapshot)
    for slot, items in pairs(snapshot.lists) do
        modrt.inventoryTake(slot)

        for _, item in ipairs(items) do
            modrt.inventoryPut(slot, item, item.X, item.Y)
        end
    end

    for index = #snapshot.storedBags, 1, -1 do
        local stored = snapshot.storedBags[index]
        stored.bags[stored.bag] = stored.list
    end
end

local function bagsOf(pl)
    local id = pl:GetIndex()
    vars.mawbags = vars.mawbags or {}
    vars.mawbags[id] = vars.mawbags[id] or {CurrentBag = 1}
    return vars.mawbags[id]
end

-- MAW's crafting bag (zzzMaw_CraftingBag.lua): bags a character marked with Shift+B (vars.CraftingBags[character]
-- [bag]), which the crafting materials picked up go into. The sorts put them there too (the port's addition).
local function isMaterial(number)
    return type(isCraftingMaterial) == "function" and isCraftingMaterial(number) == true
end

-- The character's crafting bags, in order; the open one only when `includeOpen`.
local function craftingBagsOf(pl, includeOpen)
    local flags = vars.CraftingBags and vars.CraftingBags[pl:GetIndex()]
    local list = {}

    if type(flags) == "table" then
        local open = bagsOf(pl).CurrentBag

        for bag, flagged in pairs(flags) do
            if flagged == true and (includeOpen or bag ~= open) then
                list[#list + 1] = bag
            end
        end

        table.sort(list)
    end

    return list
end

-- Puts `items` into stored (not open) bag `bag` of party member `slot`, beside what it holds, through the backpack,
-- whose own items are set aside meanwhile. Returns what did not fit; the snapshot keeps the bag as it was.
local function fillStoredBag(snapshot, slot, bag, items)
    local bags = bagsOf(Party[slot])
    snapshot.storedBags[#snapshot.storedBags + 1] = {bags = bags, bag = bag, list = bags[bag]}
    local open = modrt.inventoryTake(slot) or {}
    local kept = {}

    for _, item in ipairs(bags[bag] or {}) do
        if not modrt.inventoryPut(slot, item, item.X, item.Y) then
            kept[#kept + 1] = item
        end
    end

    local left = {}

    for _, item in ipairs(items) do
        if not modrt.inventoryPut(slot, item) then
            left[#left + 1] = item
        end
    end

    local stored = modrt.inventoryTake(slot) or {}

    for _, item in ipairs(kept) do
        stored[#stored + 1] = item
    end

    bags[bag] = #stored > 0 and stored or nil

    for _, item in ipairs(open) do
        modrt.inventoryPut(slot, item, item.X, item.Y)
    end

    return left
end

-- Fills the bags of `bagList` in turn, through the (empty) backpack of party member `slot`: each takes what it holds
-- in `filled` and then every item of `items` that still fits. Returns what fitted nowhere.
local function fillBags(slot, bagList, items, filled)
    local left = items

    for _, bag in ipairs(bagList) do
        if #left == 0 then
            break
        end

        for _, item in ipairs(filled[bag] or {}) do
            modrt.inventoryPut(slot, item, item.X, item.Y)
        end

        local carry = {}

        for _, item in ipairs(left) do
            if not modrt.inventoryPut(slot, item) then
                carry[#carry + 1] = item
            end
        end

        filled[bag] = modrt.inventoryTake(slot) or {}
        left = carry
    end

    return left
end

-- R and T: the current bag of the current character, or of everyone. Crafting materials go into the crafting bags
-- that are not open (the current character's, then the others'); the other items to the current character (alchemy
-- items to the chosen character), and to the others in turn when that backpack is full.
local function portSortInventory(all)
    local snapshot = newSnapshot()
    local items = {}
    local slots = {}

    for slot = all and 0 or Game.CurrentPlayer, all and Party.High or Game.CurrentPlayer do
        slots[#slots + 1] = slot

        for _, item in ipairs(takeInto(snapshot, slot)) do
            items[#items + 1] = item
        end
    end

    local rest, materials = {}, {}

    for _, item in ipairs(items) do
        if isMaterial(item.Number) then
            materials[#materials + 1] = item
        else
            rest[#rest + 1] = item
        end
    end

    if #materials > 0 then
        local order = {Game.CurrentPlayer}

        for slot = 0, Party.High do
            if slot ~= Game.CurrentPlayer then
                order[#order + 1] = slot
            end
        end

        for _, slot in ipairs(order) do
            for _, bag in ipairs(craftingBagsOf(Party[slot])) do
                if #materials > 0 then
                    materials = fillStoredBag(snapshot, slot, bag, materials)
                end
            end
        end

        for _, item in ipairs(materials) do
            rest[#rest + 1] = item
        end

        items = rest
    end

    table.sort(items, before)

    local alchemyPlayer = vars.alchemyPlayer or -1

    if alchemyPlayer > Party.High then
        alchemyPlayer = -1
    end

    for _, item in ipairs(items) do
        local first = alchemyPlayer >= 0 and isAlchemy(item.Number) and alchemyPlayer or Game.CurrentPlayer
        local placed = false

        for _, slot in ipairs({first, Game.CurrentPlayer, table.unpack(slots)}) do
            keepInto(snapshot, slot)

            if modrt.inventoryPut(slot, item) then
                placed = true
                break
            end
        end

        if not placed then
            restore(snapshot)
            Game.ShowStatusText("Not enough room to sort")
            return false
        end
    end

    return true
end

-- Shift+1..5 (and the solo tabs, 100 to 500): puts the backpack away as the bag it is and takes out another.
local function portChangeBag(pl, bag)
    local slot = slotOf(pl)

    if not slot then
        Game.ShowStatusText("Only a party member's bags can be switched")
        return
    end

    local bags = bagsOf(pl)
    local currentTab = math.floor(bags.CurrentBag / 100) + 1
    local currentBag = bags.CurrentBag % 100

    if bag >= 100 then
        bag = currentBag + bag - 100
    else
        bag = bag + (currentTab - 1) * 100
    end

    if bag == bags.CurrentBag then
        return
    end

    bags[bags.CurrentBag] = modrt.inventoryTake(slot) or {}
    bags.CurrentBag = bag
    local left = {}

    for _, item in ipairs(bags[bag] or {}) do
        if not modrt.inventoryPut(slot, item, item.X, item.Y) then
            left[#left + 1] = item
        end
    end

    -- Whatever did not fit (it should all fit, as it came out of this backpack) stays in the bag.
    bags[bag] = #left > 0 and left or nil
end

-- C: all five bags of a character (of its current tab), sorted together and filled in turn from the first; crafting
-- materials fill its crafting bags first and the other items the other bags, then any room left in the crafting bags.
local function sortBagsOf(slot)
    local pl = Party[slot]
    local bags = bagsOf(pl)
    local base = math.floor(bags.CurrentBag / 100) * 100
    local snapshot = newSnapshot()
    local savedBags = {}
    local items = {}

    for _, item in ipairs(takeInto(snapshot, slot)) do
        items[#items + 1] = item
    end

    for bag = base + 1, base + 5 do
        savedBags[bag] = bags[bag]

        if bag ~= bags.CurrentBag then
            for _, item in ipairs(bags[bag] or {}) do
                items[#items + 1] = item
            end
        end
    end

    table.sort(items, before)
    local crafting, normal, isCrafting = {}, {}, {}

    for _, bag in ipairs(craftingBagsOf(pl, true)) do
        if bag > base and bag <= base + 5 then
            crafting[#crafting + 1] = bag
            isCrafting[bag] = true
        end
    end

    for bag = base + 1, base + 5 do
        if not isCrafting[bag] then
            normal[#normal + 1] = bag
        end
    end

    local materials, others = {}, {}

    for _, item in ipairs(items) do
        if #crafting > 0 and isMaterial(item.Number) then
            materials[#materials + 1] = item
        else
            others[#others + 1] = item
        end
    end

    local filled = {}

    for _, item in ipairs(fillBags(slot, crafting, materials, filled)) do
        others[#others + 1] = item
    end

    table.sort(others, before)
    local left = fillBags(slot, crafting, fillBags(slot, normal, others, filled), filled)

    if #left > 0 then
        restore(snapshot)

        for savedBag = base + 1, base + 5 do
            bags[savedBag] = savedBags[savedBag]
        end

        Game.ShowStatusText("Not enough room to sort")
        return false
    end

    -- The backpack shows the bag the character had open.
    for b = base + 1, base + 5 do
        bags[b] = filled[b] and #filled[b] > 0 and filled[b] or nil
    end

    local open = bags[bags.CurrentBag] or {}
    bags[bags.CurrentBag] = nil

    for _, item in ipairs(open) do
        modrt.inventoryPut(slot, item, item.X, item.Y)
    end

    return true
end

-- G: every party member's bags, each character's on its own.
local function sortEveryonesBags()
    for slot = 0, Party.High do
        sortBagsOf(slot)
    end
end

-- MAW's bag buttons (multibagButton, bags 1..5 then tabs 1..5) go in the character screen's top bar, right of the
-- gold and food (y 0 to 29 of its 640x480; the panel right of the portraits, where MAW has them, is the Dismiss
-- button's and is to be OpenYAMM's alchemy and sort buttons'): one row, the bags and then, for a party of one, the
-- tabs, 20 apart (MAW's are 30). A button MAW has moved away (Y 1372 and 1445 while Arcomage is played) is left where
-- it is.
local BagButtonsStep = 20
local BagButtonsPlace = {[372] = {X = 214, Y = 7}, [445] = {X = 334, Y = 7}}

local function placeBagButtons()
    for i, button in ipairs(multibagButton or {}) do
        local place = BagButtonsPlace[button.Y]

        if place then
            button.X = place.X + ((i - 1) % 5) * BagButtonsStep
            button.Y = place.Y
        end
    end
end

-- MAW's crafting bag (zzzMaw_CraftingBag.lua) marks a crafting bag with a "C" on its button: a text at the button's
-- place + (12, 16) (Screen 7, y 388). It keeps the texts to itself, so they are moved as they are made: to the same
-- place on the port's buttons, which are half the size.
do
    local createText = rawget(CustomUI, "CreateText")

    rawset(CustomUI, "CreateText", function(spec)
        if type(spec) == "table" and spec.Screen == 7 and spec.Y == 372 + 16 and type(spec.X) == "number" then
            local i = (spec.X - 12 - 455) / 30

            if i >= 1 and i <= 5 and i == math.floor(i) then
                spec.X = BagButtonsPlace[372].X + (i - 1) * BagButtonsStep + 8
                spec.Y = BagButtonsPlace[372].Y + 6
            end
        end

        return createText(spec)
    end)
end

-- MAW's sorter and multi-bag load after this file and define their own; these take their place once everything has
-- loaded. MAW's key handlers call them by these names.
local function install()
    sortInventory = portSortInventory
    changeBag = portChangeBag
    sortMultibag = function()
        return sortBagsOf(Game.CurrentPlayer)
    end
    gigaChadSort = sortEveryonesBags
end

-- MAW's button tick (it hands it to its scheduler in its own GameInitialized2, after this file's) places the buttons
-- after MAW's code has.
local function installButtonTick()
    local mawTick = mawTick_MultibagButtons

    if type(mawTick) == "function" then
        mawTick_MultibagButtons = function(...)
            mawTick(...)
            placeBagButtons()
        end
    end
end

-- MAW's quick potion keys (zzMAW-Skills.lua: G a red potion, V a blue one, a party of one only) share G with its sort
-- of every bag (partyMultyBagSortKey), so G on the inventory page sorted and drank too. On that page the potion keys
-- do nothing (the port's choice): this runs before MAW's handlers and hides the keys from them for that key press.
local hiddenPotionKeys = nil

function events.KeyDown(t)
    if hiddenPotionKeys then
        healthPotionKey, manaPotionKey = hiddenPotionKeys[1], hiddenPotionKeys[2]
        hiddenPotionKeys = nil
    end

    if Game.CurrentScreen == 7 and Game.CurrentCharScreen == 103 then
        hiddenPotionKeys = {healthPotionKey, manaPotionKey}
        healthPotionKey, manaPotionKey = false, false
    end
end

function events.GameInitialized1()
    install()
end

function events.GameInitialized2()
    install()
    installButtonTick()
end
