-- OpenYAMM version of MAW's Maps/outc2.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the Shrine of Accuracy.

Game.MapEvtLines:RemoveEvent(261)
evt.hint[261] = evt.str[11]  -- "Shrine of Accuracy"
evt.map[261] = function()
	if not evt.Cmp{"QBits", Value = 1230} then         -- NPC
		evt.Set{"QBits", Value = 1230}         -- NPC
		if evt.Cmp{"QBits", Value = 1235} then         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseAccuracy", Value = 3}
		else
			evt.Set{"QBits", Value = 1235}         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseAccuracy", Value = 10}
			evt.StatusText{Str = 13}         -- "+10 Accuracy permanent"
		end
		return
	end
	evt.StatusText{Str = 12}         -- "You pray at the shrine."
end
