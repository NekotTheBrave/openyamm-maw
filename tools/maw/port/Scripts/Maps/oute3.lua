-- OpenYAMM version of MAW's Maps/oute3.lua (New Sorpigal).
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is MAW's
-- additions alone: the teleporters MAW's edited map adds to the other continents, the way to Free Haven (event 104),
-- and the Shrine of Luck. (MAW's lines for "Redone" Merge builds are left out; this is not one.)

evt.hint[1777] = "Antagarich"
evt.map[1777] = function()
	if vars.SuvivalMode then
		evt.MoveToMap{12567, 1728, 1, 512, 0, 0, 0, 0, "7out01.odm"}
		return
	end
	if evt.Cmp{"QBits", Value = 527} then
		evt.MoveToMap{-16832, 12512, 372, 0, 0, 0, 0, 3, "7out02.odm"}
	else
		evt.MoveToMap{12552, 800, 193, 512, 0, 0, 0, 3, "7out01.odm"}
	end
end

evt.hint[1888] = "Jadame"
evt.map[1888] = function()
	if evt.Cmp{"QBits", Value = 93} then
		evt.MoveToMap{10219, -15624, 265, 0, 0, 0, 0, 3, "out02.odm"}
	else
		evt.MoveToMap{3560, 7696, 544, 0, 0, 0, 0, 3, "out01.odm"}
	end
end

Game.MapEvtLines:RemoveEvent(104)
evt.map[104] = function()
	if vars.Mode ~= 2 then
		evt.MoveToMap{X = 12808, Y = 6832, Z = 64, Direction = 512, LookAngle = 0, SpeedZ = 0, HouseId = 0, Icon = 0, Name = "outb3.odm"}
	else
		evt.MoveToMap{X = -9477, Y = -13062, Z = 129, Direction = 0, LookAngle = 0, SpeedZ = 0, HouseId = 0, Icon = 0, Name = "outb3.odm"}
	end
end

Game.MapEvtLines:RemoveEvent(261)
evt.hint[261] = evt.str[23]  -- "Shrine of Luck"
evt.map[261] = function()
	if not evt.Cmp{"QBits", Value = 1230} then         -- NPC
		evt.Set{"QBits", Value = 1230}         -- NPC
		if evt.Cmp{"QBits", Value = 1237} then         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseLuck", Value = 3}
		else
			evt.Set{"QBits", Value = 1237}         -- NPC
			evt.ForPlayer("All")
			evt.Add{"BaseLuck", Value = 10}
			evt.StatusText{Str = 25}         -- "+10 Luck permanent"
		end
		return
	end
	evt.StatusText{Str = 24}         -- "You pray at the shrine."
end
