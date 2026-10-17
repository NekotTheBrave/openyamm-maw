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

-- Description of the attack on a party member being resolved, and that member's party slot.
local playerHit = nil
local playerHitSlot = nil

function WhoHitPlayer()
    return playerHit, playerHitSlot
end

-- True, attack when a player is the target of the attack being handled; false, attack when a monster is.
function IsAttackOnPlayer()
    if playerHit then
        return true, playerHit
    elseif monsterHit then
        return false, monsterHit
    end

    return nil
end

-- The last attack on each monster, kept so that MonsterKilled can say who did it.
local lastMonsterHit = nil
local lastMonsterHitIndex = nil

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
    lastMonsterHit, lastMonsterHitIndex = attack, actorIndex

    -- A party member's spell: as in the original, its damage is the CalcSpellDamage event's before the monster's
    -- resistances and CalcDamageToMonster (MAW computes all spell damage there: its dice, Intellect and Personality,
    -- crits). Result comes in as the engine's roll; HP is the monster's (Mass Distortion takes a share of it). The
    -- engine's resistance roll is kept in proportion.
    if player and fromObject and spellId and spellId > 0 then
        local monster = event.Monster
        local spellEvent = {
            Spell = spellId, Skill = skillLevel or 0, Mastery = skillMastery or 0, Result = rawDamage,
            HP = monster and monster.HP or 0, Player = player, PlayerIndex = playerIndex, Monster = monster,
            MonsterIndex = actorIndex,
        }
        ModRT.fire("CalcSpellDamage", spellEvent)
        local spellDamage = spellEvent.Result

        if type(spellDamage) == "number" and spellDamage == spellDamage and spellDamage ~= rawDamage then
            spellDamage = math.max(0, floor(spellDamage))
            resolvedDamage = rawDamage > 0 and floor(resolvedDamage * spellDamage / rawDamage + 0.5) or spellDamage
            rawDamage = spellDamage
            event.Damage, event.Result = rawDamage, resolvedDamage
        end
    end

    ModRT.fire("CalcDamageToMonster", event)
    monsterHit, monsterHitIndex = previousHit, previousIndex

    -- The original's AfterMonsterAttacked: Monster, MonsterIndex, and Attacker as WhoHitMonster has it. Raised here,
    -- before the engine takes the hit off, which its one user does not mind (MAW gives the monster its combat speed).
    ModRT.fire("AfterMonsterAttacked", {Monster = event.Monster, MonsterIndex = actorIndex, Attacker = attack})

    local result = event.Result

    if type(result) ~= "number" or result ~= result then
        return resolvedDamage
    end

    if result < 0 then
        result = 0
    end

    return floor(result)
end

-- A monster's attack on the party member in `slot`, as WhoHitPlayer returns it; nil when no monster is behind the
-- damage (a trap). action says which of the monster's attacks it was: 0 and 1 its attacks, 2 and 3 its spells.
local function monsterAttack(slot, actorIndex, action, spellId, fromObject)
    if actorIndex < 0 then
        return nil
    end

    local attack = {Monster = Map.Monsters[actorIndex], MonsterIndex = actorIndex, MonsterAction = action}

    if spellId and spellId > 0 then
        attack.Spell = spellId
    end

    if fromObject then
        local monster = attack.Monster
        attack.Object = attackObject(nil, -1, actorIndex, spellId or 0, 0, 0, monster.X, monster.Y, monster.Z)
        attack.Object.Owner = actorIndex * 8 + 3
        attack.Object.Target = slot * 8 + 4

        -- A monster's arrow has no spell number in the original either.
        if not (spellId and spellId > 0) then
            attack.Object.Spell = 0
            attack.Object.SpellType = 0
            attack.Object.Item = noItem
        end
    end

    return attack
end

-- A party member takes damage. slot is the member's party slot; actorIndex the attacking monster, -1 when there is
-- none.
function Combat.damageToPlayer(slot, rawDamage, resolvedDamage, damageKind, actorIndex, action, spellId, fromObject)
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return resolvedDamage
    end

    local attack = monsterAttack(slot, actorIndex, action, spellId, fromObject)

    local event = {
        DamageKind = damageKind,
        Damage = rawDamage,
        Result = resolvedDamage,
        Player = player,
        PlayerIndex = player:GetIndex(),
    }

    local previousHit, previousSlot = playerHit, playerHitSlot
    playerHit, playerHitSlot = attack, slot
    ModRT.fire("CalcDamageToPlayer", event)
    playerHit, playerHitSlot = previousHit, previousSlot

    local result = event.Result

    if type(result) ~= "number" or result ~= result then
        return resolvedDamage
    end

    if result < 0 then
        result = 0
    end

    return floor(result)
end

-- A monster has died. Handlers get the monster, its index, a function that would run the original engine's own
-- handling (nothing to run here: the engine has done its part), and the attack that killed it when the party's
-- last hit was on this monster.
function Combat.monsterKilled(actorIndex)
    ModRT.ensureSession()
    local killer = nil

    if lastMonsterHitIndex == actorIndex then
        killer = lastMonsterHit
        lastMonsterHit, lastMonsterHitIndex = nil, nil
    end

    ModRT.fire("MonsterKilled", Map.Monsters[actorIndex], actorIndex, function() end, killer)
end

-- A monster's attack is about to be resolved against the member in `slot`. A handler may pick another member
-- (t.PlayerSlot) or take the attack over (t.Handled). Returns handled, slot.
function Combat.playerAttacked(slot, actorIndex, action, spellId, fromObject)
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return false, slot
    end

    local attack = monsterAttack(slot, actorIndex, action, spellId, fromObject) or {}
    local event = {Attacker = attack, Player = player, PlayerSlot = slot, Handled = false}

    local previousHit, previousSlot = playerHit, playerHitSlot
    playerHit, playerHitSlot = attack, slot
    ModRT.fire("PlayerAttacked", event, attack)
    playerHit, playerHitSlot = previousHit, previousSlot

    local chosen = event.PlayerSlot

    if type(chosen) ~= "number" or chosen ~= floor(chosen) or not Party[chosen] then
        chosen = slot
    end

    return event.Handled and true or false, chosen
end

-- A monster's special attack is about to be tried on the member in `slot`; thing is its const.MonsterBonus
-- number. Returns whether it may go ahead.
function Combat.badThingToPlayer(slot, actorIndex, thing)
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return true
    end

    local event = {Thing = thing, Allow = true, Player = player, PlayerIndex = player:GetIndex()}

    if actorIndex >= 0 then
        event.Monster = Map.Monsters[actorIndex]
        event.MonsterIndex = actorIndex
    end

    ModRT.fire("DoBadThingToPlayer", event)
    return event.Allow and true or false
end

-- A party member's spell is about to take effect. Object references name what a spell is aimed at by a kind and
-- a number: kind 3 is a monster, 4 a party member. A handler may change Skill and Mastery, or set Handled to take
-- the cast over. Returns handled, skill, mastery.
function Combat.playerCastSpell(slot, spellId, manaCost, skill, mastery, targetActor, targetSlot, fromScroll)
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return false, skill, mastery
    end

    local targetKind, targetId = 0, 0

    if targetActor >= 0 then
        targetKind, targetId = 3, targetActor
    elseif targetSlot >= 0 then
        targetKind, targetId = 4, targetSlot
    end

    local event = {
        SpellId = spellId, Player = player, PlayerIndex = player:GetIndex(), InventorySlot = 0,
        SPCost = manaCost, Skill = skill, Mastery = mastery, TargetKind = targetKind, TargetId = targetId,
        IsSpellScroll = fromScroll, CurseInterrupt = false, Handled = false,
    }
    -- In the original the player clicks the spell's target, which is then under the mouse: MAW's own crowd control
    -- (castCCSpell) takes its target from Mouse:GetTarget().
    local Bindings = ModRT.Bindings
    local previousTarget = Bindings.mouseTarget

    if targetKind == 3 then
        Bindings.mouseTarget = {Kind = 3, Index = targetId}
    end

    ModRT.fire("PlayerCastSpell", event)
    Bindings.mouseTarget = previousTarget

    local newSkill, newMastery = event.Skill, event.Mastery

    if type(newSkill) ~= "number" or newSkill < 1 then
        newSkill = skill
    end

    if type(newMastery) ~= "number" or newMastery < 1 or newMastery > 4 then
        newMastery = mastery
    end

    return event.Handled and true or false, floor(newSkill), floor(newMastery)
end

-- The experience for a monster the party has just killed. In the original it comes from the monster's own record,
-- not from its kind's row, so a value a script gave this one monster wins over the engine's answer. Handlers may
-- change t.Exp or set t.Handled to give none.
function Combat.monsterKillExperience(actorIndex, experience)
    ModRT.ensureSession()
    local monster = Map.Monsters[actorIndex]

    if not monster then
        return experience
    end

    local own = monster.Experience
    local kind = monster.Id and Game.MonstersTxt[monster.Id]

    if type(own) == "number" and kind and own ~= kind.Experience then
        experience = own
    end

    local event = {Exp = experience, Handled = false, Monster = monster, MonsterIndex = actorIndex}
    ModRT.fire("MonsterKillExp", event)

    if event.Handled or type(event.Exp) ~= "number" or event.Exp ~= event.Exp or event.Exp <= 0 then
        return 0
    end

    return floor(event.Exp)
end

-- The roll for the party's attack on a monster. PlayerHitOrMiss is MAW's own event for it, raised from a patch of
-- the original engine; Range is 0 for a blow and 1 for a shot. Returns whether the attack lands.
function Combat.playerHitOrMiss(slot, actorIndex, ranged, hit)
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return hit
    end

    local event = {
        Range = ranged and 1 or 0, Bonus = 0, Result = hit, Player = player, PlayerIndex = player:GetIndex(),
        Monster = Map.Monsters[actorIndex], MonsterIndex = actorIndex,
    }
    ModRT.fire("PlayerHitOrMiss", event)
    return event.Result and true or false
end

-- The roll for a monster's blow or arrow on a party member. The original has no event here; MonsterHitOrMiss is
-- this runtime's own.
function Combat.monsterHitOrMiss(actorIndex, slot, hit)
    ModRT.ensureSession()
    local player = Party[slot]

    if not player then
        return hit
    end

    local event = {
        Result = hit, Monster = Map.Monsters[actorIndex], MonsterIndex = actorIndex, Player = player,
        PlayerIndex = player:GetIndex(), PlayerSlot = slot,
    }
    ModRT.fire("MonsterHitOrMiss", event)
    return event.Result and true or false
end
