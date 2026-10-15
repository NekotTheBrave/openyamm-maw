-- OpenYAMM port of MAW's General/NPCMercenaries.lua: MAW's edits to Merge's mercenaries alone.
--
-- OpenYAMM generates Merge's mercenaries natively (game/gameplay/MercenaryRecruitmentRuntime.cpp), so MAW's copy of
-- Merge's script is not run; what MAW changed in it is set here:
--   - the class kinds hired on each continent (Merge takes the continent's starting classes): the original classes
--     and peasants everywhere, MAW's own classes in Enroth;
--   - skill points: 4 + floor(level / 10 + 1) for each level above the first (Merge's: 5);
--   - the items they come with: no rarity tier (BonusExpireTime), and an item with both a special enchantment and
--     MAW's second enchantment keeps one of the two, either at random.
-- Left out as OpenYAMM has no need of them: Merge's class list by continent rebuilt from the character selection
-- (MAW keeps its own), the Master of Elements starting skills fix, the continent check and repeated class fixes
-- (OpenYAMM's own code), and the Madness mode guards (out of scope).

modrt.setRule("mercenary_class_kinds_1", "2 3 4 6 7 12 13 15 16") -- Jadame
modrt.setRule("mercenary_class_kinds_2", "1 2 5 6 8 9 10 11 14 16") -- Antagarich
modrt.setRule("mercenary_class_kinds_3", "1 2 5 6 9 14 16 17 18 19 20") -- Enroth
modrt.setRule("mercenary_skill_points", "tenth_level_bonus")

function events.MercenaryItemGenerated(t)
	local item = t.Item
	item.BonusExpireTime = 0
	if item.Bonus2 > 0 and HasEnc2(item) then
		if math.random() < 0.5 then
			item.Bonus2 = 0
		else
			item.Charges = 0
		end
	end
end
