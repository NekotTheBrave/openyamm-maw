-- OpenYAMM replacement for MAW's General/MenuExtraSettings.lua.
--
-- The original is Merge's "extra settings" menu with MAW's difficulty and game-mode selectors added. The menu is
-- drawn with the original engine's interface code and cannot run here; what the rest of MAW needs from the file is
-- the state it sets up, and that is what this file provides:
--
--   Game.BolsterAmount   difficulty: 0 Easy, 50 Normal, 100 MAW, 150 Hard, 200 Hell, 250 Nightmare
--   MAWBOLSTER           the names of those values (MAW edits entry 600 when a game mode is active)
--   Game.Mode            game mode: 0 Normal, 2 Doom, 4 Road to Insanity, 6 Beyond Madness, 8 Austerity
--   Game.freeProgression MAW's "free progression" switch: on, monsters everywhere follow the party's level; off,
--                        each map keeps its own level ("horizontal" progression). On by default, as in MAW.
--   vars.ExtraSettings   where Merge keeps these settings in the save
--
-- The difficulty is chosen in the game: on character creation for a new game, and in the options (Gameplay >
-- Difficulty) for the game being played, which MAW's settings page does in the original (its monsters are worked
-- out again at once, as MAW does when that page is left). The engine asks ModRT.Difficulty for MAW's names. A new
-- game starts at the one chosen last, else the options' ([features] difficulty), else mods.yml's:
--
--   mods:
--     - id: maw
--       settings:
--         difficulty: MAW          # Easy | Normal | MAW | Hard | Hell | Nightmare
--         free_progression: true   # true | false
--
-- After that it belongs to the save, as in MAW.
--
-- The game mode is chosen on character creation (ModRT.GameMode) for the new game and is the save's from then on, as
-- in MAW, where it is a selector on the same page. MAW keeps it in vars: Doom is vars.Mode 2, Road to Insanity adds
-- vars.insanityMode, Beyond Madness adds vars.madnessMode too, and Austerity is vars.AusterityMode, with any
-- difficulty. Doom and the two above it play at bolster 600 whatever the difficulty (MAW's nightmare() sets it), so
-- the difficulty then is the mode's own: its one choice is the mode's name.

MAWBOLSTER = {
    [0] = "Easy", [50] = "Norm.", [100] = "MAW", [150] = "Hard", [200] = "Hell", [250] = "Night\nmare",
    [300] = "Night\nmare", [600] = "Doom",
}

local difficultyByName = {easy = 0, normal = 50, maw = 100, hard = 150, hell = 200, nightmare = 250}
local difficultyNames = {"Easy", "Normal", "MAW", "Hard", "Hell", "Nightmare"}
-- The difficulty chosen in the game (character creation, the options) for the next new game.
local chosenDifficulty = nil

local function configuredDifficulty()
    if chosenDifficulty then
        return chosenDifficulty
    end

    local setting = modrt.difficultySetting and modrt.difficultySetting() or ""
    local name = tostring(setting ~= "" and setting or ModSettings.difficulty or "MAW"):lower()
    local value = difficultyByName[name]

    if value == nil then
        print("MenuExtraSettings: unknown difficulty '" .. name .. "', using MAW")
        value = 100
    end

    return value
end

local function difficultyName(value)
    if value >= 250 then
        return "Nightmare"
    end

    return difficultyNames[math.floor(value / 50) + 1] or "MAW"
end

local function inGame()
    return Map ~= nil and Map.Name ~= nil and modrt.gameGet("MapReady") == true
end

-- The game modes, by MAW's numbers (gameMode in MAW's file), and the one chosen for the next new game.
local modeNames = {"Normal", "Doom", "Road to Insanity", "Beyond Madness", "Austerity"}
local modeByName = {normal = 0, doom = 2, ["road to insanity"] = 4, ["beyond madness"] = 6, austerity = 8}
local chosenMode = 0
-- A mode chosen on character creation and not yet started: the difficulties offered follow it, not the game loaded.
local modePending = false

local function modeName(value)
    return modeNames[math.floor(value / 2) + 1] or "Normal"
end

-- The name of the difficulty a Doom-family mode plays at (MAW's MAWBOLSTER[600], which zzMaw-Monsters.lua names by
-- the mode), or nil for a mode played at the chosen difficulty.
local function modeDifficulty(mode, insanity, madness)
    if mode ~= 2 then
        return nil
    end

    return madness and "Mad" or insanity and "Insane" or "Doom"
end

local function currentModeDifficulty()
    if inGame() and not modePending then
        return modeDifficulty(vars.Mode, vars.insanityMode, vars.madnessMode)
    end

    return modeDifficulty(chosenMode >= 2 and chosenMode <= 6 and 2 or 0, chosenMode >= 4, chosenMode == 6)
end

ModRT.GameMode = {
    choices = function()
        return modeNames
    end,
    -- The mode the next new game starts in.
    get = function()
        return modeName(chosenMode)
    end,
    set = function(name)
        local value = modeByName[tostring(name):lower()]

        if value ~= nil then
            chosenMode = value
            modePending = true
        end
    end,
}

ModRT.Difficulty = {
    choices = function()
        local fixed = currentModeDifficulty()
        return fixed and {fixed} or difficultyNames
    end,
    get = function()
        local playing = inGame() and not modePending
        return currentModeDifficulty() or difficultyName(playing and Game.BolsterAmount or configuredDifficulty())
    end,
    -- The game being played changes at once, its monsters worked out again (MAW does so when its settings page is
    -- left, events.Action 113 in zzMaw-Monsters.lua); the next new game starts at it too.
    set = function(name)
        local value = difficultyByName[tostring(name):lower()]

        if value == nil or (inGame() and vars.Mode == 2) then
            return
        end

        chosenDifficulty = value

        if inGame() and Game.BolsterAmount ~= value then
            Game.BolsterAmount = value
            vars.ExtraSettings = vars.ExtraSettings or {}
            vars.ExtraSettings.BolsterAmount = value

            if type(recalculateMonsterTable) == "function" then
                recalculateMonsterTable()
            end

            if type(recalculateMawMonster) == "function" then
                recalculateMawMonster()
            end
        end
    end,
}

local function configuredFreeProgression()
    local setting = ModSettings.free_progression

    if setting == nil then
        return true
    end

    setting = tostring(setting):lower()
    return not (setting == "false" or setting == "0" or setting == "no" or setting == "off")
end

Game.BolsterAmount = configuredDifficulty()
Game.Mode = 0
Game.UseMonsterBolster = false
Game.freeProgression = configuredFreeProgression()

function events.BeforeNewGameAutosave()
    Game.BolsterAmount = configuredDifficulty()
    vars.ExtraSettings = vars.ExtraSettings or {}
    vars.ExtraSettings.UseMonsterBolster = false
    Game.freeProgression = configuredFreeProgression()

    -- The mode's switches, as MAW's own file sets them.
    modePending = false
    Game.Mode = chosenMode
    vars.Mode = Game.Mode

    if vars.Mode == 4 then
        vars.insanityMode = true
        vars.Mode = 2
    end

    if vars.Mode == 2 then
        vars.DoomPartyNeedInit = true
    end

    if vars.Mode == 8 then
        vars.AusterityMode = true
    end

    if vars.Mode == 6 then
        vars.Mode = 2
        vars.insanityMode = true
        vars.madnessMode = true
        vars.freeProgression = false
        Game.freeProgression = false
        vars.DoomPartyNeedInit = true
    end

    -- Doom and above play at MAW's 600 from the start (MAW's nightmare() keeps it so).
    if vars.Mode == 2 then
        Game.BolsterAmount = 600
    end

    vars.ExtraSettings.BolsterAmount = Game.BolsterAmount
    vars.ExtraSettings.freeProgression = Game.freeProgression
end

-- A Doom-family party starts with 5000 gold and 10000 experience shared out; a Beyond Madness one also starts every
-- character on the roster at level 1 with its skills at 1, as MAW's own file does on arriving at the first map.
local function initDoomParty()
    vars.DoomPartyNeedInit = nil

    if vars.madnessMode then
        vars.Mode = 2
        vars.insanityMode = true
        vars.freeProgression = false
        Game.freeProgression = false

        for i = 0, Party.PlayersArray.High do
            local pl = Party.PlayersArray[i]
            pl.LevelBase = 1
            pl.Experience = 0
            pl.SkillPoints = 0

            for j = 0, 38 do
                if pl.Skills[j] > 0 then
                    pl.Skills[j] = 1
                end
            end
        end
    end

    Party.Gold = 5000

    for i = 0, Party.High do
        Party[i].Experience = Party[i].Experience + 10000 / Party.Count
    end
end

-- A loaded game plays at the difficulty it was saved with.
function events.BeforeLoadMap(wasInGame)
    if wasInGame then
        return
    end

    if vars.DoomPartyNeedInit then
        initDoomParty()
    end

    vars.ExtraSettings = vars.ExtraSettings or {}

    if type(vars.ExtraSettings.BolsterAmount) == "number" then
        Game.BolsterAmount = vars.ExtraSettings.BolsterAmount
    else
        vars.ExtraSettings.BolsterAmount = Game.BolsterAmount
    end

    if type(vars.ExtraSettings.freeProgression) == "boolean" then
        Game.freeProgression = vars.ExtraSettings.freeProgression
    else
        vars.ExtraSettings.freeProgression = Game.freeProgression
    end
end

-- MAW changes the difficulty itself in places (Nightmare confirms and becomes 300); keep the save in step.
function events.BeforeSaveGame()
    vars.ExtraSettings = vars.ExtraSettings or {}
    vars.ExtraSettings.BolsterAmount = Game.BolsterAmount
    vars.ExtraSettings.freeProgression = Game.freeProgression and true or false
end
