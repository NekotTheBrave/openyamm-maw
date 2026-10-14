-- OpenYAMM version of MAW's Maps/outb3.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the entrance to MAW's dungeon (znwc).

Game.MapEvtLines:RemoveEvent(92)
evt.map[92] = function()
	if vars.Mode==2 then
		Game.ShowStatusText("Access have been destroyed")
		return
	end
	if Party[0].LevelBase>=255 or Game.BolsterAmount<=100 then
		evt.MoveToMap{X = -640, Y = 512, Z = -416, Direction = 512, LookAngle = 0, SpeedZ = 0, HouseId = 0, Icon = 0, Name = "znwc.blv"}
	else
		Game.ShowStatusText("You must prove your worth first")
	end
end
