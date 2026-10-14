-- OpenYAMM version of MAW's Maps/7d34.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: a door switch that works both doors.

Game.MapEvtLines:RemoveEvent(151)
evt.map[151] = function()
	evt.SetDoorState{Id = 1, State = 2}         -- switch state
	evt.SetDoorState{Id = 2, State = 2}         -- switch state
end
