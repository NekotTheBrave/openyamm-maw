-- Boss mage (Necromancer A, monster 307; see the boss mage demo in worlds/mm6/models/actors.yml).
--
-- Phase 1: the native AI fights with Toxic Cloud and its staff, but never flees.
-- Phase 2 (60% health): a shadow ward stops every projectile from outside; the party must step inside it to hurt
--   the mage with missiles, or close to melee. Its spell becomes Shrapmetal. The ward lapses and returns.
-- Phase 3 (30% health): it raises ghosts and answers with a grandmaster Toxic Cloud.

local WardRadius = 400
local WardSeconds = 20
local WardReturnSeconds = 10

local function raiseWard(self, s)
    self:perform{
        clip = "cast_ward",
        seconds = 1.6,
        fallback = "attack_ranged",
        sound = "17shield03",
        effect = "openyamm:fx/arcane_pulse",
    }
    self:addWard{
        id = "shadow",
        radius = WardRadius,
        seconds = WardSeconds,
        effect = "openyamm:fx/shadow_ward",
        effectScale = WardRadius / 400,
        effectInterval = 0.8,
        visual = {color = {175, 100, 255}, intensity = 1.0, rim = 2.6, noise = 0.45},
    }
    s.wardsRaised = (s.wardsRaised or 0) + 1
end

return Behavior.define{
    version = 1,
    think_interval = 1.0,

    onStart = function(self, s)
        s.phase = 1
        self:setNoFlee(true)
    end,

    onHealthBelow = {
        [0.6] = function(self, s)
            s.phase = 2
            self:say("The Necromancer wraps himself in a ward of shadow!")
            self:setSpell{spell = "Shrapmetal", skill = 8, mastery = "master", chance = 40}
            raiseWard(self, s)
        end,

        [0.3] = function(self, s)
            s.phase = 3
            self:say("The Necromancer calls the dead to his side!")
            self:perform{clip = "cast_summon", seconds = 1.2, fallback = "attack_ranged", sound = "7reanimate"}
            self:summon(268, 2, 300)
            self:castNext{spell = "Toxic Cloud", skill = 10, mastery = "grandmaster"}
        end,
    },

    onWardBlocked = function(self, s, wardId)
        if Behavior.cooldown(self, s, "ward_taunt", 6) then
            self:say("Your missiles break against the shadow ward.")
        end
    end,

    onWardExpired = function(self, s, wardId)
        self:say("The shadow ward fades.")
        self:startTimer("ward_return", WardReturnSeconds)
    end,

    onTimer = {
        ward_return = function(self, s)
            raiseWard(self, s)
        end,
    },

    onDeath = function(self, s)
        self:say("The Necromancer's ward collapses with a scream.")
        self:effect("openyamm:fx/arcane_pulse", 1.5)
    end,
}
