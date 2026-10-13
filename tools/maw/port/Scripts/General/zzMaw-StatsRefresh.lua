-- OpenYAMM addition to MAW: the game's numbers follow MAW's item table.
--
-- MAW keeps what a character's equipment gives in a table of its own (plItemsStats) and rebuilds it with
-- mawRefresh: on arriving at a map, a tick after every action, after a hit. In the original the engine works a
-- character's statistics out afresh every time they are asked for, so they follow that table the moment it
-- changes. OpenYAMM keeps them from one bonus rebuild to the next. This wraps mawRefresh so that a rebuild
-- follows each refresh.
--
-- Asked for while a rebuild is already running, the rebuild is put off until that one is done, so the wrapper is
-- safe wherever MAW calls mawRefresh from.

function events.GameInitialized2()
    local refresh = mawRefresh

    if type(refresh) ~= "function" then
        return
    end

    function mawRefresh(...)
        local results = table.pack(refresh(...))
        Party.RefreshBonuses()
        return table.unpack(results, 1, results.n)
    end
end
