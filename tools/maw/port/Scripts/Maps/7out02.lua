-- OpenYAMM version of MAW's Maps/7out02.lua (Harmondale).
--
-- MAW's file is Merge's map script with MAW's changes; OpenYAMM does Merge's part itself, so this file is MAW's
-- additions alone: the ambush on the road, the goblin fort and its reinforcements, and the teleporters MAW's edited
-- map adds to the other continents. (MAW's file also drops Merge's handling of Judge Grey's death, which OpenYAMM
-- keeps, and has lines for "Redone" Merge builds, left out as this is not one.)

--MAW
evt.Map[1000] = function()
	if not mapvars.ambush then
		if Party.X>17390 and Party.X<19429 and Party.Y>-20015 and Party.Y<-19454 and Party.Z<800 then
			Game.ShowStatusText("It's an ambush!")
			pseudoSpawnpoint{monster = 271,  x = 19613, y = -17760, z = 1595 , count = 2, powerChances = {60, 30, 10}, radius = 64, group = 1}
			pseudoSpawnpoint{monster = 271,  x = 19307, y = -18344, z = 1568 , count = 2, powerChances = {60, 30, 10}, radius = 64, group = 1}
			pseudoSpawnpoint{monster = 271,  x = 18344, y = -18861, z = 1666 , count = 2, powerChances = {60, 30, 10}, radius = 64, group = 1}
			pseudoSpawnpoint{monster = 271,  x = 16201, y = -21635, z = 3022  , count = 8, powerChances = {60, 30, 10}, radius = 1400, group = 1}
			mapvars.ambush=true
			mawmapvarsend("ambush",true)
		end
	end
end
Timer(evt.map[1000].last, const.Minute*0.5)

--fort
Game.PlaceMonTxt[301]="Goblin's Lieutenant"
if not mapvars.maw then
	mapvars.maw=true
	mawmapvarsend("maw",true)
	pseudoSpawnpoint{monster = 106,  x = 8200, y = 4098, z = 385 , count = 1, powerChances = {100, 0, 0}, radius = 0, group = 1, transform = function(mon) mon.NameId=301 mon.Z=385 mon.Velocity=0 mon.SpellChance=100 mon.HP=mon.HP*3 mon.FullHP=mon.HP end}
end
 
 
function events.MonsterKilled(mon)
	if mon.NameId==301 then
		mapvars.fortclear=true
	end
end

evt.Map[1001] = function()
	mapvars.spawn=mapvars.spawn or 0
	if not mapvars.fortclear and getDistance(8200,4098,385)<2000 then
		Game.ShowStatusText("Goblin Reinforcements")
		pseudoSpawnpoint{monster = 271,  x = 6613, y = 2546, z = 122 , count = 1, powerChances = {60, 30, 10}, radius = 64, group = 1}
		pseudoSpawnpoint{monster = 271,  x = 6601, y = 5690, z = 122 , count = 1, powerChances = {60, 30, 10}, radius = 64, group = 1}
		pseudoSpawnpoint{monster = 271,  x = 9765, y = 2500, z = 122 , count = 1, powerChances = {60, 30, 10}, radius = 64, group = 1}
		pseudoSpawnpoint{monster = 271,  x = 9765, y = 2500, z = 122  , count = 1, powerChances = {60, 30, 10}, radius = 64, group = 1}
		mapvars.spawn=mapvars.spawn+1
		mawmapvarsend("spawn",mapvars.spawn)
		if mapvars.spawn>=10 then 
			mapvars.fortclear=true
		end
	end
end
Timer(evt.map[1001].last, const.Minute*5)

evt.hint[2666]="Enroth"
evt.map[2666] = function()
	if vars.SuvivalMode then
		evt.MoveToMap{12567, 1728, 1, 512, 0, 0, 0, 0, "7out01.odm"}
		return
	end
	evt.MoveToMap{-9729, -10555, 160, 512, 0, 0, 0, 3, "oute3.odm"}
end

evt.hint[1888]="Jadame"
evt.map[1888] = function()
	if evt.Cmp{"QBits", Value = 93} then
		evt.MoveToMap{10219, -15624, 265, 0, 0, 0, 0, 3, "out02.odm"}
	else
		evt.MoveToMap{3560, 7696, 544, 0, 0, 0, 0, 3, "out01.odm"}
	end
end
