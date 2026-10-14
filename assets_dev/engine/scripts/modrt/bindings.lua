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
    fallback("AIState", {
        Stand = 0, Active = 1, MeleeAttack = 2, RangedAttack = 3, Dying = 4, Dead = 5, Pursue = 6, Flee = 7,
        Stunned = 8, Fidget = 9, Interact = 10, Removed = 11, RangedAttack2 = 12, CastSpell = 13, Stoned = 14,
        Paralyzed = 15, Resurrect = 16, Summoned = 17, RangedAttack4 = 18, Invisible = 19,
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
            elseif key == "Items" then
                return items
            elseif key == "EquippedItems" then
                return equipped
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
    -- Iteration visits the characters that exist, in the order of their indexes.
    next = function(control)
        local best = nil
        local bestSlot = nil

        for slot = 0, partyCount() - 1 do
            local index = modrt.playerGet(slot, "ModIndex")

            if index and (control == nil or index > control) and (best == nil or index < best) then
                best = index
                bestSlot = slot
            end
        end

        if best == nil then
            return nil
        end

        return best, playerAt(bestSlot)
    end,
})

Party = boundObject("Party", {
    owned = "Party",
    bound = {
        Gold = true, Food = true, BankGold = true, Count = true, count = true, High = true, high = true,
        CurrentPlayer = true, X = true, Y = true, Z = true, YawRadians = true, PlayersArray = true,
        SpellBuffs = true,
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
        elseif key == "SpellBuffs" then
            return partySpellBuffs
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
                return skill(key)
            elseif resistanceKindByField[key] then
                return get("Resistance" .. resistanceKindByField[key])
            end

            return get(key)
        end,
        set = function(key, value)
            if resistanceKindByField[key] then
                return set("Resistance" .. resistanceKindByField[key], value)
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
        return modrt.placeMonSet(floor(index), tostring(value))
    end,
    get = sizeFields(function() return modrt.placeMonGet("#") or 0 end),
    range = function() return 0, (modrt.placeMonGet("#") or 0) - 1 end,
})

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

Game = boundObject("Game", {
    owned = "Game",
    bound = {
        ItemsTxt = true, MonstersTxt = true, CharacterPortraits = true, MapStats = true, PlaceMonTxt = true,
        CurrentPlayer = true, Time = true, Classes = true, ClassNames = true, ClassesExtra = true,
    },
    get = function(key)
        if key == "ItemsTxt" then
            return itemsTxt
        elseif key == "MonstersTxt" then
            return monstersTxt
        elseif key == "CharacterPortraits" then
            return characterPortraits
        elseif key == "MapStats" then
            return mapStats
        elseif key == "PlaceMonTxt" then
            return placeMonTxt
        elseif key == "Classes" then
            return classes
        elseif key == "ClassNames" then
            return classNames
        elseif key == "ClassesExtra" then
            return classesExtra
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
        GetStatisticEffect = getStatisticEffect,
        -- The engine's random number: 0 to 32767.
        Rand = function()
            return math.random(0, 32767)
        end,
    },
})

local function monsterCount()
    return modrt.mapMonsterGet(0, "#") or 0
end

-- A monster on the map: its own fields first, then its kind's row.
local mapMonsterCache = {}

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
            return modrt.mapMonsterSet(index, field, value)
        end, {
            GetIndex = function()
                return index
            end,
        })
        mapMonsterCache[index] = monster
    end

    return monster
end

-- What scripts wrote to the monsters of one map does not carry over to the next, and neither does the speed they
-- gave a monster: a map's monsters start every arrival at their kind's speed, as in the original.
function Bindings.resetMapMonsters()
    mapMonsterCache = {}
    modrt.mapMonstersResetSpeed()
end

local mapMonsters = boundObject("Map.Monsters", {
    index = function(index)
        if index >= 0 and index < monsterCount() then
            return mapMonster(index)
        end

        return nil
    end,
    get = sizeFields(monsterCount),
    range = function() return 0, monsterCount() - 1 end,
})

Map = boundObject("Map", {
    owned = "Map",
    bound = {Monsters = true, Name = true, MapStatsIndex = true, IndoorOrOutdoor = true},
    get = function(key)
        if key == "Monsters" then
            return mapMonsters
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
    },
})
