-- OpenYAMM addition to MAW: the skills MAW adds (Cover, Mana Shield, Enlightenment, Retaliation) are skills of the
-- game's.
--
-- MAW's Skills module (MawCore/Skills.lua) keeps skills 39 and up in memory that the original's patch allocates,
-- which the port does not have. This routes its Skillz API for them to the game:
--   Skillz.new_weapon/magic/armor/misc register the skill, kept as "Skill<id>" and shown under the name MAW gives it
--     (zMaw-SkillTexts.lua) in that group of the skill page;
--   Skillz.get/set (and get2/set2) read and write a party member's skill;
--   Skillz.learn_at has the houses of that type teach it, and Skillz.MasteryLimit is the class's limit the game has;
--   the classes' limits come from MAW's "Class Skillz.txt" (N, E, M, G; "=" and "-" are none).
-- Only party members have them; characters waiting in the Adventurer's Inn are not reached.

local BASE_SKILLS = 39
local Groups = {new_weapon = "weapon", new_magic = "magic", new_armor = "armor", new_misc = "misc"}
-- The Merge's house types by number (const.HouseType), by the house table's names.
local HouseTypes = {
    [1] = "Weapon Shop", [2] = "Armor Shop", [3] = "Magic Shop", [4] = "Alchemist", [5] = "Fire Guild",
    [6] = "Air Guild", [7] = "Water Guild", [8] = "Earth Guild", [9] = "Spirit Guild", [10] = "Mind Guild",
    [11] = "Body Guild", [12] = "Light Guild", [13] = "Dark Guild", [14] = "Elemental Guild", [15] = "Self Guild",
    [21] = "Tavern", [23] = "Temple", [27] = "Stables", [28] = "Boats", [30] = "Training",
}
local Letters = {B = 1, N = 1, E = 2, M = 3, G = 4}
local MasteryFlags = {[0] = 0, [1] = 0, [2] = 1024, [3] = 2048, [4] = 4096}

-- MAW's class limits for its own skills: [skill id][class id] = 0..4.
local classLimits = nil

local function readClassLimits()
    classLimits = {}
    local file = io.open(AppPath .. "Data\\Tables\\Class Skillz.txt", "r")

    if not file then
        return
    end

    for line in file:lines() do
        local cells = {}

        for cell in (line .. "\t"):gmatch("([^\t]*)\t") do
            cells[#cells + 1] = cell
        end

        local id = tonumber((cells[1] or ""):match("^Skill(%d+)$"))

        if id and id >= BASE_SKILLS then
            classLimits[id] = {}

            for column = 2, #cells do
                classLimits[id][column - 2] = Letters[(cells[column] or ""):upper()] or 0
            end
        end
    end

    file:close()
end

local function gameSkill(id)
    return id >= BASE_SKILLS and ModRT.Bindings.skillNamesById[id] ~= nil
end

local function register(id, group)
    if not ModRT.Bindings.registerSkill(id, "Skill" .. id, group) then
        return
    end

    if classLimits == nil then
        readClassLimits()
    end

    for class, mastery in pairs(classLimits[id] or {}) do
        if Game.ClassNames[class] ~= nil then
            Game.Classes.Skills[class][id] = mastery
        end
    end
end

local function wrap()
    local api = Skillz

    if type(api) ~= "table" or api.mawPortSkills then
        return
    end

    api.mawPortSkills = true

    for name, group in pairs(Groups) do
        local original = api[name]

        api[name] = function(id)
            original(id)
            register(id, group)
        end
    end

    local get, set, get2, set2, learnAt = api.get, api.set, api.get2, api.set2, api.learn_at
    local masteryLimit = api.MasteryLimit

    -- The highest mastery the character's class may have, as the game keeps it.
    api.MasteryLimit = function(pl, id)
        if gameSkill(id) then
            return Game.Classes.Skills[pl.Class][id] or 0
        end

        return masteryLimit(pl, id)
    end

    api.get = function(pl, id)
        if gameSkill(id) then
            return pl.Skills[id] or 0
        end

        return get(pl, id)
    end

    api.set = function(pl, id, value)
        if gameSkill(id) then
            pl.Skills[id] = value
            return
        end

        return set(pl, id, value)
    end

    -- Packed as (mastery << 16) + level.
    api.get2 = function(pl, id)
        if gameSkill(id) then
            local level, mastery = SplitSkill(pl.Skills[id] or 0)
            return mastery * 65536 + level
        end

        return get2(pl, id)
    end

    api.set2 = function(pl, id, value)
        if gameSkill(id) then
            local mastery = math.min(math.floor(value / 65536), 4)
            pl.Skills[id] = value % 1024 + (MasteryFlags[mastery] or 0)
            return
        end

        return set2(pl, id, value)
    end

    api.learn_at = function(id, house)
        local result = learnAt(id, house)

        if gameSkill(id) and HouseTypes[house] then
            modrt.houseOfferSkill(HouseTypes[house], "Skill" .. id)
        end

        return result
    end
end

-- MAW's Skills module puts its API in place when it starts, and adds its skills from GameInitialized2 on.
function events.GameInitialized1()
    wrap()
end

function events.GameInitialized2()
    wrap()
end
