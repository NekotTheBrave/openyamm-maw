-- OpenYAMM version of MAW's buff snapshot (MawCore/Engine.lua: snapshotPlayerBuffs, restorePlayerBuffs).
--
-- MAW's Endless Arena keeps the party's buffs across the move to the arena: it copies each character's buff records
-- before (startGeneratingWaves) and puts them back once the arena is loaded. MAW copies the original's memory
-- (mem.string, mem.copy), which the port does not have; this copies the buffs' fields instead.

local Fields = {"ExpireTime", "Power", "Skill", "Caster", "OverlayId", "Bits"}

local function snapshotPlayerBuffs()
    local snapshot = {}

    for _, pl in Party do
        local buffs = {}

        for i = 0, pl.SpellBuffs.High do
            local buff = pl.SpellBuffs[i]
            local saved = {}

            for _, field in ipairs(Fields) do
                saved[field] = buff[field]
            end

            buffs[i] = saved
        end

        snapshot[pl:GetIndex()] = buffs
    end

    return snapshot
end

local function restorePlayerBuffs(snapshot)
    for _, pl in Party do
        local buffs = snapshot[pl:GetIndex()]

        if buffs then
            for i, saved in pairs(buffs) do
                local buff = pl.SpellBuffs[i]

                for _, field in ipairs(Fields) do
                    if saved[field] ~= nil then
                        buff[field] = saved[field]
                    end
                end
            end
        end
    end
end

local function install()
    if MawCore and MawCore.Engine then
        MawCore.Engine.snapshotPlayerBuffs = snapshotPlayerBuffs
        MawCore.Engine.restorePlayerBuffs = restorePlayerBuffs
    end
end

function events.GameInitialized1()
    install()
end

function events.GameInitialized2()
    install()
end
