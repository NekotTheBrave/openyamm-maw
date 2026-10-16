CrossContinents = CrossContinents or {}

CrossContinents.Config = CrossContinents.Config or {
    VerdantNpcId = 803,
    ChargedConnectorStoneItemId = 624,
    DischargedConnectorStoneItemId = 625,
    SharedLifeRingItemId = 543,
    EnrothLetterItemId = 2125,
    ScrollChestX = 0,
    ScrollChestY = 0,
    ScrollExperienceThreshold = 50001,
    IntroTopicId = 1778,
    DimensionalTravelTopicId = 10000,
    ConnectorTopicId = 1788,
    CompleteQBits = {
        [1] = 228, -- Jadame
        [2] = 783, -- Antagarich
        [3] = 784, -- Enroth
    },
    ContinentTopicIds = {
        [1] = 1782, -- Jadame
        [2] = 1783, -- Antagarich
        [3] = 1784, -- Enroth
    },
    MeetSpotHouseIds = {
        [1] = 185,
        [2] = 641,
        [3] = 1195,
    },
    ScrollItemIds = {
        [1] = 770,
        [2] = 771,
        [3] = 772,
    },
    ContinentMessageText = {
        [1] = "Jadame starts on Dagger Wound. Help the caravan, reach Ravenshore, and hold the alliance together.",
        [2] = "Antagarich begins with the Emerald Island scavenger hunt and leads to Harmondale. "
            .. "Become lords, then keep your land alive.",
        [3] = "Enroth is the simple path: train hard, help where you can, "
            .. "and try not to mistake every future devil for an apprentice wizard.",
    },
}

function CrossContinents.Var(name)
    return "MMerge.CrossContinents." .. name
end

function CrossContinents.GetVar(name, defaultValue)
    return evt.GetGlobalVar(CrossContinents.Var(name), defaultValue or 0)
end

function CrossContinents.SetVar(name, value)
    evt.SetGlobalVar(CrossContinents.Var(name), value or 0)
end

function CrossContinents.CurrentContinent()
    local continent = CurrentContinent()
    if continent >= 1 and continent <= 3 then
        return continent
    end

    local mapName = string.lower(evt.GetCurrentMapName() or "")
    if string.sub(mapName, 1, 1) == "7" then
        return 2
    end

    return continent
end

function CrossContinents.IsContinentFinished(continent)
    local qbitId = CrossContinents.Config.CompleteQBits[continent]
    if qbitId == nil then
        return false
    end

    return IsQBitSet(QBit(qbitId)) or CrossContinents.GetVar("Finished." .. tostring(continent), 0) ~= 0
end

function CrossContinents.MarkContinentFinished(continent)
    CrossContinents.SetVar("Finished." .. tostring(continent), 1)
end

function CrossContinents.RewardCount()
    local count = 0
    for continent = 1, 3 do
        if CrossContinents.GetVar("Reward." .. tostring(continent), 0) ~= 0 then
            count = count + 1
        end
    end
    return count
end

function CrossContinents.GivePartyItem(itemId)
    if evt.GivePartyMemberItem(0, itemId, 1) then
        return true
    end

    evt.StatusText("Inventory is full")
    return false
end

function CrossContinents.PlaceVerdantForCurrentContinent()
    if CrossContinents.GetVar("MetVerdant", 0) == 0 and CrossContinents.GetVar("GotMainQuest", 0) == 0 then
        return
    end

    local houseId = CrossContinents.Config.MeetSpotHouseIds[CrossContinents.CurrentContinent()]
    if houseId ~= nil then
        evt.MoveNPC(CrossContinents.Config.VerdantNpcId, houseId)
    end
end

function CrossContinents.UpdateVerdantTopics()
    local npcId = CrossContinents.Config.VerdantNpcId
    evt.SetNPCTopic(npcId, 0, 0)
    evt.SetNPCTopic(npcId, 1, 0)
    evt.SetNPCTopic(npcId, 2, 0)
    evt.SetNPCTopic(npcId, 3, 0)

    if CrossContinents.GetVar("GotMainQuest", 0) == 0 then
        evt.SetNPCTopic(npcId, 0, CrossContinents.Config.IntroTopicId)
        return
    end

    local continent = CrossContinents.CurrentContinent()
    local topicId = CrossContinents.Config.ContinentTopicIds[continent]
    evt.SetNPCTopic(npcId, 3, CrossContinents.Config.DimensionalTravelTopicId)
    evt.SetNPCTopic(npcId, 0, topicId or 0)

    if CrossContinents.GetVar("GotConnectorStone", 0) ~= 0 then
        evt.SetNPCTopic(npcId, 2, CrossContinents.Config.ConnectorTopicId)
    end
end

function CrossContinents.HasScroll()
    for _, itemId in pairs(CrossContinents.Config.ScrollItemIds) do
        evt.ForPlayer(Players.All)
        if HasItem(itemId) then
            return true, itemId
        end
    end

    return false, 0
end

function CrossContinents.UpdateScrollState()
    local hasScroll, itemId = CrossContinents.HasScroll()
    if hasScroll then
        CrossContinents.SetVar("ScrollGotten", 1)
        CrossContinents.SetVar("ScrollItemId", itemId)
    end
end

function CrossContinents.TryGenerateScrollForChest(chestId)
    if CrossContinents.GetVar("MetVerdant", 0) ~= 0
        or CrossContinents.GetVar("GotMainQuest", 0) ~= 0
        or CrossContinents.GetVar("ScrollGotten", 0) ~= 0
        or CrossContinents.GetVar("ScrollGenerated", 0) ~= 0 then
        return false
    end

    evt.ForPlayer(Players.Member0)
    if not IsAtLeast(Experience, CrossContinents.Config.ScrollExperienceThreshold) then
        return false
    end

    local continent = CrossContinents.CurrentContinent()
    local itemId = CrossContinents.Config.ScrollItemIds[continent] or CrossContinents.Config.ScrollItemIds[1]
    evt.EnsureChestItem(
        chestId or 0,
        itemId,
        CrossContinents.Config.ScrollChestX,
        CrossContinents.Config.ScrollChestY)
    CrossContinents.SetVar("ScrollGenerated", 1)
    CrossContinents.SetVar("ScrollItemId", itemId)
    return true
end

function CrossContinents.SpeakVerdant(greetingId)
    local npcId = CrossContinents.Config.VerdantNpcId
    if greetingId ~= nil then
        evt.SetNPCGreeting(npcId, greetingId)
    end

    CrossContinents.UpdateVerdantTopics()
    evt.SpeakNPC(npcId)
end

function CrossContinents.TryFirstContact(reason)
    if CrossContinents.GetVar("GotMainQuest", 0) ~= 0 then
        return false
    end

    CrossContinents.UpdateScrollState()

    local greetingId = 323
    if reason == "scroll" or CrossContinents.GetVar("ScrollGotten", 0) ~= 0 then
        greetingId = 329
    elseif reason == "enroth_letter" then
        greetingId = 330
    end

    CrossContinents.SpeakVerdant(greetingId)
    return true
end

function CrossContinents.TryDimensionDoorContact()
    return CrossContinents.TryFirstContact("dimension_door")
end

function CrossContinents.HandleVerdantIntro()
    local step = CrossContinents.GetVar("IntroStep", 0)
    CrossContinents.SetVar("MetVerdant", 1)

    if step == 0 then
        evt.SimpleMessage("Verdant introduces herself as a time traveler and says the world needs your help.")
        CrossContinents.SetVar("IntroStep", 1)
    elseif step == 1 then
        evt.SimpleMessage("Verdant explains that missing heroes have left several histories without champions.")
        CrossContinents.SetVar("IntroStep", 2)
    elseif step == 2 then
        evt.SimpleMessage("Verdant believes your party can take their place without causing another disaster.")
        CrossContinents.SetVar("IntroStep", 3)
    else
        evt.SimpleMessage(
            "Verdant asks you to save each continent's story and use dimension doors to move between them.")
        CrossContinents.SetVar("GotMainQuest", 1)
        CrossContinents.SetVar("StartedContinent", CrossContinents.CurrentContinent())
        evt.SetNPCGreeting(CrossContinents.Config.VerdantNpcId, 328)
    end

    CrossContinents.UpdateVerdantTopics()
end

function CrossContinents.ExplainCurrentContinent(continent)
    evt.SimpleMessage(
        CrossContinents.Config.ContinentMessageText[continent]
        or "Use dimension doors to continue the displaced heroes' stories.")
    CrossContinents.UpdateVerdantTopics()
end

function CrossContinents.ExplainDimensionDoors()
    evt.SimpleMessage("Dimension doors in magically active places let you step onto the other heroes' paths.")
    CrossContinents.UpdateVerdantTopics()
end

function CrossContinents.HandleConnectorStone()
    if CrossContinents.GetVar("GotConnectorStone", 0) == 0 then
        if not CrossContinents.GivePartyItem(CrossContinents.Config.ChargedConnectorStoneItemId) then
            return
        end

        CrossContinents.SetVar("GotConnectorStone", 1)
    end

    local message = "The connector stone lets you call Verdant, but it must recharge after each use."
    if CrossContinents.GetVar("ImprovedConnector", 0) ~= 0 then
        message = message .. " Verdant has improved it so the charged stone can also help restore the party."
    end
    evt.SimpleMessage(message)
    CrossContinents.UpdateVerdantTopics()
end

function CrossContinents.ApplyCompletionReward(continent)
    if CrossContinents.GetVar("Reward." .. tostring(continent), 0) ~= 0 then
        return false
    end

    local rewardCount = CrossContinents.RewardCount()

    if rewardCount < 2 then
        if not CrossContinents.GivePartyItem(CrossContinents.Config.SharedLifeRingItemId) then
            return false
        end
    else
        CrossContinents.SetVar("ImprovedConnector", 1)
    end

    CrossContinents.SetVar("Reward." .. tostring(continent), 1)
    return true
end

function CrossContinents.CatchCurrentCompletion()
    local continent = CrossContinents.CurrentContinent()
    if continent < 1 or continent > 3 or not CrossContinents.IsContinentFinished(continent) then
        return false
    end

    CrossContinents.MarkContinentFinished(continent)
    if CrossContinents.ApplyCompletionReward(continent) then
        CrossContinents.SpeakVerdant(328 - continent)
        return true
    end

    return false
end

function CrossContinents.UpdateConnectorState()
    local currentContinent = CrossContinents.CurrentContinent()
    local startedContinent = CrossContinents.GetVar("StartedContinent", 0)
    if CrossContinents.GetVar("GotMainQuest", 0) ~= 0
        and currentContinent >= 1 and currentContinent <= 3
        and startedContinent ~= 0
        and currentContinent ~= startedContinent
        and CrossContinents.GetVar("GotConnectorStone", 0) == 0 then
        if not CrossContinents.GivePartyItem(CrossContinents.Config.ChargedConnectorStoneItemId) then
            return
        end

        CrossContinents.SetVar("GotConnectorStone", 1)
        CrossContinents.SpeakVerdant(331)
    end
end

function CrossContinents.UpdateEnrothLetterState()
    if CrossContinents.CurrentContinent() ~= 3
        or CrossContinents.GetVar("MetVerdant", 0) == 0
        or IsQBitSet(QBit(1104)) then
        return
    end

    if not CrossContinents.GivePartyItem(CrossContinents.Config.EnrothLetterItemId) then
        return
    end

    SetQBit(QBit(1104))
    SetQBit(QBit(1105))
    CrossContinents.SpeakVerdant(330)
end

function CrossContinents.UpdateFallbackFirstContact()
    if CrossContinents.GetVar("MetVerdant", 0) ~= 0
        or CrossContinents.GetVar("GotMainQuest", 0) ~= 0 then
        return
    end

    local now = CurrentGameMinutes()
    local meetTime = CrossContinents.GetVar("MeetTime", 0)
    if meetTime == 0 then
        CrossContinents.SetVar("MeetTime", now + (8 * 28 * 24 * 60))
        return
    end

    if now >= meetTime then
        CrossContinents.TryFirstContact("fallback")
        CrossContinents.SetVar("MeetTime", now + (2 * 24 * 60))
    end
end

function CrossContinents.UpdateState()
    CrossContinents.UpdateScrollState()
    CrossContinents.PlaceVerdantForCurrentContinent()

    for continent = 1, 3 do
        if CrossContinents.IsContinentFinished(continent) then
            CrossContinents.MarkContinentFinished(continent)
        end
    end

    if CrossContinents.CatchCurrentCompletion() then
        CrossContinents.UpdateVerdantTopics()
        return
    end

    CrossContinents.UpdateConnectorState()
    CrossContinents.UpdateEnrothLetterState()
    CrossContinents.UpdateFallbackFirstContact()
    CrossContinents.UpdateVerdantTopics()
end

function CrossContinents.HandleVerdantEnter(context)
    if context == nil or context.npcId ~= CrossContinents.Config.VerdantNpcId then
        return
    end

    CrossContinents.UpdateState()
end

function CrossContinents.RechargeConnectorStones()
    local rechargeMinutes = CrossContinents.GetVar("NextConnectorRecharge", 0)
    if CurrentGameMinutes() < rechargeMinutes then
        return 0
    end

    return ReplacePartyInventoryItems(
        CrossContinents.Config.DischargedConnectorStoneItemId,
        CrossContinents.Config.ChargedConnectorStoneItemId)
end

RegisterGlobalOnLoadEvent(65200, "MMerge CrossContinents shared state", function()
    CrossContinents.UpdateState()
end)

RegisterGlobalTimerEvent(65201, 600, function()
    CrossContinents.UpdateState()
end, "MMerge CrossContinents shared timer", nil, 600)

RegisterGlobalChestOpenHook(65202, "MMerge CrossContinents scroll chest", function(context)
    CrossContinents.TryGenerateScrollForChest(context.chestId)
end)

RegisterGlobalNpcEnterHook(65203, "MMerge CrossContinents Verdant state", function(context)
    CrossContinents.HandleVerdantEnter(context)
end)

RegisterGlobalInventoryOpenHook(65204, "MMerge CrossContinents connector recharge", function(context)
    CrossContinents.RechargeConnectorStones()
end)

RegisterGlobalEvent(1778, "MMMerge Verdant important matter", function()
    CrossContinents.HandleVerdantIntro()
end)

RegisterGlobalEvent(1781, "MMMerge Verdant dimension doors", function()
    CrossContinents.ExplainDimensionDoors()
end)

RegisterGlobalEvent(10000, "Verdant dimensional travel", function()
    evt.OpenDimensionDoor()
end)

RegisterGlobalEvent(1782, "MMMerge Verdant Jadame", function()
    CrossContinents.ExplainCurrentContinent(1)
end)

RegisterGlobalEvent(1783, "MMMerge Verdant Antagarich", function()
    CrossContinents.ExplainCurrentContinent(2)
end)

RegisterGlobalEvent(1784, "MMMerge Verdant Enroth", function()
    CrossContinents.ExplainCurrentContinent(3)
end)

RegisterGlobalEvent(1788, "MMMerge Verdant connector stone", function()
    CrossContinents.HandleConnectorStone()
end)
