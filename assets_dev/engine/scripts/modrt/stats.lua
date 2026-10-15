-- Stat events, in the shape MMExtension gives them.
--
-- The original engine computes a character's bonus to each statistic from three sources on demand: equipped items,
-- magic, and skills, and MMExtension raises an event at each. OpenYAMM instead rebuilds all of a character's bonuses
-- in one pass (Party::rebuildMagicalBonusesFromBuffs). The engine side calls in here at the end of that pass with
-- what it worked out natively per statistic; the events run with those values as the default result, and what they
-- leave behind is what the game uses.

-- None of these start the session (the per-game scripts and the map events). They are called from the middle of
-- the engine's own calculations, and a map's handlers must not run halfway through a bonus rebuild: MAW builds its
-- item table in AfterLoadMap from the characters' skills, which a rebuild in progress has not answered yet. The
-- session starts from the calls the game makes between its own work (a tick, an event, a console command).

local ModRT = ModRT
local modrt = modrt
local Stats = {}
ModRT.Stats = Stats

local floor = math.floor
local mathType = math.type
local type = type

local function wholeNumber(value, fallback)
    if type(value) ~= "number" or value ~= value then
        return fallback
    end

    if mathType(value) == "float" then
        if value >= 2147483647 then
            return 2147483647
        elseif value <= -2147483648 then
            return -2147483648
        end

        return floor(value)
    end

    return value
end

-- Artifact and magic item bonuses to one statistic do not stack in the original engine: the largest applies.
-- Handlers raise them through these two methods of the event table.
local function setArtifactBonus(event, value)
    local old = event.ArtifactBonus

    if value > old then
        event.ArtifactBonus = value
        event.Result = event.Result + value - old
    end
end

local function setMagicBonus(event, value)
    local old = event.MagicBonus

    if value > old then
        event.MagicBonus = value
        event.Result = event.Result + value - old
    end
end

-- flat = {statId, fromItems, fromMagic, statId, fromItems, fromMagic, ...}; returns the total bonus per statistic,
-- in the same order.
function Stats.characterBonuses(slot, flat)
    local player = Party[slot]
    local totals = {}

    if not player then
        for index = 1, #flat, 3 do
            totals[#totals + 1] = flat[index + 1] + flat[index + 2]
        end

        return totals
    end

    local playerIndex = player:GetIndex()

    for index = 1, #flat, 3 do
        local stat, fromItems, fromMagic = flat[index], flat[index + 1], flat[index + 2]

        local items = {
            Stat = stat,
            Result = fromItems,
            IgnoreExtraHand = false,
            ArtifactBonus = 0,
            MagicBonus = 0,
            SetArtifactBonus = setArtifactBonus,
            SetMagicBonus = setMagicBonus,
            Player = player,
            PlayerIndex = playerIndex,
        }
        ModRT.fire("CalcStatBonusByItems", items)

        local magic = {Stat = stat, Result = fromMagic, Player = player, PlayerIndex = playerIndex}
        ModRT.fire("CalcStatBonusByMagic", magic)

        local itemsAnswer = wholeNumber(items.Result, fromItems)
        local magicAnswer = wholeNumber(magic.Result, fromMagic)
        totals[#totals + 1] = itemsAnswer + magicAnswer
        -- Handed over at once: handlers for the statistics that follow may ask for this one.
        modrt.setStatBonus(slot, stat, itemsAnswer, magicAnswer)
    end

    return totals
end

-- flat = {skillId, baseLevel, mastery, bonus, ...}; returns the level bonus per skill, in the same order.
-- GetSkill's result is the packed skill value including bonuses; only its level part can change here.
function Stats.characterSkills(slot, flat)
    local player = Party[slot]
    local bonuses = {}

    if not player then
        for index = 1, #flat, 4 do
            bonuses[#bonuses + 1] = flat[index + 3]
        end

        return bonuses
    end

    local playerIndex = player:GetIndex()

    for index = 1, #flat, 4 do
        local skill, baseLevel, mastery, bonus = flat[index], flat[index + 1], flat[index + 2], flat[index + 3]
        local native = baseLevel > 0 and JoinSkill(baseLevel + bonus, mastery) or 0
        local event = {Skill = skill, Result = native, Player = player, PlayerIndex = playerIndex}
        ModRT.fire("GetSkill", event)
        local result = wholeNumber(event.Result, native)

        if result == native then
            bonuses[#bonuses + 1] = bonus
        else
            local level = SplitSkill(result < 0 and 0 or result)
            bonuses[#bonuses + 1] = level - baseLevel
        end
    end

    return bonuses
end

-- The effect of a statistic's value; `effect` is the engine's answer. Asked for constantly, from anywhere in the
-- engine, so this does nothing but run the event.
function Stats.statisticEffect(value, effect)
    local event = {Value = value, Result = effect}
    ModRT.fire("GetStatisticEffect", event)
    return wholeNumber(event.Result, effect)
end

-- Recovery after an attack in ticks; `ticks` is the engine's answer.
function Stats.attackDelay(slot, ranged, ticks)
    local player = Party[slot]

    if not player then
        return ticks
    end

    local event = {Ranged = ranged, Result = ticks, Player = player, PlayerIndex = player:GetIndex()}
    ModRT.fire("GetAttackDelay", event)
    return wholeNumber(event.Result, ticks)
end

-- What an item is worth; `value` is the engine's answer. The item is the one the engine is pricing, wherever it is
-- (a backpack, a shop's shelf, a chest), reached as the event's own item.
function Stats.itemValue(value)
    local event = {Item = ModRT.Bindings.eventItem, Value = value}
    ModRT.fire("CalcItemValue", event)
    return wholeNumber(event.Value, value)
end

-- A character's total in a skill the game asks of one character: GetMerchantTotalSkill, GetDisarmTrapTotalSkill or
-- GetLearningTotalSkill (the percentage more experience they get), with the engine's answer in Result.
function Stats.playerSkillTotal(eventName, slot, value)
    local player = Party[slot]

    if not player then
        return value
    end

    local event = {Result = value, Player = player, PlayerIndex = player:GetIndex()}
    ModRT.fire(eventName, event)
    return wholeNumber(event.Result, value)
end

-- Whether the party can identify (repair) the event's item: CanIdentifyItem (CanRepairItem), with the engine's
-- answer in CanIdentify (CanRepair) and the active member as Player.
function Stats.canHandleItem(repair, answer)
    local event = {Item = ModRT.Bindings.eventItem, Player = Party[math.max(0, Game.CurrentPlayer or 0)]}
    local field = repair and "CanRepair" or "CanIdentify"
    event[field] = answer
    ModRT.fire(repair and "CanRepairItem" or "CanIdentifyItem", event)
    return event[field] == true
end

-- A teacher is asked for a mastery of a skill (Merge's numbering) for the member in `slot`: the original's
-- CanTeachSkillMastery. Mastery is 2 Expert, 3 Master, 4 Grandmaster; Allow, Cost and Text come in as the game's
-- verdict (Text what the teacher says: the offer, or why not). Returns them as the handlers leave them.
function Stats.canTeachSkillMastery(slot, skill, mastery, cost, allow, text)
    local event = {
        Skill = skill, Mastery = mastery, Cost = cost, Allow = allow, Text = text, Player = Party[slot],
        PlayerIndex = slot,
    }
    ModRT.fire("CanTeachSkillMastery", event)
    local newCost = tonumber(event.Cost) or cost
    return event.Allow and true or false, math.floor(newCost), tostring(event.Text or text)
end

-- The held item (Mouse.Item) is used on the party member in `slot`: the original's UseMouseItem. A handler that does
-- the use itself sets Allow to false (MAW drinks its potions its own way). Returns Allow.
function Stats.useMouseItem(slot)
    local event = {PlayerSlot = slot, Player = Party[slot], Allow = true}
    ModRT.fire("UseMouseItem", event)
    return event.Allow ~= false
end

-- The held potion is used on an item in the backpack (ModRT.Bindings.eventItem): its evt.PotionEffects function, if
-- the mod has one, as the original calls it (isDrunk false, the item, the potion's power). Returns whether there was
-- one.
function Stats.potionEffect(potion, power)
    local effect = rawget(evt, "PotionEffects")
    effect = type(effect) == "table" and effect[potion]

    if type(effect) ~= "function" then
        return false
    end

    effect(false, ModRT.Bindings.eventItem, power, 0)
    return true
end

-- The member in `slot` is about to learn a spell from a book: the original's CanLearnSpell. NeedMastery comes in as
-- the game's (1 to 4) and goes out as the handlers leave it; above 4 the spell cannot be learned (MAW's Death Knights,
-- Elementalists and the Thief line get their spells from it instead).
function Stats.canLearnSpell(slot, spell, needMastery)
    local player = Party[slot]
    local event = {Player = player, PlayerIndex = player and player:GetIndex(), Spell = spell, NeedMastery = needMastery}
    ModRT.fire("CanLearnSpell", event)
    return math.floor(tonumber(event.NeedMastery) or needMastery)
end

-- The party asks a town hall about its bounty hunt. Not an MMExtension event (the original's scripts hook the game's
-- code for it): a mod that runs the hunts itself answers TownHallBounty by setting Text, which the hall says instead
-- of running its own hunt. Returns the text, or nil.
function Stats.townHallBounty()
    local event = {}
    ModRT.fire("TownHallBounty", event)
    return type(event.Text) == "string" and event.Text or nil
end

-- Monster `index` (Map.Monsters) has died and drops item `itemId` of its kind's death drops (the original's reagent
-- drop; Merge's MonsterItems.lua raises it for its own). Returns whether a handler took the drop over (Handled).
function Stats.monsterDropItem(index, itemId)
    local event = {MonsterIndex = index, Monster = Map.Monsters[index], ItemId = itemId, Handled = false}
    ModRT.fire("MonsterDropItem", event)
    return event.Handled == true
end

-- An item the game made for a mercenary it generates (Merge's NPCMercenaries.lua, which mods edit; not an
-- MMExtension event): t.Item is the item, which the handlers change in place.
function Stats.mercenaryItemGenerated()
    ModRT.fire("MercenaryItemGenerated", {Item = ModRT.Bindings.eventItem})
end

-- The size monster `index` (Map.Monsters) is drawn at: the original's MonsterSpriteScale, its Scale here a factor of
-- the game's own size (the original's is the sprite's own scale, which handlers only multiply). Returns the factor.
function Stats.monsterSpriteScale(index)
    local monster = Map.Monsters[index]

    if not monster then
        return 1
    end

    local event = {Scale = 1, MonsterIndex = index, Monster = monster}
    ModRT.fire("MonsterSpriteScale", event)
    return tonumber(event.Scale) or 1
end

-- The information box of monster `index` (Map.Monsters), as the original's BuildMonsterInformationBox has it: each
-- row a table with its Text (label, tab stop and value), the effects and resistances lists of such tables under their
-- headers, and the Identified flags (the game shows everything it shows). Mouse:GetTarget() is the monster meanwhile.
-- A handler setting a row to false leaves it out. Returns the texts.
function Stats.monsterInformationBox(index, texts)
    local event = {MonsterIndex = index, Monster = Map.Monsters[index], Identified = true}

    for _, name in ipairs({"Name", "HitPoints", "ArmorClass", "Attack", "Damage", "SpellFirst", "SpellSecond"}) do
        event[name] = {Text = texts[name]}
        event["Identified" .. name] = true
    end

    for _, pair in ipairs({{"EffectsHeader", "Effects"}, {"ResistancesHeader", "Resistances"}}) do
        local header, items = pair[1], pair[2]
        event[header] = {Text = texts[header]}
        event[items] = {}

        for i, text in ipairs(texts[items] or {}) do
            event[items][i] = {Text = text, Id = items == "Resistances" and i - 1 or nil}
        end

        event["Identified" .. items] = true
    end

    ModRT.Bindings.mouseTarget = {Kind = 3, Index = index}
    local ok = ModRT.protectedCall(function() ModRT.fire("BuildMonsterInformationBox", event) end,
        "BuildMonsterInformationBox")
    ModRT.Bindings.mouseTarget = nil

    local function text(row)
        if row == false or row == nil then
            return ""
        end

        return tostring(row.Text or "")
    end

    local result = {}

    for _, name in ipairs({"Name", "HitPoints", "ArmorClass", "Attack", "Damage", "SpellFirst", "SpellSecond",
            "EffectsHeader", "ResistancesHeader"}) do
        result[name] = ok and text(event[name]) or texts[name]
    end

    for _, items in ipairs({"Effects", "Resistances"}) do
        result[items] = {}

        for i, original in ipairs(texts[items] or {}) do
            result[items][i] = ok and type(event[items]) == "table" and text(event[items][i]) or original
        end
    end

    return result
end

-- The party member in `slot` would put on the held item, `itemId` (Mouse.Item), and the game lets them: the
-- original's CanWearItem, which handlers answer by setting Available to false. Returns whether they may.
function Stats.canWearItem(slot, itemId)
    local event = {PlayerId = slot, ItemId = itemId, Available = true}
    ModRT.fire("CanWearItem", event)
    return event.Available ~= false
end

-- The information box of a row of the stats page, for Game.CurrentPlayer: the original's BuildStatInformationBox.
-- Stat is the row (0 to 6 the statistics, 7 hit points, 8 spell points, 9 armor class, 10 condition, 11 quick
-- spell, 12 age, 13 level, 14 experience, 15 attack, 16 melee damage, 17 shoot, 18 ranged damage, 19 to 24 the
-- resistances); Text comes in as the game's. Returns the text the handlers leave.
function Stats.statInformationBox(stat, text)
    local event = {Stat = stat, Text = text}
    ModRT.fire("BuildStatInformationBox", event)
    return tostring(event.Text or text)
end

-- The active member (Game.CurrentPlayer) has trained a level: the original's CalcTrainingTime, with Time in ticks
-- (256 to the game minute). Takes and returns game minutes.
function Stats.trainingTime(minutes)
    local event = {Time = math.floor(minutes * 256), Player = Party[math.max(0, Game.CurrentPlayer or 0)]}
    ModRT.fire("CalcTrainingTime", event)
    local ticks = tonumber(event.Time)

    if not ticks or ticks ~= ticks then
        return minutes
    end

    return math.max(0, ticks) / 256
end

-- The player is about to take an action: the original's Action event, raised for the actions the engine reports
-- (23 attack, 25 the quick spell key or an attack spell). A handler that does the action itself sets t.Handled. A
-- handler may ask for another game action in its place with DoGameAction, which the game then takes (MAW has a
-- healer short of spell points attack this way). Returns whether it was handled and the action asked for.
function Stats.action(action, param, param2, withHeldItem)
    local event = {Action = action, Param = param, Param2 = param2, Handled = false}
    local before = Stats.requestedAction
    Stats.requestedAction = nil
    Stats.inAction = true
    -- An action about the held item (133, putting it on the doll) has Mouse.Item the item.
    local previousItem = rawget(Mouse, "Item")

    if withHeldItem then
        rawset(Mouse, "Item", ModRT.Bindings.eventItem)
    end

    ModRT.fire("Action", event)

    if withHeldItem then
        rawset(Mouse, "Item", previousItem)
    end

    Stats.inAction = false
    local requested = Stats.requestedAction
    Stats.requestedAction = before
    return event.Handled == true, requested or 0
end

-- DoGameAction(action, param, param2): asks the game to take an action. Only answered while the Action event is
-- being handled, where the game takes it in place of the action it was about to take; elsewhere it does nothing.
function DoGameAction(action, param, param2)
    if Stats.inAction then
        Stats.requestedAction = math.floor(action or 0)
    end
end

-- A dead monster's loot is about to be rolled. The original raises PickCorpse when the body is searched and then
-- rolls the loot from the monster's own treasure fields, which handlers rewrite; here the loot is rolled when the
-- monster dies, so the event comes then, and what the handlers leave in the monster's fields is what is rolled.
-- Returns the gold dice, the chance and level of an item, whether the item may be of any kind (the original's
-- TreasureItemType 0), and whether the body is to hold anything at all.
function Stats.pickCorpse(index, diceCount, diceSides, itemPercent, itemLevel)
    local monster = Map.Monsters[index]

    if not monster then
        return diceCount, diceSides, itemPercent, itemLevel, false, true
    end

    local event = {MonsterIndex = index, Monster = monster, Allow = true, CallDefault = function() end}
    ModRT.fire("PickCorpse", event)

    local function field(name, fallback)
        local value = monster[name]
        return type(value) == "number" and value or fallback
    end

    return field("TreasureDiceCount", diceCount), field("TreasureDiceSides", diceSides),
        field("TreasureItemPercent", itemPercent), field("TreasureItemLevel", itemLevel),
        monster.TreasureItemType == 0, event.Allow ~= false
end

-- A random item is about to be made. `strength` is its treasure level, which a handler may change; `mode` says what
-- the item is for (0 anything, 1 a shop's shelf, 2 a monster's body, 3 a chest). While a shop's item is being made
-- Game.HouseScreen reads as the shop's buying screen, which is how the original's scripts tell.
function Stats.generateItem(strength, mode)
    local before = Stats.itemGenerationMode
    Stats.itemGenerationMode = mode
    local event = {Item = ModRT.Bindings.eventItem, Strength = strength, Type = 0, Handled = false}
    ModRT.fire("GenerateItem", event)
    Stats.itemGenerationMode = before
    return wholeNumber(event.Strength, strength)
end

-- A random item has been made; the event's item is the item itself, and what handlers write to it is what the game
-- gets.
function Stats.itemGenerated(strength, mode)
    local before = Stats.itemGenerationMode
    Stats.itemGenerationMode = mode
    local event = {Item = ModRT.Bindings.eventItem, Strength = strength, Type = 0, Handled = false}
    ModRT.fire("ItemGenerated", event)
    Stats.itemGenerationMode = before
end

-- The texts of an item's information box. The original raises BuildItemInformationBox three times for one box:
-- with Type, Enchantment and BasicStat; with Description; with Name. A handler tells them apart by which is there.
function Stats.itemTooltip(name, typeText, basicStat, enchantment, description)
    local item = ModRT.Bindings.eventItem
    local lines = {Item = item, Type = typeText, Enchantment = enchantment, BasicStat = basicStat}
    ModRT.fire("BuildItemInformationBox", lines)
    local body = {Item = item, Description = description}
    ModRT.fire("BuildItemInformationBox", body)
    local title = {Item = item, Name = name}
    ModRT.fire("BuildItemInformationBox", title)

    local function text(value, fallback)
        return type(value) == "string" and value or type(value) == "number" and tostring(value) or fallback
    end

    return text(title.Name, name), text(lines.Type, typeText), text(lines.BasicStat, basicStat),
        text(lines.Enchantment, enchantment), text(body.Description, description)
end

-- What a character's skills add to one statistic (HP, SP, armor class, attack, damage); `bonus` is the engine's
-- answer.
function Stats.skillStatBonus(slot, stat, bonus)
    local player = Party[slot]

    if not player then
        return bonus
    end

    local event = {Stat = stat, Result = bonus, Player = player, PlayerIndex = player:GetIndex()}
    ModRT.fire("CalcStatBonusBySkills", event)
    return wholeNumber(event.Result, bonus)
end
