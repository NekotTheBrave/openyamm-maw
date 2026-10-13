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

// A monster's attack is about to be resolved against a party member, before any roll. The mod may pick another
// member (`partyMemberIndex`) or take the attack over altogether (`handled`: the engine then does nothing more).
struct ModPlayerAttackedEvent
{
    size_t partyMemberIndex = 0;
    std::optional<size_t> actorIndex;
    int attackAction = 0;
    uint32_t spellId = 0;
    bool fromObject = false;
    bool handled = false;
};

// A party member's spell is about to take effect: targets are chosen and the cost is known. The mod may change
// the skill the spell is cast with, or take the cast over (`handled`: no effect, no cost, no recovery).
struct ModPlayerCastSpellEvent
{
    size_t partyMemberIndex = 0;
    uint32_t spellId = 0;
    int manaCost = 0;
    uint32_t skillLevel = 0;
    uint32_t skillMastery = 0;
    std::optional<size_t> targetActorIndex;
    std::optional<size_t> targetPartyMemberIndex;
    bool fromScroll = false;
    bool handled = false;
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

    // The experience the party gets for a monster it has just killed; `engineExperience` is the engine's answer
    // from the monster table. Mods give single monsters an experience value of their own.
    virtual uint32_t monsterKillExperience(size_t actorIndex, uint32_t engineExperience) = 0;

    virtual void playerAttacked(ModPlayerAttackedEvent &event) = 0;
    virtual void playerCastSpell(ModPlayerCastSpellEvent &event) = 0;

    // A monster's special attack (curse, poison, breaking an item, ...) is about to be tried on a party member,
    // before the member's saving throw. `thing` is the original engine's number for it (const.MonsterBonus).
    // Returns false to forbid it.
    virtual bool allowBadThingToPlayer(size_t partyMemberIndex, std::optional<size_t> actorIndex, int thing) = 0;

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

// const.Damage: the original engine's number for a kind of damage, which is how mods name it.
int mm8DamageKind(CombatDamageType damageType);

// Runs the mod's event for damage to a party member and returns the damage to apply, which is
// event.resolvedDamage when no mod runtime is present.
int modDamageToPlayer(const ModDamageToPlayerEvent &event);

// The experience to grant for a kill: the mod's answer, or `engineExperience` when no mod runtime is present.
uint32_t modKillExperience(size_t actorIndex, uint32_t engineExperience);

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
