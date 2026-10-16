-- OpenYAMM addition to MAW: MAW's changes to the Dragon Hatchling quest (its Global/Quest_DragonHatchling.lua, an
-- override of Merge's file that the port does not run: OpenYAMM has its own version of the quest, the engine's
-- global event 789 for the Baby Dragon, NPC 396).
--
-- MAW's visible changes, on OpenYAMM's quest: feeding says how much the dragon has eaten ("Eaten so far: N of 100"),
-- that the party has too little food, and once it has eaten enough, how many days are left until it is grown; a
-- dragon following the party can be released for good. The quest's state stays the engine's (its cross-continent
-- variables), so a game under way goes on where it was. In MAW (as in Merge) the grown dragon joins the party as a
-- character; in OpenYAMM it follows the party, which is kept.

local DragonNPC = 396
local FedText = "Yum! The dragon ate 5 food. Eaten so far: %d of 100."
local DaysLeftText = "Days left: %d."
local HungryText = "Grrr! The dragon needs 5 food, the party has less."
local ReleaseTopic = "Release the dragon"
local ReleaseAsk = "The dragon will fly away for good. Release him? (Y/N)"
local ReleasedText = "The dragon spread his wings and flew away."
local MinutesPerDay = 24 * 60
local GrowMinutes = 28 * MinutesPerDay

local function crossVar(name)
	return "MMerge.CrossContinents." .. name
end

local function get(name)
	return tonumber(evt.GetGlobalVar(crossVar(name), 0)) or 0
end

local function set(name, value)
	evt.SetGlobalVar(crossVar(name), value)
end

local function nowMinutes()
	return math.floor(Game.Time / const.Minute)
end

Game.GlobalEvtLines:RemoveEvent(789)

evt.Global[789] = function()
	if get("DragonReleased") ~= 0 then
		Message(ReleasedText)
		return
	end

	if get("DragonJoined") ~= 0 then
		Message("The dragon is already traveling with you.")
		return
	end

	if get("DragonGrown") ~= 0 then
		evt.AddFollowerNpc(DragonNPC, 0, 0)
		set("DragonJoined", 1)
		Message("The grown dragon joins your company.")
		return
	end

	local food = get("DragonFood")

	if food >= 100 then
		local firstFeed = get("DragonFirstFeedMinutes")

		if nowMinutes() >= firstFeed + GrowMinutes then
			set("DragonGrown", 1)
			Message(Game.NPCText[1689]) -- "Dragon grown!"
		else
			local daysLeft = math.max(1, math.ceil((firstFeed + GrowMinutes - nowMinutes()) / MinutesPerDay))
			Message(Game.NPCText[1684] .. "\n" .. string.format(DaysLeftText, daysLeft)) -- "Dragon ate enough"
		end

		return
	end

	if Party.Food < 5 then
		evt.PlaySound{27}
		Message(HungryText)
		return
	end

	Party.Food = Party.Food - 5

	if get("DragonFirstFeedMinutes") == 0 then
		set("DragonFirstFeedMinutes", nowMinutes())
	end

	food = food + 5
	set("DragonFood", food)
	evt.PlaySound{205}
	Message(string.format(FedText, food))
end

-- Release the dragon for good: OpenYAMM gives him no dismiss topic either.
NPCTopic{
	NPC = DragonNPC,
	Branch = "",
	Slot = 1,
	Topic = ReleaseTopic,
	CanShow = function()
		return Game.NPC[DragonNPC].Hired
	end,
	Ungive = ModRT.async(function()
		if string.lower(Question(ReleaseAsk) or "") ~= "y" then
			return
		end

		evt.RemoveFollowerNpc(DragonNPC)
		set("DragonJoined", 0)
		set("DragonReleased", 1)
		Game.ShowStatusText(ReleasedText)
	end),
}
