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

-- memberIndex is the party slot (-1 when the party is not the attacker); spellId 0 means no spell.
function Combat.damageToMonster(actorIndex, memberIndex, rawDamage, resolvedDamage, damageKind, spellId, melee)
    ModRT.ensureSession()
    local player = memberIndex >= 0 and Party[memberIndex] or nil
    local playerIndex = player and player:GetIndex() or nil
    local attack = {}

    if player then
        attack.Player = player
        attack.PlayerIndex = playerIndex
    end

    if spellId and spellId > 0 then
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
