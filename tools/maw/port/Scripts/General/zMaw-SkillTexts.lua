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

-- A teacher's own text names MAW's requirement and price (MAW's AfterPopulateNPCDialog of a TeachSkill dialog in
-- zzMAW-Skills.lua, which the game does not raise; it asks TeacherText instead). That file keeps getReqAndCost and its
-- tables local, so the values of the supported modes are repeated here.
local horizontalSkills = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 27, 28,
    30, 32, 33, 35, 38}
local learningRequirementsNormal = {0, 4, 7, 10}
local normalCosts = {0, 1000, 4000, 20000}

-- MAW's getReqAndCost: the skill a mastery needs and its price, more for each other skill at that mastery already.
local function reqAndCost(mastery, playerIndex)
	local pl = Party[playerIndex]
	local baseCost = normalCosts[mastery]
	local requirements = GetLearningRequirements()[mastery]

	if not pl or not baseCost then
		return requirements, nil
	end

	local cost = baseCost
	local oldMasteries = vars.oldPlayerMasteries and vars.oldPlayerMasteries[pl:GetIndex()]

	for _, skillId in pairs(horizontalSkills) do
		local s, m = SplitSkill(pl.Skills[skillId])
		if (m >= mastery and s ~= 0) or (oldMasteries and (oldMasteries[skillId] or 0) - 1 > mastery) then
			cost = cost + baseCost
		end
	end

	return requirements, cost
end

function events.TeacherText(t)
	if not table.find(horizontalSkills, t.Skill) then
		return
	end

	local req, cost = reqAndCost(t.Mastery, t.PlayerIndex)

	if not req or not cost then
		return
	end

	-- MAW's reading of the text: every number of 100 or more is the price; the requirement is the number equal to the
	-- original's requirement, else the first one up to 12. A text that lacks either gets both on a line of its own.
	local message = t.Text
	local digits = {}

	for d in message:gmatch("%d+") do
		digits[#digits + 1] = d
	end

	local reqDigits = tostring(learningRequirementsNormal[t.Mastery])

	if not table.find(digits, reqDigits) then
		reqDigits = nil

		for _, d in ipairs(digits) do
			if tonumber(d) <= 12 then
				reqDigits = d
				break
			end
		end
	end

	local reqDone, costDone = false, false
	local text = message:gsub("%d+", function(d)
		if tonumber(d) >= 100 then
			costDone = true
			return tostring(cost)
		end

		if d == reqDigits and not reqDone then
			reqDone = true
			return tostring(req)
		end
	end)

	if not (reqDone and costDone) then
		text = text .. "\n\nYou need at least " .. req .. " skill and " .. cost .. " gold."
	end

	t.Text = text
end

-- The skill page's information box: MAW builds its texts per character when the box is shown
-- (MawCore/SkillTooltip.lua: its builders, e.g. regeneration and meditation now and at the next level, else the
-- skill's own texts), from a patch of the original's code that does nothing here. The game asks for them instead.
function events.BuildSkillInformationBox(t)
    local tooltip = MawCore and MawCore.SkillTooltip

    if not (tooltip and tooltip.partText and t.Player) then
        return
    end

    for part = 1, 5 do
        local ok, text = pcall(tooltip.partText, t.Player, t.Skill, part)

        if ok and type(text) == "string" and text ~= "" then
            t.Parts[part] = text
        end
    end
end
