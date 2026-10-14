-- OpenYAMM version of MAW's Maps/outc1.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the Shrine of Endurance.

Game.MapEvtLines:RemoveEvent(261)
evt.hint[261] = evt.str[12]  -- "Shrine of Endurance"
evt.map[261] = function()
	if not evt.Cmp{"QBits", Value = 1230} then         -- NPC
		evt.Set{"QBits", Value = 1230}         -- NPC
		if evt.Cmp{"QBits", Value = 1234} then         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseEndurance", Value = 3}
		else
			evt.Set{"QBits", Value = 1234}         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseEndurance", Value = 10}
			evt.StatusText{Str = 14}         -- "+10 Endurance permanent"
		end
		return
	end
	evt.StatusText{Str = 13}         -- "You pray at the shrine."
end
