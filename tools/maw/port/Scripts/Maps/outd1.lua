-- OpenYAMM version of MAW's Maps/outd1.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the Shrine of Personality.

Game.MapEvtLines:RemoveEvent(261)
evt.hint[261] = evt.str[13]  -- "Shrine of Personality"
evt.map[261] = function()
	if not evt.Cmp{"QBits", Value = 1230} then         -- NPC
		evt.Set{"QBits", Value = 1230}         -- NPC
		if evt.Cmp{"QBits", Value = 1233} then         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BasePersonality", Value = 3}
		else
			evt.Set{"QBits", Value = 1233}         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BasePersonality", Value = 10}
			evt.StatusText{Str = 15}         -- "+10 Personality permanent"
		end
		return
	end
	evt.StatusText{Str = 14}         -- "You pray at the shrine."
end
