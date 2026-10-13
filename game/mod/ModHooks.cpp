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
}
