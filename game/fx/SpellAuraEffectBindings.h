#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

namespace OpenYAMM::Game
{
class EffectLibrary;

// Where an enhanced spell aura plays.
enum class SpellAuraKind
{
    // A party spell's debuff landing on a creature; it follows the creature.
    CreatureDebuff,
    // A creature's buff or heal on itself; it follows the creature.
    CreatureBuff,
    // A buff the party casts on itself, played just in front of the party.
    PartyBuff,
    // A heal or cure the party casts on itself, played just in front of the party.
    Heal,
};

// The named effects that replace the procedural aura particles of buffs, heals and debuffs in the enhanced
// projectile style: each kind has a default, and spells may name their own.
class SpellAuraEffectBindings
{
public:
    // The spell's own effect for this kind, else the kind's default; empty when neither is bound.
    const std::string &find(SpellAuraKind kind, uint32_t spellId) const;
    bool empty() const;
    // Every effect id the bindings name, once each.
    std::vector<std::string> effectIds() const;

    void setDefault(SpellAuraKind kind, std::string effectId);
    void setSpell(SpellAuraKind kind, uint32_t spellId, std::string effectId);

private:
    static constexpr size_t KindCount = 4;
    std::array<std::string, KindCount> m_defaults;
    std::array<std::unordered_map<uint32_t, std::string>, KindCount> m_spells;
};

// Reads engine/effects/spell_auras.yml (schema openyamm.spellAuraEffects.v1): a defaults map and a spells map from
// spell id to a map, both keyed by creature_debuff, creature_buff, party_buff and heal. A missing file binds nothing;
// an unknown key, spell id or effect id fails the load.
bool loadSpellAuraEffectBindings(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &path,
    const EffectLibrary &library,
    SpellAuraEffectBindings &bindings,
    std::string &error);
// The same from YAML text; path names the source in errors.
bool parseSpellAuraEffectBindings(
    const std::string &text,
    const std::string &path,
    const EffectLibrary &library,
    SpellAuraEffectBindings &bindings,
    std::string &error);
}
