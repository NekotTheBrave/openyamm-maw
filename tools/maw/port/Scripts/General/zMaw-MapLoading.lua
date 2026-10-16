-- OpenYAMM addition: MAW's writes to the map table for the map being entered, made before the game reads it.
--
-- In the original BeforeLoadMap comes before a map's spawn points make its monsters, and MAW writes the map table
-- there, in this order: the map's three monster kinds (zzMaw-Maps.lua: a Dimension Map's, mappingMonsters, or else
-- the map's own, baseSpawnMonsters, put back after a Dimension Map changed them), the number of monsters of every
-- map (zzMaw-Monsters.lua, AdjustMonsterDensity, by the party's level, with a Dimension Map's extra,
-- mapMonsterDensity), and last the randomizer's shuffle of every map's monsters. The port raises BeforeLoadMap once
-- the map is loaded, too late for those, so the first two are made here as well, on the port's MapLoading (the game
-- about to read the map's row). In a randomizer game the shuffle, already in the table, has the last word, so the
-- kinds are left as they are. MAW's own BeforeLoadMap handlers then run as they are: they write the same values
-- again (the Dimension Map's extra is not added twice: mapMonsterDensity is cleared).

-- MAW's randomizer handler of BeforeLoadMap (zzzzMalekith-Randomizer.lua): the shuffle of every map's monsters.
local function runRandomizerShuffle()
    for _, handler in ipairs(events.handlers("BeforeLoadMap")) do
        local info = debug.getinfo(handler, "S")

        if info and info.source:find("Malekith%-Randomizer") then
            handler()
        end
    end
end

function events.MapLoading(t)
    -- A new game's first map: MAW turns the randomizer on when it hears the game began (BeforeNewGameAutosave),
    -- which the port raises once that map is made, too late for its monsters. It is turned on here, and MAW's
    -- shuffle made, before the game reads the map's row; MAW's handler makes the same shuffle again after.
    if ModRT.State.newGamePending and Game.RandomizerMode and not vars.RandomizerMode then
        vars.RandomizerMode = true
        runRandomizerShuffle()
    end

    local map = Game.MapStats[t.MapStatsIndex]

    if map and not vars.RandomizerMode then
        local pics = mappingMonsters or (baseSpawnMonsters and baseSpawnMonsters[t.MapStatsIndex])

        if pics then
            map.Monster1Pic, map.Monster2Pic, map.Monster3Pic = pics[1], pics[2], pics[3]
        end
    end

    -- MAW's guard (zzMaw-Monsters.lua): with the randomizer on, only a Dimension Map's numbers are changed.
    if AdjustMonsterDensity and BackupMapStats and not (vars.RandomizerMode and not mapMonsterDensity) then
        AdjustMonsterDensity()
    end
end
