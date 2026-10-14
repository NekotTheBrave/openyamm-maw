-- OpenYAMM version of MAW's Maps/7d29.lua.
--
-- MAW's file is Merge's map script with MAW's changes; OpenYAMM does Merge's part itself, so this file is MAW's
-- addition alone: the Goblin King, who comes once the goblins are dead. (MAW's file also drops Merge's note that the
-- continent's quest is done, which OpenYAMM keeps.)

Game.PlaceMonTxt[300]="Goblin King"
evt.map[1000] = function()  
	if not vars.goblingKing and not vars.goblingKing then
		if evt.CheckMonstersKilled{CheckType = 1, Id = 56, Count = 0, InvisibleAsDead = 0} then
			vars.goblingKing=true
			mapvars.goblingKing=true
			mawmapvarsend("goblingKing",true)
			Sleep(1)
			Game.ShowStatusText("Noises from the throne room")
			pseudoSpawnpoint{monster = 271, x = -5110, y = 2852, z = 65, count = 1, powerChances = {0, 0, 100}, radius = 64, group = 1, 
								transform = function(mon) 
								mon.FullHP = mon.FullHP*3 
								mon.HP = mon.FullHP  
								mon.NameId=300
								mon.Velocity=400
								end}
			pseudoSpawnpoint{monster = 271, x = -5122, y = 2219, z = 1, count = 2, powerChances = {0, 100, 0}, radius = 128, group = 1, transform = function(mon) mon.Velocity=400 end}
			pseudoSpawnpoint{monster = 271,  x = -4423, y = 1456, z = 1 , count = 3, powerChances = {100, 0, 0}, radius = 256, group = 1, transform = function(mon) mon.Velocity=400 end}
			pseudoSpawnpoint{monster = 271,  x = -5937, y = 1456, z = 1  , count = 3, powerChances = {100, 0, 0}, radius = 256, group = 1, transform = function(mon) mon.Velocity=400 end}
		end
	end
end


Timer(evt.map[1000].last, const.Minute)
