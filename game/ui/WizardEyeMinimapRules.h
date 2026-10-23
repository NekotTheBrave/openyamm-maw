#pragma once

namespace OpenYAMM::Game
{
// A corpse is marked only while it still holds loot; an empty one sinks away and leaves nothing to find.
inline bool wizardEyeShowsActorMarker(bool isDead, bool corpseIsEmpty, bool hasDetectedParty, bool hostileToParty)
{
    if (isDead)
    {
        return !corpseIsEmpty;
    }

    return hasDetectedParty || !hostileToParty;
}
}
