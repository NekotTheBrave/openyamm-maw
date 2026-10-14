-- OpenYAMM version of MAW's Maps/7d06.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: MAW's extra monsters.

--MAW
if not mapvars.maw then
	mapvars.maw=true
	mawmapvarsend("maw",true)
	pseudoSpawnpoint{monster = 397, x = -3954, y = 5878, z = -95, count = 3, powerChances = {100, 0, 0}, radius = 64, group = 1}
	pseudoSpawnpoint{monster = 397, x = -3954, y = 5878, z = -95, count = 1, powerChances = {0, 100, 0}, radius = 64, group = 1}
end
