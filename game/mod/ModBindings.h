#pragma once

struct lua_State;

namespace OpenYAMM::Game
{
struct ModHost;

// Adds the game-state natives to the `modrt` table. The host must outlive the Lua state.
void registerModBindings(lua_State *pLuaState, const ModHost &host);
}
