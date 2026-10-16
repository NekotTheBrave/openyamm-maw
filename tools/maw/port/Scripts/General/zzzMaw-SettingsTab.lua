-- OpenYAMM addition to MAW: MAW's options on a MAW tab of the game's settings window (ModRT.SettingsOptions).
--
-- MAW has two kinds of option. Its settings page (zz_Maw-initialize.lua, opened from the Extra Settings page in the
-- original, which OpenYAMM does not have) holds switches that belong to the game being played (vars.MAWSETTINGS);
-- M&M6 projectiles and homing projectiles are OpenYAMM's own options (Gameplay > Challenge), so the other four are
-- here, and can be changed in a game only. Its settings file (MAWSettings.lua, in mods/maw/files) holds the rest,
-- for every game; those shown here are written back to the file, keeping the rest of it as it is. Not here: its
-- melee-only challenge (disableBow: MAW stops the arrow in events.ArrowProjectile, which OpenYAMM does not raise),
-- its movement options (faster strafing, slower backpedalling: patches of the original's code, which do nothing in
-- OpenYAMM), and its keys (OpenYAMM's key bindings' business, zz_Maw-KeyClashes.lua).

local SettingsPath = "MAWSettings.lua"
local LootFilters = {"OFF", "Common", "Uncom.", "Rare", "Epic", "Ancient", "Primordial"}

local function inGame()
	return Map ~= nil and Map.Name ~= nil and modrt.gameGet("MapReady") == true
end

-- MAW's settings page copies its own copy of the switches into vars.MAWSETTINGS whenever a dialog is left (Action
-- 113) and takes them back from vars on LoadMap; what is changed here is put back after it until then.
local changed = {}

local function pageValue(key, default)
	local settings = vars and vars.MAWSETTINGS
	local value = settings and settings[key]

	if value == nil then
		return default
	end

	return value
end

local function setPageValue(key, value)
	if not inGame() then
		return
	end

	vars.MAWSETTINGS = vars.MAWSETTINGS or {}
	local before = vars.MAWSETTINGS[key]
	vars.MAWSETTINGS[key] = value
	changed[key] = value

	if key == "buffRework" and before ~= value and type(adjustSpellTooltips) == "function" then
		adjustSpellTooltips()
	end

	if key == "extraItemTooltip" and type(UpdateExtraDescription) == "function" then
		UpdateExtraDescription()
	end
end

local function pageSwitch(name, key, default, description)
	return {
		Name = name, Group = "This game", Description = description, Enabled = inGame,
		Get = function()
			local value = pageValue(key, default)
			return value == "ON" or value == true
		end,
		Set = function(on)
			setPageValue(key, on and "ON" or "OFF")
		end,
	}
end

-- Writes `key = value` into MAW's settings file: in place where it is there, else before the table's last brace.
local function writeSetting(key, value)
	local file = io.open(SettingsPath, "r")

	if not file then
		print("MAW settings tab: " .. SettingsPath .. " not found")
		return false
	end

	local text = file:read("*a")
	file:close()
	local replaced
	text, replaced = text:gsub("(\n[ \t]*" .. key .. "[ \t]*=[ \t]*)[%w_]+", "%1" .. tostring(value), 1)

	if replaced == 0 then
		local position = text:match(".*()}")

		if not position then
			print("MAW settings tab: " .. SettingsPath .. " has no settings table")
			return false
		end

		text = text:sub(1, position - 1) .. "    " .. key .. " = " .. tostring(value) .. ",\n" .. text:sub(position)
	end

	file = io.open(SettingsPath, "w")

	if not file then
		print("MAW settings tab: cannot write " .. SettingsPath)
		return false
	end

	file:write(text)
	file:close()
	return true
end

-- An option of MAW's settings file, a global of the same name; `inverted` shows MAW's "disable..." as its opposite.
local function fileSwitch(group, name, key, default, description, inverted)
	return {
		Name = name, Group = group, Description = description,
		Get = function()
			local value = _G[key]

			if value == nil then
				value = default
			end

			if inverted then
				return not value
			end

			return value == true
		end,
		Set = function(on)
			local value = on

			if inverted then
				value = not on
			end

			_G[key] = value
			writeSetting(key, value)
		end,
	}
end

ModRT.SettingsTitle = "MAW"
ModRT.SettingsOptions = {
	pageSwitch("Buff rework", "buffRework", "ON", "Buffs reserve part of the caster's mana and last until switched off."),
	pageSwitch("Damage on friendly units", "friendlyDamage", "OFF", "The party's attacks hurt monsters that are not hostile."),
	{
		Name = "Loot filter", Group = "This game", Choices = LootFilters, Enabled = inGame,
		Description = "Items found up to this rarity turn into their worth in gold.",
		Get = function()
			return tostring(pageValue("lootFilter", "OFF"))
		end,
		Set = function(value)
			setPageValue("lootFilter", value)
		end,
	},
	pageSwitch("Extra item tooltip", "extraItemTooltip", "OFF", "Items always show their extra details (else hold Alt)."),
	fileSwitch("Combat", "Auto-target heals", "autoTargetHeals", true,
		"Healing spells go to the character with the lowest share of hit points."),
	fileSwitch("Combat", "Disintegration", "enableDisintegrate", true,
		"Road to Insanity: a character taking far too much damage is gone for good."),
	fileSwitch("Interface", "Death counter", "showDeathCounter", true, "Beyond Madness: the deaths so far are shown."),
	fileSwitch("Interface", "Completion messages", "disableCompletitionMessage", false,
		"A message when a map's monsters are all dead.", true),
	fileSwitch("Interface", "Coloured stats", "ColouredStats", true,
		"The character screen's stats in colour. From the next start of the game."),
	fileSwitch("Interface", "New spellbook look", "disableSpellBookRework", false,
		"MAW's spellbook pictures. From the next start of the game.", true),
	fileSwitch("World", "Trainers on all continents", "enableAllTrainers", true,
		"Trainers missing from a continent are found there too. From the next start."),
}

function events.LoadMap()
	changed = {}
end

function events.GameInitialized2()
	-- After MAW's own Action handlers, registered as its scripts loaded.
	function events.Action(t)
		if t.Action == 113 and vars and vars.MAWSETTINGS then
			for key, value in pairs(changed) do
				vars.MAWSETTINGS[key] = value
			end
		end
	end
end
