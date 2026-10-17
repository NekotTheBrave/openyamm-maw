-- OpenYAMM addition to MAW: keeps MAW's log of debug-key and editor attempts (vars.q1, vars.r1; zzMaw-Monsters.lua,
-- "leecher drain") from growing without end in saves made before the port's fix.
--
-- MAW blocks the original's editor by wrapping getmetatable(Editor).__call and noting each attempt in vars.r1. In
-- the port Editor was a stand-in sharing its metatable with every other stand-in, so every call to any of them was
-- noted: games reached millions of entries (a 98 MB save) and slowed every map change and save. The stand-ins have
-- their own metatables now (stubs.lua); this cuts what older games collected down to its last entries. Nothing in
-- MAW reads the log.

local Keep = 100

local function trim(list)
    if type(list) ~= "table" or #list <= Keep then
        return list
    end

    local kept = {}

    for index = #list - Keep + 1, #list do
        kept[#kept + 1] = list[index]
    end

    return kept
end

function events.LoadMap()
    if vars then
        vars.q1 = trim(vars.q1)
        vars.r1 = trim(vars.r1)
    end
end
