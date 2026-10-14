-- OpenYAMM version of MAW's Maps/7d37.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: MAW's guardian monster.

--MAW
if not mapvars.mawSpawn then
	pseudoSpawnpoint{monster = 307,  x = -25, y = 1312, z = -31, count = 1, powerChances = {100, 0, 0}, radius = 0, group = 1,transform = function(mon)
		mon.HP=mon.HP*2.5 
		mon.FullHP=mon.HP 
		mon.Attack1Type=0 
		mon.Attack1Missile = 0 
		mon.Hostile=true
		mon.ShowAsHostile=true
		mon.SpellChance  =30 
		mon.SpellSkill= 1
		mon.TreasureItemPercent = 100 
		if bolsterLevel>25 then
			mon.Items[0].Number=174
		else
			mon.Items[0].Number=171
		end
		mon.Items[0].Charges=66
		end
		}
	mapvars.mawSpawn=true
	mawmapvarsend("mawSpawn",true)
end
