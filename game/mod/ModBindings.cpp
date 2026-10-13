#include "game/mod/ModBindings.h"

#include "game/data/GameDataLoader.h"
#include "game/gameplay/GameMechanics.h"
#include "game/gameplay/GameplayRuntimeInterfaces.h"
#include "game/mod/ModHost.h"
#include "game/party/Party.h"
#include "game/party/SkillData.h"
#include "game/tables/CharacterDollTable.h"
#include "game/tables/ClassSkillTable.h"
#include "game/tables/ItemTable.h"
#include "game/tables/MonsterTable.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>

// Natives behind the MMExtension-shaped objects that scripts/modrt/bindings.lua builds (Party, Party[i], item and
// monster tables, map monsters). Each native addresses one object by index and one field by name and returns nil for
// anything it does not know, so the Lua side can fall back to a recording stand-in and the report keeps listing
// what is still missing. Field names are MMExtension's; the mapping onto OpenYAMM state lives here and nowhere else.

namespace OpenYAMM::Game
{
namespace
{
const ModHost *hostFromUpvalue(lua_State *pLuaState)
{
    return static_cast<const ModHost *>(lua_touserdata(pLuaState, lua_upvalueindex(1)));
}

Party *partyFromHost(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    return pHost != nullptr && pHost->party ? pHost->party() : nullptr;
}

IGameplayWorldRuntime *worldFromHost(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    return pHost != nullptr && pHost->world ? pHost->world() : nullptr;
}

const GameDataLoader *dataFromHost(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    return pHost != nullptr && pHost->data ? pHost->data() : nullptr;
}

void pushString(lua_State *pLuaState, const std::string &text)
{
    lua_pushlstring(pLuaState, text.data(), text.size());
}

// Scripts written for Lua 5.1 hand over floats where the engine stores integers; they are truncated like 5.1 did.
int64_t integerArgument(lua_State *pLuaState, int index)
{
    if (lua_isinteger(pLuaState, index))
    {
        return lua_tointeger(pLuaState, index);
    }

    return static_cast<int64_t>(luaL_checknumber(pLuaState, index));
}

template <typename T>
T clampedInteger(int64_t value)
{
    const int64_t low = static_cast<int64_t>(std::numeric_limits<T>::min());
    const int64_t high = static_cast<int64_t>(std::numeric_limits<T>::max());
    return static_cast<T>(std::clamp(value, low, high));
}

// MMExtension scripts key their own per-character data by the character's index in the roster of all characters.
// OpenYAMM has no such roster (created characters all carry roster id 0), so each character gets a small number of
// its own the first time a mod asks. It is kept in the character's event variables, under a tag no event uses, so
// it is saved with the character and follows it through party reordering and the Adventurer's Inn.
constexpr uint16_t ModIndexVariableTag = 0xFFEE;

int64_t modIndexOf(Party &party, Character &character)
{
    return ensureModCharacterIndex(party, character);
}
}

const char *mergeSkillName(size_t skillId)
{
    static const char *const names[MergeSkillCount] = {
        "Staff", "Sword", "Dagger", "Axe", "Spear", "Bow", "Mace", "Blaster", "Shield", "Leather", "Chain", "Plate",
        "Fire", "Air", "Water", "Earth", "Spirit", "Mind", "Body", "Light", "Dark", "DarkElfAbility",
        "VampireAbility", "DragonAbility", "IdentifyItem", "Merchant", "Repair", "Bodybuilding", "Meditation",
        "Perception", "Regeneration", "DisarmTraps", "Dodging", "Unarmed", "IdentifyMonster", "Armsmaster",
        "Stealing", "Alchemy", "Learning",
    };
    return skillId < MergeSkillCount ? names[skillId] : nullptr;
}

int64_t ensureModCharacterIndex(Party &party, Character &character)
{
    const std::unordered_map<uint16_t, int32_t>::const_iterator existing =
        character.eventVariables.find(ModIndexVariableTag);

    if (existing != character.eventVariables.end() && existing->second > 0)
    {
        return existing->second - 1;
    }

    std::array<bool, 128> used = {};

    const auto markUsed = [&used](const Character &other)
    {
        const std::unordered_map<uint16_t, int32_t>::const_iterator it =
            other.eventVariables.find(ModIndexVariableTag);

        if (it != other.eventVariables.end() && it->second > 0 && static_cast<size_t>(it->second - 1) < used.size())
        {
            used[static_cast<size_t>(it->second - 1)] = true;
        }
    };

    for (const Character &member : party.members())
    {
        markUsed(member);
    }

    for (const AdventurersInnMember &innMember : party.adventurersInnMembers())
    {
        markUsed(innMember.character);
    }

    for (size_t candidate = 0; candidate < used.size(); ++candidate)
    {
        if (!used[candidate])
        {
            character.eventVariables[ModIndexVariableTag] = static_cast<int32_t>(candidate + 1);
            return static_cast<int64_t>(candidate);
        }
    }

    return -1;
}

namespace
{
Character *playerArgument(lua_State *pLuaState, int index)
{
    Party *pParty = partyFromHost(pLuaState);

    if (pParty == nullptr)
    {
        return nullptr;
    }

    const int64_t memberIndex = integerArgument(pLuaState, index);
    return memberIndex >= 0 ? pParty->member(static_cast<size_t>(memberIndex)) : nullptr;
}

// --- party ---------------------------------------------------------------------------------------------------------

int nativePartyGet(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    const std::string field = luaL_checkstring(pLuaState, 1);

    if (pParty == nullptr)
    {
        return 0;
    }

    if (field == "Gold")
    {
        lua_pushinteger(pLuaState, pParty->gold());
    }
    else if (field == "Food")
    {
        lua_pushinteger(pLuaState, pParty->food());
    }
    else if (field == "BankGold")
    {
        lua_pushinteger(pLuaState, pParty->bankGold());
    }
    else if (field == "Count")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pParty->memberCount()));
    }
    else if (field == "CurrentPlayer")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pParty->activeMemberIndex()));
    }
    else if (field == "X" || field == "Y" || field == "Z" || field == "YawRadians")
    {
        const IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);

        if (pWorld == nullptr)
        {
            return 0;
        }

        if (field == "YawRadians")
        {
            lua_pushnumber(pLuaState, pWorld->gameplayCameraYawRadians());
        }
        else
        {
            const float value = field == "X" ? pWorld->partyX() : field == "Y" ? pWorld->partyY() : pWorld->partyFootZ();
            lua_pushinteger(pLuaState, static_cast<lua_Integer>(value));
        }
    }
    else
    {
        return 0;
    }

    return 1;
}

int nativePartySet(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    const std::string field = luaL_checkstring(pLuaState, 1);
    bool handled = pParty != nullptr;

    if (pParty == nullptr)
    {
        handled = false;
    }
    else if (field == "Gold")
    {
        pParty->addGold(clampedInteger<int>(integerArgument(pLuaState, 2)) - pParty->gold());
    }
    else if (field == "Food")
    {
        pParty->addFood(clampedInteger<int>(integerArgument(pLuaState, 2)) - pParty->food());
    }
    else
    {
        handled = false;
    }

    lua_pushboolean(pLuaState, handled);
    return 1;
}

// --- players ---------------------------------------------------------------------------------------------------------

struct PlayerUnsignedField
{
    const char *pName;
    uint32_t Character::*pMember;
};

struct PlayerSignedField
{
    const char *pName;
    int Character::*pMember;
};

constexpr PlayerUnsignedField PlayerUnsignedFields[] = {
    {"LevelBase", &Character::level},
    {"Experience", &Character::experience},
    {"SkillPoints", &Character::skillPoints},
    {"MightBase", &Character::might},
    {"IntellectBase", &Character::intellect},
    {"PersonalityBase", &Character::personality},
    {"EnduranceBase", &Character::endurance},
    {"SpeedBase", &Character::speed},
    {"AccuracyBase", &Character::accuracy},
    {"LuckBase", &Character::luck},
    {"Face", &Character::portraitPictureId},
    {"BirthYear", &Character::birthYear},
    {"RosterId", &Character::rosterId},
    {"RaceId", &Character::raceId},
    {"SexId", &Character::sexId},
};

constexpr PlayerSignedField PlayerSignedFields[] = {
    {"HP", &Character::health},
    {"SP", &Character::spellPoints},
    {"FullHPBase", &Character::maxHealth},
    {"FullSPBase", &Character::maxSpellPoints},
    {"Voice", &Character::voiceId},
};

int nativePlayerGet(lua_State *pLuaState)
{
    const Character *pCharacter = playerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pCharacter == nullptr)
    {
        return 0;
    }

    for (const PlayerUnsignedField &entry : PlayerUnsignedFields)
    {
        if (field == entry.pName)
        {
            lua_pushinteger(pLuaState, static_cast<lua_Integer>(pCharacter->*entry.pMember));
            return 1;
        }
    }

    for (const PlayerSignedField &entry : PlayerSignedFields)
    {
        if (field == entry.pName)
        {
            lua_pushinteger(pLuaState, pCharacter->*entry.pMember);
            return 1;
        }
    }

    if (field == "Name")
    {
        pushString(pLuaState, pCharacter->name);
        return 1;
    }

    if (field == "ClassName")
    {
        pushString(pLuaState, pCharacter->className);
        return 1;
    }

    if (field == "Class")
    {
        const Party *pParty = partyFromHost(pLuaState);
        const ClassSkillTable *pClassSkillTable = pParty != nullptr ? pParty->classSkillTable() : nullptr;
        const std::optional<uint32_t> classId =
            pClassSkillTable != nullptr ? pClassSkillTable->classIdForName(pCharacter->className) : std::nullopt;

        if (!classId)
        {
            return 0;
        }

        lua_pushinteger(pLuaState, static_cast<lua_Integer>(*classId));
        return 1;
    }

    if (field == "ItemCount")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pCharacter->inventory.size()));
        return 1;
    }

    // Computed values, as the original engine's Get* functions return them.
    if (field == "GetMight" || field == "GetIntellect" || field == "GetPersonality" || field == "GetEndurance"
        || field == "GetAccuracy" || field == "GetSpeed" || field == "GetLuck")
    {
        const CharacterStatBonuses &permanent = pCharacter->permanentBonuses;
        const CharacterStatBonuses &magical = pCharacter->magicalBonuses;
        int value = 0;

        if (field == "GetMight")
        {
            value = static_cast<int>(pCharacter->might) + permanent.might + magical.might;
        }
        else if (field == "GetIntellect")
        {
            value = static_cast<int>(pCharacter->intellect) + permanent.intellect + magical.intellect;
        }
        else if (field == "GetPersonality")
        {
            value = static_cast<int>(pCharacter->personality) + permanent.personality + magical.personality;
        }
        else if (field == "GetEndurance")
        {
            value = static_cast<int>(pCharacter->endurance) + permanent.endurance + magical.endurance;
        }
        else if (field == "GetAccuracy")
        {
            value = static_cast<int>(pCharacter->accuracy) + permanent.accuracy + magical.accuracy;
        }
        else if (field == "GetSpeed")
        {
            value = static_cast<int>(pCharacter->speed) + permanent.speed + magical.speed;
        }
        else
        {
            value = static_cast<int>(pCharacter->luck) + permanent.luck + magical.luck;
        }

        lua_pushinteger(pLuaState, value);
        return 1;
    }

    if (field == "GetFullHP" || field == "GetFullSP")
    {
        const Party *pParty = partyFromHost(pLuaState);
        const ClassMultiplierTable *pClassMultiplierTable =
            pParty != nullptr ? pParty->classMultiplierTable() : nullptr;
        lua_pushinteger(
            pLuaState,
            field == "GetFullHP"
                ? GameMechanics::calculateEffectiveCharacterMaxHealth(*pCharacter, pClassMultiplierTable)
                : GameMechanics::calculateEffectiveCharacterMaxSpellPoints(*pCharacter, pClassMultiplierTable));
        return 1;
    }

    if (field == "GetArmorClass")
    {
        const Party *pParty = partyFromHost(pLuaState);

        if (pParty == nullptr)
        {
            return 0;
        }

        const CharacterSheetSummary summary = GameMechanics::buildCharacterSheetSummary(
            *pCharacter,
            pParty->itemTable(),
            pParty->standardItemEnchantTable(),
            pParty->specialItemEnchantTable());
        lua_pushinteger(pLuaState, summary.armorClass.actual);
        return 1;
    }

    if (field == "GetMeleeDamageMin" || field == "GetMeleeDamageMax" || field == "GetMeleeAttack"
        || field == "GetRangedDamageMin" || field == "GetRangedDamageMax" || field == "GetRangedAttack")
    {
        const Party *pParty = partyFromHost(pLuaState);
        const GameDataLoader *pData = dataFromHost(pLuaState);

        if (pParty == nullptr)
        {
            return 0;
        }

        const CharacterAttackProfile profile = GameMechanics::buildCharacterAttackProfile(
            *pCharacter,
            pParty->itemTable(),
            pData != nullptr ? &pData->getSpellTable() : nullptr);

        if (field == "GetMeleeDamageMin")
        {
            lua_pushinteger(pLuaState, profile.meleeMinDamage);
        }
        else if (field == "GetMeleeDamageMax")
        {
            lua_pushinteger(pLuaState, profile.meleeMaxDamage);
        }
        else if (field == "GetMeleeAttack")
        {
            lua_pushinteger(pLuaState, profile.meleeAttackBonus);
        }
        else if (field == "GetRangedDamageMin")
        {
            lua_pushinteger(pLuaState, profile.rangedMinDamage);
        }
        else if (field == "GetRangedDamageMax")
        {
            lua_pushinteger(pLuaState, profile.rangedMaxDamage);
        }
        else
        {
            lua_pushinteger(pLuaState, profile.rangedAttackBonus.value_or(0));
        }

        return 1;
    }

    if (field == "ModIndex")
    {
        Party *pParty = partyFromHost(pLuaState);
        Character *pMutableCharacter = playerArgument(pLuaState, 1);

        if (pParty == nullptr || pMutableCharacter == nullptr)
        {
            return 0;
        }

        lua_pushinteger(pLuaState, modIndexOf(*pParty, *pMutableCharacter));
        return 1;
    }

    return 0;
}

int nativePlayerSet(lua_State *pLuaState)
{
    Character *pCharacter = playerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pCharacter == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    for (const PlayerUnsignedField &entry : PlayerUnsignedFields)
    {
        if (field == entry.pName)
        {
            pCharacter->*entry.pMember = clampedInteger<uint32_t>(integerArgument(pLuaState, 3));
            lua_pushboolean(pLuaState, true);
            return 1;
        }
    }

    for (const PlayerSignedField &entry : PlayerSignedFields)
    {
        if (field == entry.pName)
        {
            pCharacter->*entry.pMember = clampedInteger<int>(integerArgument(pLuaState, 3));
            lua_pushboolean(pLuaState, true);
            return 1;
        }
    }

    if (field == "Name")
    {
        pCharacter->name = luaL_checkstring(pLuaState, 3);
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    lua_pushboolean(pLuaState, false);
    return 1;
}

// Skills are addressed by their Merge name; OpenYAMM stores them under its own canonical name.
int nativePlayerSkillGet(lua_State *pLuaState)
{
    const Character *pCharacter = playerArgument(pLuaState, 1);
    const std::string skillName = canonicalSkillName(luaL_checkstring(pLuaState, 2));

    if (pCharacter == nullptr)
    {
        return 0;
    }

    const CharacterSkill *pSkill = pCharacter->findSkillByCanonicalName(skillName);

    if (pSkill == nullptr)
    {
        lua_pushinteger(pLuaState, 0);
        lua_pushinteger(pLuaState, 0);
        return 2;
    }

    lua_pushinteger(pLuaState, static_cast<lua_Integer>(pSkill->level));
    lua_pushinteger(pLuaState, static_cast<lua_Integer>(pSkill->mastery));
    lua_pushinteger(pLuaState, pCharacter->skillBonus(skillName));
    return 3;
}

// Recomputes every member's bonuses now, which runs the stat events (mods call this after changing what the
// events depend on; the game itself does it whenever equipment or buffs change).
int nativePartyRefreshBonuses(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);

    if (pParty != nullptr)
    {
        pParty->rebuildMagicalBonusesFromBuffs();
    }

    return 0;
}

int nativePlayerSkillSet(lua_State *pLuaState)
{
    Character *pCharacter = playerArgument(pLuaState, 1);
    const std::string skillName = canonicalSkillName(luaL_checkstring(pLuaState, 2));
    const int64_t level = std::max<int64_t>(0, integerArgument(pLuaState, 3));
    const int64_t mastery = std::clamp<int64_t>(integerArgument(pLuaState, 4), 0, 4);

    if (pCharacter == nullptr || skillName.empty())
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    if (level == 0 || mastery == 0)
    {
        pCharacter->skills.erase(skillName);
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    CharacterSkill &skill = pCharacter->skills[skillName];
    skill.name = skillName;
    skill.level = clampedInteger<uint32_t>(level);
    skill.mastery = static_cast<SkillMastery>(mastery);
    lua_pushboolean(pLuaState, true);
    return 1;
}

// --- items -----------------------------------------------------------------------------------------------------------

// One view over the two ways the engine holds an item: in a backpack, or equipped (id plus runtime state).
struct ItemView
{
    uint32_t *pItemId = nullptr;
    bool *pIdentified = nullptr;
    bool *pBroken = nullptr;
    bool *pStolen = nullptr;
    uint16_t *pStandardEnchantId = nullptr;
    uint16_t *pStandardEnchantPower = nullptr;
    uint16_t *pSpecialEnchantId = nullptr;
    uint16_t *pCurrentCharges = nullptr;
    uint16_t *pMaxCharges = nullptr;
    ItemModData *pModData = nullptr;
    const InventoryItem *pInventoryItem = nullptr;
    // 0 in the backpack, otherwise the equipment slot plus one (Item.BodyLocation).
    int bodyLocation = 0;
};

struct EquipmentSlotMembers
{
    uint32_t CharacterEquipment::*pItemId;
    EquippedItemRuntimeState CharacterEquipmentRuntimeState::*pRuntime;
};

constexpr std::array<EquipmentSlotMembers, 16> EquipmentSlots = {{
    {&CharacterEquipment::offHand, &CharacterEquipmentRuntimeState::offHand},
    {&CharacterEquipment::mainHand, &CharacterEquipmentRuntimeState::mainHand},
    {&CharacterEquipment::bow, &CharacterEquipmentRuntimeState::bow},
    {&CharacterEquipment::armor, &CharacterEquipmentRuntimeState::armor},
    {&CharacterEquipment::helm, &CharacterEquipmentRuntimeState::helm},
    {&CharacterEquipment::belt, &CharacterEquipmentRuntimeState::belt},
    {&CharacterEquipment::cloak, &CharacterEquipmentRuntimeState::cloak},
    {&CharacterEquipment::gauntlets, &CharacterEquipmentRuntimeState::gauntlets},
    {&CharacterEquipment::boots, &CharacterEquipmentRuntimeState::boots},
    {&CharacterEquipment::amulet, &CharacterEquipmentRuntimeState::amulet},
    {&CharacterEquipment::ring1, &CharacterEquipmentRuntimeState::ring1},
    {&CharacterEquipment::ring2, &CharacterEquipmentRuntimeState::ring2},
    {&CharacterEquipment::ring3, &CharacterEquipmentRuntimeState::ring3},
    {&CharacterEquipment::ring4, &CharacterEquipmentRuntimeState::ring4},
    {&CharacterEquipment::ring5, &CharacterEquipmentRuntimeState::ring5},
    {&CharacterEquipment::ring6, &CharacterEquipmentRuntimeState::ring6},
}};

// Arguments: player index, "inv" | "equip", zero-based index within that group.
bool itemViewArgument(lua_State *pLuaState, ItemView &view)
{
    Character *pCharacter = playerArgument(pLuaState, 1);
    const std::string group = luaL_checkstring(pLuaState, 2);
    const int64_t index = integerArgument(pLuaState, 3);

    if (pCharacter == nullptr || index < 0)
    {
        return false;
    }

    if (group == "inv")
    {
        if (static_cast<size_t>(index) >= pCharacter->inventory.size())
        {
            return false;
        }

        InventoryItem &item = pCharacter->inventory[static_cast<size_t>(index)];
        view.pItemId = &item.objectDescriptionId;
        view.pIdentified = &item.identified;
        view.pBroken = &item.broken;
        view.pStolen = &item.stolen;
        view.pStandardEnchantId = &item.standardEnchantId;
        view.pStandardEnchantPower = &item.standardEnchantPower;
        view.pSpecialEnchantId = &item.specialEnchantId;
        view.pCurrentCharges = &item.currentCharges;
        view.pMaxCharges = &item.maxCharges;
        view.pModData = &item.modData;
        view.pInventoryItem = &item;
        return true;
    }

    if (group == "equip")
    {
        if (static_cast<size_t>(index) >= EquipmentSlots.size())
        {
            return false;
        }

        const EquipmentSlotMembers &slot = EquipmentSlots[static_cast<size_t>(index)];
        EquippedItemRuntimeState &runtime = pCharacter->equipmentRuntime.*slot.pRuntime;
        view.pItemId = &(pCharacter->equipment.*slot.pItemId);
        view.pIdentified = &runtime.identified;
        view.pBroken = &runtime.broken;
        view.pStolen = &runtime.stolen;
        view.pStandardEnchantId = &runtime.standardEnchantId;
        view.pStandardEnchantPower = &runtime.standardEnchantPower;
        view.pSpecialEnchantId = &runtime.specialEnchantId;
        view.pCurrentCharges = &runtime.currentCharges;
        view.pMaxCharges = &runtime.maxCharges;
        view.pModData = &runtime.modData;
        view.bodyLocation = static_cast<int>(index) + 1;
        return true;
    }

    return false;
}

int nativeItemGet(lua_State *pLuaState)
{
    ItemView view = {};
    const std::string field = luaL_checkstring(pLuaState, 4);

    if (!itemViewArgument(pLuaState, view))
    {
        return 0;
    }

    if (field == "Number")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(*view.pItemId));
    }
    else if (field == "Bonus")
    {
        lua_pushinteger(pLuaState, *view.pStandardEnchantId);
    }
    else if (field == "BonusStrength")
    {
        lua_pushinteger(pLuaState, *view.pStandardEnchantPower);
    }
    else if (field == "Bonus2")
    {
        lua_pushinteger(pLuaState, *view.pSpecialEnchantId);
    }
    else if (field == "Charges")
    {
        lua_pushinteger(pLuaState, view.pModData->charges);
    }
    else if (field == "MaxCharges")
    {
        lua_pushinteger(pLuaState, view.pModData->maxCharges);
    }
    else if (field == "BonusExpireTime")
    {
        lua_pushinteger(pLuaState, view.pModData->bonusExpireTime);
    }
    else if (field == "WandCharges")
    {
        lua_pushinteger(pLuaState, *view.pCurrentCharges);
    }
    else if (field == "WandMaxCharges")
    {
        lua_pushinteger(pLuaState, *view.pMaxCharges);
    }
    else if (field == "Identified")
    {
        lua_pushboolean(pLuaState, *view.pIdentified);
    }
    else if (field == "Broken")
    {
        lua_pushboolean(pLuaState, *view.pBroken);
    }
    else if (field == "Stolen")
    {
        lua_pushboolean(pLuaState, *view.pStolen);
    }
    else if (field == "BodyLocation")
    {
        lua_pushinteger(pLuaState, view.bodyLocation);
    }
    else if (field == "Condition")
    {
        // Item.Condition bits of the original engine: 1 identified, 2 broken, 0x100 stolen.
        lua_pushinteger(
            pLuaState,
            (*view.pIdentified ? 1 : 0) | (*view.pBroken ? 2 : 0) | (*view.pStolen ? 0x100 : 0));
    }
    else if (field == "GridX" && view.pInventoryItem != nullptr)
    {
        lua_pushinteger(pLuaState, view.pInventoryItem->gridX);
    }
    else if (field == "GridY" && view.pInventoryItem != nullptr)
    {
        lua_pushinteger(pLuaState, view.pInventoryItem->gridY);
    }
    else
    {
        return 0;
    }

    return 1;
}

int nativeItemSet(lua_State *pLuaState)
{
    ItemView view = {};
    const std::string field = luaL_checkstring(pLuaState, 4);
    bool handled = itemViewArgument(pLuaState, view);

    if (!handled)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    if (field == "Bonus")
    {
        *view.pStandardEnchantId = clampedInteger<uint16_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "BonusStrength")
    {
        *view.pStandardEnchantPower = clampedInteger<uint16_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "Bonus2")
    {
        *view.pSpecialEnchantId = clampedInteger<uint16_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "Charges")
    {
        view.pModData->charges = clampedInteger<int32_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "MaxCharges")
    {
        view.pModData->maxCharges = clampedInteger<int32_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "BonusExpireTime")
    {
        view.pModData->bonusExpireTime = clampedInteger<int32_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "WandCharges")
    {
        *view.pCurrentCharges = clampedInteger<uint16_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "WandMaxCharges")
    {
        *view.pMaxCharges = clampedInteger<uint16_t>(integerArgument(pLuaState, 5));
    }
    else if (field == "Condition")
    {
        const int64_t bits = integerArgument(pLuaState, 5);
        *view.pIdentified = (bits & 1) != 0;
        *view.pBroken = (bits & 2) != 0;
        *view.pStolen = (bits & 0x100) != 0;
    }
    else if (field == "Identified")
    {
        *view.pIdentified = lua_toboolean(pLuaState, 5) != 0;
    }
    else if (field == "Broken")
    {
        *view.pBroken = lua_toboolean(pLuaState, 5) != 0;
    }
    else if (field == "Stolen")
    {
        *view.pStolen = lua_toboolean(pLuaState, 5) != 0;
    }
    else
    {
        handled = false;
    }

    lua_pushboolean(pLuaState, handled);
    return 1;
}

// --- tables ----------------------------------------------------------------------------------------------------------

// ItemsTxt.EquipStat as the original engine numbers the "Equip Stat" column of items.txt (const.ItemType - 1).
int mm8EquipStat(const std::string &equipStat)
{
    static const std::unordered_map<std::string, int> values = {
        {"weapon", 0}, {"weapon1or2", 0}, {"weapon2", 1}, {"missile", 2}, {"bow", 2}, {"armor", 3},
        {"shield", 4}, {"helm", 5}, {"belt", 6}, {"cloak", 7}, {"gauntlets", 8}, {"boots", 9}, {"ring", 10},
        {"amulet", 11}, {"weaponw", 12}, {"reagent", 13}, {"herb", 13}, {"bottle", 14}, {"sscroll", 15},
        {"book", 16}, {"mscroll", 17}, {"gold", 18}, {"gem", 19},
    };
    std::string key = equipStat;

    for (char &character : key)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    const std::unordered_map<std::string, int>::const_iterator it = values.find(key);
    return it != values.end() ? it->second : 20;
}

// ItemsTxt.Skill: the "Skill Group" column as a skill id. Club and Misc follow the 39 real skills.
int mm8ItemSkill(const std::string &skillGroup)
{
    static const std::unordered_map<std::string, int> values = {
        {"staff", 0}, {"sword", 1}, {"dagger", 2}, {"axe", 3}, {"spear", 4}, {"bow", 5}, {"mace", 6},
        {"blaster", 7}, {"shield", 8}, {"leather", 9}, {"chain", 10}, {"plate", 11}, {"club", 39},
    };
    std::string key = skillGroup;

    for (char &character : key)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    const std::unordered_map<std::string, int>::const_iterator it = values.find(key);
    return it != values.end() ? it->second : 40;
}

// "2d3" -> 2 and 3; anything else -> 0 and 0.
void parseDice(const std::string &text, int &count, int &sides)
{
    count = 0;
    sides = 0;
    const size_t separator = text.find_first_of("dD");

    if (separator == std::string::npos || separator == 0)
    {
        return;
    }

    try
    {
        count = std::stoi(text.substr(0, separator));
        sides = std::stoi(text.substr(separator + 1));
    }
    catch (const std::exception &)
    {
        count = 0;
        sides = 0;
    }
}

int integerOrZero(const std::string &text)
{
    try
    {
        return std::stoi(text);
    }
    catch (const std::exception &)
    {
        return 0;
    }
}

int nativeItemTxtGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t itemId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pData == nullptr)
    {
        return 0;
    }

    const ItemTable &itemTable = pData->getItemTable();

    if (field == "#")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(itemTable.entries().size()));
        return 1;
    }

    const ItemDefinition *pItem = itemId >= 0 ? itemTable.get(static_cast<uint32_t>(itemId)) : nullptr;

    if (pItem == nullptr)
    {
        return 0;
    }

    if (field == "Name")
    {
        pushString(pLuaState, pItem->name);
    }
    else if (field == "NotIdentifiedName")
    {
        pushString(pLuaState, pItem->unidentifiedName);
    }
    else if (field == "Picture")
    {
        pushString(pLuaState, pItem->iconName);
    }
    else if (field == "Value")
    {
        lua_pushinteger(pLuaState, pItem->value);
    }
    else if (field == "EquipStatName")
    {
        pushString(pLuaState, pItem->equipStat);
    }
    else if (field == "SkillName")
    {
        pushString(pLuaState, pItem->skillGroup);
    }
    else if (field == "EquipStat")
    {
        lua_pushinteger(pLuaState, mm8EquipStat(pItem->equipStat));
    }
    else if (field == "Skill")
    {
        lua_pushinteger(pLuaState, mm8ItemSkill(pItem->skillGroup));
    }
    else if (field == "Mod1DiceCount" || field == "Mod1DiceSides")
    {
        int count = 0;
        int sides = 0;
        parseDice(pItem->mod1, count, sides);
        lua_pushinteger(pLuaState, field == "Mod1DiceCount" ? count : sides);
    }
    else if (field == "Mod1Text")
    {
        pushString(pLuaState, pItem->mod1);
    }
    else if (field == "Mod2")
    {
        lua_pushinteger(pLuaState, integerOrZero(pItem->mod2));
    }
    else if (field == "SpriteIndex")
    {
        lua_pushinteger(pLuaState, pItem->spriteIndex);
    }
    else if (field == "EquipX")
    {
        lua_pushinteger(pLuaState, pItem->equipX);
    }
    else if (field == "EquipY")
    {
        lua_pushinteger(pLuaState, pItem->equipY);
    }
    else if (field == "Notes")
    {
        pushString(pLuaState, pItem->notes);
    }
    else if (field == "Material")
    {
        lua_pushinteger(pLuaState, pItem->material);
    }
    else if (field == "IdRepSt")
    {
        lua_pushinteger(pLuaState, pItem->identifyRepairDifficulty);
    }
    else if (field == "VarA")
    {
        lua_pushinteger(pLuaState, pItem->varA);
    }
    else if (field == "VarB")
    {
        lua_pushinteger(pLuaState, pItem->varB);
    }
    else
    {
        return 0;
    }

    return 1;
}

int nativeMonsterTxtGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t monsterId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pData == nullptr)
    {
        return 0;
    }

    const MonsterTable::MonsterStatsEntry *pStats =
        pData->getMonsterTable().findStatsById(clampedInteger<int16_t>(monsterId));

    if (pStats == nullptr)
    {
        return 0;
    }

    if (field == "Name")
    {
        pushString(pLuaState, pStats->name);
    }
    else if (field == "Level")
    {
        lua_pushinteger(pLuaState, pStats->level);
    }
    else if (field == "FullHP" || field == "FullHitPoints")
    {
        lua_pushinteger(pLuaState, pStats->hitPoints);
    }
    else if (field == "ArmorClass")
    {
        lua_pushinteger(pLuaState, pStats->armorClass);
    }
    else if (field == "Experience")
    {
        lua_pushinteger(pLuaState, pStats->experience);
    }
    else
    {
        return 0;
    }

    return 1;
}

int nativePortraitGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t portraitId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pData == nullptr || portraitId < 0)
    {
        return 0;
    }

    // Scripts number portraits from 0 (Player.Face); OpenYAMM's character table numbers its rows from 1.
    const CharacterDollEntry *pEntry =
        pData->getCharacterDollTable().getCharacter(static_cast<uint32_t>(portraitId) + 1);

    if (pEntry == nullptr)
    {
        return 0;
    }

    if (field == "Race" && pEntry->raceId >= 0)
    {
        lua_pushinteger(pLuaState, pEntry->raceId);
        return 1;
    }

    return 0;
}

// --- map monsters ----------------------------------------------------------------------------------------------------

std::optional<GameplayModActorField> actorFieldFromName(const std::string &field)
{
    static const std::unordered_map<std::string, GameplayModActorField> fields = {
        {"Id", GameplayModActorField::MonsterId},
        {"HP", GameplayModActorField::CurrentHp},
        {"HitPoints", GameplayModActorField::CurrentHp},
        {"FullHP", GameplayModActorField::MaxHp},
        {"FullHitPoints", GameplayModActorField::MaxHp},
        {"X", GameplayModActorField::X},
        {"Y", GameplayModActorField::Y},
        {"Z", GameplayModActorField::Z},
        {"Group", GameplayModActorField::Group},
    };
    const std::unordered_map<std::string, GameplayModActorField>::const_iterator it = fields.find(field);
    return it != fields.end() ? std::optional<GameplayModActorField>(it->second) : std::nullopt;
}

int nativeMapMonsterGet(lua_State *pLuaState)
{
    const IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const int64_t actorIndex = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pWorld == nullptr)
    {
        if (field == "#")
        {
            lua_pushinteger(pLuaState, 0);
            return 1;
        }

        return 0;
    }

    if (field == "#")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pWorld->mapActorCount()));
        return 1;
    }

    if (field == "Hostile" || field == "IsDead")
    {
        GameplayRuntimeActorState state = {};

        if (actorIndex < 0 || !pWorld->actorRuntimeState(static_cast<size_t>(actorIndex), state))
        {
            return 0;
        }

        lua_pushboolean(pLuaState, field == "Hostile" ? state.hostileToParty : state.isDead);
        return 1;
    }

    const std::optional<GameplayModActorField> actorField = actorFieldFromName(field);
    int64_t value = 0;

    if (!actorField || actorIndex < 0 || !pWorld->modActorField(static_cast<size_t>(actorIndex), *actorField, value))
    {
        return 0;
    }

    lua_pushinteger(pLuaState, value);
    return 1;
}

int nativeMapMonsterSet(lua_State *pLuaState)
{
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const int64_t actorIndex = integerArgument(pLuaState, 1);
    const std::optional<GameplayModActorField> actorField = actorFieldFromName(luaL_checkstring(pLuaState, 2));
    const bool handled = pWorld != nullptr
        && actorField.has_value()
        && actorIndex >= 0
        && pWorld->setModActorField(static_cast<size_t>(actorIndex), *actorField, integerArgument(pLuaState, 3));
    lua_pushboolean(pLuaState, handled);
    return 1;
}

// --- game ------------------------------------------------------------------------------------------------------------

int nativeGameGet(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    const std::string field = luaL_checkstring(pLuaState, 1);

    if (field == "MapFileName" && pHost->currentMapFileName)
    {
        pushString(pLuaState, pHost->currentMapFileName());
        return 1;
    }

    if (field == "GameMinutes" && pHost->gameMinutes)
    {
        lua_pushnumber(pLuaState, pHost->gameMinutes());
        return 1;
    }

    if (field == "HasSession")
    {
        lua_pushboolean(pLuaState, partyFromHost(pLuaState) != nullptr);
        return 1;
    }

    if (field == "MapReady")
    {
        lua_pushboolean(
            pLuaState,
            pHost->mapReady ? pHost->mapReady() : (worldFromHost(pLuaState) != nullptr));
        return 1;
    }

    return 0;
}

int nativeShowStatusText(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    size_t length = 0;
    const char *pText = luaL_tolstring(pLuaState, 1, &length);

    if (pHost->showStatusText)
    {
        pHost->showStatusText(std::string(pText, length));
    }

    return 0;
}
}

void registerModBindings(lua_State *pLuaState, const ModHost &host)
{
    struct NativeFunction
    {
        const char *pName;
        lua_CFunction function;
    };

    const NativeFunction natives[] = {
        {"partyGet", nativePartyGet},
        {"partySet", nativePartySet},
        {"playerGet", nativePlayerGet},
        {"playerSet", nativePlayerSet},
        {"playerSkillGet", nativePlayerSkillGet},
        {"playerSkillSet", nativePlayerSkillSet},
        {"partyRefreshBonuses", nativePartyRefreshBonuses},
        {"itemGet", nativeItemGet},
        {"itemSet", nativeItemSet},
        {"itemTxtGet", nativeItemTxtGet},
        {"monsterTxtGet", nativeMonsterTxtGet},
        {"portraitGet", nativePortraitGet},
        {"mapMonsterGet", nativeMapMonsterGet},
        {"mapMonsterSet", nativeMapMonsterSet},
        {"gameGet", nativeGameGet},
        {"showStatusText", nativeShowStatusText},
    };

    lua_getglobal(pLuaState, "modrt");

    for (const NativeFunction &native : natives)
    {
        lua_pushlightuserdata(pLuaState, const_cast<ModHost *>(&host));
        lua_pushcclosure(pLuaState, native.function, 1);
        lua_setfield(pLuaState, -2, native.pName);
    }

    lua_pop(pLuaState, 1);
}
}
