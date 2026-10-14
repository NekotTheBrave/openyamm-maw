-- OpenYAMM version of MAW's Maps/oute2.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the Shrine of Intellect.

Game.MapEvtLines:RemoveEvent(261)
evt.hint[261] = evt.str[15]  -- "Shrine of Intellect"
evt.map[261] = function()
	if not evt.Cmp{"QBits", Value = 1230} then         -- NPC
		evt.Set{"QBits", Value = 1230}         -- NPC
		if evt.Cmp{"QBits", Value = 1232} then         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseIntellect", Value = 3}
		else
			evt.Set{"QBits", Value = 1232}         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseIntellect", Value = 10}
			evt.StatusText{Str = 17}         -- "+10 Intellect permanent"
		end
		return
	end
	evt.StatusText{Str = 16}         -- "You pray at the shrine."
end
