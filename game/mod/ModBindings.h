#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace OpenYAMM::Game
{
class Party;
struct Character;
struct ModHost;

// The character's index as mods see it (GetIndex); assigned on first use. -1 when none is free.
int64_t ensureModCharacterIndex(Party &party, Character &character);

// Skill names in Merge's numbering, 0..MergeSkillCount-1.
constexpr size_t MergeSkillCount = 39;
const char *mergeSkillName(size_t skillId);

// Adds the game-state natives to the `modrt` table. The host must outlive the Lua state.
void registerModBindings(lua_State *pLuaState, const ModHost &host);
}
