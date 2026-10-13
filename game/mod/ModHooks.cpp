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
