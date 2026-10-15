-- OpenYAMM addition: Merge's rules that MAW's own copies of Merge's scripts leave out or change.
--
-- MAW ships edited copies of some of Merge's scripts. OpenYAMM does Merge's part natively and does not run those
-- copies (they would apply Merge's features a second time), so what MAW changed in them is set here instead, as the
-- engine's native rules (modrt.setRule; ModHooks.h lists them):
--
--   General/MiscTweaks.lua          Meditation's spell points every five minutes are commented out (MAW regains
--                                   spell points by Meditation itself, zzMAW-Skills.lua); Merge's repair of
--                                   Grandmaster Unarmed's dodge is commented out; mixing two catalysts gets MAW's
--                                   power formula, held to the mixer's Alchemy.
--   General/SpellsTweaks.lua        Slow (and Mass Distortion) always take hold.
--   Global/NPCFollowersSkills.lua   followers' bonuses to a skill: the largest, not the sum.
--
-- Merge's racial skill rules (Structs/RacialSkills.lua, which MAW empties) and its class caps are replaced through
-- the converted tables (tools/maw/convert_tables.py: Race Skillz.txt and Class Skillz.txt).

modrt.setRule("meditation_regeneration", "off")
modrt.setRule("unarmed_dodge", "off")
modrt.setRule("catalyst_combination", "alchemy_capped")
modrt.setRule("slow_resistance", "off")
modrt.setRule("follower_skill_bonuses", "largest")

-- Not MAW's: a potion an event gives (a quest's reward, such as the black potion turn-ins) has power 0 in the real
-- game too, which makes MAW's potions that scale with power useless; the player asked for such potions to have 50.
modrt.setRule("event_potion_power", "50")

-- Merge's Structs/After/RemoveSkillValueLimits.lua packs skills with the masteries at 0x400, 0x800 and 0x1000, and
-- MAW's zzMAW-Skills.lua converts a game's skills to that packing once (UncapSkills, over the whole roster). OpenYAMM
-- keeps them packed so already: converting them again would make an Expert's level 3 (0x403) a Grandmaster's 771.
function events.LoadMap()
	vars.UncappedSkills = vars.UncappedSkills or 1
end
