#pragma once

#include "game/gameplay/GameMechanics.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace OpenYAMM::Game
{
// Points in gameplay where a mod may change the outcome. Gameplay code asks activeModHooks() and, when a mod runtime
// is present, lets it adjust the value; with no runtime the pointer is null and gameplay is untouched.

// The party hits a map actor. `rawDamage` is the attack's damage before the target's resistances, `resolvedDamage`
// what the engine would apply after them; the hook returns the damage to apply.
struct ModDamageToMonsterEvent
{
    size_t actorIndex = 0;
    std::optional<size_t> partyMemberIndex;
    int rawDamage = 0;
    int resolvedDamage = 0;
    CombatDamageType damageType = CombatDamageType::Physical;
    uint32_t spellId = 0;
    bool melee = true;
    // Set when the hit comes from something in the world (an arrow, a spell missile, a spell's effect) rather than
    // from a blow: where that is, and for a spell the skill it was cast with.
    bool fromObject = false;
    uint32_t skillLevel = 0;
    uint32_t skillMastery = 0;
    float objectX = 0.0f;
    float objectY = 0.0f;
    float objectZ = 0.0f;
};

// A party member takes damage. `rawDamage` is the amount before the character's resistances, `resolvedDamage` what
// the engine would apply after them; the hook returns the damage to apply. The attacker is a map actor when there
// is one (a trap has none); `attackAction` says which of its attacks it was (0 and 1 its two attacks, 2 and 3 its
// two spells), and `fromObject` that it arrived as a missile rather than a blow.
struct ModDamageToPlayerEvent
{
    size_t partyMemberIndex = 0;
    int rawDamage = 0;
    int resolvedDamage = 0;
    CombatDamageType damageType = CombatDamageType::Physical;
    std::optional<size_t> actorIndex;
    int attackAction = 0;
    uint32_t spellId = 0;
    bool fromObject = false;
};

class Party;

class IModHooks
{
public:
    virtual ~IModHooks() = default;

    virtual int calcDamageToMonster(const ModDamageToMonsterEvent &event) = 0;
    virtual int calcDamageToPlayer(const ModDamageToPlayerEvent &event) = 0;

    // A map actor has just died, whatever killed it.
    virtual void monsterKilled(size_t actorIndex) = 0;

    // Called at the end of Party::rebuildMagicalBonusesFromBuffs for each member. At that point the member's
    // magicalBonuses holds buffs plus equipped items and itemSkillBonuses holds item skill bonuses; `buffBonuses`
    // is the buffs-only part. The mod may replace the item part, the magic part and the skill bonuses.
    virtual void adjustCharacterBonuses(
        Party &party,
        size_t memberIndex,
        const CharacterStatBonuses &buffBonuses) = 0;

    // The effect of a statistic's value on what it governs (the engine's threshold table: 13 gives 0, 15 gives 1,
    // ...). `engineEffect` is the table's answer; the hook returns the effect to use.
    virtual int statisticEffect(int value, int engineEffect) = 0;

    // Recovery after an attack, in the original engine's ticks (100 is an unarmed swing, 30 its floor for melee).
    // `ranged` is true for the bow slot and false for whatever the main hand does. `engineTicks` is the engine's
    // answer; the hook returns the ticks to use.
    virtual int attackDelay(const Character &character, bool ranged, int engineTicks) = 0;

    // The part of a statistic that comes from the character's skills: Bodybuilding in hit points, the armor
    // skills in armor class, the weapon skills in attack and damage. `statId` is the original engine's statistic
    // number (7 HP, 8 SP, 9 armor class, 25 melee attack, 26 melee damage, 29 ranged attack, 30 ranged damage).
    virtual int skillStatBonus(const Character &character, int statId, int engineBonus) = 0;
};

IModHooks *activeModHooks();
void setActiveModHooks(IModHooks *pModHooks);

// Runs the mod's event for damage to a party member and returns the damage to apply, which is
// event.resolvedDamage when no mod runtime is present.
int modDamageToPlayer(const ModDamageToPlayerEvent &event);

// The party's missile or spell hits a map actor: runs the mod's damage event and returns the damage to apply, which
// is `resolvedDamage` when no mod runtime is present. `spellId` is 0 for an arrow or a blaster shot.
int modObjectDamageToMonster(
    size_t actorIndex,
    std::optional<size_t> partyMemberIndex,
    int rawDamage,
    int resolvedDamage,
    CombatDamageType damageType,
    uint32_t spellId,
    uint32_t skillLevel,
    uint32_t skillMastery,
    float objectX,
    float objectY,
    float objectZ);
}
