-- OpenYAMM version of MAW's Maps/pyramid.lua.
--
-- MAW's file is Merge's map script with MAW's additions; OpenYAMM does Merge's part itself, so this file is
-- MAW's additions alone: the radiation, which the Crystal Skull absorbs.

Game.MapEvtLines:RemoveEvent(12)
evt.map[12] = function()  -- Timer(<function>, 1.5*const.Minute)
	local i
	if evt.Cmp{"MapVar0", Value = 1} then
		if evt.Cmp{"Inventory", Value = 2086} then         -- "Crystal Skull"
			evt.StatusText{Str = 18}         -- "Crystal Skull absorbs radiation damage."
		else
			i = Game.Rand() % 6
			if i == 1 then
				evt.DamagePlayer{Player = "All", DamageType = const.Damage.Phys, Damage = 5}
				evt.StatusText{Str = 4}         -- "Radiation Damage!"
			elseif i == 2 then
				evt.DamagePlayer{Player = "All", DamageType = const.Damage.Water, Damage = 5}
				evt.StatusText{Str = 10}         -- "Radiation Damage!"
			elseif i == 3 then
				evt.DamagePlayer{Player = "All", DamageType = const.Damage.Earth, Damage = 5}
				evt.StatusText{Str = 13}         -- "Radiation Damage!"
			elseif i == 4 then
				evt.DamagePlayer{Player = "All", DamageType = const.Damage.Air, Damage = 5}
				evt.StatusText{Str = 14}         -- "Radiation Damage!"
			elseif i == 5 then
				evt.DamagePlayer{Player = "All", DamageType = const.Damage.Fire, Damage = 5}
				evt.StatusText{Str = 13}         -- "Radiation Damage!"
			else
				evt.DamagePlayer{Player = "All", DamageType = const.Damage.Body, Damage = 5}
				evt.StatusText{Str = 10}         -- "Radiation Damage!"
			end
		end
	end
end

Timer(evt.map[12].last, 1.5*const.Minute)
