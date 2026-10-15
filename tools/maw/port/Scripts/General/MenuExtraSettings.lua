-- OpenYAMM replacement for MAW's General/MenuExtraSettings.lua.
--
-- The original is Merge's "extra settings" menu with MAW's difficulty and game-mode selectors added. The menu is
-- drawn with the original engine's interface code and cannot run here; what the rest of MAW needs from the file is
-- the state it sets up, and that is what this file provides:
--
--   Game.BolsterAmount   difficulty: 0 Easy, 50 Normal, 100 MAW, 150 Hard, 200 Hell, 250 Nightmare
--   MAWBOLSTER           the names of those values (MAW edits entry 600 when a game mode is active)
--   Game.Mode            game mode; only 0, Normal, is supported
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

ModRT.Difficulty = {
    choices = function()
        return difficultyNames
    end,
    get = function()
        return difficultyName(inGame() and Game.BolsterAmount or configuredDifficulty())
    end,
    -- The game being played changes at once, its monsters worked out again (MAW does so when its settings page is
    -- left, events.Action 113 in zzMaw-Monsters.lua); the next new game starts at it too.
    set = function(name)
        local value = difficultyByName[tostring(name):lower()]

        if value == nil then
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
    Game.Mode = 0
    vars.Mode = 0
    vars.ExtraSettings = vars.ExtraSettings or {}
    vars.ExtraSettings.BolsterAmount = Game.BolsterAmount
    vars.ExtraSettings.UseMonsterBolster = false
    Game.freeProgression = configuredFreeProgression()
    vars.ExtraSettings.freeProgression = Game.freeProgression
end

-- A loaded game plays at the difficulty it was saved with.
function events.BeforeLoadMap(wasInGame)
    if wasInGame then
        return
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
