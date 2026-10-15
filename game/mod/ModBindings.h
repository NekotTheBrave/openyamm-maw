#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
// The item the event under way is about, nullptr when there is none.
InventoryItem *modEventItem();
// Takes out of the backpacks the items scripts used up (an item's Number set to 0: MAW's quick potions), which stay
// in place, as empty records, until the script call is over so that the items after them keep their numbers.
void removeUsedUpModItems();

// Skill names in Merge's numbering, 0..MergeSkillCount-1.
constexpr size_t MergeSkillCount = 39;
const char *mergeSkillName(size_t skillId);
// Skills a mod adds take numbers from MergeSkillCount up (MAW's 50 to 53): the number, the name the engine keeps the
// skill under, and its group on the skill page.
void registerMergeSkill(size_t skillId, const std::string &skillName);
// Every skill number with a name, the 39 first.
std::vector<size_t> mergeSkillIds();

// Adds the game-state natives to the `modrt` table. The host must outlive the Lua state.
void registerModBindings(lua_State *pLuaState, const ModHost &host);
}
