#include "game/mod/ModHooks.h"

namespace OpenYAMM::Game
{
namespace
{
// One mod runtime per process; gameplay code reaches it from many layers that share no owner object.
IModHooks *g_pActiveModHooks = nullptr;
}

IModHooks *activeModHooks()
{
    return g_pActiveModHooks;
}

void setActiveModHooks(IModHooks *pModHooks)
{
    g_pActiveModHooks = pModHooks;
}

int mm8DamageKind(CombatDamageType damageType)
{
    switch (damageType)
    {
        case CombatDamageType::Fire: return 0;
        case CombatDamageType::Air: return 1;
        case CombatDamageType::Water: return 2;
        case CombatDamageType::Earth: return 3;
        case CombatDamageType::Physical: return 4;
        case CombatDamageType::Spirit: return 6;
        case CombatDamageType::Mind: return 7;
        case CombatDamageType::Body: return 8;
        case CombatDamageType::Light: return 9;
        case CombatDamageType::Dark: return 10;
        case CombatDamageType::Energy: return 12;
        case CombatDamageType::Irresistible: return 12;
    }

    return 4;
}

int modDamageToPlayer(const ModDamageToPlayerEvent &event)
{
    return g_pActiveModHooks != nullptr ? g_pActiveModHooks->calcDamageToPlayer(event) : event.resolvedDamage;
}

uint32_t modKillExperience(size_t actorIndex, uint32_t engineExperience)
{
    return g_pActiveModHooks != nullptr
        ? g_pActiveModHooks->monsterKillExperience(actorIndex, engineExperience)
        : engineExperience;
}

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
    float objectZ)
{
    if (g_pActiveModHooks == nullptr)
    {
        return resolvedDamage;
    }

    ModDamageToMonsterEvent event = {};
    event.actorIndex = actorIndex;
    event.partyMemberIndex = partyMemberIndex;
    event.rawDamage = rawDamage;
    event.resolvedDamage = resolvedDamage;
    event.damageType = damageType;
    event.spellId = spellId;
    event.melee = false;
    event.fromObject = true;
    event.skillLevel = skillLevel;
    event.skillMastery = skillMastery;
    event.objectX = objectX;
    event.objectY = objectY;
    event.objectZ = objectZ;
    return g_pActiveModHooks->calcDamageToMonster(event);
}
}
