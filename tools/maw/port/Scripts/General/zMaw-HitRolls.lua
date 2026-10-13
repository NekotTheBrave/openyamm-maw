-- OpenYAMM addition to MAW: the two rolls that decide whether an attack lands.
--
-- MAW replaces both rolls of the original engine by patching its code: the party's attack on a monster in
-- General/zMaw-hooks.lua, a monster's attack on a party member in General/zzMaw_MonsterHit.lua. Those patches
-- cannot run here. The rules themselves are MAW's own functions (MawCore.Formulas.mawPlayerHitChance,
-- MawRollDodge), which load as they are; this file calls them from the events OpenYAMM raises at the same two
-- rolls.
--
-- It loads before MAW's other files register handlers, so, as in MAW, MAW's formula gets first refusal and any
-- PlayerHitOrMiss handler still has the final say.

-- t.Result is the engine's own roll. t.Range is 0 for a blow and 1 for a shot; t.Bonus is an extra to-hit bonus.
function events.PlayerHitOrMiss(t)
    if not (t.Player and t.Monster and MawCore and MawCore.Formulas and MawCore.Formulas.mawPlayerHitChance) then
        return
    end

    -- nil keeps the engine's roll
    local chance = MawCore.Formulas.mawPlayerHitChance(t.Player, t.Monster, t.Range, t.Bonus)

    if chance then
        t.Result = chance >= math.random()
    end
end

-- Not an event of the original: OpenYAMM raises it where a monster's blow or arrow is rolled against a party
-- member. t.Result is the engine's own roll.
function events.MonsterHitOrMiss(t)
    if not MawRollDodge then
        return
    end

    local hit = MawRollDodge(t.Monster, t.Player)

    if hit ~= nil then
        t.Result = hit
    end
end
