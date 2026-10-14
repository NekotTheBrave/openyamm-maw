-- OpenYAMM addition to MAW: the Skillz API's names and descriptions of the 39 skills reach the game.
--
-- MAW's Skills module (MawCore/Skills.lua) names and describes skills by pointing the original engine's own text
-- arrays at its strings, which does nothing here. For the 39 skills the game has, this routes Skillz.setName,
-- getName, setDesc, getDesc and restoreDesc to the game's texts (Game.SkillNames, Game.SkillDescriptions and
-- Game.SkillDes<Mastery>), which the skill page and the skill's information box show. Parts are as the original
-- numbers them: 1 the description, 2 to 5 Normal, Expert, Master and Grandmaster. The skills MAW adds (39 and up)
-- are the game's too once zMaw-Skillz.lua has registered them.

local BASE_SKILLS = 39
local partArrays = {"SkillDescriptions", "SkillDesNormal", "SkillDesExpert", "SkillDesMaster", "SkillDesGM"}

local function gameSkill(id)
    return id >= 0 and (id < BASE_SKILLS or ModRT.Bindings.skillNamesById[id] ~= nil)
end

local function wrap()
    local api = Skillz

    if type(api) ~= "table" or api.mawPortTexts then
        return
    end

    api.mawPortTexts = true
    local setName, getName, setDesc, getDesc = api.setName, api.getName, api.setDesc, api.getDesc
    local originals = {}

    api.setName = function(id, text)
        setName(id, text)

        if gameSkill(id) then
            Game.SkillNames[id] = text
        end
    end

    api.getName = function(id)
        if gameSkill(id) then
            return Game.SkillNames[id]
        end

        return getName(id)
    end

    api.setDesc = function(id, part, text)
        setDesc(id, part, text)
        local array = gameSkill(id) and partArrays[part]

        if array then
            local key = part .. "_" .. id

            if originals[key] == nil then
                originals[key] = Game[array][id] or ""
            end

            Game[array][id] = text
        end
    end

    api.getDesc = function(id, part)
        local array = gameSkill(id) and partArrays[part]

        if array then
            return Game[array][id] or ""
        end

        return getDesc(id, part)
    end

    -- The game's own text back for one part, after a setDesc.
    api.restoreDesc = function(id, part)
        local original = originals[part .. "_" .. id]

        if original ~= nil then
            api.setDesc(id, part, original)
        end
    end
end

-- MAW's Skills module puts its API in place when it starts; its users name and describe skills from
-- GameInitialized2 on.
function events.GameInitialized1()
    wrap()
end

function events.GameInitialized2()
    wrap()
end
