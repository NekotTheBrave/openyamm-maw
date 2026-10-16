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

-- Fields of the engine's root objects that the mods' own scripts assign somewhere (Game.RandomizerMode = true).
-- Mods keep flags of their own on these objects. Such a field is the mod's: until a script sets it, it reads as
-- nil, as in the original, and not as a stand-in, which a script's `if Game.SomeMode then` would take for "on".
-- The loader fills this in from the scripts' text before any of them runs.
Bindings.ownedFields = {Game = {}, Party = {}, Map = {}, Mouse = {}}

-- spec.get(key) returns nil for a field it does not know; spec.set(key, value) returns true when it took the value.
-- spec.owned names the root object (a key of Bindings.ownedFields) whose mod-owned fields live on this object;
-- spec.bound lists the fields spec.get answers, which stay the engine's even while they have no value (before a
-- game is in progress).
-- spec.range() returns the first and last index of an array, which makes the object iterable the MMExtension way:
-- `for index, value in Party do`.
local function boundObject(path, spec)
    local fallback = Stubs.new(path)
    local call = nil
    local owned = spec.owned and Bindings.ownedFields[spec.owned] or nil
    local ownedValues = owned and {} or nil

    if spec.range then
        call = function(self, _, control)
            local low, high = spec.range()
            local index = control == nil and low or control + 1

            if index > high then
                return nil
            end

            return index, self[index]
        end
    elseif spec.next then
        call = function(_, _, control)
            return spec.next(control)
        end
    end

    return setmetatable({}, {
        __call = call,
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

            if owned and owned[key] and not (spec.bound and spec.bound[key]) then
                return ownedValues[key]
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

            if owned and owned[key] and not (spec.bound and spec.bound[key]) then
                ownedValues[key] = value
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

-- A skill a mod adds (MAW's Skillz: Cover is 50): its number, the name the engine keeps it under (scripts see the
-- name they give it through Game.SkillNames), and its group on the skill page: "weapon", "magic", "armor", "misc".
local SkillGroups = {weapon = 0, magic = 1, armor = 2, misc = 3}

function Bindings.registerSkill(id, name, group)
    id = floor(id)

    if id < 39 or not SkillGroups[group] then
        return false
    end

    if modrt.skillRegister(id, name, SkillGroups[group]) then
        skillNamesById[id] = name
        return true
    end

    return false
end

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

    -- The rest of what the bindings themselves are numbered by, for a run without the Merge base's constants.
    local function fallback(name, value)
        local existing = rawget(constants, name)

        if existing == nil or Stubs.isStub(existing) then
            rawset(constants, name, value)
        end
    end

    fallback("Minute", 256)
    fallback("Hour", 256 * 60)
    fallback("Day", 256 * 60 * 24)
    fallback("PartyBuff", {
        AirResistance = 0, BodyResistance = 1, DayOfGods = 2, DetectLife = 3, EarthResistance = 4, FeatherFall = 5,
        FireResistance = 6, Fly = 7, Haste = 8, Heroism = 9, Immolation = 10, Invisibility = 11,
        MindResistance = 12, ProtectionFromMagic = 13, Shield = 14, Stoneskin = 15, TorchLight = 16,
        WaterResistance = 17, WaterWalk = 18, WizardEye = 19,
    })
    fallback("PlayerBuff", {
        AirResistance = 0, Bless = 1, BodyResistance = 2, EarthResistance = 3, Fate = 4, FireResistance = 5,
        Hammerhands = 6, Haste = 7, Heroism = 8, MindResistance = 9, PainReflection = 10, Preservation = 11,
        Regeneration = 12, Shield = 13, Stoneskin = 14, TempAccuracy = 15, TempEndurance = 16, TempIntellect = 17,
        TempLuck = 18, TempMight = 19, TempPersonality = 20, TempSpeed = 21, WaterResistance = 22,
        WaterBreathing = 23, Glamour = 24, Levitate = 25, Misform = 26,
    })
    fallback("Condition", {
        Cursed = 0, Weak = 1, Asleep = 2, Afraid = 3, Drunk = 4, Insane = 5, Poison1 = 6, Disease1 = 7,
        Poison2 = 8, Disease2 = 9, Poison3 = 10, Disease3 = 11, Paralyzed = 12, Unconscious = 13, Dead = 14,
        Stoned = 15, Eradicated = 16, Zombie = 17, Good = 18,
    })
    fallback("Screens", {
        Game = 0, Menu = 1, Controls = 2, Info = 3, NPC = 4, Rest = 5, Query = 6, Inventory = 7, SpellBook = 8,
        NewGameBriefing = 9, Chest = 10, SaveGame = 11, LoadGame = 12, House = 13, InventoryInShop = 14,
        InventoryInChest = 15, MainManu = 16, WalkToMap = 17, MapEntrance = 18, SimpleMessage = 19,
        SelectTarget = 20, CreateParty = 21, EscMessage = 22, ItemSpell = 23, KeyConfig = 26, VideoOptions = 28,
        AdventurersInn = 29, ItemSpellMM6 = 103, QuickReference = 104,
    })
    fallback("AIState", {
        Stand = 0, Active = 1, MeleeAttack = 2, RangedAttack = 3, Dying = 4, Dead = 5, Pursue = 6, Flee = 7,
        Stunned = 8, Fidget = 9, Interact = 10, Removed = 11, RangedAttack2 = 12, CastSpell = 13, Stoned = 14,
        Paralyzed = 15, Resurrect = 16, Summoned = 17, RangedAttack4 = 18, Invisible = 19,
    })
    fallback("MonsterBuff", {
        Null = 0, Charm = 1, Summoned = 2, ShrinkingRay = 3, Fear = 4, Stoned = 5, Paralyze = 6, Slow = 7,
        Berserk = 8, MassDistortion = 9, Fate = 10, Enslave = 11, DayOfProtection = 12, HourOfPower = 13, Shield = 14,
        StoneSkin = 15, Bless = 16, Heroism = 17, Haste = 18, PainReflection = 19, Hammerhands = 20,
        ArmorHalved = 21, MeleeOnly = 22, DamageHalved = 23, Wander = 24, Mistform = 25,
    })

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
            -- What the game takes the item to be worth.
            GetValue = function(self)
                return modrt.itemGet(playerIndex, group, index, "Value") or 0
            end,
            -- The item becomes a new random one of that treasure level, made by the game's generator. The
            -- original's `type` (which kind of item) and `alwaysEnchant` are not followed.
            Randomize = function(self, strength, itemType, alwaysEnchant)
                return modrt.itemRandomize(playerIndex, group, index, floor(strength or 1), floor(itemType or 0))
            end,
            -- Not the original's: the item's information box after the scripts' handlers, as a table of Name,
            -- Type, BasicStat, Enchantment and Description, starting from the texts given.
            BuildInformationBox = function(self, texts)
                texts = texts or {}
                local name, typeText, basicStat, enchantment, description = modrt.itemTooltip(playerIndex, group,
                    index, texts.Name or "", texts.Type or "", texts.BasicStat or "", texts.Enchantment or "",
                    texts.Description or "")
                return {Name = name, Type = typeText, BasicStat = basicStat, Enchantment = enchantment,
                    Description = description}
            end,
        },
    })
end

-- The item of the event being handled, when it is not one a party member holds.
Bindings.eventItem = itemObject(0, "event", 0, "Item(event)")

-- No item's record (the original's item 0): numbers 0, texts empty, so `Mouse.Item:T().Skill` reads as nothing.
local blankItemRecord = setmetatable({}, {
    __index = function(_, key)
        if key == "Name" or key == "NotIdentifiedName" or key == "Notes" or key == "Picture" then
            return ""
        end

        return 0
    end,
    __newindex = function() end,
})

local emptyItem = boundObject("Item(empty)", {
    methods = {
        T = function()
            return blankItemRecord
        end,
    },
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
        range = function() return 1, MaxItemRecords end,
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

-- Party[i].Spells[spell]: whether the character knows the spell; writes teach or take it away.
local SpellCount = 132

local function spellsObject(playerIndex, path)
    return boundObject(path, {
        index = function(id)
            id = floor(id)
            return id >= 1 and id <= SpellCount and modrt.playerSpellGet(playerIndex, id) or false
        end,
        newindex = function(id, value)
            id = floor(id)
            return id >= 1 and id <= SpellCount
                and modrt.playerSpellSet(playerIndex, id, value ~= nil and value ~= false and value ~= 0)
        end,
        get = sizeFields(function() return SpellCount + 1 end),
        range = function() return 0, SpellCount end,
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

---------------------------------------------------------------------------------------------------------------------
-- spell buffs
--
-- A buff record as scripts see it: ExpireTime (the game time it runs out at, 0 when it is not running), Power,
-- Skill (level and mastery packed), Caster, OverlayId, Bits. Where OpenYAMM has the buff, ExpireTime, Power, Skill
-- and Caster are its own record. The other buffs, and the other fields, are kept by the runtime in the save: the
-- scripts see what they wrote, the game does nothing with it.

local PartyBuffCount = 20
local PlayerBuffCount = 27
local buffFields = {ExpireTime = true, Power = true, Skill = true, Caster = true, OverlayId = true, Bits = true}
local bonusesDirty = false

-- Buff writes come in runs (time, power, skill); the bonuses are rebuilt once, when the scripts hand control back.
function Bindings.flushBonuses()
    if bonusesDirty then
        bonusesDirty = false
        modrt.partyRefreshBonuses()
    end
end

local function spellBuff(owner, id, path, storeKey)
    local function read(field)
        if field == "Skill" then
            local level = modrt.buffGet(owner, id, "SkillLevel")

            if level ~= nil then
                return JoinSkill(level, modrt.buffGet(owner, id, "SkillMastery") or 0)
            end
        elseif field == "ExpireTime" or field == "Power" or field == "Caster" then
            local value = modrt.buffGet(owner, id, field)

            if value ~= nil then
                return value
            end
        end

        local buffs = ModRT.State.runtime.buffs
        local record = buffs and buffs[storeKey()]
        return record and record[field] or 0
    end

    local function write(field, value)
        value = tonumber(value) or 0
        local taken = false

        if field == "Skill" then
            local level, mastery = SplitSkill(floor(value))
            taken = modrt.buffSet(owner, id, "SkillLevel", level)
                and modrt.buffSet(owner, id, "SkillMastery", mastery)
        elseif field == "ExpireTime" then
            taken = modrt.buffSet(owner, id, field, value)
        elseif field == "Power" or field == "Caster" then
            taken = modrt.buffSet(owner, id, field, floor(value))
        end

        if taken then
            bonusesDirty = true
            return true
        end

        local runtime = ModRT.State.runtime
        runtime.buffs = runtime.buffs or {}
        local key = storeKey()
        runtime.buffs[key] = runtime.buffs[key] or {}
        runtime.buffs[key][field] = value
        return true
    end

    return boundObject(path, {
        get = function(key)
            if buffFields[key] then
                return read(key)
            end

            return nil
        end,
        set = function(key, value)
            if buffFields[key] then
                return write(key, value)
            end

            return false
        end,
        methods = {
            -- buff:Set(expireTime, skill, power, overlayId, caster), as MMExtension has it.
            Set = function(_, time, skill, power, overlay, caster)
                write("ExpireTime", time)
                write("Skill", skill or 1)
                write("Power", power or 1)
                write("OverlayId", overlay or 0)
                write("Caster", caster or 0)
            end,
        },
    })
end

local function spellBuffArray(owner, count, path, storeKey)
    local entry = cached(function(id)
        return spellBuff(owner, id, path .. "[]", function() return storeKey(id) end)
    end)

    return boundObject(path, {
        index = function(id)
            id = floor(id)

            if id >= 0 and id < count then
                return entry(id)
            end

            return nil
        end,
        get = sizeFields(function() return count end),
        range = function() return 0, count - 1 end,
    })
end

local partySpellBuffs = spellBuffArray(-1, PartyBuffCount, "Party.SpellBuffs", function(id)
    return "party:" .. id
end)

-- const.Condition, which is also the engine's own order. A condition reads as the game time it set in at, 0 when
-- the character does not have it; writing 0 lifts it.
local conditionIds = {
    Cursed = 0, Weak = 1, Asleep = 2, Afraid = 3, Drunk = 4, Insane = 5, Poison1 = 6, Disease1 = 7, Poison2 = 8,
    Disease2 = 9, Poison3 = 10, Disease3 = 11, Paralyzed = 12, Unconscious = 13, Dead = 14, Stoned = 15,
    Eradicated = 16, Zombie = 17,
}
local ConditionCount = 18

-- const.Damage numbers the entries of Player.Resistances; 4 and 5 are unused slots in the original structure.
local resistanceNames = {
    [0] = "Fire", [1] = "Air", [2] = "Water", [3] = "Earth", [4] = "Physical", [5] = "Magic", [6] = "Spirit",
    [7] = "Mind", [8] = "Body", [9] = "Light", [10] = "Dark",
}

-- const.Stats for the six resistances a character sheet has.
local resistanceByStat = {[10] = "Fire", [11] = "Air", [12] = "Water", [13] = "Earth", [14] = "Mind", [15] = "Body"}

-- const.Stats numbers the entries of Player.Stats.
local statNames = {
    [0] = "Might", [1] = "Intellect", [2] = "Personality", [3] = "Endurance", [4] = "Speed", [5] = "Accuracy",
    [6] = "Luck",
}

-- An array of {Base, Bonus} pairs over the character's plain fields: Resistances[0].Base is FireResistanceBase.
local function baseBonusArray(playerIndex, path, names, middle)
    local entry = cached(function(index)
        local name = names[index]

        return boundObject(path .. "[]", {
            get = function(key)
                if key == "Base" or key == "Bonus" then
                    return modrt.playerGet(playerIndex, name .. middle .. key)
                end

                return nil
            end,
            set = function(key, value)
                if key == "Base" or key == "Bonus" then
                    return modrt.playerSet(playerIndex, name .. middle .. key, value)
                end

                return false
            end,
        })
    end)

    return boundObject(path, {
        index = function(index)
            if names[index] then
                return entry(index)
            end

            return nil
        end,
        get = sizeFields(function() return #names + 1 end),
        range = function() return 0, #names end,
    })
end

local function playerObject(playerIndex)
    local path = "Party[]"
    local skills = skillsObject(playerIndex, path .. ".Skills")
    local spells = spellsObject(playerIndex, path .. ".Spells")
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

    -- Inventory[cell]: the backpack's cells, 14 x 9 row by row (the item's number in Items where its top left corner
    -- is, -(that cell + 1) in the others it covers, 0 when empty). Read only: items move through the game's own
    -- backpack.
    local inventory = boundObject(path .. ".Inventory", {
        index = function(cell)
            return modrt.inventoryCell(playerIndex, floor(cell))
        end,
        get = sizeFields(function() return 126 end),
        range = function() return 0, 125 end,
    })

    local conditions = boundObject(path .. ".Conditions", {
        index = function(condition)
            return modrt.playerConditionGet(playerIndex, floor(condition))
        end,
        newindex = function(condition, value)
            return modrt.playerConditionSet(playerIndex, floor(condition), tonumber(value) or 0)
        end,
        get = sizeFields(function() return ConditionCount end),
        range = function() return 0, ConditionCount - 1 end,
    })
    local resistances = baseBonusArray(playerIndex, path .. ".Resistances", resistanceNames, "Resistance")
    -- A character's own records follow the character, not the party slot it stands in.
    local spellBuffs = spellBuffArray(playerIndex, PlayerBuffCount, path .. ".SpellBuffs", function(id)
        return (modrt.playerGet(playerIndex, "ModIndex") or -1) .. ":" .. id
    end)
    local stats = baseBonusArray(playerIndex, path .. ".Stats", statNames, "")

    return boundObject(path, {
        get = function(key)
            if key == "Skills" then
                return skills
            elseif key == "Spells" then
                return spells
            elseif key == "Items" then
                return items
            elseif key == "EquippedItems" then
                return equipped
            elseif key == "Inventory" then
                return inventory
            elseif key == "Conditions" then
                return conditions
            elseif key == "Resistances" then
                return resistances
            elseif key == "SpellBuffs" then
                return spellBuffs
            elseif key == "Stats" then
                return stats
            elseif conditionIds[key] then
                return modrt.playerConditionGet(playerIndex, conditionIds[key])
            elseif key == "?ptr" then
                -- Scripts derive a character's index from its address in the original player array. Nothing lives
                -- at this address here; it only has to give that arithmetic the right answer.
                local index = modrt.playerGet(playerIndex, "ModIndex")
                return index and (OriginalPlayerArrayAddress + index * OriginalPlayerSize) or nil
            end

            return modrt.playerGet(playerIndex, key)
        end,
        set = function(key, value)
            if conditionIds[key] then
                return modrt.playerConditionSet(playerIndex, conditionIds[key], tonumber(value) or 0)
            end

            return modrt.playerSet(playerIndex, key, value)
        end,
        methods = {
            -- Not part of MMExtension: casts a spell the game's own way, as if the player had chosen it and
            -- its target. pl:CastSpell(spell, targetMonsterIndex) or pl:CastSpell(spell, nil, targetPartySlot).
            -- Returns whether it went through, and the game's message when it did not.
            CastSpell = function(_, spell, targetMonster, targetPlayer)
                return modrt.castSpell(playerIndex, floor(spell), targetMonster or -1, targetPlayer or -1)
            end,
            -- The condition shown on the portrait (const.Condition); 18, Good, when there is none.
            GetMainCondition = function()
                return modrt.playerGet(playerIndex, "MainCondition")
            end,
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
            -- Awake and able to act: not asleep, paralyzed, unconscious, dead, stoned or eradicated.
            IsConscious = function() return modrt.playerGet(playerIndex, "IsConscious") == true end,
            GetFullHP = function() return modrt.playerGet(playerIndex, "GetFullHP") end,
            GetFullSP = function() return modrt.playerGet(playerIndex, "GetFullSP") end,
            GetArmorClass = function() return modrt.playerGet(playerIndex, "GetArmorClass") end,
            GetMeleeAttack = function() return modrt.playerGet(playerIndex, "GetMeleeAttack") end,
            GetMeleeDamageMin = function() return modrt.playerGet(playerIndex, "GetMeleeDamageMin") end,
            GetMeleeDamageMax = function() return modrt.playerGet(playerIndex, "GetMeleeDamageMax") end,
            GetRangedAttack = function() return modrt.playerGet(playerIndex, "GetRangedAttack") end,
            GetRangedDamageMin = function() return modrt.playerGet(playerIndex, "GetRangedDamageMin") end,
            GetRangedDamageMax = function() return modrt.playerGet(playerIndex, "GetRangedDamageMax") end,
            -- pl:GetResistance(stat): stat is the statistic number of the resistance (const.Stats, 10 Fire to
            -- 15 Body).
            GetResistance = function(_, stat)
                local element = resistanceByStat[stat]
                return element and modrt.playerGet(playerIndex, "GetResistance" .. element) or 0
            end,
            -- Recovery after an attack in ticks: pl:GetAttackDelay() for the main hand, (true) for the bow.
            GetAttackDelay = function(_, ranged)
                return modrt.playerGet(playerIndex, ranged and "GetAttackDelayRanged" or "GetAttackDelay")
            end,
            -- The character's merchant and disarm-trap totals, as the game uses them.
            GetMerchant = function() return modrt.playerGet(playerIndex, "GetMerchant") end,
            GetDisarmTrap = function() return modrt.playerGet(playerIndex, "GetDisarmTrap") end,
            -- The character is busy for this many ticks from now (RecoveryDelay).
            SetRecoveryDelay = function(_, ticks)
                return modrt.playerSet(playerIndex, "RecoveryDelay", floor(ticks or 0))
            end,
        },
    })
end

---------------------------------------------------------------------------------------------------------------------
-- roots

local function partyCount()
    return modrt.partyGet("Count") or 0
end

local playerAt = cached(playerObject)

-- The characters waiting in the Adventurer's Inn are reached as players 1000 up (the natives take them so).
local InnPlayerBase = 1000
local RosterSize = 50

-- A roster place with no character: its fields read as 0, its skills as none, its items as no item; writes go
-- nowhere. The original's roster has 50 records, empty ones zeroed, and scripts walk all of them.
local blankPlayer = cached(function(index)
    local skills = boundObject("Party.PlayersArray[].Skills", {
        index = function() return 0 end,
        newindex = function() return true end,
    })
    local items = boundObject("Party.PlayersArray[].Items", {
        index = function() return emptyItem end,
        get = sizeFields(function() return MaxItemRecords end),
        range = function() return 1, MaxItemRecords end,
    })
    local zeros = boundObject("Party.PlayersArray[].*", {
        index = function() return 0 end,
        newindex = function() return true end,
    })

    return boundObject("Party.PlayersArray[]", {
        get = function(key)
            if key == "Skills" then
                return skills
            elseif key == "Items" then
                return items
            elseif key == "EquippedItems" or key == "Inventory" or key == "Conditions" or key == "Resistances"
                or key == "SpellBuffs" or key == "Stats" or key == "Spells" then
                return zeros
            elseif key == "Name" then
                return ""
            end

            return 0
        end,
        set = function() return true end,
        methods = {
            GetIndex = function() return index end,
            GetSkill = function() return 0 end,
            GetActiveItem = function() return nil end,
            EnumActiveItems = function() return function() return nil end end,
            GetMainCondition = function() return 18 end,
        },
    })
end)

-- The player handle (party slot, or InnPlayerBase + place in the Inn) of the character with roster index `index`.
local function rosterHandle(index)
    for slot = 0, partyCount() - 1 do
        if modrt.playerGet(slot, "ModIndex") == index then
            return slot
        end
    end

    for place = 0, (modrt.partyGet("InnCount") or 0) - 1 do
        if modrt.playerGet(InnPlayerBase + place, "ModIndex") == index then
            return InnPlayerBase + place
        end
    end

    return nil
end

-- The roster of all characters, addressed by a character's own index (GetIndex): the party's members and those
-- waiting in the Adventurer's Inn, 50 places as the original's (a place no character has reads as a blank one).
local playersArray = boundObject("Party.PlayersArray", {
    index = function(index)
        index = floor(index)

        if index < 0 or index >= RosterSize then
            return nil
        end

        local handle = rosterHandle(index)
        return handle and playerAt(handle) or blankPlayer(index)
    end,
    get = sizeFields(function() return RosterSize end),
    range = function() return 0, RosterSize - 1 end,
})

-- Party.PlayersIndexes[slot]: the roster index (GetIndex) of the party member in `slot`.
local playersIndexes = boundObject("Party.PlayersIndexes", {
    index = function(slot)
        slot = floor(slot)

        if slot >= 0 and slot < partyCount() then
            return playerAt(slot):GetIndex()
        end

        return nil
    end,
    get = sizeFields(partyCount),
    range = function() return 0, partyCount() - 1 end,
})

-- Party.QBits[bit]: the party's quest bits, read and written.
local partyQBits = boundObject("Party.QBits", {
    index = function(index)
        return modrt.questBitGet(index)
    end,
    newindex = function(index, value)
        return modrt.questBitSet(index, value and value ~= 0)
    end,
})

Party = boundObject("Party", {
    owned = "Party",
    bound = {
        Gold = true, Food = true, BankGold = true, Count = true, count = true, High = true, high = true,
        CurrentPlayer = true, X = true, Y = true, Z = true, YawRadians = true, PlayersArray = true,
        SpellBuffs = true, QBits = true, PlayersIndexes = true,
    },
    index = function(index)
        if index >= 0 and index < partyCount() then
            return playerAt(index)
        end

        return nil
    end,
    range = function() return 0, partyCount() - 1 end,
    get = function(key)
        local size = sizeFields(partyCount)(key)

        if size ~= nil then
            return size
        elseif key == "PlayersArray" then
            return playersArray
        elseif key == "PlayersIndexes" then
            return playersIndexes
        elseif key == "SpellBuffs" then
            return partySpellBuffs
        elseif key == "QBits" then
            return partyQBits
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

-- Rows of the game's tables. A write goes to the engine's table where `setter` can make it. For the fields it
-- cannot change yet, what a script writes is kept with the row here: scripts read it back, but the game goes on
-- using its own value. Bindings.tableWrites counts those writes for the report, so that they are not mistaken for
-- working.
Bindings.tableWrites = {}

local function isPlainValue(value)
    local kind = type(value)
    return kind == "number" or kind == "string" or kind == "boolean"
end

local function tableRow(path, getter, setter)
    return cached(function(id)
        local written = nil

        return boundObject(path, {
            get = function(key)
                if written then
                    local value = written[key]

                    if value ~= nil then
                        return value
                    end
                end

                return getter(id, key)
            end,
            set = function(key, value)
                -- Only fields the row has; anything else is recorded as a missing binding.
                if value == nil or getter(id, key) == nil then
                    return false
                end

                -- Only plain values go to the engine; a script may also park a stand-in or a table here.
                if setter and isPlainValue(value) and setter(id, key, value) then
                    return true
                end

                written = written or {}
                written[key] = value
                local name = path .. "." .. key
                Bindings.tableWrites[name] = (Bindings.tableWrites[name] or 0) + 1
                return true
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

local mapStatsRow = tableRow("Game.MapStats[]", modrt.mapStatsGet, function(id, key, value)
    if type(value) == "number" then
        value = floor(value)
    end

    return modrt.mapStatsSet(id, key, value)
end)

-- Scripts walk this table from 0 or 1 to High and expect a row at every index. High is the end of the unbroken
-- run of maps from 1: the Merge's maps. OpenYAMM numbers maps of its own far above them (from 9001); those rows
-- are reached by number (Map.MapStatsIndex on such a map) but are not part of the walk. Row 0 is an empty row,
-- as in the original; its fields fall through to stand-ins.
local mergeMapCount = nil

local function mapStatsCount()
    if mergeMapCount == nil then
        local last = 0

        while modrt.mapStatsGet(last + 1, "FileName") ~= nil do
            last = last + 1
        end

        if last == 0 then
            return 0
        end

        mergeMapCount = last + 1
    end

    return mergeMapCount
end

local mapStats = boundObject("Game.MapStats", {
    index = function(id)
        if id < 0 then
            return nil
        elseif id >= mapStatsCount() and modrt.mapStatsGet(id, "FileName") == nil then
            return nil
        end

        return mapStatsRow(id)
    end,
    get = sizeFields(mapStatsCount),
    range = function() return 0, mapStatsCount() - 1 end,
})

local itemRow = tableRow("Game.ItemsTxt[]", modrt.itemTxtGet, function(id, key, value)
    if type(value) == "number" then
        value = floor(value)
    end

    return modrt.itemTxtSet(id, key, value)
end)
---------------------------------------------------------------------------------------------------------------------
-- monster records
--
-- The original has one record layout for a row of the monster table and for a monster on the map (each map
-- monster carries its own copy of its kind's row). Here a kind's row is the engine's table, and a map monster
-- has only a few fields of its own (position, hit points, the name it goes by); everything else it reads from
-- its kind's row. What a script writes to a field the engine cannot take is kept with the record for scripts to
-- read back, and counted in Bindings.tableWrites.

local resistanceKindByField = {
    FireResistance = 0, AirResistance = 1, WaterResistance = 2, EarthResistance = 3, PhysResistance = 4,
    SpiritResistance = 6, MindResistance = 7, BodyResistance = 8, LightResistance = 9, DarkResistance = 10,
}
local MonsterResistanceCount = 11

-- read(field) returns nil for a field the record does not have; write(field, value) returns true when the engine
-- took the value. Fields are flat for both: Attack1.Type is "Attack1Type", Resistances[3] is "Resistance3".
local function monsterRecord(path, read, write, methods)
    local written = nil

    local function get(field)
        if written then
            local value = written[field]

            if value ~= nil then
                return value
            end
        end

        return read(field)
    end

    local function set(field, value)
        if value == nil then
            return false
        end

        if isPlainValue(value) and write(field, type(value) == "number" and floor(value) or value) then
            if written then
                written[field] = nil
            end

            return true
        end

        -- Only fields the record has; anything else is recorded as a missing binding.
        if read(field) == nil then
            return false
        end

        written = written or {}
        written[field] = value
        local name = path .. "." .. field
        Bindings.tableWrites[name] = (Bindings.tableWrites[name] or 0) + 1
        return true
    end

    local function attack(name)
        return boundObject(path .. "." .. name, {
            get = function(key)
                return get(name .. key)
            end,
            set = function(key, value)
                return set(name .. key, value)
            end,
        })
    end

    local attack1, attack2 = attack("Attack1"), attack("Attack2")
    -- Numbered by const.Damage; the original has no magic resistance for monsters (5), which reads as 0.
    local resistances = boundObject(path .. ".Resistances", {
        index = function(kind)
            kind = floor(kind)

            if kind < 0 or kind >= MonsterResistanceCount then
                return nil
            end

            return get("Resistance" .. kind) or 0
        end,
        newindex = function(kind, value)
            kind = floor(kind)

            if kind < 0 or kind >= MonsterResistanceCount then
                return false
            end

            return set("Resistance" .. kind, value) or true
        end,
        get = sizeFields(function() return MonsterResistanceCount end),
        range = function() return 0, MonsterResistanceCount - 1 end,
    })

    local function skill(prefix)
        local level = get(prefix .. "Level")

        if level == nil then
            return nil
        end

        return JoinSkill(level, get(prefix .. "Mastery") or 0)
    end

    return boundObject(path, {
        get = function(key)
            if key == "Attack1" then
                return attack1
            elseif key == "Attack2" then
                return attack2
            elseif key == "Resistances" then
                return resistances
            elseif key == "SpellSkill" or key == "Spell2Skill" then
                -- A value the engine did not take reads back as the script wrote it.
                return (written and written[key]) or skill(key)
            elseif resistanceKindByField[key] then
                return get("Resistance" .. resistanceKindByField[key])
            end

            return get(key)
        end,
        set = function(key, value)
            if resistanceKindByField[key] then
                return set("Resistance" .. resistanceKindByField[key], value)
            end

            -- The engine keeps a spell's skill as its level and mastery.
            if (key == "SpellSkill" or key == "Spell2Skill") and type(value) == "number" then
                local level, mastery = SplitSkill(value)

                if write(key .. "Level", level) and write(key .. "Mastery", mastery) then
                    return true
                end
            end

            return set(key, value)
        end,
        methods = methods,
    })
end

local monsterRow = cached(function(id)
    return monsterRecord("Game.MonstersTxt[]", function(field)
        return modrt.monsterTxtGet(id, field)
    end, function(field, value)
        return modrt.monsterTxtSet(id, field, value)
    end)
end)

local itemsTxt = boundObject("Game.ItemsTxt", {
    index = function(id)
        if modrt.itemTxtGet(id, "Name") == nil then
            return nil
        end

        return itemRow(id)
    end,
    get = sizeFields(function() return modrt.itemTxtGet(0, "#") or 0 end),
})

-- The enchant tables: Game.SpcItemsTxt (an item's Bonus2 - 1) and Game.StdItemsTxt (its Bonus - 1), numbered from
-- 0. Read only; what a script writes to a row stays with the script.
local function enchantTable(path, special, slotCount)
    local function count()
        return modrt.enchantTxtGet(special, 0, "#") or 0
    end

    local row = tableRow(path .. "[]", function(id, key)
        if key == "ChanceForSlot" then
            return boundObject(path .. "[].ChanceForSlot", {
                index = function(slot)
                    return modrt.enchantTxtGet(special, id, "ChanceForSlot", floor(slot)) or 0
                end,
                get = sizeFields(function() return slotCount end),
                range = function() return 0, slotCount - 1 end,
            })
        end

        return modrt.enchantTxtGet(special, id, key)
    end)

    return boundObject(path, {
        index = function(id)
            if id < 0 or id >= count() then
                return nil
            end

            return row(floor(id))
        end,
        get = sizeFields(count),
        range = function() return 0, count() - 1 end,
    })
end

local spcItemsTxt = enchantTable("Game.SpcItemsTxt", true, 12)

-- The texts of the 39 skills, as the original's arrays: Game.SkillNames, Game.SkillDescriptions and Game.SkillDes<Mastery>
-- (what Normal, Expert, Master and GM give). Writes reach what the game shows on the skill page and in the skill's
-- information box.
local function skillTextArray(path, part)
    return boundObject(path, {
        index = function(skill)
            return modrt.skillTextGet(floor(skill), part)
        end,
        newindex = function(skill, value)
            return modrt.skillTextSet(floor(skill), part, tostring(value))
        end,
        get = sizeFields(function() return 39 end),
        range = function() return 0, 38 end,
    })
end

local skillTexts = {
    SkillNames = skillTextArray("Game.SkillNames", 0),
    SkillDescriptions = skillTextArray("Game.SkillDescriptions", 1),
    SkillDesNormal = skillTextArray("Game.SkillDesNormal", 2),
    SkillDesExpert = skillTextArray("Game.SkillDesExpert", 3),
    SkillDesMaster = skillTextArray("Game.SkillDesMaster", 4),
    SkillDesGM = skillTextArray("Game.SkillDesGM", 5),
}

-- Game.GlobalTxt[i]: the original's interface texts (global.txt). Writes are kept, and those the game shows (the
-- labels of the stats page) reach the screen; the original text written back gives the game its own label again.
local globalTexts
local globalTextWrites = {}

local function globalText(index)
    if not globalTexts then
        globalTexts = {}
        local source = modrt.readAsset("engine/data_tables/english/Global.txt") or ""

        for line in source:gmatch("[^\n]+") do
            local id, text = line:match("^(%d+)\t([^\t\r]*)")

            if id then
                globalTexts[tonumber(id)] = text:match('^"(.*)"$') or text
            end
        end
    end

    return globalTexts[index]
end

local globalTxt = boundObject("Game.GlobalTxt", {
    index = function(index)
        index = floor(index)
        local written = globalTextWrites[index]

        if written ~= nil then
            return written
        end

        return globalText(index) or ""
    end,
    newindex = function(index, value)
        index = floor(index)
        value = tostring(value)
        globalTextWrites[index] = value
        modrt.globalTextSet(index, value == globalText(index) and "" or value)
        return true
    end,
})

-- Game.HouseRules.Training[i].Quality: the highest level the training halls of the original's number i train to
-- (MAW raises them). Other house rules are not bound.
local trainingRules = boundObject("Game.HouseRules.Training", {
    index = function(index)
        index = floor(index)

        if modrt.trainingLevel(index) == nil then
            return nil
        end

        return setmetatable({}, {
            __index = function(_, key)
                if key == "Quality" then
                    return modrt.trainingLevel(index)
                end
            end,
            __newindex = function(_, key, value)
                if key == "Quality" then
                    modrt.trainingLevel(index, floor(tonumber(value) or 0))
                end
            end,
        })
    end,
})
local houseRules = boundObject("Game.HouseRules", {
    get = function(key)
        if key == "Training" then
            return trainingRules
        end
    end,
})

-- Game.StatsDescriptions[0..6]: what the stats page's information box says of Might to Luck.
local statsDescriptions = boundObject("Game.StatsDescriptions", {
    index = function(stat)
        return modrt.statDescription(floor(stat))
    end,
    newindex = function(stat, value)
        return modrt.statDescription(floor(stat), tostring(value))
    end,
    get = sizeFields(function() return 7 end),
    range = function() return 0, 6 end,
})

-- Game.NPCText[id]: the game's NPC texts (what teachers and other people say), read-only.
local npcText = boundObject("Game.NPCText", {
    index = function(id)
        return modrt.npcText(floor(id))
    end,
})

-- Game.NPC[id]: an NPC as the game has it now. Name, Pic, House, Profession, Greet and Hired; Events[0..5] (also
-- EventA ... EventF) its topics, which a write sets as evt.SetNPCTopic does.
local npcEventLetters = {EventA = 0, EventB = 1, EventC = 2, EventD = 3, EventE = 4, EventF = 5}
local npcObjects = {}

local function npcObject(id)
    local object = npcObjects[id]

    if object then
        return object
    end

    local events = boundObject("Game.NPC[].Events", {
        index = function(slot)
            return slot >= 0 and slot <= 5 and modrt.npcGet(id, "Event" .. floor(slot)) or nil
        end,
        newindex = function(slot, value)
            return slot >= 0 and slot <= 5 and modrt.npcSet(id, "Event" .. floor(slot), floor(tonumber(value) or 0))
        end,
        get = sizeFields(function() return 6 end),
        range = function() return 0, 5 end,
    })

    object = boundObject("Game.NPC[]", {
        get = function(key)
            if key == "Events" then
                return events
            elseif npcEventLetters[key] then
                return modrt.npcGet(id, "Event" .. npcEventLetters[key])
            end

            return modrt.npcGet(id, key)
        end,
        set = function(key, value)
            if npcEventLetters[key] then
                return modrt.npcSet(id, "Event" .. npcEventLetters[key], floor(tonumber(value) or 0))
            end

            return modrt.npcSet(id, key, value)
        end,
    })
    npcObjects[id] = object
    return object
end

local npcs = boundObject("Game.NPC", {
    index = function(id)
        return modrt.npcGet(floor(id), "Name") ~= nil and npcObject(floor(id)) or nil
    end,
})

-- Game.NPCTopic[event]: a topic's name; a write renames it wherever it is shown.
local npcTopics = boundObject("Game.NPCTopic", {
    index = function(id)
        return modrt.npcTopicGet(floor(id))
    end,
    newindex = function(id, value)
        modrt.npcTopicSet(floor(id), tostring(value or ""))
        return true
    end,
})

-- Game.QuestsTxt[bit]: the quest log's text for a quest bit; a write shows it there while the bit is set.
local questsTxt = boundObject("Game.QuestsTxt", {
    index = function(id)
        return modrt.questTextGet(floor(id))
    end,
    newindex = function(id, value)
        modrt.questTextSet(floor(id), tostring(value or ""))
        return true
    end,
})

-- Game.ClassDescriptions[class]: what the game says of a class.
local classDescriptions = boundObject("Game.ClassDescriptions", {
    index = function(class)
        return modrt.classDescription(floor(class))
    end,
    newindex = function(class, value)
        return modrt.classDescription(floor(class), tostring(value))
    end,
})

-- Game.Spells: the spell table by spell number. SpellPoints<Mastery> and Delay<Mastery> read and write the game's
-- figures (casting goes by a written one); SpellPoints[mastery] and Delay[mastery] are the same by number, 1 Normal
-- to 4 GM.
local masteryNames = {"Normal", "Expert", "Master", "GM"}

local spellRow = tableRow("Game.Spells[]", function(id, key)
    if key == "SpellPoints" or key == "Delay" then
        return boundObject("Game.Spells[]." .. key, {
            index = function(mastery)
                local name = masteryNames[floor(mastery)]
                return name and modrt.spellTxtGet(id, key .. name) or nil
            end,
            newindex = function(mastery, value)
                local name = masteryNames[floor(mastery)]
                return name ~= nil and type(value) == "number" and modrt.spellTxtSet(id, key .. name, floor(value))
            end,
        })
    end

    local value = modrt.spellTxtGet(id, key)

    -- A number the engine has no spell for is an empty row, as row 0 is in the original: its figures are 0.
    if value == nil and (key:sub(1, 11) == "SpellPoints" or key:sub(1, 5) == "Delay") then
        return 0
    end

    return value
end, function(id, key, value)
    return type(value) == "number" and modrt.spellTxtSet(id, key, floor(value))
end)

local spellsTxt = boundObject("Game.Spells", {
    index = function(id)
        id = floor(id)

        if id < 0 or id > (modrt.spellTxtGet(0, "#") or 0) then
            return nil
        end

        return spellRow(id)
    end,
    get = sizeFields(function() return (modrt.spellTxtGet(0, "#") or 0) + 1 end),
    range = function() return 0, modrt.spellTxtGet(0, "#") or 0 end,
})
local stdItemsTxt = enchantTable("Game.StdItemsTxt", false, 9)

-- Game.SpellsTxt[id]: a spell's texts, Name, ShortName, Description and Normal, Expert, Master, GM (what each mastery
-- gives). Writes reach the spellbook, the name included (MAW's Death Knights call Heal "Blood Leech").
local SpellTextFields = {
    Name = true, ShortName = true, Description = true, Normal = true, Expert = true, Master = true, GM = true,
}

local function spellTextRow(id)
    return boundObject("Game.SpellsTxt[]", {
        get = function(key)
            if not SpellTextFields[key] then
                return nil
            end

            return modrt.spellTextGet(id, key)
        end,
        set = function(key, value)
            if not SpellTextFields[key] then
                return false
            end

            return modrt.spellTextSet(id, key, tostring(value))
        end,
    })
end

local spellTextRowAt = cached(spellTextRow)
local spellTexts = boundObject("Game.SpellsTxt", {
    index = function(id)
        id = floor(id)

        if id < 0 or id > (modrt.spellTxtGet(0, "#") or 0) then
            return nil
        end

        return spellTextRowAt(id)
    end,
    get = sizeFields(function() return (modrt.spellTxtGet(0, "#") or 0) + 1 end),
    range = function() return 0, modrt.spellTxtGet(0, "#") or 0 end,
})

-- Rows are numbered from 1, as monsters are. Scripts walk the table from 1 to High and expect a row at every
-- number, so High is the end of the unbroken run from 1: the Merge's monsters. OpenYAMM numbers a few monsters
-- of its own far above them; those rows can be reached by number but are not part of the walk.
local monsterKinds = nil

local function monsterKindCount()
    if monsterKinds == nil then
        local last = 0

        while modrt.monsterTxtGet(last + 1, "Name") ~= nil do
            last = last + 1
        end

        if last == 0 then
            return 0
        end

        monsterKinds = last + 1
    end

    return monsterKinds
end

-- Row 0 is an empty record in the original, and scripts that walk the table from 0 read it: every field row 1
-- has is there, blank or zero. What a script writes to it is kept for scripts.
local blankMonsterRow = monsterRecord("Game.MonstersTxt[0]", function(field)
    local sample = modrt.monsterTxtGet(1, field)

    if type(sample) == "string" then
        return ""
    elseif sample ~= nil then
        return 0
    end

    return nil
end, function()
    return false
end)

local monstersTxt = boundObject("Game.MonstersTxt", {
    index = function(id)
        if id == 0 then
            return blankMonsterRow
        elseif modrt.monsterTxtGet(id, "Name") == nil then
            return nil
        end

        return monsterRow(id)
    end,
    get = sizeFields(monsterKindCount),
    range = function() return 0, monsterKindCount() - 1 end,
})

-- The names placed monsters go by (Map.Monsters[i].NameId points here; 0 is none). Mods add names of their own.
local placeMonTxt = boundObject("Game.PlaceMonTxt", {
    index = function(index)
        return modrt.placeMonGet(floor(index))
    end,
    newindex = function(index, value)
        index, value = floor(index), tostring(value)

        if modrt.placeMonGet(index) == value then
            return true
        end

        local written = modrt.placeMonSet(index, value)

        -- The map's monsters going by this name show it: MAW sets a boss's NameId, then writes its name.
        if written then
            for monster = 0, (modrt.mapMonsterGet(0, "#") or 0) - 1 do
                if modrt.mapMonsterGet(monster, "NameId") == index then
                    modrt.mapMonsterSet(monster, "NameId", index)
                end
            end
        end

        return written
    end,
    get = sizeFields(function() return modrt.placeMonGet("#") or 0 end),
    range = function() return 0, (modrt.placeMonGet("#") or 0) - 1 end,
})

-- Game.HostileTxt[actor][target]: how monsters of class `actor` treat those of class `target` (0 the party; a
-- monster's class is ceil(Id / 3)), 0 not hostile, 1 to 4 hostile from that distance up. The game's own table:
-- MAW makes zombies and ghouls attack the party and leave each other alone, Merge's map scripts change it for their
-- map (LocalHostileTxt).
local function hostileClassCount()
    return modrt.hostileGet() or 0
end

local hostileRow = cached(function(actor)
    return boundObject("Game.HostileTxt[]", {
        index = function(target)
            return modrt.hostileGet(actor, floor(target))
        end,
        newindex = function(target, value)
            return modrt.hostileSet(actor, floor(target), floor(tonumber(value) or 0))
        end,
        get = sizeFields(hostileClassCount),
        range = function() return 0, hostileClassCount() - 1 end,
    })
end)

local hostileTxt = boundObject("Game.HostileTxt", {
    index = function(actor)
        actor = floor(actor)
        return actor >= 0 and actor < hostileClassCount() and hostileRow(actor) or nil
    end,
    get = sizeFields(hostileClassCount),
    range = function() return 0, hostileClassCount() - 1 end,
})

-- Game.MonListBin[id]: a monster kind's list entry, by monster id from 1: Height, Radius, Velocity, Radius2 (the
-- radius for being hit). Monsters take them as they are made.
local monListFields = {Height = true, Radius = true, Velocity = true, Radius2 = true}

local monListEntry = cached(function(id)
    return boundObject("Game.MonListBin[]", {
        get = function(key)
            return monListFields[key] and modrt.monListGet(id, key) or nil
        end,
        set = function(key, value)
            return monListFields[key] and modrt.monListSet(id, key, floor(tonumber(value) or 0)) or false
        end,
    })
end)

local function monListHigh()
    return (modrt.monListGet("#") or 1) - 1
end

local monListBin = boundObject("Game.MonListBin", {
    index = function(id)
        id = floor(id)
        return id >= 1 and id <= monListHigh() and monListEntry(id) or nil
    end,
    get = function(key)
        if key == "High" or key == "high" then
            return monListHigh()
        elseif key == "Count" or key == "count" or key == "Length" or key == "length" then
            return monListHigh()
        end

        return nil
    end,
    range = function() return 1, monListHigh() end,
})

-- Merge's LocalHostileTxt(): what the map's scripts write to Game.HostileTxt from now on lasts until the party
-- leaves the map (state.lua puts the table back on every map change).
function LocalHostileTxt()
    modrt.hostileKeep()
end

---------------------------------------------------------------------------------------------------------------------
-- classes: Game.Classes.HPFactor[class] and its sibling arrays, Game.Classes.Skills[class][skill] (the highest
-- mastery the class can reach), Game.ClassNames[class], Game.ClassesExtra[class].Kind/.Step.

local function classCount()
    return modrt.classGet(0, "#") or 0
end

local function classArray(path, field, writable)
    return boundObject(path, {
        index = function(class)
            return modrt.classGet(floor(class), field)
        end,
        newindex = writable and function(class, value)
            if type(value) ~= "number" then
                return false
            end

            return modrt.classSet(floor(class), field, floor(value))
        end or nil,
        get = sizeFields(classCount),
        range = function() return 0, classCount() - 1 end,
    })
end

local classSkills = cached(function(class)
    return boundObject("Game.Classes.Skills[]", {
        index = function(skill)
            local name = skillName(floor(skill))
            return name and modrt.classSkillGet(class, name) or nil
        end,
        -- The highest mastery the class can reach, 0 none to 4 Grandmaster; the game's teachers go by it.
        newindex = function(skill, value)
            local name = skillName(floor(skill))
            return name ~= nil and type(value) == "number" and modrt.classSkillSet(class, name, floor(value))
        end,
        get = sizeFields(function() return 39 end),
        range = function() return 0, 38 end,
    })
end)

local classParts = {
    HPFactor = classArray("Game.Classes.HPFactor", "HPFactor", true),
    HPBase = classArray("Game.Classes.HPBase", "HPBase", true),
    SPFactor = classArray("Game.Classes.SPFactor", "SPFactor", true),
    SPBase = classArray("Game.Classes.SPBase", "SPBase", true),
    SPStats = classArray("Game.Classes.SPStats", "SPStats", true),
    Skills = boundObject("Game.Classes.Skills", {
        index = function(class)
            class = floor(class)

            if class < 0 or class >= classCount() then
                return nil
            end

            return classSkills(class)
        end,
        get = sizeFields(classCount),
        range = function() return 0, classCount() - 1 end,
    }),
}

local classes = boundObject("Game.Classes", {
    get = function(key)
        return classParts[key]
    end,
})

local classNames = classArray("Game.ClassNames", "Name", false)

local classExtraRow = cached(function(class)
    return boundObject("Game.ClassesExtra[]", {
        get = function(key)
            if key == "Kind" or key == "Step" then
                return modrt.classGet(class, key)
            end

            return nil
        end,
    })
end)

local classesExtra = boundObject("Game.ClassesExtra", {
    index = function(class)
        class = floor(class)

        if class < 0 or class >= classCount() then
            return nil
        end

        return classExtraRow(class)
    end,
    get = sizeFields(classCount),
    range = function() return 0, classCount() - 1 end,
})

-- Game.GetStatisticEffect(value): what a statistic's value is worth (13 gives 0, 15 gives 1, ...).
local function getStatisticEffect(value)
    return modrt.statisticEffect(floor(tonumber(value) or 0))
end

-- Game.Houses[id]: a house of the house table. Val is its price multiplier; a script's Val stays with the scripts
-- and reaches the engine where it asks them for a house's prices (GetShopItemTreatment: identifying, repairing).
-- OpenHour and CloseHour are the house's own (MAW opens at 5 what opens at 6); a number missing from the table
-- reads as no house.
local houseVals = {}

function Bindings.houseVal(id)
    return houseVals[id] or modrt.houseGet(id, "Val")
end

local houses = boundObject("Game.Houses", {
    index = function(id)
        id = floor(id)

        return setmetatable({}, {
            __index = function(_, field)
                if field == "Val" then
                    return Bindings.houseVal(id)
                end

                return modrt.houseGet(id, field)
            end,
            __newindex = function(_, field, value)
                if field == "Val" then
                    houseVals[id] = tonumber(value)
                elseif (field == "OpenHour" or field == "CloseHour") and tonumber(value) then
                    modrt.houseSet(id, field, floor(tonumber(value)))
                end
            end,
        })
    end,
    get = sizeFields(function() return modrt.houseGet("#") or 0 end),
    range = function() return 0, (modrt.houseGet("#") or 0) - 1 end,
})

-- Game.ShopItems[house][slot] and Game.ShopSpecialItems[house][slot] (slots 0 to 11): a shop's goods and special
-- goods, made as a visit makes them if the shop has none yet. An empty slot reads as no item.
local function shopGoods(group)
    local houses = {}

    return boundObject("Game." .. (group == "shop" and "ShopItems" or "ShopSpecialItems"), {
        index = function(houseId)
            houseId = floor(houseId)

            houses[houseId] = houses[houseId] or boundObject("Game.ShopItems[]", {
                index = function(slot)
                    slot = floor(slot)

                    if modrt.itemGet(houseId, group, slot, "Number") == nil then
                        return emptyItem
                    end

                    return itemObject(houseId, group, slot, "Game.ShopItems[][]")
                end,
            })

            return houses[houseId]
        end,
    })
end

local shopItems = shopGoods("shop")
local shopSpecialItems = shopGoods("shopSpecial")

-- The house whose dialog is open (0 for none): Merge's GetCurrentHouse. While a house names its prices
-- (GetShopItemTreatment) it is that house.
function GetCurrentHouse()
    return Bindings.pricingHouse or modrt.gameGet("CurrentHouse") or 0
end

Game = boundObject("Game", {
    owned = "Game",
    bound = {
        ItemsTxt = true, MonstersTxt = true, CharacterPortraits = true, MapStats = true, PlaceMonTxt = true,
        SpcItemsTxt = true, StdItemsTxt = true, Spells = true, CurrentScreen = true,
        SkillNames = true, SkillDescriptions = true, SkillDesNormal = true, SkillDesExpert = true,
        SkillDesMaster = true, SkillDesGM = true, ClassDescriptions = true,
        CurrentPlayer = true, Time = true, Classes = true, ClassNames = true, ClassesExtra = true,
        HouseScreen = true, GlobalTxt = true, CurrentCharScreen = true, NPCText = true, HouseRules = true,
        SpellsTxt = true, StatsDescriptions = true, NPC = true, NPCTopic = true, QuestsTxt = true,
        TurnBased = true, TurnBasedPhase = true, Houses = true, ShopItems = true, ShopSpecialItems = true,
        HostileTxt = true, MonListBin = true, Party = true,
    },
    get = function(key)
        if key == "ItemsTxt" then
            return itemsTxt
        elseif key == "SpcItemsTxt" then
            return spcItemsTxt
        elseif skillTexts[key] then
            return skillTexts[key]
        elseif key == "ClassDescriptions" then
            return classDescriptions
        elseif key == "GlobalTxt" then
            return globalTxt
        elseif key == "NPCText" then
            return npcText
        elseif key == "NPC" then
            return npcs
        elseif key == "NPCTopic" then
            return npcTopics
        elseif key == "QuestsTxt" then
            return questsTxt
        elseif key == "HouseRules" then
            return houseRules
        elseif key == "SpellsTxt" then
            return spellTexts
        elseif key == "StatsDescriptions" then
            return statsDescriptions
        elseif key == "CurrentCharScreen" then
            -- The character screen's page, by const.CharScreens: 100 stats, 101 skills, 102 awards, 103 inventory.
            return modrt.gameGet("CurrentCharScreen") or 100
        elseif key == "CurrentScreen" then
            -- Which screen is up, by const.Screens: 0 the game itself, 7 the character screen, 13 a house...
            return modrt.gameGet("CurrentScreen") or 0
        elseif key == "Houses" then
            return houses
        elseif key == "ShopItems" then
            return shopItems
        elseif key == "ShopSpecialItems" then
            return shopSpecialItems
        elseif key == "TurnBased" then
            return (modrt.gameGet("TurnBasedPhase") or 0) ~= 0
        elseif key == "TurnBasedPhase" then
            -- 1 the monsters' turn, 2 the party's attacks, 3 the party walking; 0 out of turn-based combat.
            return modrt.gameGet("TurnBasedPhase") or 0
        elseif key == "Spells" then
            return spellsTxt
        elseif key == "StdItemsTxt" then
            return stdItemsTxt
        elseif key == "MonstersTxt" then
            return monstersTxt
        elseif key == "CharacterPortraits" then
            return characterPortraits
        elseif key == "MapStats" then
            return mapStats
        elseif key == "PlaceMonTxt" then
            return placeMonTxt
        elseif key == "HostileTxt" then
            return hostileTxt
        elseif key == "MonListBin" then
            return monListBin
        elseif key == "Party" then
            -- MMExtension's other name for Party (MAW's next-character key reads Game.Party.High).
            return Party
        elseif key == "Classes" then
            return classes
        elseif key == "ClassNames" then
            return classNames
        elseif key == "ClassesExtra" then
            return classesExtra
        elseif key == "CurrentPlayer" then
            return modrt.partyGet("CurrentPlayer")
        elseif key == "HouseScreen" and (modrt.gameGet("ShopPage") or 0) ~= 0 then
            -- The shop page open: 2 its goods, 95 its special goods.
            return modrt.gameGet("ShopPage")
        elseif key == "HouseScreen" then
            -- Which screen of a house's dialogue is up. Houses are not bound yet; the one thing scripts ask is
            -- whether an item is being made for a shop's shelf, which is the buying screen, 2.
            return ModRT.Stats.itemGenerationMode == 1 and 2 or 0
        elseif key == "Time" then
            -- MMExtension counts game time in ticks, 256 to the game minute.
            local minutes = modrt.gameGet("GameMinutes")
            return minutes and floor(minutes * 256) or nil
        end

        return nil
    end,
    methods = {
        -- Called as Game:ExitHouseScreen(): leaves the screen that is open (the character screen; not yet a house's
        -- dialog).
        ExitHouseScreen = function()
            modrt.exitScreen()
        end,
        ShowStatusText = function(text)
            if ModRT.Stats.inAction then
                ModRT.Stats.actionStatusText = tostring(text or "")
            end

            modrt.showStatusText(text)
        end,
        GetStatisticEffect = getStatisticEffect,
        -- Called as Game:GetCurrentHouse(): the house whose dialog is open, nil outside a house (MMExtension's; Merge's
        -- global GetCurrentHouse gives 0 there). MAW tells "not in a house" by `not Game:GetCurrentHouse()`.
        GetCurrentHouse = function()
            local id = GetCurrentHouse()
            return id ~= 0 and id or nil
        end,
        -- The game builds an NPC dialog's topics again after each topic, which is when scripts ask for it.
        UpdateDialogTopics = function() end,
        -- The map's monsters the party can see, by index: those within 5120 in its line of sight (the original
        -- also needs them on the screen).
        GetMonstersInSight = function()
            return modrt.monstersInSight()
        end,
        -- The engine's random number: 0 to 32767.
        Rand = function()
            return math.random(0, 32767)
        end,
    },
    set = function(key, value)
        -- Setting the phase away from 3 ends the party's turn-based walk (MAW's Doom limits it so); the engine moves
        -- through its other phases itself.
        if key == "TurnBasedPhase" then
            if tonumber(value) ~= 3 and modrt.gameGet("TurnBasedPhase") == 3 then
                modrt.turnBasedEndWalk()
            end

            return true
        end

        return false
    end,
})

local function monsterCount()
    return modrt.mapMonsterGet(0, "#") or 0
end

-- A monster on the map: its own fields first, then its kind's row.
local mapMonsterCache = {}
-- Fields of monster spell effects the engine does not keep (Skill, Caster, Bits, and whole effects it has no
-- field for), by monster and effect; they stay with the scripts.
local monsterBuffWrites = {}
-- Game time runs at 128 ticks to the real second, which is how the engine counts an effect's time.
local TicksPerSecond = 128
local MonsterBuffCount = 26

-- Map.Monsters[i].SpellBuffs[b]: a monster's spell effects by const.MonsterBuff. ExpireTime (game time, 0 when the
-- effect is off) and Power are the engine's; see ModActorBuffs.h for which effects it has.
local function monsterSpellBuffs(index)
    local buffs = {}

    local function buff(id)
        local key = index .. ":" .. id

        return setmetatable({}, {
            __index = function(_, field)
                local seconds, power = modrt.monsterBuffGet(index, id)
                local stash = monsterBuffWrites[key]

                if seconds ~= nil and field == "ExpireTime" then
                    return seconds > 0 and Game.Time + floor(seconds * TicksPerSecond) or 0
                elseif seconds ~= nil and field == "Power" then
                    return power
                end

                return stash and stash[field] or 0
            end,
            __newindex = function(_, field, value)
                value = tonumber(value) or 0

                if field == "ExpireTime" then
                    local seconds = math.max(0, (value - Game.Time) / TicksPerSecond)

                    if modrt.monsterBuffSet(index, id, seconds, nil) then
                        return
                    end
                elseif field == "Power" and modrt.monsterBuffSet(index, id, nil, floor(value)) then
                    return
                end

                monsterBuffWrites[key] = monsterBuffWrites[key] or {}
                monsterBuffWrites[key][field] = value
            end,
        })
    end

    return boundObject("Map.Monsters[].SpellBuffs", {
        index = function(id)
            id = floor(id)

            if id < 0 or id >= MonsterBuffCount then
                return nil
            end

            buffs[id] = buffs[id] or buff(id)
            return buffs[id]
        end,
        get = sizeFields(function() return MonsterBuffCount end),
        range = function() return 0, MonsterBuffCount - 1 end,
    })
end

local function mapMonster(index)
    local monster = mapMonsterCache[index]

    if monster == nil then
        monster = monsterRecord("Map.Monsters[]", function(field)
            local value = modrt.mapMonsterGet(index, field)

            if value ~= nil then
                return value
            end

            local kind = modrt.mapMonsterGet(index, "Id")
            return kind and modrt.monsterTxtGet(kind, field) or nil
        end, function(field, value)
            local written = modrt.mapMonsterSet(index, field, value)

            if written and (field == "Bonus" or field == "BonusMul") then
                Bindings.rememberMonsterSpecial(index, field, value)
            end

            return written
        end, {
            GetIndex = function()
                return index
            end,
            -- How the monster stands toward the party: 0 friendly, else the original's strongest relation (4). The
            -- engine keeps one notion of hostility to the party; another monster as the argument is not followed.
            IsAgainst = function()
                return modrt.mapMonsterGet(index, "Hostile") and 4 or 0
            end,
            -- Not a method: the record hands out its entries as they are.
            SpellBuffs = monsterSpellBuffs(index),
        })
        mapMonsterCache[index] = monster
    end

    return monster
end

-- What scripts wrote to the monsters of one map does not carry over to the next, and neither does the speed they
-- gave a monster: a map's monsters start every arrival at their kind's speed, as in the original.
function Bindings.resetMapMonsters()
    mapMonsterCache = {}
    monsterBuffWrites = {}
    modrt.mapMonstersResetSpeed()
end

-- A monster's special attack (Bonus) and its strength (BonusMul) as a script set them stay with it, as they do in the
-- original's map data: they are kept with the save by map and monster, and given back when the map is entered
-- (MAW's Doom gives them once per map).
local function monsterSpecials(mapName, create)
    local runtime = ModRT.State.runtime
    runtime.monsterSpecials = runtime.monsterSpecials or {}

    if create then
        runtime.monsterSpecials[mapName] = runtime.monsterSpecials[mapName] or {}
    end

    return runtime.monsterSpecials[mapName]
end

function Bindings.rememberMonsterSpecial(index, field, value)
    local mapName = (modrt.gameGet("MapFileName") or ""):lower()

    if mapName ~= "" then
        local specials = monsterSpecials(mapName, true)
        specials[index] = specials[index] or {}
        specials[index][field] = math.floor(tonumber(value) or 0)
    end
end

function Bindings.restoreMonsterSpecials(mapName)
    for index, fields in pairs(monsterSpecials(mapName, false) or {}) do
        for field, value in pairs(fields) do
            modrt.mapMonsterSet(index, field, value)
        end
    end
end

-- Map.Chests[i]: Trapped is known while a chest is being opened (CanOpenChest); the engine's chests are not bound
-- otherwise.
local openingChest = {}

function Bindings.setOpeningChest(chestId, trapped)
    openingChest = chestId and {[chestId] = trapped} or {}
end

-- Map.Objects[i]: the items lying on the map. X, Y and Z are where one lies (a write moves it); Item the item, as a
-- backpack's are (Bonus, BonusStrength, Charges...; MAW sets a dropped reagent's power), its Number 0 for a place
-- holding another object. While PickObject runs, the object being picked up has the item itself as Item (writes to
-- the object, as MAW makes the item vanish, are the engine's to do once a handler took it).
local pickedObject = nil

function Bindings.setPickedObject(objectId)
    pickedObject = objectId
end

local function mapObject(index)
    -- Its item: Number (a write makes it one of that kind, looking it: MAW's randomizer), the other fields as a
    -- backpack item's, and T(), the item's row.
    local item = boundObject("Map.Objects[].Item", {
        get = function(key)
            if key == "Number" then
                return modrt.worldItemGet(index, "Number") or 0
            end

            return modrt.itemGet(index, "world", 0, key)
        end,
        set = function(key, value)
            if key == "Number" then
                return tonumber(value) ~= nil and modrt.worldItemSetNumber(index, floor(tonumber(value)))
            end

            return modrt.itemSet(index, "world", 0, key, value)
        end,
        methods = {
            T = function()
                return Game.ItemsTxt[modrt.worldItemGet(index, "Number") or 0]
            end,
        },
    })

    return setmetatable({}, {
        __index = function(_, key)
            if key == "Item" then
                return item
            elseif key == "X" or key == "Y" or key == "Z" then
                return modrt.worldItemGet(index, key)
            end

            return 0
        end,
        __newindex = function(_, key, value)
            if key == "X" or key == "Y" or key == "Z" then
                local position = {X = modrt.worldItemGet(index, "X"), Y = modrt.worldItemGet(index, "Y"),
                    Z = modrt.worldItemGet(index, "Z")}

                if position.X and tonumber(value) then
                    position[key] = tonumber(value)
                    modrt.worldItemMove(index, position.X, position.Y, position.Z)
                end
            end
        end,
    })
end

local function objectCount()
    return modrt.worldItemGet(0, "#") or 0
end

local mapObjects = boundObject("Map.Objects", {
    index = function(index)
        index = floor(index)

        if pickedObject ~= nil and index == pickedObject then
            return {Item = Bindings.eventItem, Type = 0, TypeIndex = 0}
        end

        if index >= 0 and index < objectCount() then
            return mapObject(index)
        end

        return nil
    end,
    get = sizeFields(objectCount),
    range = function() return 0, objectCount() - 1 end,
})

-- Map.Chests[i].Items[k].Number: the item records of the map's chests (k from 0; negative a random item of that
-- level), read and written; a chest already opened keeps the contents it had (MAW's randomizer changes them on
-- arriving at a map).
local function chestItems(chestId)
    local function count()
        return modrt.chestGet(chestId, "ItemCount") or 0
    end

    return boundObject("Map.Chests[].Items", {
        index = function(slot)
            slot = floor(slot)

            if slot < 0 or slot >= count() then
                return nil
            end

            return setmetatable({}, {
                __index = function(_, key)
                    if key == "Number" then
                        return modrt.chestGet(chestId, "Number", slot) or 0
                    elseif key == "T" then
                        return function(self)
                            return Game.ItemsTxt[self.Number]
                        end
                    elseif key == "Randomize" then
                        -- A random item of that strength, made when the chest is opened (the record's number
                        -- -strength, as the map's own random items are; MAW makes artifacts placed in chests so).
                        return function(self, strength)
                            modrt.chestSet(chestId, "Number", slot, -math.max(1, math.min(6, floor(strength or 1))))
                        end
                    end

                    return 0
                end,
                __newindex = function(_, key, value)
                    if key == "Number" and tonumber(value) then
                        modrt.chestSet(chestId, "Number", slot, floor(tonumber(value)))
                    end
                end,
            })
        end,
        get = sizeFields(count),
        range = function() return 0, count() - 1 end,
    })
end

local function chestCount()
    return modrt.chestGet(0, "#") or 0
end

local mapChests = boundObject("Map.Chests", {
    index = function(index)
        index = floor(index)
        local trapped = openingChest[index]
        return {Trapped = trapped == true, Items = chestItems(index)}
    end,
    get = sizeFields(chestCount),
    range = function() return 0, chestCount() - 1 end,
})

local MapMonsterLimit = 3000
local monsterSizes = sizeFields(monsterCount)

local mapMonsters = boundObject("Map.Monsters", {
    index = function(index)
        if index >= 0 and index < monsterCount() then
            return mapMonster(index)
        end

        return nil
    end,
    get = function(key)
        -- The engine has no limit on a map's monsters; this is the Merge's, which scripts leave room under.
        if key == "Limit" or key == "limit" then
            return MapMonsterLimit
        end

        return monsterSizes(key)
    end,
    range = function() return 0, monsterCount() - 1 end,
})

-- The original's sign for "no floor here".
local NoFloorLevel = -30000

-- Map.Notes[0..99]: the current map's notes as the map screen shows them (Active, X, Y, Text; Id reads 0).
local MapNoteCount = 100
local mapNote = cached(function(slot)
    return boundObject("Map.Notes[]", {
        get = function(key)
            return modrt.mapNoteGet(slot, key)
        end,
        set = function(key, value)
            if key == "Id" then
                return true
            end

            modrt.mapNoteSet(slot, key, value)
            return key == "Active" or key == "X" or key == "Y" or key == "Text"
        end,
    })
end)

local mapNotes = boundObject("Map.Notes", {
    index = function(slot)
        slot = floor(slot)
        return slot >= 0 and slot < MapNoteCount and mapNote(slot) or nil
    end,
    get = sizeFields(function() return MapNoteCount end),
    range = function() return 0, MapNoteCount - 1 end,
})

Map = boundObject("Map", {
    owned = "Map",
    bound = {Monsters = true, Name = true, MapStatsIndex = true, IndoorOrOutdoor = true, TilesetsFile = true,
        Notes = true, Chests = true, Objects = true},
    get = function(key)
        if key == "Notes" then
            return mapNotes
        elseif key == "Chests" then
            return mapChests
        elseif key == "Objects" then
            return mapObjects
        elseif key == "Monsters" then
            return mapMonsters
        elseif key == "TilesetsFile" then
            -- Which of the Merge's three tile tables the map uses. The engine keeps one.
            return 0
        elseif key == "Name" then
            return modrt.gameGet("MapFileName")
        elseif key == "MapStatsIndex" then
            return modrt.gameGet("MapStatsIndex")
        elseif key == "IndoorOrOutdoor" then
            -- the original's numbers: 1 indoor, 2 outdoor
            local indoor = modrt.gameGet("IsIndoor")

            if indoor == nil then
                return nil
            end

            return indoor and 1 or 2
        end

        return nil
    end,
    methods = {
        -- Called without the object, as functions: Map.IsIndoor().
        IsIndoor = function()
            return modrt.gameGet("IsIndoor") == true
        end,
        IsIndoors = function()
            return modrt.gameGet("IsIndoor") == true
        end,
        IsOutdoor = function()
            return modrt.gameGet("IsIndoor") == false
        end,
        IsOutdoors = function()
            return modrt.gameGet("IsIndoor") == false
        end,
        -- The height of the ground under a point of an outdoor map.
        GetGroundLevel = function(x, y)
            local room, level = modrt.mapPointInfo(x, y, 0)
            return level and floor(level) or 0
        end,
        -- The room (sector) of an indoor map a point is in; 0 when it is in none, as inside a wall.
        RoomFromPoint = function(x, y, z)
            if type(x) == "table" then
                x, y, z = x.X, x.Y, x.Z
            end

            return (modrt.mapPointInfo(x, y, z or 0)) or 0
        end,
        -- The height of the floor under a point.
        GetFloorLevel = function(x, y, z, room)
            local found, level = modrt.mapPointInfo(x, y, z or 0)
            return level and floor(level) or NoFloorLevel
        end,
    },
})

-- SummonMonster(id, x, y, z, treasure, place): puts one monster of the kind on the map and returns its record. The
-- engine scatters what it summons around the point, so the monster is then put on the point, as in the original.
-- A summoned monster is hostile to the party whatever its kind. `place` (which slot of the list to reuse) has no
-- meaning here; the monster's treasure is its kind's either way.
function SummonMonster(id, x, y, z, treasure, place)
    x, y, z = x or Party.X, y or Party.Y, z or Party.Z
    local index = modrt.summonMonster(floor(id), x, y, z)

    if not index then
        return nil
    end

    local monster = Map.Monsters[index]
    monster.X, monster.Y, monster.Z = floor(x), floor(y), floor(z)
    return monster, index
end

-- SummonItem(number, x, y, z, speed): an item of that kind on the map at the point, nil when the game could not
-- place it. As the original, it returns the map object (Map.Objects), whose Item scripts then adjust: MAW sets a
-- dropped reagent's power and a Dimensional Map's affixes that way. When the game did not tell where it put the
-- item, a stand-in object takes the writes, which then do not reach the item.
function SummonItem(number, x, y, z, speed)
    number = floor(number)
    local placed = modrt.summonItem(number, x or Party.X, y or Party.Y, z or Party.Z, floor(speed or 0))

    if not placed then
        return nil
    elseif math.type(placed) == "integer" then
        return Map.Objects[placed]
    end

    return {Item = {Number = number, Bonus = 0, BonusStrength = 0, Bonus2 = 0, Charges = 0, MaxCharges = 0}}
end

-- The original's message box. Dialogue boxes are not bound yet, so the text goes to the status line, colour marks
-- and line breaks taken out, and in full to the log.
function Message(text, keepSound, global)
    text = tostring(text or "")
    local line = text:gsub("\f%d%d%d%d%d", ""):gsub("%s*\n%s*", " ")
    modrt.showStatusText(line)
    print("[message] " .. text)
end

SimpleMessage = Message

-- The original asks the player and returns the reply, the game waiting meanwhile. Here the question is the game's
-- answer prompt (the text and "Answer: _"), and the asking function waits for the reply when it runs in a coroutine
-- (ModRT.async); elsewhere, or with no map to ask on, the text is said and the reply is empty: no.
function Question(text, questionText)
    if coroutine.isyieldable() and ModRT.MapEvents.askQuestion(tostring(text or "")) then
        return tostring(coroutine.yield() or "")
    end

    Message(text)
    return ""
end

-- Keys.IsPressed(key): whether the key (a Windows virtual key, as const.Keys numbers them) is held, as far as the
-- key events that reached the scripts say.
rawset(Keys, "IsPressed", function(key)
    return ModRT.keysDown[key] == true
end)

-- Mouse.Item: the item the player holds on the mouse pointer; with none, an empty item (Number 0).
local heldItem = itemObject(0, "held", 0, "Mouse.Item")

rawset(Mouse, "Item", setmetatable({}, {
    __index = function(_, key)
        if modrt.heldItemPresent() then
            return heldItem[key]
        end

        return key == "Number" and 0 or emptyItem[key]
    end,
    __newindex = function(_, key, value)
        if modrt.heldItemPresent() then
            heldItem[key] = value
        end
    end,
}))

-- What is under the mouse pointer: the monster whose information box is being built, or the one a spell being cast
-- is aimed at (Kind 3), else nothing (Kind 0).
-- Mouse:GetTarget(): what is under the mouse pointer, Kind 3 a map monster (Index). During a monster's information
-- box or a spell's cast, the monster the event is about; otherwise the monster the game shows as hovered.
rawset(Mouse, "GetTarget", function()
    if Bindings.mouseTarget then
        return Bindings.mouseTarget
    end

    local hovered = modrt.hoveredMonster()
    return hovered and {Kind = 3, Index = hovered} or {Kind = 0, Index = 0}
end)
