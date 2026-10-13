#include "game/mod/ModBindings.h"

#include "game/data/GameDataLoader.h"
#include "game/gameplay/GameMechanics.h"
#include "game/gameplay/GameplayRuntimeInterfaces.h"
#include "game/mod/ModHost.h"
#include "game/party/Party.h"
#include "game/party/PartySpellSystem.h"
#include "game/party/SkillData.h"
#include "game/tables/CharacterDollTable.h"
#include "game/tables/ClassSkillTable.h"
#include "game/tables/ItemTable.h"
#include "game/tables/MapStats.h"
#include "game/tables/MonsterTable.h"
#include "game/tables/SpellTable.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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

std::optional<size_t> findModPartySlot(const Party &party, const Character &character)
{
    const std::vector<Character> &members = party.members();

    for (size_t slot = 0; slot < members.size(); ++slot)
    {
        if (&members[slot] == &character)
        {
            return slot;
        }
    }

    const std::unordered_map<uint16_t, int32_t>::const_iterator tag =
        character.eventVariables.find(ModIndexVariableTag);

    if (tag == character.eventVariables.end() || tag->second <= 0)
    {
        return std::nullopt;
    }

    for (size_t slot = 0; slot < members.size(); ++slot)
    {
        const std::unordered_map<uint16_t, int32_t>::const_iterator other =
            members[slot].eventVariables.find(ModIndexVariableTag);

        if (other != members[slot].eventVariables.end() && other->second == tag->second)
        {
            return slot;
        }
    }

    return std::nullopt;
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
    {"LevelBonus", &Character::levelModifier},
};

// The original engine keeps two numbers per primary statistic and per resistance: the base value and a temporary
// bonus (wells, shrines, potions) that rest clears. In OpenYAMM those bonuses are temporaryEventBonuses.
struct PlayerStatBonusField
{
    const char *pName;
    int CharacterStatBonuses::*pMember;
};

constexpr PlayerStatBonusField PlayerStatBonusFields[] = {
    {"MightBonus", &CharacterStatBonuses::might},
    {"IntellectBonus", &CharacterStatBonuses::intellect},
    {"PersonalityBonus", &CharacterStatBonuses::personality},
    {"EnduranceBonus", &CharacterStatBonuses::endurance},
    {"SpeedBonus", &CharacterStatBonuses::speed},
    {"AccuracyBonus", &CharacterStatBonuses::accuracy},
    {"LuckBonus", &CharacterStatBonuses::luck},
};

struct PlayerResistanceField
{
    const char *pName;
    int CharacterResistanceSet::*pMember;
};

constexpr PlayerResistanceField PlayerResistanceFields[] = {
    {"Fire", &CharacterResistanceSet::fire},
    {"Air", &CharacterResistanceSet::air},
    {"Water", &CharacterResistanceSet::water},
    {"Earth", &CharacterResistanceSet::earth},
    {"Spirit", &CharacterResistanceSet::spirit},
    {"Mind", &CharacterResistanceSet::mind},
    {"Body", &CharacterResistanceSet::body},
    {"Light", &CharacterResistanceSet::light},
    {"Dark", &CharacterResistanceSet::dark},
    {"Physical", &CharacterResistanceSet::physical},
    {"Magic", &CharacterResistanceSet::magic},
};

// "FireResistanceBase" or "FireResistanceBonus": the field of the character that name stands for, or null.
int *playerResistanceField(Character &character, const std::string &field)
{
    static const std::string baseSuffix = "ResistanceBase";
    static const std::string bonusSuffix = "ResistanceBonus";

    for (const PlayerResistanceField &entry : PlayerResistanceFields)
    {
        const std::string name = entry.pName;

        if (field == name + baseSuffix)
        {
            return &(character.baseResistances.*entry.pMember);
        }

        if (field == name + bonusSuffix)
        {
            return &(character.temporaryEventBonuses.resistances.*entry.pMember);
        }
    }

    return nullptr;
}

const int *playerResistanceField(const Character &character, const std::string &field)
{
    return playerResistanceField(const_cast<Character &>(character), field);
}

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

    for (const PlayerStatBonusField &entry : PlayerStatBonusFields)
    {
        if (field == entry.pName)
        {
            lua_pushinteger(pLuaState, pCharacter->temporaryEventBonuses.*entry.pMember);
            return 1;
        }
    }

    if (const int *pResistance = playerResistanceField(*pCharacter, field))
    {
        lua_pushinteger(pLuaState, *pResistance);
        return 1;
    }

    // The spell the character casts with the quick-cast key, as a spell number; 0 when none is chosen.
    if (field == "QuickSpell")
    {
        const GameDataLoader *pData = dataFromHost(pLuaState);
        const SpellEntry *pSpell = pData != nullptr && !pCharacter->quickSpellName.empty()
            ? pData->getSpellTable().findByName(pCharacter->quickSpellName)
            : nullptr;
        lua_pushinteger(pLuaState, pSpell != nullptr ? pSpell->id : 0);
        return 1;
    }

    // The condition that shows on the portrait, as a const.Condition number; 18 is Good.
    if (field == "MainCondition")
    {
        const std::optional<CharacterCondition> condition = GameMechanics::displayedCondition(*pCharacter);
        lua_pushinteger(pLuaState, condition ? static_cast<lua_Integer>(*condition) : lua_Integer(18));
        return 1;
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
        || field == "GetRangedDamageMin" || field == "GetRangedDamageMax" || field == "GetRangedAttack"
        || field == "GetAttackDelay" || field == "GetAttackDelayRanged")
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
        else if (field == "GetAttackDelay")
        {
            lua_pushinteger(pLuaState, profile.meleeRecoveryTicks);
        }
        else if (field == "GetAttackDelayRanged")
        {
            lua_pushinteger(pLuaState, profile.rangedRecoveryTicks);
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

    for (const PlayerStatBonusField &entry : PlayerStatBonusFields)
    {
        if (field == entry.pName)
        {
            pCharacter->temporaryEventBonuses.*entry.pMember = clampedInteger<int>(integerArgument(pLuaState, 3));
            lua_pushboolean(pLuaState, true);
            return 1;
        }
    }

    if (int *pResistance = playerResistanceField(*pCharacter, field))
    {
        *pResistance = clampedInteger<int>(integerArgument(pLuaState, 3));
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    if (field == "QuickSpell")
    {
        const GameDataLoader *pData = dataFromHost(pLuaState);
        const int64_t spellId = integerArgument(pLuaState, 3);
        const SpellEntry *pSpell = pData != nullptr && spellId > 0
            ? pData->getSpellTable().findById(static_cast<int>(spellId))
            : nullptr;

        if (spellId > 0 && pSpell == nullptr)
        {
            lua_pushboolean(pLuaState, false);
            return 1;
        }

        pCharacter->quickSpellName = pSpell != nullptr ? pSpell->name : std::string();
        lua_pushboolean(pLuaState, true);
        return 1;
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

// modrt.playerConditionGet(player, condition): the game time (MMExtension ticks) at which the condition set in, 0
// when the character does not have it. Conditions are numbered as in const.Condition, which is the engine's order.
int nativePlayerConditionGet(lua_State *pLuaState)
{
    const Character *pCharacter = playerArgument(pLuaState, 1);
    const int64_t condition = integerArgument(pLuaState, 2);

    if (pCharacter == nullptr || condition < 0 || condition >= static_cast<int64_t>(CharacterConditionCount))
    {
        return 0;
    }

    const size_t index = static_cast<size_t>(condition);

    if (!pCharacter->conditions.test(index))
    {
        lua_pushinteger(pLuaState, 0);
        return 1;
    }

    // A condition that set in at time 0 must still read as present.
    const double ticks = std::floor(static_cast<double>(pCharacter->conditionStartGameMinutes[index]) * 256.0);
    lua_pushinteger(pLuaState, std::max<lua_Integer>(1, static_cast<lua_Integer>(ticks)));
    return 1;
}

// modrt.playerConditionSet(player, condition, time): 0 lifts the condition, anything else gives it. As in the
// original engine this is a plain write: immunities are not consulted.
int nativePlayerConditionSet(lua_State *pLuaState)
{
    Character *pCharacter = playerArgument(pLuaState, 1);
    const int64_t condition = integerArgument(pLuaState, 2);
    const lua_Number ticks = luaL_optnumber(pLuaState, 3, 0.0);

    if (pCharacter == nullptr || condition < 0 || condition >= static_cast<int64_t>(CharacterConditionCount))
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    const size_t index = static_cast<size_t>(condition);

    if (ticks == 0.0)
    {
        pCharacter->conditions.reset(index);
        pCharacter->conditionStartGameMinutes[index] = 0.0f;
    }
    else
    {
        pCharacter->conditions.set(index);
        pCharacter->conditionStartGameMinutes[index] = static_cast<float>(std::max(0.0, ticks / 256.0));
    }

    lua_pushboolean(pLuaState, true);
    return 1;
}

// --- spell buffs ---------------------------------------------------------------------------------------------------
//
// Scripts number buffs as the original engine does (const.PartyBuff, const.PlayerBuff). Every party buff has a
// counterpart here. Of the per-character ones only some do; the rest (the potion buffs: temporary statistics,
// single-character resistances, Haste, Heroism, Shield, Stoneskin) have no record in OpenYAMM, and the natives
// answer nothing for them so that the scripts' side can keep its own.

constexpr PartyBuffId ModPartyBuffIds[] = {
    PartyBuffId::AirResistance, PartyBuffId::BodyResistance, PartyBuffId::DayOfGods, PartyBuffId::DetectLife,
    PartyBuffId::EarthResistance, PartyBuffId::FeatherFall, PartyBuffId::FireResistance, PartyBuffId::Fly,
    PartyBuffId::Haste, PartyBuffId::Heroism, PartyBuffId::Immolation, PartyBuffId::Invisibility,
    PartyBuffId::MindResistance, PartyBuffId::ProtectionFromMagic, PartyBuffId::Shield, PartyBuffId::Stoneskin,
    PartyBuffId::TorchLight, PartyBuffId::WaterResistance, PartyBuffId::WaterWalk, PartyBuffId::WizardEye,
};

std::optional<CharacterBuffId> modCharacterBuffId(int64_t playerBuff)
{
    switch (playerBuff)
    {
        case 1: return CharacterBuffId::Bless;
        case 4: return CharacterBuffId::Fate;
        case 6: return CharacterBuffId::Hammerhands;
        case 10: return CharacterBuffId::PainReflection;
        case 11: return CharacterBuffId::Preservation;
        case 12: return CharacterBuffId::Regeneration;
        case 24: return CharacterBuffId::Glamour;
        case 26: return CharacterBuffId::Mistform;
        default: return std::nullopt;
    }
}

float hostGameMinutes(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    return pHost != nullptr && pHost->gameMinutes ? pHost->gameMinutes() : 0.0f;
}

// OpenYAMM counts a buff down in game seconds; scripts see the game time (256 ticks to the minute) it runs out at,
// 0 for a buff that is not running.
template <typename BuffState>
int pushBuffField(lua_State *pLuaState, const BuffState &buff, const std::string &field)
{
    if (field == "ExpireTime")
    {
        if (!buff.active())
        {
            lua_pushinteger(pLuaState, 0);
            return 1;
        }

        const double minutes = static_cast<double>(hostGameMinutes(pLuaState)) + buff.remainingSeconds / 60.0;
        lua_pushinteger(pLuaState, std::max<lua_Integer>(1, static_cast<lua_Integer>(std::floor(minutes * 256.0))));
    }
    else if (field == "Power")
    {
        lua_pushinteger(pLuaState, buff.power);
    }
    else if (field == "SkillLevel")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(buff.skillLevel));
    }
    else if (field == "SkillMastery")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(buff.skillMastery));
    }
    else if (field == "Caster")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(buff.casterMemberIndex));
    }
    else
    {
        return 0;
    }

    return 1;
}

template <typename BuffState>
bool setBuffField(lua_State *pLuaState, BuffState &buff, const std::string &field, int valueIndex)
{
    if (field == "ExpireTime")
    {
        const double minutes = luaL_checknumber(pLuaState, valueIndex) / 256.0;
        const double remainingSeconds = (minutes - static_cast<double>(hostGameMinutes(pLuaState))) * 60.0;
        buff.remainingSeconds = static_cast<float>(std::clamp(remainingSeconds, 0.0, 4.0e9));
    }
    else if (field == "Power")
    {
        buff.power = clampedInteger<int>(integerArgument(pLuaState, valueIndex));
    }
    else if (field == "SkillLevel")
    {
        buff.skillLevel = clampedInteger<uint32_t>(integerArgument(pLuaState, valueIndex));
    }
    else if (field == "SkillMastery")
    {
        buff.skillMastery =
            static_cast<SkillMastery>(std::clamp<int64_t>(integerArgument(pLuaState, valueIndex), 0, 4));
    }
    else if (field == "Caster")
    {
        buff.casterMemberIndex = clampedInteger<uint32_t>(integerArgument(pLuaState, valueIndex));
    }
    else
    {
        return false;
    }

    return true;
}

// modrt.buffGet(player, buff, field): player is a party slot, or -1 for the party's own buffs.
int nativeBuffGet(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    const int64_t owner = integerArgument(pLuaState, 1);
    const int64_t buffId = integerArgument(pLuaState, 2);
    const std::string field = luaL_checkstring(pLuaState, 3);

    if (pParty == nullptr)
    {
        return 0;
    }

    if (owner < 0)
    {
        if (buffId < 0 || buffId >= static_cast<int64_t>(std::size(ModPartyBuffIds)))
        {
            return 0;
        }

        return pushBuffField(pLuaState, pParty->modPartyBuff(ModPartyBuffIds[buffId]), field);
    }

    const std::optional<CharacterBuffId> characterBuffId = modCharacterBuffId(buffId);
    const CharacterBuffState *pBuff =
        characterBuffId ? pParty->modCharacterBuff(static_cast<size_t>(owner), *characterBuffId) : nullptr;
    return pBuff != nullptr ? pushBuffField(pLuaState, *pBuff, field) : 0;
}

// modrt.buffSet(player, buff, field, value): true when the buff has a record here and the field was written. The
// bonuses are not rebuilt; the scripts' side does that once after a run of writes.
int nativeBuffSet(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    const int64_t owner = integerArgument(pLuaState, 1);
    const int64_t buffId = integerArgument(pLuaState, 2);
    const std::string field = luaL_checkstring(pLuaState, 3);
    bool written = false;

    if (pParty != nullptr && owner < 0)
    {
        if (buffId >= 0 && buffId < static_cast<int64_t>(std::size(ModPartyBuffIds)))
        {
            written = setBuffField(pLuaState, pParty->modPartyBuff(ModPartyBuffIds[buffId]), field, 4);
        }
    }
    else if (pParty != nullptr)
    {
        const std::optional<CharacterBuffId> characterBuffId = modCharacterBuffId(buffId);
        CharacterBuffState *pBuff =
            characterBuffId ? pParty->modCharacterBuff(static_cast<size_t>(owner), *characterBuffId) : nullptr;
        written = pBuff != nullptr && setBuffField(pLuaState, *pBuff, field, 4);
    }

    lua_pushboolean(pLuaState, written);
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
// The item table's "Mod1" column as the original engine reads it: "3d4" is three four-sided dice, a bare number
// (an armor's armor class) is that many one-sided dice, and a spell reference ("S12") is no dice at all.
void parseDice(const std::string &text, int &count, int &sides)
{
    count = 0;
    sides = 0;
    const size_t separator = text.find_first_of("dD");

    if (separator == std::string::npos || separator == 0)
    {
        if (!text.empty() && text[0] != 's' && text[0] != 'S')
        {
            try
            {
                count = std::stoi(text);
                sides = 1;
            }
            catch (const std::exception &)
            {
                count = 0;
                sides = 0;
            }
        }

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

// modrt.itemTxtSet(item, field, value): changes the game's item table, for every item of that kind and until the
// game is closed, which is what a write to Game.ItemsTxt[] does in the original. Returns false for a field that
// cannot be changed here yet.
int nativeItemTxtSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t itemId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);
    const ItemDefinition *pConstItem =
        pData != nullptr && itemId >= 0 ? pData->getItemTable().get(static_cast<uint32_t>(itemId)) : nullptr;

    if (pConstItem == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    ItemDefinition &item = const_cast<ItemDefinition &>(*pConstItem);
    bool applied = true;

    if (field == "Name")
    {
        item.name = luaL_checkstring(pLuaState, 3);
    }
    else if (field == "NotIdentifiedName")
    {
        item.unidentifiedName = luaL_checkstring(pLuaState, 3);
    }
    else if (field == "Notes")
    {
        item.notes = luaL_checkstring(pLuaState, 3);
    }
    else if (field == "Value")
    {
        item.value = clampedInteger<int>(integerArgument(pLuaState, 3));
    }
    else if (field == "Mod2")
    {
        item.mod2 = std::to_string(clampedInteger<int>(integerArgument(pLuaState, 3)));
    }
    else if (field == "Mod1DiceCount" || field == "Mod1DiceSides")
    {
        int count = 0;
        int sides = 0;
        parseDice(item.mod1, count, sides);

        if (sides == 0)
        {
            // A spell reference; there are no dice to change.
            applied = false;
        }
        else
        {
            const int value = std::max(0, clampedInteger<int>(integerArgument(pLuaState, 3)));

            if (field == "Mod1DiceCount")
            {
                count = value;
            }
            else
            {
                sides = value;
            }

            const bool written = item.mod1.find_first_of("dD") != std::string::npos;
            item.mod1 = written || sides != 1
                ? std::to_string(count) + "d" + std::to_string(sides)
                : std::to_string(count);
        }
    }
    else
    {
        applied = false;
    }

    lua_pushboolean(pLuaState, applied);
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

int nativeMapStatsGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t mapId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pData == nullptr)
    {
        return 0;
    }

    const MapStats &mapStats = pData->getMapStats();

    if (field == "#")
    {
        int highestId = 0;

        for (const MapStatsEntry &entry : mapStats.getEntries())
        {
            highestId = std::max(highestId, entry.id);
        }

        lua_pushinteger(pLuaState, highestId + 1);
        return 1;
    }

    const MapStatsEntry *pEntry = mapId >= 0 ? mapStats.findById(static_cast<uint32_t>(mapId)) : nullptr;

    if (pEntry == nullptr)
    {
        return 0;
    }

    if (field == "Name")
    {
        pushString(pLuaState, pEntry->name);
    }
    else if (field == "FileName")
    {
        pushString(pLuaState, pEntry->fileName);
    }
    else if (field == "WorldId")
    {
        pushString(pLuaState, pEntry->worldId);
    }
    else if (field == "RefillDays")
    {
        lua_pushinteger(pLuaState, pEntry->respawnIntervalDays);
    }
    else if (field == "Per")
    {
        lua_pushinteger(pLuaState, pEntry->perceptionDifficulty);
    }
    else if (field == "StealPerm")
    {
        lua_pushinteger(pLuaState, pEntry->baseStealingFine);
    }
    else if (field == "Lock")
    {
        lua_pushinteger(pLuaState, pEntry->disarmDifficulty);
    }
    else if (field == "Trap")
    {
        lua_pushinteger(pLuaState, pEntry->trapDamageD20DiceCount);
    }
    else if (field == "Tres")
    {
        lua_pushinteger(pLuaState, pEntry->treasureLevel);
    }
    else if (field == "EncounterChance")
    {
        lua_pushinteger(pLuaState, pEntry->encounterChance);
    }
    else if (field == "RedbookTrack")
    {
        lua_pushinteger(pLuaState, pEntry->redbookTrack);
    }
    else
    {
        // The three random encounters: Monster1Pic, Mon1Dif, Mon1Low, Mon1Hi, EncounterChanceM1, and so on.
        const MapEncounterInfo *const encounters[3] = {&pEntry->encounter1, &pEntry->encounter2, &pEntry->encounter3};

        for (int number = 1; number <= 3; ++number)
        {
            const MapEncounterInfo &encounter = *encounters[number - 1];
            const std::string digit = std::to_string(number);

            if (field == "Monster" + digit + "Pic")
            {
                pushString(pLuaState, encounter.pictureName);
                return 1;
            }

            if (field == "Mon" + digit + "Dif")
            {
                lua_pushinteger(pLuaState, encounter.difficulty);
                return 1;
            }

            if (field == "Mon" + digit + "Low")
            {
                lua_pushinteger(pLuaState, encounter.minCount);
                return 1;
            }

            if (field == "Mon" + digit + "Hi")
            {
                lua_pushinteger(pLuaState, encounter.maxCount);
                return 1;
            }

            if (field == "EncounterChanceM" + digit)
            {
                lua_pushinteger(pLuaState, encounter.chance);
                return 1;
            }
        }

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

    if (field == "MapStatsIndex" && pHost->currentMapFileName)
    {
        const GameDataLoader *pData = dataFromHost(pLuaState);
        const MapStatsEntry *pEntry =
            pData != nullptr ? pData->getMapStats().findByFileName(pHost->currentMapFileName()) : nullptr;

        if (pEntry == nullptr)
        {
            return 0;
        }

        lua_pushinteger(pLuaState, pEntry->id);
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

// modrt.castSpell(player, spell, targetMonster, targetPlayer): has the party member cast the spell through the
// game's own spell code, with the member's own skill, cost and recovery, at a map monster or a party member
// (-1 for neither). Returns whether the cast went through, and the game's message when it did not.
int nativeCastSpell(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t memberIndex = integerArgument(pLuaState, 1);
    const int64_t spellId = integerArgument(pLuaState, 2);
    const int64_t targetActor = luaL_optinteger(pLuaState, 3, -1);
    const int64_t targetMember = luaL_optinteger(pLuaState, 4, -1);

    if (pParty == nullptr || pWorld == nullptr || pData == nullptr || memberIndex < 0 || spellId <= 0)
    {
        lua_pushboolean(pLuaState, false);
        lua_pushliteral(pLuaState, "no game in progress");
        return 2;
    }

    PartySpellCastRequest request = {};
    request.casterMemberIndex = static_cast<size_t>(memberIndex);
    request.spellId = static_cast<uint32_t>(spellId);

    if (targetActor >= 0)
    {
        request.targetActorIndex = static_cast<size_t>(targetActor);
    }

    if (targetMember >= 0)
    {
        request.targetCharacterIndex = static_cast<size_t>(targetMember);
    }

    const PartySpellCastResult result =
        PartySpellSystem::castSpell(*pParty, *pWorld, pData->getSpellTable(), request);
    lua_pushboolean(pLuaState, result.succeeded());
    pushString(pLuaState, result.statusText);
    return 2;
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

// modrt.statisticEffect(value): the effect of a statistic's value, as the game computes it (mods included).
int nativeStatisticEffect(lua_State *pLuaState)
{
    lua_pushinteger(
        pLuaState,
        GameMechanics::statisticEffect(static_cast<int>(luaL_optinteger(pLuaState, 1, 0))));
    return 1;
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
        {"buffGet", nativeBuffGet},
        {"buffSet", nativeBuffSet},
        {"playerConditionGet", nativePlayerConditionGet},
        {"playerConditionSet", nativePlayerConditionSet},
        {"playerSkillGet", nativePlayerSkillGet},
        {"playerSkillSet", nativePlayerSkillSet},
        {"partyRefreshBonuses", nativePartyRefreshBonuses},
        {"itemGet", nativeItemGet},
        {"itemSet", nativeItemSet},
        {"itemTxtGet", nativeItemTxtGet},
        {"itemTxtSet", nativeItemTxtSet},
        {"monsterTxtGet", nativeMonsterTxtGet},
        {"portraitGet", nativePortraitGet},
        {"mapStatsGet", nativeMapStatsGet},
        {"mapMonsterGet", nativeMapMonsterGet},
        {"mapMonsterSet", nativeMapMonsterSet},
        {"gameGet", nativeGameGet},
        {"castSpell", nativeCastSpell},
        {"showStatusText", nativeShowStatusText},
        {"statisticEffect", nativeStatisticEffect},
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
