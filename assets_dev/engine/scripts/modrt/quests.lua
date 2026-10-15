-- Merge's quests (its Structs/After/LocalizationAndQuests.lua): Quest{...}, Greeting{...}, NPCTopic{...},
-- KillMonstersQuest{...}, QuestBranch and the rest, on OpenYAMM's NPC dialogs.
--
-- The quests' own logic is Merge's (a quest's state in vars.Quests, given, done, its texts and rewards). What Merge
-- does in the original's memory goes through the game instead:
--   - the topics of the NPC being spoken with: the game asks for them as it builds the dialog (dispatch
--     "npcTopics"); a quest named by text shows on one of the free events 995 to 1000, the slot's;
--   - a click on such a topic runs that global event; Quests.execute runs the quest for it (from "globalEventDone",
--     before the mod's EvtGlobal handlers, as Merge's handler comes before theirs);
--   - the greeting ("npcGreeting"), the dialog left ("npcDialogClosed"), the quest log ("questLog").
-- Left out: localization files (texts are used as given), awards and autonotes without a number of their own, and the
-- effects on the portraits when a quest is taken.

local ModRT = ModRT
local Quests_ = {}
ModRT.Quests = Quests_

local FreeEvents = {[0] = 995, 996, 997, 998, 999, 1000}
local SlotLiterals = {A = 0, B = 1, C = 2, D = 3, E = 4, F = 5}

local RegQuests, LastSlot, AutoQuests
local CurrentNPC, CurrentQuests, CurrentBranch, BranchStack

local function saved(name)
    vars[name] = vars[name] or {}
    return vars[name]
end

local function questStates()
    return saved("Quests")
end

-- What the scripts of a playthrough registered goes with them (Loader.loadSessionScripts).
function Quests_.reset()
    _G.Quests = {}
    RegQuests, LastSlot, AutoQuests = {}, {}, {}
    CurrentNPC, CurrentQuests, CurrentBranch, BranchStack = nil, {}, "", {}
end

Quests_.reset()

--!v Either "Add" or "Set": how a quest's bit is given.
TakeQuestOperation = "Add"
QuestNPC = nil

local function protected(fn, ...)
    local ok, result = ModRT.protectedCall(fn, "quest", ...)
    return ok and result or nil
end

local function FindCurrentQuest(npc, slot)
    local t = RegQuests[npc * 7 + slot]

    while t and (t.Branch and t.Branch ~= CurrentBranch or t.CanShow and not protected(t.CanShow, t)) do
        t = t.LastQuest
    end

    return t
end

local function enterNPC(npc)
    -- The playthrough's scripts register the quests; a dialog may be the first thing asked of them.
    ModRT.ensureSession()

    if npc ~= CurrentNPC then
        CurrentNPC = npc
        CurrentBranch = saved("QuestBranches")[npc] or ""
        BranchStack = {}
        events.cocalls("EnterNPC", npc)
    end

    QuestNPC = npc
end

-- The topics the quests set for NPC `npc`'s dialog: {Slot = n, Event = n, Label = text}.
function Quests_.topics(npc)
    enterNPC(npc)
    events.cocalls("ShowNPCTopics", npc)
    CurrentQuests = {}
    local list = {}

    for slot = 0, 5 do
        local t = FindCurrentQuest(-1, slot) or FindCurrentQuest(npc, slot)

        if t then
            local topic = protected(t.GetTopic, t) or 0

            if type(topic) == "number" then
                list[#list + 1] = {Slot = slot, Event = topic}
            else
                local event = FreeEvents[slot]
                list[#list + 1] = {Slot = slot, Event = event, Label = tostring(topic)}
                CurrentQuests[event] = t
            end
        end
    end

    return list
end

-- A quest's topic was clicked (global event `eventId`): runs the quest. Whether there was one.
function Quests_.execute(eventId)
    local t = CurrentNPC and CurrentQuests[eventId]

    if not t then
        return false
    end

    QuestNPC = CurrentNPC
    protected(t.Execute, t)
    return true
end

-- Whether global event `eventId` is the topic of a quest in the dialog shown.
function Quests_.owns(eventId)
    return CurrentNPC ~= nil and CurrentQuests[eventId] ~= nil
end

function Quests_.greeting(npc, seen)
    enterNPC(npc)
    local s, s1

    for slot = 5, -1, -1 do
        local t = FindCurrentQuest(npc, slot)

        if t then
            s = s or protected(t.GetGreeting, t, seen)

            if s then
                break
            elseif not seen then
                s1 = s1 or protected(t.GetGreeting, t, true)
            end
        end
    end

    return s or s1
end

function Quests_.dialogClosed()
    if CurrentNPC then
        events.cocalls("ExitNPC", CurrentNPC)
    end

    CurrentNPC, CurrentQuests, CurrentBranch, BranchStack = nil, {}, "", {}
end

-- The quest log entries of the quests that have no bit of their own (Merge's AutoQuest).
function Quests_.questLog()
    local texts = {}

    for _, t in ipairs(AutoQuests) do
        if protected(t.IsQuestVisible, t) then
            local text = t.Texts and t.Texts.Quest or t.QuestText

            if type(text) == "string" and text ~= "" then
                texts[#texts + 1] = text
            end
        end
    end

    return texts
end

function GetCurrentNPC()
    return CurrentNPC
end

GetCurrentAnyNPC = GetCurrentNPC

-- The game builds the dialog's topics again after each topic, so there is nothing to do here.
function UpdateNPCQuests() end

-- Closing the dialog from a script is not supported; the player leaves it.
function ExitScreen() end
ExitCurrentScreen = ExitScreen

-- Sets the dialog's branch to `branch`; `persist` keeps it for the next time the NPC is spoken with. Without
-- arguments: the current branch.
function QuestBranch(branch, persist)
    if not branch then
        return CurrentBranch
    end

    CurrentBranch = branch

    if persist and CurrentNPC then
        saved("QuestBranches")[CurrentNPC] = branch
    end
end

function QuestBranchScreen(branch)
    BranchStack[#BranchStack + 1] = CurrentBranch
    CurrentBranch = branch or ""
end

function ExitQuestBranch(all)
    if all then
        CurrentBranch = BranchStack[1] or CurrentBranch
        BranchStack = {}
    elseif BranchStack[1] then
        CurrentBranch = BranchStack[#BranchStack]
        BranchStack[#BranchStack] = nil
        return #BranchStack
    end
end

function GetQuestBranchStack()
    return BranchStack
end

local function RegisterQuest(t)
    if not t.Slot then
        for slot = 0, 5 do
            if not RegQuests[t.NPC * 7 + slot] then
                t.Slot = slot
                break
            end
        end
    elseif t.Slot == "same" or t.Slot == "Same" then
        t.Slot = LastSlot[t.NPC] or 0
    end

    assert(t.Slot and t.Slot >= -1 and t.Slot < 6, "invalid slot number")
    LastSlot[t.NPC] = t.Slot
    t.LastQuest = RegQuests[t.NPC * 7 + t.Slot]
    RegQuests[t.NPC * 7 + t.Slot] = t
end

-- A quest with a log entry and no bit of its own.
function AutoQuest(t, text)
    if type(t) == "function" then
        t = {IsQuestVisible = t}
    end

    AutoQuests[#AutoQuests + 1] = t
    t.QuestIndex = t.QuestIndex or ("auto" .. #AutoQuests)
    t.QuestText = text or t.QuestText
    return t, t.QuestIndex
end

function AddGoldExp(gold, exp)
    if gold and gold ~= 0 then
        evt[0][gold > 0 and "Add" or "Subtract"]("Gold", math.abs(gold))
    end

    if exp and exp ~= 0 then
        evt.All.Add("Experience", exp)
    end
end

-- Takes item `id` from the first party member who has it (keeps it with `keep`). A table of ids: all of them; with
-- Count: that many of the listed ones.
function TakeItemFromParty(id, keep)
    if type(id) ~= "table" then
        for i = 0, Party.Count - 1 do
            if evt[i].Cmp("Inventory", id) then
                if not keep then
                    evt[i].Subtract("Inventory", id)
                end

                return true
            end
        end

        return false
    end

    local n = id.Count or id.count

    if n then
        local have = 0

        for _, v in ipairs(id) do
            for i = 0, Party.Count - 1 do
                if evt[i].Cmp("Inventory", v) then
                    have = have + 1
                end
            end
        end

        if have < n then
            return false
        end

        if not keep then
            local taken = 0

            for _, v in ipairs(id) do
                while taken < n and TakeItemFromParty(v) do
                    taken = taken + 1
                end
            end
        end

        return true
    end

    for _, v in ipairs(id) do
        if not TakeItemFromParty(v, true) then
            return false
        end
    end

    if not keep then
        for _, v in ipairs(id) do
            TakeItemFromParty(v)
        end
    end

    return true
end

local QuestProto = {
    IsGiven = function(t) return questStates()[t.BaseName] == t.GivenState end,
    IsQuestVisible = function(t) return t:IsGiven() end,

    IsAwarded = function(t, idx)
        if not t.StoreAwards then
            return questStates()[t.BaseName] == t.DoneState
        end

        local a = saved("QuestAwards")[t.BaseName]
        return a and a[idx]
    end,

    IsAutonoteVisible = function(t)
        return saved("QuestAutonotes")[t.BaseName]
    end,

    GetGreeting = function(t, seen)
        if not seen then
            return t.Texts.FirstGreet
        end

        local topic = t.Texts["Greet" .. (questStates()[t.BaseName] or "")]

        if topic then
            return topic
        elseif topic == nil then
            return t.Texts.Greet
        end
    end,

    AddQuestBit = function(t)
        if tonumber(t.Quest) and t.TakeQuestOperation then
            evt[t.TakeQuestOperation == "Set" and "Set" or "Add"]("QBits", t.Quest)
        end
    end,

    AddAward = function(t)
        if tonumber(t.Award) then
            evt.All.Add("Awards", t.Award)
        end
    end,

    AddAutonote = function(t, force)
        if tonumber(t.Autonote) then
            if force or not evt.Cmp("AutonotesBits", t.Autonote) then
                evt.Add("AutonotesBits", t.Autonote)
            end
        end
    end,
}

function Quest(t)
    for k, v in pairs(QuestProto) do
        if t[k] == nil then
            t[k] = v
        end
    end

    t.Name = t.Name or t[1]

    if t.Name and Quests[t.Name] then
        error(("Quest called %q already exists"):format(t.Name), 2)
    end

    t.NPC = t.NPC or QuestNPC
    t.Slot = t.Slot or t.Event
    t.Slot = SlotLiterals[t.Slot] or t.Slot
    RegisterQuest(t)

    if not t.Name then
        t.Name = ("NPC = %d, %sSlot = %s"):format(t.NPC, t.Branch and "Branch = '" .. t.Branch .. "', " or "", t.Slot)

        while Quests[t.Name] do
            t.Name = t.Name .. " +"
        end
    end

    t.Award = t.Award or t.Awards
    Quests[t.Name] = t
    t.BaseName = t.BaseName or t.Name
    t.GivenState = t.GivenState or "Given"
    t.DoneState = t.DoneState or "Done"
    t.QuestIndex = t.Quest or nil

    if t.Quest == true then
        AutoQuest(t)
    end

    t.SetTexts = t.SetTexts or function(t1, texts)
        t.Texts = texts or t1

        if t.Texts.Quest and t.Quest ~= false then
            if not t.QuestIndex then
                t.Quest = true
                AutoQuest(t)
            end

            if type(t.QuestIndex) == "number" then
                Game.QuestsTxt[t.QuestIndex] = t.Texts.Quest
            end
        end

        return t
    end

    t:SetTexts(t.Texts or {})

    if t.StoreAwards == nil then
        t.StoreAwards = true
    end

    t.TakeQuestOperation = t.TakeQuestOperation or TakeQuestOperation or "Add"

    if t.Slot < 0 then
        return t
    end

    t.GetTopic = t.GetTopic or function(t)
        local s = questStates()[t.BaseName] or ""
        local topic

        if s == "" then
            topic = t.FirstStdTopic or t.Texts.FirstTopic
        else
            topic = t["StdTopic" .. s] or t.Texts["Topic" .. s]
        end

        if topic == nil then
            return t.StdTopic or t.Texts.Topic
        end

        return topic
    end

    t.Execute = t.Execute or function(t)
        local states = questStates()
        local state = states[t.BaseName]
        local ev

        if state == nil and not t.NeverGiven then
            if (t.CheckGive == nil and (t.Give or t.Texts.Give or t.QuestIndex)) or t.CheckGive and t.CheckGive(t) then
                t:AddQuestBit(t)
                states[t.BaseName] = t.GivenState

                if t.GivenItem then
                    evt.Add("Inventory", t.GivenItem)
                end

                ev = "Give"
            else
                ev = "Ungive"
            end
        elseif state == nil or t:IsGiven() then
            if ((t.CheckDone == nil) or t.CheckDone and t.CheckDone(t))
                and Party.Gold >= (t.QuestGold or -1 / 0)
                and (t.QuestItem == nil or TakeItemFromParty(t.QuestItem, t.KeepQuestItem)) then
                AddGoldExp((t.Gold or 0) - (t.QuestGold or 0), t.Experience or t.Exp)
                t:AddAward(t)

                if t.RewardItem then
                    evt.Current.Add("Inventory", t.RewardItem)
                end

                if not t.NeverDone then
                    if tonumber(t.Quest) then
                        evt.Subtract("QBits", t.Quest)
                    end

                    states[t.BaseName] = t.DoneState
                end

                ev = "Done"
            else
                ev = "Undone"
            end
        elseif state == t.DoneState then
            ev = "After"
        else
            ev = state
        end

        if not t.CanAddAutonote or t:CanAddAutonote() then
            t:AddAutonote()
        end

        if t.Texts[ev] then
            Message(t.Texts[ev])
        end

        if t[ev] then
            t[ev](t)
        end
    end

    return t
end

function Greeting(t)
    t.Slot = t.Slot or -1
    local greet1 = t[1] or t.Text
    local texts = t.Texts or {}
    t.Texts = texts
    texts.FirstGreet = greet1 or texts.FirstGreet
    texts.Greet = t[2] or t.TextSeen or greet1 or texts.Greet
    t[1], t[2], t.Text, t.TextSeen = nil, nil, nil, nil
    return Quest(t)
end

function NPCTopic(t)
    local topic = t[1] or t.Topic

    if type(topic) == "number" then
        t.StdTopic = topic
    else
        local texts = t.Texts or {}
        t.Texts = texts
        texts.Topic = topic or texts.Topic
        texts.Ungive = t[2] or t.Text or texts.Ungive
    end

    t[1], t[2], t.Topic, t.Text = nil, nil, nil, nil
    return Quest(t)
end

-- A quest to kill monsters (Merge's KillMonstersQuest): done when the listed monsters are dead. Each task is a table of
-- evt.CheckMonstersKilled parameters with the Map it is on; it counts as done once seen done there.
function KillMonstersQuest(t)
    local tasks = {}
    local done = saved("KillMonsterTasksDone")
    t.Texts = t.Texts or {}

    for i = #t, 1, -1 do
        local task = t[i]

        if type(task) == "table" then
            table.insert(tasks, 1, task)
            task.Map = tostring(task.Map or ""):lower()
        end

        t[i] = nil
    end

    local function checkTask(task)
        if done[task.TaskId] then
            return true
        end

        if Map and Map.Name and Map.Name:lower() == task.Map and evt.CheckMonstersKilled(task) then
            done[task.TaskId] = true
            return true
        end

        return false
    end

    local checkDone = t.CheckDone

    function t.CheckDone(t1)
        for _, task in ipairs(tasks) do
            if not checkTask(task) then
                return false
            end
        end

        return not checkDone or checkDone(t1)
    end

    if t.Quest == nil then
        t.Quest = true
    end

    Quest(t)

    for i, task in ipairs(tasks) do
        task.TaskId = ("%s,%s"):format(i, t.Name)
    end

    function events.LeaveMap()
        for _, task in ipairs(tasks) do
            checkTask(task)
        end
    end

    return t
end

-- A function to show a topic only while quest `name` (the topic's own by default) is in state `state`.
function QCheck(name, ...)
    local state = ...

    if select("#", ...) == 0 then
        name, state = nil, name
    end

    return function(t) return questStates()[name or t.BaseName] == state end
end
