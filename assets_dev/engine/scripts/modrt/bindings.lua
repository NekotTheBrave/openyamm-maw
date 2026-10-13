-- MMExtension-shaped objects over live OpenYAMM state: Party, Party[i], Game tables, Map.Monsters.
--
-- Each object answers the fields the engine side knows (game/mod/ModBindings.cpp) and passes every other access to a
-- recording stand-in with the same path, so unbound fields keep showing up in the report instead of failing.
-- Indices follow MMExtension: Party[0] is the first member, Items[1] the first item, Monsters[0] the first actor.

local ModRT = ModRT
local modrt = modrt
local Stubs = ModRT.Stubs
local Bindings = {}
ModRT.Bindings = Bindings

local type = type
local floor = math.floor

-- spec.get(key) returns nil for a field it does not know; spec.set(key, value) returns true when it took the value.
local function boundObject(path, spec)
    local fallback = Stubs.new(path)

    return setmetatable({}, {
        __index = function(_, key)
            if type(key) == "number" then
                if spec.index then
                    return spec.index(key)
                end

                return fallback[key]
            end

            if type(key) ~= "string" then
                return fallback[key]
            end

            local method = spec.methods and spec.methods[key]

            if method then
                return method
            end

            if spec.get then
                local value = spec.get(key)

                if value ~= nil then
                    return value
                end
            end

            return fallback[key]
        end,
        __newindex = function(_, key, value)
            if type(key) == "number" then
                if spec.newindex and spec.newindex(key, value) then
                    return
                end
            elseif type(key) == "string" and spec.set and spec.set(key, value) then
                return
            end

            fallback[key] = value
        end,
        __len = spec.len,
    })
end

Bindings.boundObject = boundObject

local function cached(factory)
    local cache = {}

    return function(key)
        local value = cache[key]

        if value == nil then
            value = factory(key)
            cache[key] = value
        end

        return value
    end
end

local function sizeFields(count)
    return function(key)
        if key == "High" or key == "high" then
            return count() - 1
        elseif key == "Count" or key == "count" or key == "Length" or key == "length" then
            return count()
        end

        return nil
    end
end

---------------------------------------------------------------------------------------------------------------------
-- skill values: Merge packs level and mastery into one number

local MaxSkillValue = 0x3FF
local MasteryBits = {[0] = 0, [1] = 0, [2] = 0x400, [3] = 0x800, [4] = 0x1000}

function SplitSkill(value)
    if not value then
        return
    end

    local level = value % (MaxSkillValue + 1)
    local mastery = 0

    if value >= MasteryBits[4] then
        mastery = 4
    elseif value >= MasteryBits[3] then
        mastery = 3
    elseif value >= MasteryBits[2] then
        mastery = 2
    elseif value >= 1 then
        mastery = 1
    end

    return level, mastery
end

function JoinSkill(level, mastery)
    if level > MaxSkillValue then
        level = MaxSkillValue
    end

    if mastery > 4 then
        mastery = 4
    end

    return level + (MasteryBits[mastery] or 0)
end

-- Skill ids as Merge numbers them (the row order of "Class Skills.txt"). The engine side resolves these names to
-- OpenYAMM's own skill names.
local skillNamesById = {
    [0] = "Staff", "Sword", "Dagger", "Axe", "Spear", "Bow", "Mace", "Blaster", "Shield", "Leather", "Chain",
    "Plate", "Fire", "Air", "Water", "Earth", "Spirit", "Mind", "Body", "Light", "Dark", "DarkElfAbility",
    "VampireAbility", "DragonAbility", "IdentifyItem", "Merchant", "Repair", "Bodybuilding", "Meditation",
    "Perception", "Regeneration", "DisarmTraps", "Dodging", "Unarmed", "IdentifyMonster", "Armsmaster", "Stealing",
    "Alchemy", "Learning",
}

Bindings.skillNamesById = skillNamesById

local function skillName(id)
    return skillNamesById[id]
end

-- `const` normally comes from MMExtension's ConstAndBits.lua in the user's Merge checkout. Without one, the
-- constants the bindings themselves define are still provided.
function Bindings.ensureConstants()
    local constants = rawget(_G, "const")

    if constants == nil then
        return
    end

    local skills = rawget(constants, "Skills")

    if type(skills) ~= "table" or Stubs.isStub(skills) then
        skills = {}

        for id, name in pairs(skillNamesById) do
            skills[name] = id
        end

        rawset(constants, "Skills", skills)
    end

    -- MMExtension's constants describe plain MM8. Merge renumbers races, classes and monster kinds in scripts
    -- that are not run here (they patch the original executable), so its numbering is restated.
    rawset(constants, "Race", {
        Human = 0, Vampire = 1, DarkElf = 2, Minotaur = 3, Troll = 4, Dragon = 5, Undead = 6, Elf = 7, Goblin = 8,
        Dwarf = 9, Zombie = 10,
    })
    rawset(constants, "Class", {
        Archer = 0, WarriorMage = 1, MasterArcher = 2, Sniper = 3, Cleric = 4, Priest = 5, PriestLight = 6,
        PriestDark = 7, DarkElf = 8, Patriarch = 9, Dragon = 10, GreatWyrm = 11, Druid = 12, GreatDruid = 13,
        Warlock = 14, ArchDruid = 15, Knight = 16, Cavalier = 17, BlackKnight = 18, Champion = 19, Minotaur = 20,
        MinotaurLord = 21, Monk = 22, Initiate = 23, Master = 24, Ninja = 25, Paladin = 26, Crusader = 27,
        Hero = 28, Villain = 29, Ranger = 30, Hunter = 31, BountyHunter = 32, RangerLord = 33, Thief = 34,
        Rogue = 35, Assassin = 36, Spy = 37, Troll = 38, WarTroll = 39, Vampire = 40, Nosferatu = 41,
        Sorcerer = 42, Wizard = 43, Necromancer = 44, Lich = 45, ArchMage = 46, nuAM = 47, Peasant = 48,
        nuPeas = 49, HighPriest = 50, MasterWizard = 51,
    })
    rawset(constants, "MonsterKind", {
        Undead = 1, Dragon = 2, Swimmer = 3, Immobile = 4, Peasant = 5, NoArena = 6, Ogre = 7, Elemental = 8,
        Demon = 9, Titan = 10, Elf = 11, Goblin = 12, Dwarf = 13, Human = 14,
    })
end

---------------------------------------------------------------------------------------------------------------------
-- items
--
-- Provisional numbering until the backpack adapter (plan M8): Items[1..n] are the backpack items in the engine's
-- order, the next sixteen are the equipment slots, the rest are empty. An item object addresses a position, so it
-- should not be kept across changes to the backpack.

local EquipmentSlotCount = 16
local MaxItemRecords = 138
local activeItem

local function itemObject(playerIndex, group, index, path)
    return boundObject(path, {
        get = function(key)
            return modrt.itemGet(playerIndex, group, index, key)
        end,
        set = function(key, value)
            return modrt.itemSet(playerIndex, group, index, key, value)
        end,
        methods = {
            T = function(self)
                return Game.ItemsTxt[self.Number]
            end,
        },
    })
end

local emptyItem = boundObject("Item(empty)", {
    get = function(key)
        if key == "Number" or key == "Bonus" or key == "BonusStrength" or key == "Bonus2" or key == "Charges"
            or key == "MaxCharges" or key == "BonusExpireTime" then
            return 0
        elseif key == "Identified" or key == "Broken" or key == "Stolen" then
            return false
        end

        return nil
    end,
})

local function itemsObject(playerIndex, path)
    return boundObject(path, {
        index = function(number)
            local backpackCount = modrt.playerGet(playerIndex, "ItemCount") or 0

            if number >= 1 and number <= backpackCount then
                return itemObject(playerIndex, "inv", number - 1, path .. "[]")
            elseif number > backpackCount and number <= backpackCount + EquipmentSlotCount then
                return itemObject(playerIndex, "equip", number - backpackCount - 1, path .. "[]")
            elseif number >= 1 and number <= MaxItemRecords then
                return emptyItem
            end

            return nil
        end,
        get = sizeFields(function() return MaxItemRecords end),
    })
end

---------------------------------------------------------------------------------------------------------------------
-- players

local function skillsObject(playerIndex, path)
    return boundObject(path, {
        index = function(id)
            local name = skillName(id)

            if not name then
                return 0
            end

            local level, mastery = modrt.playerSkillGet(playerIndex, name)

            if not level or level == 0 then
                return 0
            end

            return JoinSkill(level, mastery)
        end,
        newindex = function(id, value)
            local name = skillName(id)

            if not name then
                return false
            end

            local level, mastery = SplitSkill(floor(value))
            return modrt.playerSkillSet(playerIndex, name, level, mastery)
        end,
    })
end

local equippedItemAt = cached(function(key)
    local playerIndex, slot = key >> 8, key & 0xFF
    return itemObject(playerIndex, "equip", slot, "Party[].Items[]")
end)

activeItem = function(playerIndex, slot, includeBroken)
    if type(slot) ~= "number" or slot < 0 or slot >= EquipmentSlotCount then
        return nil
    end

    slot = floor(slot)
    local number = modrt.itemGet(playerIndex, "equip", slot, "Number")

    if not number or number == 0 then
        return nil
    end

    if not includeBroken and modrt.itemGet(playerIndex, "equip", slot, "Broken") then
        return nil
    end

    return equippedItemAt(playerIndex << 8 | slot)
end

-- Address of the player array and size of one player in the original MM8 executable.
local OriginalPlayerArrayAddress = 0xB2187C
local OriginalPlayerSize = 0x1D28

local function playerObject(playerIndex)
    local path = "Party[]"
    local skills = skillsObject(playerIndex, path .. ".Skills")
    local items = itemsObject(playerIndex, path .. ".Items")
    -- EquippedItems[slot] is the item's number in Items, 0 for an empty slot.
    local equipped = boundObject(path .. ".EquippedItems", {
        index = function(slot)
            if slot < 0 or slot >= EquipmentSlotCount then
                return nil
            end

            local number = modrt.itemGet(playerIndex, "equip", floor(slot), "Number")

            if not number or number == 0 then
                return 0
            end

            return (modrt.playerGet(playerIndex, "ItemCount") or 0) + floor(slot) + 1
        end,
        get = sizeFields(function() return EquipmentSlotCount end),
    })

    return boundObject(path, {
        get = function(key)
            if key == "Skills" then
                return skills
            elseif key == "Items" then
                return items
            elseif key == "EquippedItems" then
                return equipped
            elseif key == "?ptr" then
                -- Scripts derive a character's index from its address in the original player array. Nothing lives
                -- at this address here; it only has to give that arithmetic the right answer.
                local index = modrt.playerGet(playerIndex, "ModIndex")
                return index and (OriginalPlayerArrayAddress + index * OriginalPlayerSize) or nil
            end

            return modrt.playerGet(playerIndex, key)
        end,
        set = function(key, value)
            return modrt.playerSet(playerIndex, key, value)
        end,
        methods = {
            -- A number of the character's own, stable across party reordering (MMExtension: its roster index).
            GetIndex = function()
                return modrt.playerGet(playerIndex, "ModIndex")
            end,
            -- Skill value including bonuses, as left by the GetSkill event at the last bonus rebuild.
            GetSkill = function(_, id)
                local name = skillName(id)

                if not name then
                    return 0
                end

                local level, mastery, bonus = modrt.playerSkillGet(playerIndex, name)

                if not level or level == 0 then
                    return 0
                end

                return JoinSkill(level + (bonus or 0), mastery)
            end,
            -- The item in an equipment slot (const.ItemSlot), or nil; broken items only on request.
            GetActiveItem = function(_, slot, includeBroken)
                return activeItem(playerIndex, slot, includeBroken, path)
            end,
            -- for item, slot in pl:EnumActiveItems() do ... end
            EnumActiveItems = function(_, includeBroken)
                local slot = -1

                return function()
                    while slot < EquipmentSlotCount - 1 do
                        slot = slot + 1
                        local item = activeItem(playerIndex, slot, includeBroken, path)

                        if item then
                            return item, slot
                        end
                    end

                    return nil
                end
            end,
            GetMight = function() return modrt.playerGet(playerIndex, "GetMight") end,
            GetIntellect = function() return modrt.playerGet(playerIndex, "GetIntellect") end,
            GetPersonality = function() return modrt.playerGet(playerIndex, "GetPersonality") end,
            GetEndurance = function() return modrt.playerGet(playerIndex, "GetEndurance") end,
            GetAccuracy = function() return modrt.playerGet(playerIndex, "GetAccuracy") end,
            GetSpeed = function() return modrt.playerGet(playerIndex, "GetSpeed") end,
            GetLuck = function() return modrt.playerGet(playerIndex, "GetLuck") end,
            GetFullHP = function() return modrt.playerGet(playerIndex, "GetFullHP") end,
            GetFullSP = function() return modrt.playerGet(playerIndex, "GetFullSP") end,
            GetArmorClass = function() return modrt.playerGet(playerIndex, "GetArmorClass") end,
            GetMeleeAttack = function() return modrt.playerGet(playerIndex, "GetMeleeAttack") end,
            GetMeleeDamageMin = function() return modrt.playerGet(playerIndex, "GetMeleeDamageMin") end,
            GetMeleeDamageMax = function() return modrt.playerGet(playerIndex, "GetMeleeDamageMax") end,
            GetRangedAttack = function() return modrt.playerGet(playerIndex, "GetRangedAttack") end,
            GetRangedDamageMin = function() return modrt.playerGet(playerIndex, "GetRangedDamageMin") end,
            GetRangedDamageMax = function() return modrt.playerGet(playerIndex, "GetRangedDamageMax") end,
        },
    })
end

---------------------------------------------------------------------------------------------------------------------
-- roots

local function partyCount()
    return modrt.partyGet("Count") or 0
end

local playerAt = cached(playerObject)

-- The roster of all characters, addressed by a character's own index (GetIndex). Only party members are reachable
-- so far; characters waiting in the Adventurer's Inn are not.
local playersArray = boundObject("Party.PlayersArray", {
    index = function(index)
        for slot = 0, partyCount() - 1 do
            if modrt.playerGet(slot, "ModIndex") == index then
                return playerAt(slot)
            end
        end

        return nil
    end,
})

Party = boundObject("Party", {
    index = function(index)
        if index >= 0 and index < partyCount() then
            return playerAt(index)
        end

        return nil
    end,
    get = function(key)
        local size = sizeFields(partyCount)(key)

        if size ~= nil then
            return size
        elseif key == "PlayersArray" then
            return playersArray
        end

        return modrt.partyGet(key)
    end,
    methods = {
        -- Not part of MMExtension: recomputes every member's bonuses now, running the stat events.
        RefreshBonuses = function()
            modrt.partyRefreshBonuses()
        end,
    },
    set = function(key, value)
        return modrt.partySet(key, value)
    end,
})

local function tableRow(path, getter)
    return cached(function(id)
        return boundObject(path, {
            get = function(key)
                return getter(id, key)
            end,
        })
    end)
end

local portraitRow = tableRow("Game.CharacterPortraits[]", modrt.portraitGet)

local characterPortraits = boundObject("Game.CharacterPortraits", {
    index = function(id)
        return portraitRow(id)
    end,
})

-- Merge's helper (Structs/CharacterOutfits.lua): a character's race comes from its portrait.
function GetRace(player, playerIndex)
    local race = Game.CharacterPortraits[player.Face].Race

    if type(race) == "number" then
        return race
    end

    return player.RaceId
end

local itemRow = tableRow("Game.ItemsTxt[]", modrt.itemTxtGet)
local monsterRow = tableRow("Game.MonstersTxt[]", modrt.monsterTxtGet)

local itemsTxt = boundObject("Game.ItemsTxt", {
    index = function(id)
        if modrt.itemTxtGet(id, "Name") == nil then
            return nil
        end

        return itemRow(id)
    end,
    get = sizeFields(function() return modrt.itemTxtGet(0, "#") or 0 end),
})

local monstersTxt = boundObject("Game.MonstersTxt", {
    index = function(id)
        if modrt.monsterTxtGet(id, "Name") == nil then
            return nil
        end

        return monsterRow(id)
    end,
})

Game = boundObject("Game", {
    get = function(key)
        if key == "ItemsTxt" then
            return itemsTxt
        elseif key == "MonstersTxt" then
            return monstersTxt
        elseif key == "CharacterPortraits" then
            return characterPortraits
        elseif key == "CurrentPlayer" then
            return modrt.partyGet("CurrentPlayer")
        elseif key == "Time" then
            -- MMExtension counts game time in ticks, 256 to the game minute.
            local minutes = modrt.gameGet("GameMinutes")
            return minutes and floor(minutes * 256) or nil
        end

        return nil
    end,
    methods = {
        ShowStatusText = function(text)
            modrt.showStatusText(text)
        end,
    },
})

local function monsterCount()
    return modrt.mapMonsterGet(0, "#") or 0
end

local mapMonster = cached(function(index)
    return boundObject("Map.Monsters[]", {
        get = function(key)
            return modrt.mapMonsterGet(index, key)
        end,
        set = function(key, value)
            return modrt.mapMonsterSet(index, key, value)
        end,
    })
end)

local mapMonsters = boundObject("Map.Monsters", {
    index = function(index)
        if index >= 0 and index < monsterCount() then
            return mapMonster(index)
        end

        return nil
    end,
    get = sizeFields(monsterCount),
})

Map = boundObject("Map", {
    get = function(key)
        if key == "Monsters" then
            return mapMonsters
        elseif key == "Name" then
            return modrt.gameGet("MapFileName")
        end

        return nil
    end,
})
