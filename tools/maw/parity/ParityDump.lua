-- ParityDump.lua -- writes what MAW computes in the real game to a text file, for comparison with the OpenYAMM port.
--
-- Not part of MAW and not used by OpenYAMM. To use it, copy this file into the real game's Scripts\General folder
-- as zzzzzz_ParityDump.lua and start the game. While a game is in progress it rewrites ParityDump.txt in the game
-- folder every two seconds: the difficulty settings, every party member's numbers, the monsters nearest the party
-- and the last hits in both directions. It changes nothing in the game. To remove it, delete the file.
--
-- MMExtension runs LuaJIT (Lua 5.1), so this file keeps to Lua 5.1.

local INTERVAL = 2          -- seconds between dumps
local NEAR_MONSTERS = 16    -- how many of the nearest living monsters to list
local LOG_LIMIT = 80        -- how many hits to keep

local lastDump = -100
local hits = {}             -- finished records, oldest first
local pending = {}          -- hits seen this tick, completed at the next one

local function try(f, ...)
	local ok, a, b, c, d = pcall(f, ...)
	if ok then
		return a, b, c, d
	end
	return nil
end

local function text(value)
	if type(value) == "number" then
		if value == math.floor(value) then
			return string.format("%d", value)
		end
		return string.format("%.3f", value)
	end
	return tostring(value)
end

local function monsterName(mon)
	local name = try(function() return Game.MonstersTxt[mon.Id].Name end) or "?"
	local nameId = try(function() return mon.NameId end) or 0
	if nameId ~= 0 then
		local unique = try(function() return Game.PlaceMonTxt[nameId] end)
		if unique and unique ~= "" then
			name = unique .. " [" .. name .. "]"
		end
	end
	return name
end

local function monsterHP(mon)
	local current = try(function() return MawCore.MonsterHP.current(mon) end)
	local max = try(function() return MawCore.MonsterHP.max(mon) end)
	return current or mon.HP, max or mon.FullHP
end

local function addHit(record)
	hits[#hits + 1] = record
	while #hits > LOG_LIMIT do
		table.remove(hits, 1)
	end
end

-- Hits are noted when the event fires and completed a tick later, from the hit points actually lost, so that it does
-- not matter whether MAW's own handlers ran before or after this one.
function events.CalcDamageToMonster(t)
	pcall(function()
		local data = WhoHitMonster()
		if not (data and data.Player and t.Monster) then
			return
		end
		-- The engine asks twice for one blow of a weapon (its damage, then its elemental part): one record for both.
		local last = pending[#pending]
		if last and last.kind == "party->monster" and last.time == Game.Time and last.monster == t.MonsterIndex
			and last.attacker == data.Player.Name then
			last.damageKind = tostring(last.damageKind) .. "+" .. tostring(t.DamageKind)
			last.raw = (last.raw or 0) + (t.Damage or 0)
			return
		end
		local hp = monsterHP(t.Monster)
		pending[#pending + 1] = {
			kind = "party->monster", time = Game.Time, monster = t.MonsterIndex, name = monsterName(t.Monster),
			attacker = data.Player.Name, object = data.Object and (data.Object.Spell or 0) or "blow",
			damageKind = t.DamageKind, raw = t.Damage, hpBefore = hp,
		}
	end)
end

function events.CalcDamageToPlayer(t)
	pcall(function()
		local data = WhoHitPlayer()
		local record = {
			kind = "monster->party", time = Game.Time, target = t.Player.Name, playerIndex = t.PlayerIndex,
			damageKind = t.DamageKind, raw = t.Damage, hpBefore = t.Player.HP,
			name = "(no attacker)", object = "blow",
		}
		if data and data.Monster then
			record.name = monsterName(data.Monster)
			record.action = data.MonsterAction
			record.object = data.Object and "missile" or "blow"
		end
		pending[#pending + 1] = record
	end)
end

local function completePending()
	for i = 1, #pending do
		local record = pending[i]
		pcall(function()
			if record.kind == "party->monster" then
				local hp = monsterHP(Map.Monsters[record.monster])
				record.applied = record.hpBefore - hp
			else
				for slot = 0, Party.High do
					if Party[slot]:GetIndex() == record.playerIndex then
						record.applied = record.hpBefore - Party[slot].HP
					end
				end
			end
		end)
		addHit(record)
	end
	pending = {}
end

local skillNames = {
	[0] = "Staff", "Sword", "Dagger", "Axe", "Spear", "Bow", "Mace", "Blaster", "Shield", "Leather", "Chain", "Plate",
	"Fire", "Air", "Water", "Earth", "Spirit", "Mind", "Body", "Light", "Dark", "DarkElfAbility", "VampireAbility",
	"DragonAbility", "IdentifyItem", "Merchant", "Repair", "Bodybuilding", "Meditation", "Perception", "Regeneration",
	"DisarmTraps", "Dodging", "Unarmed", "IdentifyMonster", "Armsmaster", "Stealing", "Alchemy", "Learning",
}

local function dumpPlayer(out, slot)
	local pl = Party[slot]
	local index = pl:GetIndex()
	local function line(s) out[#out + 1] = s end

	line(string.format("PLAYER slot=%d index=%d name=%s class=%d face=%d race=%s level=%d/%d",
		slot, index, pl.Name, pl.Class, pl.Face, text(try(function() return Game.CharacterPortraits[pl.Face].Race end)),
		pl.LevelBase, pl.LevelBase + pl.LevelBonus))
	line(string.format("  stats base: might=%d intellect=%d personality=%d endurance=%d accuracy=%d speed=%d luck=%d",
		pl.MightBase, pl.IntellectBase, pl.PersonalityBase, pl.EnduranceBase, pl.AccuracyBase, pl.SpeedBase, pl.LuckBase))
	line(string.format("  stats full: might=%d intellect=%d personality=%d endurance=%d accuracy=%d speed=%d luck=%d",
		pl:GetMight(), pl:GetIntellect(), pl:GetPersonality(), pl:GetEndurance(), pl:GetAccuracy(), pl:GetSpeed(),
		pl:GetLuck()))
	line(string.format("  hp=%d/%d sp=%d/%d ac=%d", pl.HP, pl:GetFullHP(), pl.SP, pl:GetFullSP(), pl:GetArmorClass()))
	line(string.format("  melee: attack=%d damage=%d-%d delay=%d   ranged: attack=%d damage=%d-%d delay=%d",
		pl:GetMeleeAttack(), pl:GetMeleeDamageMin(), pl:GetMeleeDamageMax(), pl:GetAttackDelay(),
		pl:GetRangedAttack(), pl:GetRangedDamageMin(), pl:GetRangedDamageMax(), pl:GetAttackDelay(true)))

	local resistances = {}
	for stat = 10, 15 do
		resistances[#resistances + 1] = text(try(function() return pl:GetResistance(stat) end))
	end
	line("  resistances fire,air,water,earth,mind,body: " .. table.concat(resistances, ","))

	local skills = {}
	for id = 0, 38 do
		local raw = pl.Skills[id]
		if raw ~= 0 then
			local level, mastery = SplitSkill(raw)
			local buffedLevel = SplitSkill(pl:GetSkill(id))
			skills[#skills + 1] = string.format("%s %d/%d(+%d)", skillNames[id] or id, level, mastery, buffedLevel - level)
		end
	end
	line("  skills level/mastery(+bonus): " .. table.concat(skills, ", "))

	for item, itemSlot in pl:EnumActiveItems() do
		line(string.format("  equipped slot=%d number=%d name=%s bonus=%d strength=%d bonus2=%d charges=%d/%d value=%s",
			itemSlot, item.Number, Game.ItemsTxt[item.Number].Name, item.Bonus, item.BonusStrength, item.Bonus2,
			item.Charges, item.MaxCharges, text(try(function() return getItemValue(item) end))))
	end

	local stats = plItemsStats and plItemsStats[index]
	if stats then
		local parts = {}
		for k = 1, 120 do
			if stats[k] and stats[k] ~= 0 then
				parts[#parts + 1] = k .. ":" .. text(stats[k])
			end
		end
		line("  plItemsStats: " .. table.concat(parts, " "))
	end

	local multiplier = damageMultiplier and damageMultiplier[index]
	if multiplier then
		local parts = {}
		for k, v in pairs(multiplier) do
			parts[#parts + 1] = k .. "=" .. text(v)
		end
		table.sort(parts)
		line("  damageMultiplier: " .. table.concat(parts, " "))
	end

	local melee, ranged, spell, vitality = try(calcPowerVitality, pl, true)
	line(string.format("  MAW power: melee=%s ranged=%s spell=%s vitality=%s", text(melee), text(ranged), text(spell),
		text(vitality)))

	-- Chance and multiplier of a critical hit with the weapon in hand, against monsters of level 2, 4 and 50.
	local crits = {}
	for _, level in ipairs{2, 4, 50} do
		local chance, multiplier = try(getCritInfo, pl, false, level)
		crits[#crits + 1] = string.format("L%d chance=%s mult=%s", level, text(chance), text(multiplier))
	end
	line("  crit: " .. table.concat(crits, "  "))
end

local function dumpMonsters(out)
	local list = {}
	for i = 0, Map.Monsters.High do
		local mon = Map.Monsters[i]
		if mon.HP > 0 and mon.AIState ~= 5 and mon.AIState ~= 11 then
			local dx, dy, dz = mon.X - Party.X, mon.Y - Party.Y, mon.Z - Party.Z
			list[#list + 1] = {index = i, mon = mon, distance = math.sqrt(dx * dx + dy * dy + dz * dz)}
		end
	end
	table.sort(list, function(a, b) return a.distance < b.distance end)
	out[#out + 1] = string.format("MONSTERS alive=%d of %d (nearest %d listed)", #list, Map.Monsters.High + 1,
		math.min(#list, NEAR_MONSTERS))

	-- How many of each kind are alive, to compare what the map spawned.
	local byKind, kinds = {}, {}
	for _, entry in ipairs(list) do
		local id = entry.mon.Id
		if not byKind[id] then
			byKind[id] = 0
			kinds[#kinds + 1] = id
		end
		byKind[id] = byKind[id] + 1
	end
	table.sort(kinds)
	for n, id in ipairs(kinds) do
		kinds[n] = string.format("%d %s x%d", id, text(try(function() return Game.MonstersTxt[id].Name end)), byKind[id])
	end
	out[#out + 1] = "KINDS " .. table.concat(kinds, ", ")

	for n = 1, math.min(#list, NEAR_MONSTERS) do
		local entry = list[n]
		local mon = entry.mon
		local hp, maxHp = monsterHP(mon)
		local resistances = {}
		for _, kind in ipairs{0, 1, 2, 3, 4, 6, 7, 8, 9, 10} do
			resistances[#resistances + 1] = kind .. ":" .. text(try(function() return mon.Resistances[kind] end))
		end
		out[#out + 1] = string.format(
			"  #%d id=%d %s dist=%d level=%d mawLevel=%s hp=%s/%s (raw %d/%d) ac=%d exp=%d aiType=%s speed=%s velocity=%s aiState=%s hostile=%s",
			entry.index, mon.Id, monsterName(mon), entry.distance, mon.Level,
			text(try(function() return getMonsterLevel(mon) end)), text(hp), text(maxHp), mon.HP, mon.FullHP,
			mon.ArmorClass, mon.Experience, text(try(function() return mon.AIType end)),
			text(try(function() return mon.MoveSpeed end)), text(try(function() return mon.Velocity end)),
			text(try(function() return mon.AIState end)), text(try(function() return mon.Hostile end)))
		out[#out + 1] = string.format(
			"      attack1 type=%d %dd%d+%d missile=%d   attack2 chance=%d type=%d %dd%d+%d missile=%d   spell=%d chance=%d",
			mon.Attack1.Type, mon.Attack1.DamageDiceCount, mon.Attack1.DamageDiceSides, mon.Attack1.DamageAdd,
			mon.Attack1.Missile, mon.Attack2Chance, mon.Attack2.Type, mon.Attack2.DamageDiceCount,
			mon.Attack2.DamageDiceSides, mon.Attack2.DamageAdd, mon.Attack2.Missile, mon.Spell, mon.SpellChance)
		out[#out + 1] = "      resistances (const.Damage): " .. table.concat(resistances, " ")
	end
end

local function dump()
	local out = {}
	out[#out + 1] = "ParityDump  " .. os.date("%Y-%m-%d %H:%M:%S")
	out[#out + 1] = string.format("map=%s mapStatsIndex=%s gameTime=%s", tostring(Map.Name), text(Map.MapStatsIndex),
		text(Game.Time))
	out[#out + 1] = string.format("settings: BolsterAmount=%s freeProgression=%s Mode=%s UseMonsterBolster=%s",
		text(Game.BolsterAmount), text(Game.freeProgression), text(Game.Mode), text(Game.UseMonsterBolster))
	out[#out + 1] = string.format("vars: Mode=%s insanityMode=%s madnessMode=%s AusterityMode=%s RandomizerMode=%s",
		text(vars.Mode), text(vars.insanityMode), text(vars.madnessMode), text(vars.AusterityMode),
		text(vars.RandomizerMode))
	out[#out + 1] = string.format("party: X=%d Y=%d Z=%d gold=%d", Party.X, Party.Y, Party.Z, Party.Gold)
	out[#out + 1] = ""

	for slot = 0, Party.High do
		local ok, err = pcall(dumpPlayer, out, slot)
		if not ok then
			out[#out + 1] = "  (player " .. slot .. " failed: " .. tostring(err) .. ")"
		end
		out[#out + 1] = ""
	end

	local ok, err = pcall(dumpMonsters, out)
	if not ok then
		out[#out + 1] = "(monsters failed: " .. tostring(err) .. ")"
	end
	out[#out + 1] = ""

	out[#out + 1] = string.format("HITS (last %d, oldest first; applied = hit points actually lost)", #hits)
	for i = 1, #hits do
		local h = hits[i]
		if h.kind == "party->monster" then
			out[#out + 1] = string.format("  t=%s %s -> #%s %s  by=%s kind=%s raw=%s applied=%s (hp before %s)",
				text(h.time), tostring(h.attacker), text(h.monster), tostring(h.name), text(h.object), text(h.damageKind),
				text(h.raw), text(h.applied), text(h.hpBefore))
		else
			out[#out + 1] = string.format("  t=%s %s -> %s  by=%s action=%s kind=%s raw=%s applied=%s (hp before %s)",
				text(h.time), tostring(h.name), tostring(h.target), text(h.object), text(h.action), text(h.damageKind),
				text(h.raw), text(h.applied), text(h.hpBefore))
		end
	end

	local file = io.open(AppPath .. "ParityDump.txt", "w")
	if file then
		file:write(table.concat(out, "\n"), "\n")
		file:close()
	end
end

function events.Tick()
	if #pending > 0 then
		pcall(completePending)
	end
	local now = os.clock()
	if now - lastDump >= INTERVAL then
		lastDump = now
		pcall(dump)
	end
end
