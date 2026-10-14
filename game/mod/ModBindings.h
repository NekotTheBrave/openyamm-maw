#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

struct lua_State;

namespace OpenYAMM::Game
{
class Party;
struct Character;
struct InventoryItem;
struct ModHost;

// The character's index as mods see it (GetIndex); assigned on first use. -1 when none is free.
int64_t ensureModCharacterIndex(Party &party, Character &character);
// The party slot of a character the engine handed over by reference: the member itself or a copy of one.
std::optional<size_t> findModPartySlot(const Party &party, const Character &character);

// The item an event is about, when it is not one a party member holds (a shop's, a chest's, the one in hand):
// scripts reach it as the item of group "event". Returns the item that was set before, to be put back.
InventoryItem *setModEventItem(InventoryItem *pItem);

// Skill names in Merge's numbering, 0..MergeSkillCount-1.
constexpr size_t MergeSkillCount = 39;
const char *mergeSkillName(size_t skillId);

// Adds the game-state natives to the `modrt` table. The host must outlive the Lua state.
void registerModBindings(lua_State *pLuaState, const ModHost &host);
}
