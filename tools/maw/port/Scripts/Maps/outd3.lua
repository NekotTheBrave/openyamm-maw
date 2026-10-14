-- OpenYAMM version of MAW's Maps/outd3.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the Shrine of Electricity.

Game.MapEvtLines:RemoveEvent(261)
evt.hint[261] = evt.str[18]  -- "Shrine of Electricity"
evt.map[261] = function()
	if not evt.Cmp{"QBits", Value = 1230} then         -- NPC
		evt.Set{"QBits", Value = 1230}         -- NPC
		if evt.Cmp{"QBits", Value = 1239} then         -- NPC
			evt.ForPlayer("All")
			evt.Add{"AirResistance", Value = 3}
		else
			evt.Set{"QBits", Value = 1239}         -- NPC
			evt.ForPlayer("All")
			evt.Add{"AirResistance", Value = 10}
			evt.StatusText{Str = 20}         -- "+10 Electricity resistance permanent"
		end
		return
	end
	evt.StatusText{Str = 19}         -- "You pray at the shrine."
end
