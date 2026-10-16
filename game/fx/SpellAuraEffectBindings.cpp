#include "game/fx/SpellAuraEffectBindings.h"

#include "engine/AssetFileSystem.h"
#include "game/fx/EffectSystem.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <optional>

namespace OpenYAMM::Game
{
namespace
{
std::optional<SpellAuraKind> auraKindFromName(const std::string &name)
{
    if (name == "creature_debuff")
    {
        return SpellAuraKind::CreatureDebuff;
    }
    if (name == "creature_buff")
    {
        return SpellAuraKind::CreatureBuff;
    }
    if (name == "party_buff")
    {
        return SpellAuraKind::PartyBuff;
    }
    if (name == "heal")
    {
        return SpellAuraKind::Heal;
    }
    return std::nullopt;
}

// Reads a {kind: effect id} map, calling store for each entry.
template <typename Store>
bool readKindMap(
    const YAML::Node &node,
    const EffectLibrary &library,
    const std::string &context,
    Store store,
    std::string &error)
{
    if (!node.IsMap())
    {
        error = context + ": expected a map of kind to effect id";
        return false;
    }
    for (const auto &entry : node)
    {
        const std::string kindName = entry.first.as<std::string>();
        const std::optional<SpellAuraKind> kind = auraKindFromName(kindName);
        if (!kind)
        {
            error = context + ": unknown aura kind " + kindName;
            return false;
        }
        const std::string effectId = entry.second.as<std::string>();
        if (library.find(effectId) == nullptr)
        {
            error = context + ": unknown " + kindName + " effect " + effectId;
            return false;
        }
        store(*kind, effectId);
    }
    return true;
}
}

const std::string &SpellAuraEffectBindings::find(SpellAuraKind kind, uint32_t spellId) const
{
    const size_t index = static_cast<size_t>(kind);
    const auto it = m_spells[index].find(spellId);
    return it != m_spells[index].end() ? it->second : m_defaults[index];
}

bool SpellAuraEffectBindings::empty() const
{
    return std::all_of(m_defaults.begin(), m_defaults.end(), [](const std::string &id) { return id.empty(); }) &&
        std::all_of(m_spells.begin(), m_spells.end(), [](const auto &spells) { return spells.empty(); });
}

std::vector<std::string> SpellAuraEffectBindings::effectIds() const
{
    std::vector<std::string> ids;
    for (size_t index = 0; index < KindCount; ++index)
    {
        if (!m_defaults[index].empty())
        {
            ids.push_back(m_defaults[index]);
        }
        for (const auto &[spellId, effectId] : m_spells[index])
        {
            ids.push_back(effectId);
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

void SpellAuraEffectBindings::setDefault(SpellAuraKind kind, std::string effectId)
{
    m_defaults[static_cast<size_t>(kind)] = std::move(effectId);
}

void SpellAuraEffectBindings::setSpell(SpellAuraKind kind, uint32_t spellId, std::string effectId)
{
    m_spells[static_cast<size_t>(kind)][spellId] = std::move(effectId);
}

bool loadSpellAuraEffectBindings(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &path,
    const EffectLibrary &library,
    SpellAuraEffectBindings &bindings,
    std::string &error)
{
    if (!assetFileSystem.exists(path))
    {
        return true;
    }
    const std::optional<std::string> text = assetFileSystem.readTextFile(path);
    if (!text)
    {
        error = path + ": unreadable";
        return false;
    }
    return parseSpellAuraEffectBindings(*text, path, library, bindings, error);
}

bool parseSpellAuraEffectBindings(
    const std::string &text,
    const std::string &path,
    const EffectLibrary &library,
    SpellAuraEffectBindings &bindings,
    std::string &error)
{
    try
    {
        const YAML::Node root = YAML::Load(text);
        if (!root["schema"] || root["schema"].as<std::string>() != "openyamm.spellAuraEffects.v1")
        {
            error = path + ": expected schema openyamm.spellAuraEffects.v1";
            return false;
        }
        for (const auto &entry : root)
        {
            const std::string key = entry.first.as<std::string>();
            if (key != "schema" && key != "defaults" && key != "spells")
            {
                error = path + ": unknown key " + key;
                return false;
            }
        }
        if (const YAML::Node defaults = root["defaults"])
        {
            if (!readKindMap(defaults, library, path + ": defaults",
                    [&](SpellAuraKind kind, const std::string &effectId) { bindings.setDefault(kind, effectId); },
                    error))
            {
                return false;
            }
        }
        if (const YAML::Node spells = root["spells"])
        {
            if (!spells.IsMap())
            {
                error = path + ": spells must map spell ids to aura kinds";
                return false;
            }
            for (const auto &entry : spells)
            {
                const int spellId = entry.first.as<int>();
                if (spellId <= 0)
                {
                    error = path + ": invalid spell id " + entry.first.as<std::string>();
                    return false;
                }
                if (!readKindMap(entry.second, library, path + ": spell " + std::to_string(spellId),
                        [&](SpellAuraKind kind, const std::string &effectId)
                        {
                            bindings.setSpell(kind, static_cast<uint32_t>(spellId), effectId);
                        },
                        error))
                {
                    return false;
                }
            }
        }
    }
    catch (const YAML::Exception &exception)
    {
        error = path + ": " + exception.what();
        return false;
    }
    return true;
}
}
