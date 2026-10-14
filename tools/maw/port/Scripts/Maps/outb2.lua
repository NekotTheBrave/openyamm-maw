-- OpenYAMM version of MAW's Maps/outb2.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the Shrine of Magic.

Game.MapEvtLines:RemoveEvent(261)
evt.hint[261] = evt.str[18]  -- "Shrine of Magic"
evt.map[261] = function()
	if not evt.Cmp{"QBits", Value = 1230} then         -- NPC
		evt.Set{"QBits", Value = 1230}         -- NPC
		if evt.Cmp{"QBits", Value = 1242} then         -- NPC
			evt.ForPlayer("All")
			evt.Add{"FireResistance", Value = 3}
			evt.StatusText{Str = 21}         -- "+3 Magic permanent"
		else
			evt.Set{"QBits", Value = 1242}         -- NPC
			evt.ForPlayer("All")
			evt.Add{"FireResistance", Value = 10}
			evt.StatusText{Str = 20}         -- "+10 Magic permanent"
		end
		return
	end
	evt.StatusText{Str = 19}         -- "You pray at the shrine."
end
