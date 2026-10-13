-- OpenYAMM addition to MAW: named monsters start from the level stored in their map.
--
-- In the original a monster placed in a map carries a record of its own in the map's file, and that record is not
-- always the monster table's: the guards of New Sorpigal are level 11 there and level 13 in MAW's table. MAW works
-- the level of a named monster out from what it finds on the monster (oldUniqueMonsterTable in zzMaw-Monsters.lua).
-- OpenYAMM does not keep that part of the record and fills every monster from the table, so the stored levels come
-- from a list read out of a Merge install (MawPort/NamedMonsterLevels.lua, written by
-- tools/maw/named_monster_levels.py).
--
-- This runs on LoadMap, ahead of MAW's own work in AfterLoadMap, on every arrival: MAW's level for the monster, once
-- it has one, is written over this as it is over the stored record in the original.

local storedLevels = require("MawPort.NamedMonsterLevels")

function events.LoadMap()
    local levels = storedLevels[Map.Name:lower()]

    if not levels then
        return
    end

    for index, level in pairs(levels) do
        if index <= Map.Monsters.High then
            local mon = Map.Monsters[index]

            if mon.NameId ~= 0 then
                mon.Level = level
            end
        end
    end
end
