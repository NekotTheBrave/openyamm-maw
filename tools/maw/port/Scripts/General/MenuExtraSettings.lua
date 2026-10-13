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
-- The difficulty of a new game comes from mods.yml:
--
--   mods:
--     - id: maw
--       settings:
--         difficulty: MAW          # Easy | Normal | MAW | Hard | Hell | Nightmare
--         free_progression: true   # true | false
--
-- After that it belongs to the save, as in MAW, and mods.yml is not consulted again for that game.

MAWBOLSTER = {
    [0] = "Easy", [50] = "Norm.", [100] = "MAW", [150] = "Hard", [200] = "Hell", [250] = "Night\nmare",
    [300] = "Night\nmare", [600] = "Doom",
}

local difficultyByName = {easy = 0, normal = 50, maw = 100, hard = 150, hell = 200, nightmare = 250}

local function configuredDifficulty()
    local name = tostring(ModSettings.difficulty or "MAW"):lower()
    local value = difficultyByName[name]

    if value == nil then
        print("MenuExtraSettings: unknown difficulty '" .. name .. "' in mods.yml, using MAW")
        value = 100
    end

    return value
end

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
