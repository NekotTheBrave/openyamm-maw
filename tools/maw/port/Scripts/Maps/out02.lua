-- OpenYAMM version of MAW's Maps/out02.lua (Ravenshore).
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself (the Cross Continents
-- quest's Breach portal, entering Escaton's Crystal again without the key), so this file is MAW's additions alone,
-- without its multiplayer parts: the teleporters MAW's edited map adds to the other continents.

evt.hint[2666] = "Enroth"
evt.map[2666] = function()
	evt.MoveToMap{-9729, -10555, 160, 512, 0, 0, 0, 3, "oute3.odm"}
end

evt.hint[1777] = "Antagarich"
evt.map[1777] = function()
	if evt.Cmp{"QBits", Value = 527} then
		evt.MoveToMap{-16832, 12512, 372, 0, 0, 0, 0, 3, "7out02.odm"}
	else
		evt.MoveToMap{12552, 800, 193, 512, 0, 0, 0, 3, "7out01.odm"}
	end
end
