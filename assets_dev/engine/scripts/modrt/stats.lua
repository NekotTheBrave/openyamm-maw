-- Stat events, in the shape MMExtension gives them.
--
-- The original engine computes a character's bonus to each statistic from three sources on demand: equipped items,
-- magic, and skills, and MMExtension raises an event at each. OpenYAMM instead rebuilds all of a character's bonuses
-- in one pass (Party::rebuildMagicalBonusesFromBuffs). The engine side calls in here at the end of that pass with
-- what it worked out natively per statistic; the events run with those values as the default result, and what they
-- leave behind is what the game uses.

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
    ModRT.ensureSession()
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

        local total = wholeNumber(items.Result, fromItems) + wholeNumber(magic.Result, fromMagic)
        totals[#totals + 1] = total
        -- Stored at once: handlers for the statistics that follow may ask for this one.
        modrt.setStatBonus(slot, stat, total)
    end

    return totals
end

-- flat = {skillId, baseLevel, mastery, bonus, ...}; returns the level bonus per skill, in the same order.
-- GetSkill's result is the packed skill value including bonuses; only its level part can change here.
function Stats.characterSkills(slot, flat)
    ModRT.ensureSession()
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
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return ticks
    end

    local event = {Ranged = ranged, Result = ticks, Player = player, PlayerIndex = player:GetIndex()}
    ModRT.fire("GetAttackDelay", event)
    return wholeNumber(event.Result, ticks)
end

-- What a character's skills add to one statistic (HP, SP, armor class, attack, damage); `bonus` is the engine's
-- answer.
function Stats.skillStatBonus(slot, stat, bonus)
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return bonus
    end

    local event = {Stat = stat, Result = bonus, Player = player, PlayerIndex = player:GetIndex()}
    ModRT.fire("CalcStatBonusBySkills", event)
    return wholeNumber(event.Result, bonus)
end
