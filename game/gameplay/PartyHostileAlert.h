#pragma once

#include "game/gameplay/GameplayRuntimeInterfaces.h"

namespace OpenYAMM::Game
{
// Original yellow alert range: a hostile actor this close (edge distance) blocks Town Portal, Invisibility and
// leaving an indoor map.
constexpr float PartyYellowAlertDistance = 5120.0f;

bool actorRaisesPartyYellowAlert(
    const GameplayRuntimeActorState &actor,
    float partyX,
    float partyY,
    float partyFootZ);
bool partyHasYellowAlert(const IGameplayWorldRuntime &worldRuntime);
}
