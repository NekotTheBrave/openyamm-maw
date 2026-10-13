-- Combat events, in the shape MMExtension gives them.
--
-- The engine side (game/mod/ModHooks.h) reports a hit with plain numbers; this file turns that into the event table
-- scripts expect and into the attack description WhoHitMonster returns, fires the event, and hands the result back.

local ModRT = ModRT
local modrt = modrt
local Combat = {}
ModRT.Combat = Combat

local floor = math.floor

-- Description of the attack being resolved, as WhoHitMonster returns it.
local monsterHit = nil
local monsterHitIndex = nil

function WhoHitMonster()
    return monsterHit, monsterHitIndex
end

-- True, attack when a player is the target of the attack being handled; false, attack when a monster is.
function IsAttackOnPlayer()
    if monsterHit then
        return false, monsterHit
    end

    return nil
end

-- Merge's numbers for the two missiles that are not spells.
local ArrowSpell = 133
local BlasterSpell = 135
local BlasterSkill = 7

-- The thing in the world that did the damage, as scripts find it in WhoHitMonster().Object: the original engine's
-- map object record. Spell is the spell number, or the arrow's or blaster shot's; SpellSkill the skill level and
-- SpellMastery (the same field as SpellLevel) the mastery it was cast with; Item the weapon that shot it. OpenYAMM
-- has no such record for the scripts to share, so this is a description of the hit, made for the event: the
-- fields a script may read are all there, and what it writes goes nowhere.
local noItem = {
    Number = 0, Bonus = 0, BonusStrength = 0, Bonus2 = 0, Charges = 0, MaxCharges = 0, Condition = 0,
    BodyLocation = 0, Owner = 0, BonusExpireTime = 0, Identified = false, Broken = false, Stolen = false,
}

local function attackObject(player, memberIndex, actorIndex, spellId, skillLevel, skillMastery, x, y, z)
    x, y, z = floor(x or 0), floor(y or 0), floor(z or 0)
    local missile = spellId == 0
    local item = nil

    if missile then
        local mainHand = player and player:GetActiveItem(1)

        if mainHand and mainHand:T().Skill == BlasterSkill then
            spellId = BlasterSpell
            item = mainHand
        else
            spellId = ArrowSpell
            item = player and player:GetActiveItem(2)
        end
    end

    local mastery = skillMastery or 0

    return {
        Type = 0, TypeIndex = 0,
        X = x, Y = y, Z = z, Pos = {[0] = x, [1] = y, [2] = z},
        StartX = x, StartY = y, StartZ = z, StartPos = {[0] = x, [1] = y, [2] = z},
        VelocityX = 0, VelocityY = 0, VelocityZ = 0, Velocity = {[0] = 0, [1] = 0, [2] = 0},
        Direction = 0, LookAngle = 0,
        -- Visible, and for a shot Missile: "the item field holds the weapon that launched this".
        Bits = missile and 0x0101 or 0x0001,
        Visible = true, Temporary = false, HaltTurnBased = false, DroppedByPlayer = false, IgnoreRange = false,
        NoZBuffer = false, SkipAFrame = false, AttachToHead = false, Missile = missile, Removed = false,
        Room = 0, Age = 0, MaxAge = 0, LightMultiplier = 0,
        Item = item or noItem,
        Spell = spellId, SpellType = spellId, SpellSkill = skillLevel or 0, SpellLevel = mastery,
        SpellMastery = mastery,
        -- Object references pack a kind into the low three bits: 4 is a party member, 3 a monster.
        Owner = memberIndex >= 0 and memberIndex * 8 + 4 or 0, Target = actorIndex * 8 + 3,
        Range = 0, AttackType = 0,
    }
end

-- memberIndex is the party slot (-1 when the party is not the attacker); spellId 0 means no spell. fromObject is
-- true for a missile or a spell's effect, and then the remaining arguments describe it.
function Combat.damageToMonster(
    actorIndex, memberIndex, rawDamage, resolvedDamage, damageKind, spellId, melee,
    fromObject, skillLevel, skillMastery, objectX, objectY, objectZ)
    ModRT.ensureSession()
    local player = memberIndex >= 0 and Party[memberIndex] or nil
    local playerIndex = player and player:GetIndex() or nil
    local attack = {}

    if player then
        attack.Player = player
        attack.PlayerIndex = playerIndex
    end

    if fromObject then
        attack.Object = attackObject(
            player, memberIndex, actorIndex, spellId or 0, skillLevel, skillMastery, objectX, objectY, objectZ)
        attack.Spell = attack.Object.Spell
        attack.SpellSkill = attack.Object.SpellSkill
        attack.SpellMastery = attack.Object.SpellMastery
    elseif spellId and spellId > 0 then
        attack.Spell = spellId
    end

    local event = {
        DamageKind = damageKind,
        Damage = rawDamage,
        Result = resolvedDamage,
        ByPlayer = player ~= nil,
        Melee = melee,
        Player = player,
        PlayerIndex = playerIndex,
        MonsterIndex = actorIndex,
        Monster = Map.Monsters[actorIndex],
    }

    local previousHit, previousIndex = monsterHit, monsterHitIndex
    monsterHit, monsterHitIndex = attack, actorIndex
    ModRT.fire("CalcDamageToMonster", event)
    monsterHit, monsterHitIndex = previousHit, previousIndex

    local result = event.Result

    if type(result) ~= "number" or result ~= result then
        return resolvedDamage
    end

    if result < 0 then
        result = 0
    end

    return floor(result)
end
