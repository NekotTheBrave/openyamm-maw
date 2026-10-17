#include "game/gameplay/PartyHostileAlert.h"

#include <algorithm>
#include <cmath>

namespace OpenYAMM::Game
{
bool actorRaisesPartyYellowAlert(
    const GameplayRuntimeActorState &actor,
    float partyX,
    float partyY,
    float partyFootZ)
{
    if (actor.isDead || actor.isInvisible || !actor.hostileToParty || !actor.hasDetectedParty)
    {
        return false;
    }

    const float deltaX = actor.preciseX - partyX;
    const float deltaY = actor.preciseY - partyY;
    const float deltaZ = actor.preciseZ - partyFootZ;
    const float centerDistance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    const float edgeDistance = std::max(0.0f, centerDistance - static_cast<float>(actor.radius));
    return edgeDistance < PartyYellowAlertDistance;
}

bool partyHasYellowAlert(const IGameplayWorldRuntime &worldRuntime)
{
    const float partyX = worldRuntime.partyX();
    const float partyY = worldRuntime.partyY();
    const float partyFootZ = worldRuntime.partyFootZ();

    for (size_t actorIndex = 0; actorIndex < worldRuntime.mapActorCount(); ++actorIndex)
    {
        GameplayRuntimeActorState actor = {};

        if (worldRuntime.actorRuntimeState(actorIndex, actor)
            && actorRaisesPartyYellowAlert(actor, partyX, partyY, partyFootZ))
        {
            return true;
        }
    }

    return false;
}
}
