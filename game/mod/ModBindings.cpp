#include "game/mod/ModBindings.h"

#include "game/data/GameDataLoader.h"
#include "game/events/EventRuntime.h"
#include "game/events/ISceneEventContext.h"
#include "game/gameplay/GameMechanics.h"
#include "game/gameplay/GameplayRuntimeInterfaces.h"
#include "game/items/ItemEnchantRuntime.h"
#include "game/items/ItemGenerator.h"
#include "game/items/PriceCalculator.h"
#include "game/mod/ModHooks.h"
#include "game/mod/ModHost.h"
#include "game/party/Party.h"
#include "game/party/PartySpellSystem.h"
#include "game/party/SkillData.h"
#include "game/tables/CharacterDollTable.h"
#include "game/tables/ClassMultiplierTable.h"
#include "game/tables/ClassSkillTable.h"
#include "game/tables/HouseTable.h"
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
#include <map>
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

namespace
{
std::map<size_t, std::string> &registeredMergeSkills()
{
    static std::map<size_t, std::string> skills;
    return skills;
}
}

void registerMergeSkill(size_t skillId, const std::string &skillName)
{
    if (skillId >= MergeSkillCount)
    {
        registeredMergeSkills()[skillId] = skillName;
    }
}

std::vector<size_t> mergeSkillIds()
{
    std::vector<size_t> ids;

    for (size_t skillId = 0; skillId < MergeSkillCount; ++skillId)
    {
        ids.push_back(skillId);
    }

    for (const auto &[skillId, skillName] : registeredMergeSkills())
    {
        ids.push_back(skillId);
    }

    return ids;
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
    if (skillId < MergeSkillCount)
    {
        return names[skillId];
    }

    const std::map<size_t, std::string>::const_iterator it = registeredMergeSkills().find(skillId);
    return it != registeredMergeSkills().end() ? it->second.c_str() : nullptr;
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
// The players the natives take: a party slot, or InnPlayerBase + a place in the Adventurer's Inn (Party.PlayersArray
// reaches the characters waiting there so).
constexpr int64_t InnPlayerBase = 1000;

Character *playerArgument(lua_State *pLuaState, int index)
{
    Party *pParty = partyFromHost(pLuaState);

    if (pParty == nullptr)
    {
        return nullptr;
    }

    const int64_t memberIndex = integerArgument(pLuaState, index);

    if (memberIndex >= InnPlayerBase)
    {
        return pParty->adventurersInnCharacter(static_cast<size_t>(memberIndex - InnPlayerBase));
    }

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

    // The enemy detector, the portraits' gems: red with a hostile monster within reach of a blow (307.2), yellow
    // within 5120, green beyond or beyond the map's engagement range (MAW's Beyond Madness ends its death timer when
    // neither is lit).
    if (field == "EnemyDetectorRed" || field == "EnemyDetectorYellow")
    {
        const IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
        float nearest = std::numeric_limits<float>::max();

        if (pWorld != nullptr)
        {
            for (size_t actorIndex = 0; actorIndex < pWorld->mapActorCount(); ++actorIndex)
            {
                GameplayRuntimeActorState actor = {};

                if (!pWorld->actorRuntimeState(actorIndex, actor) || actor.isDead || actor.isInvisible
                    || !actor.hostileToParty)
                {
                    continue;
                }

                const float deltaX = actor.preciseX - pWorld->partyX();
                const float deltaY = actor.preciseY - pWorld->partyY();
                const float deltaZ = actor.preciseZ - pWorld->partyFootZ();
                const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
                nearest = std::min(nearest, std::max(0.0f, distance - static_cast<float>(actor.radius)));
            }
        }

        const bool inRange = pWorld != nullptr && nearest < pWorld->partyEngagementRange();
        const bool red = inRange && nearest < 307.2f;
        lua_pushboolean(pLuaState, field == "EnemyDetectorRed" ? red : inRange && !red && nearest < 5120.0f);
        return 1;
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
    else if (field == "InnCount")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pParty->adventurersInnMembers().size()));
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
    {"Exp", &Character::experience},
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
    // The spell the quick spell key casts, and the one the attack key casts in place of attacking (0: none).
    if (field == "QuickSpell" || field == "AttackSpell")
    {
        const GameDataLoader *pData = dataFromHost(pLuaState);
        const std::string &spellName =
            field == "QuickSpell" ? pCharacter->quickSpellName : pCharacter->attackSpellName;
        const SpellEntry *pSpell = pData != nullptr && !spellName.empty()
            ? pData->getSpellTable().findByName(spellName)
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

    // How long the character is still busy after their last action, in the original's ticks. The engine keeps
    // seconds of real time; a tick there is 1/128 second of game time, which the engine plays 2.13 times slower,
    // so 60 ticks to the second.
    if (field == "RecoveryDelay")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(std::lround(pCharacter->recoverySecondsRemaining * 60.0f)));
        return 1;
    }

    // The character's merchant and disarm-trap totals as the game uses them, the mod's word included.
    if (field == "GetMerchant")
    {
        lua_pushinteger(pLuaState, PriceCalculator::playerMerchant(pCharacter));
        return 1;
    }

    if (field == "GetDisarmTrap")
    {
        lua_pushinteger(pLuaState, GameMechanics::resolveCharacterDisarmTrapValue(*pCharacter));
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

    // Awake and able to act: not asleep, paralyzed, unconscious, dead, stoned or eradicated.
    if (field == "IsConscious")
    {
        lua_pushboolean(pLuaState, GameMechanics::canAct(*pCharacter));
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

    // Resistance with everything counted, as the character sheet shows it.
    if (field.compare(0, 13, "GetResistance") == 0)
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
        const std::string element = field.substr(13);
        const CharacterSheetValue *pValue =
            element == "Fire" ? &summary.fireResistance
            : element == "Air" ? &summary.airResistance
            : element == "Water" ? &summary.waterResistance
            : element == "Earth" ? &summary.earthResistance
            : element == "Mind" ? &summary.mindResistance
            : element == "Body" ? &summary.bodyResistance
            : nullptr;

        if (pValue == nullptr)
        {
            return 0;
        }

        lua_pushinteger(pLuaState, pValue->actual);
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
        else if (profile.rangedAttackBonus)
        {
            lua_pushinteger(pLuaState, *profile.rangedAttackBonus);
        }
        else
        {
            // With nothing to shoot with the original still answers, from accuracy and the bonuses.
            const int accuracy = static_cast<int>(pCharacter->accuracy)
                + pCharacter->permanentBonuses.accuracy
                + pCharacter->magicalBonuses.accuracy;
            lua_pushinteger(
                pLuaState,
                GameMechanics::statisticEffect(accuracy)
                    + pCharacter->permanentBonuses.rangedAttack
                    + pCharacter->magicalBonuses.rangedAttack);
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

    if (field == "RecoveryDelay")
    {
        pCharacter->recoverySecondsRemaining =
            static_cast<float>(std::clamp<int64_t>(integerArgument(pLuaState, 3), 0, 1000000)) / 60.0f;
        lua_pushboolean(pLuaState, true);
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

    if (field == "QuickSpell" || field == "AttackSpell")
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

        (field == "QuickSpell" ? pCharacter->quickSpellName : pCharacter->attackSpellName) =
            pSpell != nullptr ? pSpell->lookupName : std::string();
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    if (field == "Name")
    {
        pCharacter->name = luaL_checkstring(pLuaState, 3);
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    // A new class by number (MAW promotes within a line of classes this way): the character takes its name, and
    // hit points and spell points follow the class's factors.
    if (field == "Class")
    {
        const Party *pParty = partyFromHost(pLuaState);
        const ClassSkillTable *pClassSkillTable = pParty != nullptr ? pParty->classSkillTable() : nullptr;
        const int64_t classId = integerArgument(pLuaState, 3);
        const std::optional<std::string> className = pClassSkillTable != nullptr && classId >= 0
            ? pClassSkillTable->classNameForId(static_cast<uint32_t>(classId))
            : std::nullopt;

        if (!className)
        {
            lua_pushboolean(pLuaState, false);
            return 1;
        }

        pCharacter->className = *className;
        GameMechanics::refreshCharacterBaseResources(*pCharacter, false, pParty->classMultiplierTable());
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

// modrt.mercenaryRecruits(): the mercenaries the game generated for the current map's houses, as a list of
// {Class = name, Level = n, SkillPoints = n} (for checks; MAW reads its own roster otherwise).
int nativeMercenaryRecruits(lua_State *pLuaState)
{
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const EventRuntimeState *pState = pWorld != nullptr ? pWorld->eventRuntimeState() : nullptr;
    lua_newtable(pLuaState);

    if (pState == nullptr)
    {
        return 1;
    }

    std::map<uint32_t, const EventRuntimeState::GeneratedMercenaryRecruit *> recruits;

    for (const auto &[npcId, recruit] : pState->generatedMercenaryRecruitsByNpcId)
    {
        recruits[npcId] = &recruit;
    }

    lua_Integer index = 1;

    for (const auto &[npcId, pRecruit] : recruits)
    {
        (void)npcId;
        lua_newtable(pLuaState);
        lua_pushstring(pLuaState, pRecruit->character.className.c_str());
        lua_setfield(pLuaState, -2, "Class");
        lua_pushinteger(pLuaState, pRecruit->character.level);
        lua_setfield(pLuaState, -2, "Level");
        lua_pushinteger(pLuaState, pRecruit->character.skillPoints);
        lua_setfield(pLuaState, -2, "SkillPoints");
        lua_rawseti(pLuaState, -2, index++);
    }

    return 1;
}

// modrt.hudTextSet(id, text, x, y, screen, alignLeft, width, visible): a mod's screen text (CustomUI.CreateText).
int nativeHudTextSet(lua_State *pLuaState)
{
    ModHudText text = {};
    const int id = static_cast<int>(luaL_checkinteger(pLuaState, 1));
    text.text = luaL_optstring(pLuaState, 2, "");
    text.x = static_cast<float>(luaL_optnumber(pLuaState, 3, 0));
    text.y = static_cast<float>(luaL_optnumber(pLuaState, 4, 0));
    text.screen = static_cast<int>(luaL_optinteger(pLuaState, 5, 0));
    text.alignLeft = lua_isnoneornil(pLuaState, 6) || lua_toboolean(pLuaState, 6) != 0;
    text.width = static_cast<float>(luaL_optnumber(pLuaState, 7, 0));
    text.visible = lua_isnoneornil(pLuaState, 8) || lua_toboolean(pLuaState, 8) != 0;
    setModHudText(id, text);
    return 0;
}

// modrt.setRule(name, value): sets one of the engine's native rules a mod changes (modRule in ModHooks.h lists
// them); no value restores the engine's own.
int nativeSetRule(lua_State *pLuaState)
{
    const std::string name = luaL_checkstring(pLuaState, 1);
    const std::string value = lua_isstring(pLuaState, 2) ? lua_tostring(pLuaState, 2) : std::string();
    setModRule(name, value);
    return 0;
}

// modrt.playerSkillCap(player, skill): the highest mastery (0 none to 4 grandmaster) the character's class and race
// let them reach in the skill (named as playerSkillGet names it), as teachers ask it.
int nativePlayerSkillCap(lua_State *pLuaState)
{
    const Character *pCharacter = playerArgument(pLuaState, 1);
    const std::string skillName = canonicalSkillName(luaL_checkstring(pLuaState, 2));
    const Party *pParty = partyFromHost(pLuaState);
    const ClassSkillTable *pClassSkillTable = pParty != nullptr ? pParty->classSkillTable() : nullptr;

    if (pCharacter == nullptr || pClassSkillTable == nullptr)
    {
        return 0;
    }

    lua_pushinteger(
        pLuaState,
        static_cast<lua_Integer>(
            pClassSkillTable->getEffectiveCap(pCharacter->className, pCharacter->raceId, skillName)));
    return 1;
}

// Recomputes every member's bonuses now, which runs the stat events (mods call this after changing what the
// events depend on; the game itself does it whenever equipment or buffs change).
int nativePartyRefreshBonuses(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    IModHooks *pModHooks = activeModHooks();

    // Asked for from inside a rebuild, it is put off until that one is done.
    if (pParty != nullptr && (pModHooks == nullptr || !pModHooks->deferBonusRebuild()))
    {
        pParty->rebuildMagicalBonusesFromBuffs();
    }

    return 0;
}

// modrt.questBitGet(bit): whether the party has quest bit `bit` set (Party.QBits[bit]).
int nativeQuestBitGet(lua_State *pLuaState)
{
    const Party *pParty = partyFromHost(pLuaState);
    const int64_t bit = integerArgument(pLuaState, 1);
    lua_pushboolean(pLuaState, pParty != nullptr && bit > 0 && pParty->hasQuestBit(static_cast<uint32_t>(bit)));
    return 1;
}

// modrt.questBitSet(bit, value): sets or clears quest bit `bit`; whether there was a party to set it on.
int nativeQuestBitSet(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    const int64_t bit = integerArgument(pLuaState, 1);

    if (pParty == nullptr || bit <= 0)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    pParty->setQuestBit(static_cast<uint32_t>(bit), lua_toboolean(pLuaState, 2) != 0);
    lua_pushboolean(pLuaState, true);
    return 1;
}

// modrt.playerSpellGet(player, spell): whether the character knows the spell (Party[i].Spells[spell]).
int nativePlayerSpellGet(lua_State *pLuaState)
{
    const Character *pCharacter = playerArgument(pLuaState, 1);
    const int64_t spellId = integerArgument(pLuaState, 2);
    lua_pushboolean(pLuaState, pCharacter != nullptr && spellId > 0 && pCharacter->knowsSpell(static_cast<uint32_t>(spellId)));
    return 1;
}

// modrt.playerSpellSet(player, spell, known): the character learns or forgets the spell.
int nativePlayerSpellSet(lua_State *pLuaState)
{
    Character *pCharacter = playerArgument(pLuaState, 1);
    const int64_t spellId = integerArgument(pLuaState, 2);

    if (pCharacter == nullptr || spellId <= 0)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    if (lua_toboolean(pLuaState, 3))
    {
        pCharacter->learnSpell(static_cast<uint32_t>(spellId));
    }
    else
    {
        pCharacter->forgetSpell(static_cast<uint32_t>(spellId));
    }

    lua_pushboolean(pLuaState, true);
    return 1;
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
    InventoryItem *pInventoryItem = nullptr;
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
InventoryItem *g_pModEventItem = nullptr;

void itemViewOfInventoryItem(InventoryItem &item, ItemView &view)
{
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
}

bool itemViewArgument(lua_State *pLuaState, ItemView &view)
{
    const std::string group = luaL_checkstring(pLuaState, 2);

    // The item the player holds on the mouse pointer (Mouse.Item).
    if (group == "held")
    {
        const ModHost *pHost = hostFromUpvalue(pLuaState);
        InventoryItem *pHeld = pHost != nullptr && pHost->heldItem ? pHost->heldItem() : nullptr;

        if (pHeld == nullptr)
        {
            return false;
        }

        itemViewOfInventoryItem(*pHeld, view);
        return true;
    }

    // The item of the event being handled; it belongs to no party member.
    if (group == "event")
    {
        if (g_pModEventItem == nullptr)
        {
            return false;
        }

        itemViewOfInventoryItem(*g_pModEventItem, view);
        return true;
    }

    Character *pCharacter = playerArgument(pLuaState, 1);
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

        itemViewOfInventoryItem(pCharacter->inventory[static_cast<size_t>(index)], view);
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

// A potion's or a reagent's Bonus is its power, as the original keeps it; the game keeps that power where it keeps
// an enchantment's.
bool bonusIsPower(lua_State *pLuaState, uint32_t itemId)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const ItemDefinition *pDefinition = pData != nullptr ? pData->getItemTable().get(itemId) : nullptr;
    return pDefinition != nullptr && (pDefinition->equipStat == "Bottle" || pDefinition->equipStat == "Reagent");
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
        lua_pushinteger(
            pLuaState,
            bonusIsPower(pLuaState, *view.pItemId) ? *view.pStandardEnchantPower : *view.pStandardEnchantId);
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
    else if (field == "Value")
    {
        // What the game takes the item to be worth (Item:GetValue), a mod's own valuation included.
        const GameDataLoader *pData = dataFromHost(pLuaState);
        const ItemDefinition *pDefinition = pData != nullptr ? pData->getItemTable().get(*view.pItemId) : nullptr;

        if (pDefinition == nullptr)
        {
            return 0;
        }

        // An equipped item is kept as its slot's fields; priced as the item they make up.
        InventoryItem item = {};
        item.objectDescriptionId = *view.pItemId;
        item.identified = *view.pIdentified;
        item.broken = *view.pBroken;
        item.stolen = *view.pStolen;
        item.standardEnchantId = *view.pStandardEnchantId;
        item.standardEnchantPower = *view.pStandardEnchantPower;
        item.specialEnchantId = *view.pSpecialEnchantId;
        item.currentCharges = *view.pCurrentCharges;
        item.maxCharges = *view.pMaxCharges;
        item.modData = *view.pModData;

        if (view.pInventoryItem != nullptr)
        {
            item = *view.pInventoryItem;
        }

        lua_pushinteger(
            pLuaState,
            ItemEnchantRuntime::itemValue(
                item,
                *pDefinition,
                &pData->getStandardItemEnchantTable(),
                &pData->getSpecialItemEnchantTable()));
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
        (bonusIsPower(pLuaState, *view.pItemId) ? *view.pStandardEnchantPower : *view.pStandardEnchantId) =
            clampedInteger<uint16_t>(integerArgument(pLuaState, 5));
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
    else if (field == "Number")
    {
        // The item becomes one of another kind (MAW turns a drop into an artifact or a crafting material this
        // way). Only a kind the item table has; what follows from the kind goes with it.
        const GameDataLoader *pData = dataFromHost(pLuaState);
        const int64_t itemId = integerArgument(pLuaState, 5);
        const ModHost *pHost = hostFromUpvalue(pLuaState);
        const InventoryItem *pHeld = pHost != nullptr && pHost->heldItem ? pHost->heldItem() : nullptr;

        // The held item made nothing: it is used up (MAW's Mouse.Item.Number = 0). The game takes it off the pointer
        // when the event is over.
        if (itemId == 0 && view.pInventoryItem != nullptr && view.pInventoryItem == pHeld)
        {
            view.pInventoryItem->objectDescriptionId = 0;
        }
        else if (pData == nullptr || itemId <= 0
            || pData->getItemTable().get(static_cast<uint32_t>(itemId)) == nullptr)
        {
            handled = false;
        }
        else if (view.pInventoryItem != nullptr)
        {
            ItemGenerator::applyItemKind(*view.pInventoryItem, static_cast<uint32_t>(itemId), pData->getItemTable());
        }
        else
        {
            *view.pItemId = static_cast<uint32_t>(itemId);
        }
    }
    else
    {
        handled = false;
    }

    lua_pushboolean(pLuaState, handled);
    return 1;
}

// modrt.itemRandomize(player, group, index, strength): the item becomes a new random one of that treasure level
// (1 to 6), made by the game's own generator, the mod's item events included. It keeps its place in the backpack.
// Returns whether an item was made.
int nativeItemRandomize(lua_State *pLuaState)
{
    ItemView view = {};
    const GameDataLoader *pData = dataFromHost(pLuaState);

    if (pData == nullptr || !itemViewArgument(pLuaState, view))
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    static std::mt19937 rng{std::random_device{}()};
    ItemGenerationRequest request = {};
    request.treasureLevel = static_cast<int>(std::clamp<int64_t>(integerArgument(pLuaState, 4), 1, 6));
    request.allowRareItems = false;
    const std::optional<InventoryItem> generated = ItemGenerator::generateRandomInventoryItem(
        pData->getItemTable(),
        pData->getStandardItemEnchantTable(),
        pData->getSpecialItemEnchantTable(),
        request,
        partyFromHost(pLuaState),
        rng);

    if (!generated)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    if (view.pInventoryItem != nullptr)
    {
        const uint8_t gridX = view.pInventoryItem->gridX;
        const uint8_t gridY = view.pInventoryItem->gridY;
        *view.pInventoryItem = *generated;
        view.pInventoryItem->gridX = gridX;
        view.pInventoryItem->gridY = gridY;
    }
    else
    {
        *view.pItemId = generated->objectDescriptionId;
        *view.pIdentified = generated->identified;
        *view.pBroken = generated->broken;
        *view.pStolen = generated->stolen;
        *view.pStandardEnchantId = generated->standardEnchantId;
        *view.pStandardEnchantPower = generated->standardEnchantPower;
        *view.pSpecialEnchantId = generated->specialEnchantId;
        *view.pCurrentCharges = generated->currentCharges;
        *view.pMaxCharges = generated->maxCharges;
        *view.pModData = generated->modData;
    }

    lua_pushboolean(pLuaState, true);
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
    else if (field == "Picture")
    {
        const ModHost *pHost = hostFromUpvalue(pLuaState);

        if (pHost != nullptr && pHost->pAssetFileSystem != nullptr)
        {
            ItemTable::setItemIcon(item, luaL_checkstring(pLuaState, 3), *pHost->pAssetFileSystem);
        }
        else
        {
            item.iconName = luaL_checkstring(pLuaState, 3);
        }
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

// --- monsters --------------------------------------------------------------------------------------------------------
//
// A row of the monster table, under the field names of the original's record. Nested parts are flattened for the
// natives and put back together by the scripts' side: Attack1.Type is "Attack1Type", Resistances[kind] is
// "Resistance<kind>" with kind numbered by const.Damage.

// The resistance of a row for a const.Damage number, or null: the original has no magic or energy resistance for
// monsters.
template <typename StatsType>
auto monsterResistanceField(StatsType &stats, int64_t kind) -> decltype(&stats.fireResistance)
{
    switch (kind)
    {
        case 0: return &stats.fireResistance;
        case 1: return &stats.airResistance;
        case 2: return &stats.waterResistance;
        case 3: return &stats.earthResistance;
        case 4: return &stats.physicalResistance;
        case 6: return &stats.spiritResistance;
        case 7: return &stats.mindResistance;
        case 8: return &stats.bodyResistance;
        case 9: return &stats.lightResistance;
        case 10: return &stats.darkResistance;
        default: return nullptr;
    }
}

// The plain whole-number fields a script may both read and write.
template <typename StatsType>
auto monsterNumberField(StatsType &stats, const std::string &field) -> decltype(&stats.level)
{
    static const std::string resistancePrefix = "Resistance";

    if (field == "Level")
    {
        return &stats.level;
    }

    if (field == "FullHP" || field == "FullHitPoints" || field == "HP" || field == "HitPoints")
    {
        return &stats.hitPoints;
    }

    if (field == "ArmorClass")
    {
        return &stats.armorClass;
    }

    if (field == "Experience" || field == "Exp")
    {
        return &stats.experience;
    }

    if (field == "MoveSpeed")
    {
        return &stats.speed;
    }

    if (field == "RecoveryTime")
    {
        return &stats.recovery;
    }

    if (field == "HostileType")
    {
        return &stats.hostility;
    }

    if (field == "BonusMul")
    {
        return &stats.specialAttackLevel;
    }

    if (field == "Attack1DamageDiceCount")
    {
        return &stats.attack1Damage.diceRolls;
    }

    if (field == "Attack1DamageDiceSides")
    {
        return &stats.attack1Damage.diceSides;
    }

    if (field == "Attack1DamageAdd")
    {
        return &stats.attack1Damage.bonus;
    }

    if (field == "Attack2DamageDiceCount")
    {
        return &stats.attack2Damage.diceRolls;
    }

    if (field == "Attack2DamageDiceSides")
    {
        return &stats.attack2Damage.diceSides;
    }

    if (field == "Attack2DamageAdd")
    {
        return &stats.attack2Damage.bonus;
    }

    if (field == "Attack2Chance")
    {
        return &stats.attack2Chance;
    }

    if (field == "SpellChance")
    {
        return &stats.spell1UseChance;
    }

    if (field == "Spell2Chance")
    {
        return &stats.spell2UseChance;
    }

    if (field == "TreasureDiceCount")
    {
        return &stats.loot.goldDiceRolls;
    }

    if (field == "TreasureDiceSides")
    {
        return &stats.loot.goldDiceSides;
    }

    if (field == "TreasureItemPercent")
    {
        return &stats.loot.itemChance;
    }

    if (field == "TreasureItemLevel")
    {
        return &stats.loot.itemLevel;
    }

    if (field.compare(0, resistancePrefix.size(), resistancePrefix) == 0)
    {
        try
        {
            return monsterResistanceField(stats, std::stoll(field.substr(resistancePrefix.size())));
        }
        catch (const std::exception &)
        {
            return nullptr;
        }
    }

    return nullptr;
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

    const MonsterTable &monsterTable = pData->getMonsterTable();

    if (field == "#")
    {
        int highestId = 0;

        for (const std::pair<const int, MonsterTable::MonsterStatsEntry> &entry : monsterTable.statsEntries())
        {
            highestId = std::max(highestId, entry.first);
        }

        lua_pushinteger(pLuaState, highestId + 1);
        return 1;
    }

    const MonsterTable::MonsterStatsEntry *pStats = monsterTable.findStatsById(clampedInteger<int16_t>(monsterId));

    if (pStats == nullptr)
    {
        return 0;
    }

    if (const int *pNumber = monsterNumberField(*pStats, field))
    {
        lua_pushinteger(pLuaState, *pNumber);
        return 1;
    }

    const auto spellNumber =
        [pData](const std::string &spellName) -> lua_Integer
        {
            const SpellEntry *pSpell = !spellName.empty() ? pData->getSpellTable().findByName(spellName) : nullptr;
            return pSpell != nullptr ? pSpell->id : 0;
        };

    if (field == "Name")
    {
        pushString(pLuaState, pStats->name);
    }
    else if (field == "Picture")
    {
        pushString(pLuaState, pStats->pictureName);
    }
    else if (field == "Id")
    {
        lua_pushinteger(pLuaState, pStats->id);
    }
    else if (field == "AIType")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pStats->aiType));
    }
    else if (field == "Fly")
    {
        lua_pushinteger(pLuaState, pStats->canFly ? 1 : 0);
    }
    else if (field == "Prefers")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pStats->attackPreferences));
    }
    else if (field == "Bonus")
    {
        lua_pushinteger(pLuaState, mm8MonsterBonusNumber(pStats->specialAttackKind));
    }
    else if (field == "Attack1Type")
    {
        lua_pushinteger(pLuaState, mm8DamageKind(GameMechanics::parseCombatDamageType(pStats->attack1Type)));
    }
    else if (field == "Attack2Type")
    {
        lua_pushinteger(pLuaState, mm8DamageKind(GameMechanics::parseCombatDamageType(pStats->attack2Type)));
    }
    else if (field == "Attack1Missile")
    {
        // The original holds the missile's kind here; scripts ask whether there is one.
        lua_pushinteger(pLuaState, pStats->attack1HasMissile ? 1 : 0);
    }
    else if (field == "Attack2Missile")
    {
        lua_pushinteger(pLuaState, pStats->attack2HasMissile ? 1 : 0);
    }
    else if (field == "Spell")
    {
        lua_pushinteger(pLuaState, pStats->hasSpell1 ? spellNumber(pStats->spell1Name) : 0);
    }
    else if (field == "Spell2")
    {
        lua_pushinteger(pLuaState, pStats->hasSpell2 ? spellNumber(pStats->spell2Name) : 0);
    }
    else if (field == "SpellSkillLevel")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pStats->spell1SkillLevel));
    }
    else if (field == "SpellSkillMastery")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pStats->spell1SkillMastery));
    }
    else if (field == "Spell2SkillLevel")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pStats->spell2SkillLevel));
    }
    else if (field == "Spell2SkillMastery")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pStats->spell2SkillMastery));
    }
    else if (field == "TreasureItemType")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pStats->loot.itemKind));
    }
    else
    {
        return 0;
    }

    return 1;
}

// modrt.monsterTxtSet(monster, field, value): changes the game's monster table, for every monster of that kind
// and until the game is closed, as a write to Game.MonstersTxt[] does in the original. The engine reads level,
// armor class, experience, resistances and attack dice from the table whenever it needs them; hit points are
// taken from it when a monster appears. Returns false for a field that cannot be changed here.
int nativeMonsterTxtSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t monsterId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);
    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    MonsterTable::MonsterStatsEntry *pStats = pData != nullptr
        ? const_cast<MonsterTable &>(pData->getMonsterTable()).modStatsById(clampedInteger<int16_t>(monsterId))
        : nullptr;
    int *pNumber = pStats != nullptr ? monsterNumberField(*pStats, field) : nullptr;

    if (pNumber != nullptr)
    {
        *pNumber = clampedInteger<int>(integerArgument(pLuaState, 3));
    }
    else if (pStats != nullptr && field == "AIType")
    {
        // Read when a monster appears on a map; monsters already there keep the behaviour they have.
        pStats->aiType =
            static_cast<MonsterTable::MonsterAiType>(std::clamp<int64_t>(integerArgument(pLuaState, 3), 0, 3));
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    lua_pushboolean(pLuaState, pNumber != nullptr);
    return 1;
}

// modrt.placeMonGet(index): the name a placed monster goes by, "" when the index has none; with "#", how many
// there are.
int nativePlaceMonGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);

    if (pData == nullptr)
    {
        return 0;
    }

    if (lua_type(pLuaState, 1) == LUA_TSTRING)
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pData->getMonsterTable().uniqueNameCount()));
        return 1;
    }

    const std::optional<std::string> name =
        pData->getMonsterTable().getUniqueName(clampedInteger<int32_t>(integerArgument(pLuaState, 1)));
    pushString(pLuaState, name.value_or(std::string()));
    return 1;
}

int nativePlaceMonSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t index = integerArgument(pLuaState, 1);
    size_t length = 0;
    const char *pName = luaL_checklstring(pLuaState, 2, &length);
    const bool written = pData != nullptr
        && const_cast<MonsterTable &>(pData->getMonsterTable())
            .setUniqueName(clampedInteger<int32_t>(index), std::string(pName, length));
    lua_pushboolean(pLuaState, written);
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

// modrt.mapStatsSet(map, field, value): changes the game's map table, until the game is closed, as a write to
// Game.MapStats[] does in the original. The game reads a map's refill days when the party enters it and its
// encounters when monsters are placed. Returns false for a field that cannot be changed here.
int nativeMapStatsSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t mapId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);
    const MapStatsEntry *pConstEntry =
        pData != nullptr && mapId >= 0 ? pData->getMapStats().findById(static_cast<uint32_t>(mapId)) : nullptr;

    if (pConstEntry == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    MapStatsEntry &entry = const_cast<MapStatsEntry &>(*pConstEntry);
    int *pNumber = nullptr;

    if (field == "RefillDays")
    {
        pNumber = &entry.respawnIntervalDays;
    }
    else if (field == "Per")
    {
        pNumber = &entry.perceptionDifficulty;
    }
    else if (field == "StealPerm")
    {
        pNumber = &entry.baseStealingFine;
    }
    else if (field == "Lock")
    {
        pNumber = &entry.disarmDifficulty;
    }
    else if (field == "Trap")
    {
        pNumber = &entry.trapDamageD20DiceCount;
    }
    else if (field == "Tres")
    {
        pNumber = &entry.treasureLevel;
    }
    else if (field == "EncounterChance")
    {
        pNumber = &entry.encounterChance;
    }
    else if (field == "RedbookTrack")
    {
        pNumber = &entry.redbookTrack;
    }
    else
    {
        MapEncounterInfo *const encounters[3] = {&entry.encounter1, &entry.encounter2, &entry.encounter3};

        for (int number = 1; number <= 3 && pNumber == nullptr; ++number)
        {
            MapEncounterInfo &encounter = *encounters[number - 1];
            const std::string digit = std::to_string(number);

            if (field == "Monster" + digit + "Pic")
            {
                encounter.pictureName = luaL_checkstring(pLuaState, 3);
                lua_pushboolean(pLuaState, true);
                return 1;
            }

            if (field == "Mon" + digit + "Dif")
            {
                pNumber = &encounter.difficulty;
            }
            else if (field == "Mon" + digit + "Low")
            {
                pNumber = &encounter.minCount;
            }
            else if (field == "Mon" + digit + "Hi")
            {
                pNumber = &encounter.maxCount;
            }
            else if (field == "EncounterChanceM" + digit)
            {
                pNumber = &encounter.chance;
            }
        }
    }

    if (pNumber != nullptr)
    {
        *pNumber = clampedInteger<int>(integerArgument(pLuaState, 3));
    }

    lua_pushboolean(pLuaState, pNumber != nullptr);
    return 1;
}

// --- classes ---------------------------------------------------------------------------------------------------------
//
// The original keeps a class's hit and spell points in parallel arrays indexed by class (Game.Classes.HPFactor[],
// HPBase[], SPFactor[], SPBase[], SPStats[]); here they are fields of one row per class, found by the class's
// number.

const ClassMultiplierEntry *classEntryById(const GameDataLoader &data, int64_t classId)
{
    if (classId < 0)
    {
        return nullptr;
    }

    const std::optional<std::string> className =
        data.getClassSkillTable().classNameForId(static_cast<uint32_t>(classId));
    return className ? data.getClassMultiplierTable().get(*className) : nullptr;
}

template <typename EntryType>
auto classNumberField(EntryType &entry, const std::string &field) -> decltype(&entry.baseHealth)
{
    if (field == "HPFactor")
    {
        return &entry.healthPerLevel;
    }

    if (field == "HPBase")
    {
        return &entry.baseHealth;
    }

    if (field == "SPFactor")
    {
        return &entry.manaPerLevel;
    }

    if (field == "SPBase")
    {
        return &entry.baseMana;
    }

    return nullptr;
}

// modrt.classGet(class, field); with "#" as the field, one more than the highest class number.
int nativeClassGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t classId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pData == nullptr)
    {
        return 0;
    }

    if (field == "#")
    {
        uint32_t highestId = 0;
        bool any = false;

        for (const std::pair<const std::string, ClassMultiplierEntry> &entry :
             pData->getClassMultiplierTable().entries())
        {
            if (pData->getClassSkillTable().classIdForName(entry.first))
            {
                highestId = std::max(highestId, *pData->getClassSkillTable().classIdForName(entry.first));
                any = true;
            }
        }

        lua_pushinteger(pLuaState, any ? static_cast<lua_Integer>(highestId) + 1 : 0);
        return 1;
    }

    const ClassMultiplierEntry *pEntry = classEntryById(*pData, classId);

    if (pEntry == nullptr)
    {
        return 0;
    }

    if (const int *pNumber = classNumberField(*pEntry, field))
    {
        lua_pushinteger(pLuaState, *pNumber);
    }
    else if (field == "SPStats")
    {
        // 0 none, 1 intellect, 2 personality, 3 both, as in the original.
        lua_pushinteger(pLuaState, std::min<lua_Integer>(static_cast<lua_Integer>(pEntry->manaMode), 3));
    }
    else if (field == "Name")
    {
        pushString(pLuaState, pEntry->displayName.empty() ? pEntry->className : pEntry->displayName);
    }
    else if (field == "Kind")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pEntry->classKind));
    }
    else if (field == "Step")
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(pEntry->promotionStep));
    }
    else
    {
        return 0;
    }

    return 1;
}

// modrt.classSet(class, field, value): changes the game's class table, as a write to Game.Classes does in the
// original. A character's base hit and spell points follow at the next bonus rebuild.
int nativeClassSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const std::string field = luaL_checkstring(pLuaState, 2);
    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    ClassMultiplierEntry *pEntry = pData != nullptr
        ? const_cast<ClassMultiplierEntry *>(classEntryById(*pData, integerArgument(pLuaState, 1)))
        : nullptr;
    bool written = false;

    if (pEntry != nullptr)
    {
        if (int *pNumber = classNumberField(*pEntry, field))
        {
            *pNumber = clampedInteger<int>(integerArgument(pLuaState, 3));
            written = true;
        }
        else if (field == "SPStats")
        {
            pEntry->manaMode = static_cast<ClassManaMode>(std::clamp<int64_t>(integerArgument(pLuaState, 3), 0, 3));
            written = true;
        }
    }

    lua_pushboolean(pLuaState, written);
    return 1;
}

// modrt.classSkillGet(class, skillName): the highest mastery the class can reach in the skill, 0 for none.
int nativeClassSkillGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t classId = integerArgument(pLuaState, 1);
    const std::string skillName = canonicalSkillName(luaL_checkstring(pLuaState, 2));
    const std::optional<std::string> className = pData != nullptr && classId >= 0
        ? pData->getClassSkillTable().classNameForId(static_cast<uint32_t>(classId))
        : std::nullopt;

    if (!className)
    {
        return 0;
    }

    lua_pushinteger(
        pLuaState,
        static_cast<lua_Integer>(pData->getClassSkillTable().getClassCap(*className, skillName)));
    return 1;
}

// The texts of a skill by part, as the original keeps them: 0 its name, 1 its description, 2 to 5 what Normal, Expert,
// Master and Grandmaster give. They are the texts of the skill's information box and of its name on the skill page.
std::string *skillTextField(SkillInspectEntry &entry, int64_t part)
{
    switch (part)
    {
        case 1: return &entry.description;
        case 2: return &entry.normalDescription;
        case 3: return &entry.expertDescription;
        case 4: return &entry.masterDescription;
        case 5: return &entry.grandmasterDescription;
        default: return nullptr;
    }
}

// modrt.skillTextGet(skill, part): see skillTextField; part 0 is the name the game shows.
int nativeSkillTextGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t skillId = integerArgument(pLuaState, 1);
    const int64_t part = integerArgument(pLuaState, 2);

    if (pData == nullptr || skillId < 0 || mergeSkillName(static_cast<size_t>(skillId)) == nullptr)
    {
        return 0;
    }

    const std::string skillName = mergeSkillName(static_cast<size_t>(skillId));

    if (part == 0)
    {
        pushString(pLuaState, displaySkillName(skillName));
        return 1;
    }

    const SkillInspectEntry *pEntry = pData->getCharacterInspectTable().getSkill(skillName);
    SkillInspectEntry empty = {};
    const std::string *pText = skillTextField(pEntry != nullptr ? const_cast<SkillInspectEntry &>(*pEntry) : empty, part);

    if (pText == nullptr)
    {
        return 0;
    }

    pushString(pLuaState, *pText);
    return 1;
}

// modrt.skillTextSet(skill, part, text): the game shows the text from then on. A name renames the skill everywhere
// the game names it.
int nativeSkillTextSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t skillId = integerArgument(pLuaState, 1);
    const int64_t part = integerArgument(pLuaState, 2);
    size_t length = 0;
    const char *pText = luaL_checklstring(pLuaState, 3, &length);

    if (pData == nullptr || skillId < 0 || mergeSkillName(static_cast<size_t>(skillId)) == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    const std::string skillName = mergeSkillName(static_cast<size_t>(skillId));
    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    SkillInspectEntry &entry =
        const_cast<CharacterInspectTable &>(pData->getCharacterInspectTable()).skillForWrite(skillName);

    if (part == 0)
    {
        setDisplaySkillNameOverride(skillName, std::string(pText, length));
        entry.name = std::string(pText, length);
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    std::string *pField = skillTextField(entry, part);

    if (pField == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    pField->assign(pText, length);
    lua_pushboolean(pLuaState, true);
    return 1;
}

// modrt.classDescription(class[, text]): the description of a class (Game.ClassDescriptions), set when text is given.
int nativeClassDescription(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t classId = integerArgument(pLuaState, 1);
    const std::optional<std::string> className = pData != nullptr && classId >= 0
        ? pData->getClassSkillTable().classNameForId(static_cast<uint32_t>(classId))
        : std::nullopt;

    if (!className)
    {
        return 0;
    }

    if (lua_isstring(pLuaState, 2))
    {
        size_t length = 0;
        const char *pText = lua_tolstring(pLuaState, 2, &length);
        ClassInspectEntry &entry =
            const_cast<CharacterInspectTable &>(pData->getCharacterInspectTable()).classForWrite(*className);
        entry.description.assign(pText, length);

        if (entry.name.empty())
        {
            entry.name = displayClassName(*className);
        }

        lua_pushboolean(pLuaState, true);
        return 1;
    }

    const ClassInspectEntry *pEntry = pData->getCharacterInspectTable().getClass(*className);
    pushString(pLuaState, pEntry != nullptr ? pEntry->description : std::string());
    return 1;
}

// modrt.trainingLevel(index[, level]): the highest level the training halls of the original's training hall number
// `index` (Game.HouseRules.Training, 1 up) train to, set when `level` is given. Nil for a number no hall has.
int nativeTrainingLevel(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t index = integerArgument(pLuaState, 1);

    if (pData == nullptr || index <= 0)
    {
        return 0;
    }

    const bool write = lua_isnumber(pLuaState, 2);
    const int level = write ? static_cast<int>(lua_tointeger(pLuaState, 2)) : 0;
    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    HouseTable &houses = const_cast<HouseTable &>(pData->getHouseTable());
    std::optional<int> found;

    for (auto &[houseId, entry] : houses.entriesForWrite())
    {
        const uint32_t ruleIndex = entry.typeIndex != 0 ? entry.typeIndex : entry.mapId;

        if (entry.type != "Training" || ruleIndex != static_cast<uint32_t>(index))
        {
            continue;
        }

        if (write)
        {
            entry.trainingMaxLevel = level;
        }

        found = entry.trainingMaxLevel;
    }

    if (!found)
    {
        return 0;
    }

    lua_pushinteger(pLuaState, *found);
    return 1;
}

// modrt.statDescription(stat[, text]): the description of one of the seven statistics, 0 Might to 6 Luck
// (Game.StatsDescriptions), set when text is given.
int nativeStatDescription(lua_State *pLuaState)
{
    static const char *const statNames[] = {
        "Might", "Intellect", "Personality", "Endurance", "Accuracy", "Speed", "Luck"};
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t stat = integerArgument(pLuaState, 1);

    if (pData == nullptr || stat < 0 || stat >= 7)
    {
        return 0;
    }

    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    CharacterInspectTable &table = const_cast<CharacterInspectTable &>(pData->getCharacterInspectTable());

    if (lua_isstring(pLuaState, 2))
    {
        size_t length = 0;
        const char *pText = lua_tolstring(pLuaState, 2, &length);
        table.statForWrite(statNames[stat]).description.assign(pText, length);
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    const StatInspectEntry *pEntry = table.getStat(statNames[stat]);
    pushString(pLuaState, pEntry != nullptr ? pEntry->description : std::string());
    return 1;
}

// modrt.skillRegister(id, name, group): a skill a mod adds, by its Merge number (39 up), the name the engine keeps it
// under, and its group on the skill page (0 weapons, 1 magic, 2 armor, 3 misc).
int nativeSkillRegister(lua_State *pLuaState)
{
    const int64_t skillId = integerArgument(pLuaState, 1);
    const std::string skillName = canonicalSkillName(luaL_checkstring(pLuaState, 2));
    const int64_t group = integerArgument(pLuaState, 3);

    if (skillId < static_cast<int64_t>(MergeSkillCount) || skillName.empty() || group < 0 || group > 3)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    registerMergeSkill(static_cast<size_t>(skillId), skillName);
    setModSkillGroup(skillName, static_cast<ModSkillGroup>(group));
    lua_pushboolean(pLuaState, true);
    return 1;
}

// modrt.houseOfferSkill(houseType, skill): the houses of a type (the house table's own names: "Training", "Magic
// Shop", ...) offer the skill to learn, to whoever's class may have it.
int nativeHouseOfferSkill(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const std::string houseType = luaL_checkstring(pLuaState, 1);
    const std::string skillName = canonicalSkillName(luaL_checkstring(pLuaState, 2));
    int houses = 0;

    if (pData != nullptr && !skillName.empty())
    {
        // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
        for (auto &[houseId, entry] : const_cast<HouseTable &>(pData->getHouseTable()).entriesForWrite())
        {
            if (entry.type != houseType)
            {
                continue;
            }

            if (std::find(entry.offeredSkills.begin(), entry.offeredSkills.end(), skillName)
                == entry.offeredSkills.end())
            {
                entry.offeredSkills.push_back(skillName);
            }

            ++houses;
        }
    }

    lua_pushinteger(pLuaState, houses);
    return 1;
}

// A backpack item as a table of its fields, for scripts that move items between bags. `Location` is the item's cell
// as the original numbers the 14 by 9 grid (row * 14 + column).
void pushInventoryItemTable(lua_State *pLuaState, const InventoryItem &item)
{
    lua_createtable(pLuaState, 0, 21);
    const auto number = [pLuaState](const char *pName, lua_Number value)
    {
        lua_pushnumber(pLuaState, value);
        lua_setfield(pLuaState, -2, pName);
    };
    const auto boolean = [pLuaState](const char *pName, bool value)
    {
        lua_pushboolean(pLuaState, value);
        lua_setfield(pLuaState, -2, pName);
    };
    number("Number", item.objectDescriptionId);
    number("Quantity", item.quantity);
    number("Width", item.width);
    number("Height", item.height);
    number("X", item.gridX);
    number("Y", item.gridY);
    number("Location", item.gridY * 14 + item.gridX);
    boolean("Identified", item.identified);
    boolean("Broken", item.broken);
    boolean("Stolen", item.stolen);
    number("Enchant", item.standardEnchantId);
    number("EnchantPower", item.standardEnchantPower);
    number("Special", item.specialEnchantId);
    number("Artifact", item.artifactId);
    number("Rarity", static_cast<int>(item.rarity));
    number("Charges", item.currentCharges);
    number("MaxCharges", item.maxCharges);
    number("TemporaryBonusSeconds", item.temporaryBonusRemainingSeconds);
    number("ModCharges", item.modData.charges);
    number("ModMaxCharges", item.modData.maxCharges);
    number("ModExpire", item.modData.bonusExpireTime);
}

InventoryItem inventoryItemFromTable(lua_State *pLuaState, int index)
{
    const auto number = [pLuaState, index](const char *pName, lua_Number fallback)
    {
        lua_getfield(pLuaState, index, pName);
        const lua_Number value = lua_isnumber(pLuaState, -1) ? lua_tonumber(pLuaState, -1) : fallback;
        lua_pop(pLuaState, 1);
        return value;
    };
    const auto boolean = [pLuaState, index](const char *pName, bool fallback)
    {
        lua_getfield(pLuaState, index, pName);
        const bool value = lua_isboolean(pLuaState, -1) ? lua_toboolean(pLuaState, -1) != 0 : fallback;
        lua_pop(pLuaState, 1);
        return value;
    };
    InventoryItem item = {};
    item.objectDescriptionId = static_cast<uint32_t>(number("Number", 0));
    item.quantity = static_cast<uint32_t>(std::max<lua_Number>(1, number("Quantity", 1)));
    item.width = static_cast<uint8_t>(std::clamp<lua_Number>(number("Width", 1), 1, 14));
    item.height = static_cast<uint8_t>(std::clamp<lua_Number>(number("Height", 1), 1, 9));
    item.identified = boolean("Identified", true);
    item.broken = boolean("Broken", false);
    item.stolen = boolean("Stolen", false);
    item.standardEnchantId = static_cast<uint16_t>(number("Enchant", 0));
    item.standardEnchantPower = static_cast<uint16_t>(number("EnchantPower", 0));
    item.specialEnchantId = static_cast<uint16_t>(number("Special", 0));
    item.artifactId = static_cast<uint16_t>(number("Artifact", 0));
    item.rarity = static_cast<ItemRarity>(static_cast<int>(number("Rarity", 0)));
    item.currentCharges = static_cast<uint16_t>(number("Charges", 0));
    item.maxCharges = static_cast<uint16_t>(number("MaxCharges", 0));
    item.temporaryBonusRemainingSeconds = static_cast<float>(number("TemporaryBonusSeconds", 0));
    item.modData.charges = static_cast<int32_t>(number("ModCharges", 0));
    item.modData.maxCharges = static_cast<int32_t>(number("ModMaxCharges", 0));
    item.modData.bonusExpireTime = static_cast<int32_t>(number("ModExpire", 0));
    return item;
}

// modrt.inventoryTake(player): empties the character's backpack and returns its items, each as a table of its
// fields (see pushInventoryItemTable). Equipped items stay.
int nativeInventoryTake(lua_State *pLuaState)
{
    Character *pCharacter = playerArgument(pLuaState, 1);

    if (pCharacter == nullptr)
    {
        return 0;
    }

    const std::vector<InventoryItem> items = std::move(pCharacter->inventory);
    pCharacter->inventory.clear();
    lua_createtable(pLuaState, static_cast<int>(items.size()), 0);

    for (size_t index = 0; index < items.size(); ++index)
    {
        pushInventoryItemTable(pLuaState, items[index]);
        lua_rawseti(pLuaState, -2, static_cast<lua_Integer>(index + 1));
    }

    return 1;
}

// The original's 100 map notes of the current map (Map.Notes) are the game's map notes under ids of their own:
// 0x80000000 plus a number of the map's name, hundreds of them, plus the note's place.
constexpr int64_t ModMapNoteCount = 100;

std::optional<uint32_t> modMapNoteId(lua_State *pLuaState, int64_t slot, std::string &mapFileName)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    mapFileName = pHost != nullptr && pHost->currentMapFileName ? pHost->currentMapFileName() : std::string();

    if (slot < 0 || slot >= ModMapNoteCount || mapFileName.empty())
    {
        return std::nullopt;
    }

    std::transform(mapFileName.begin(), mapFileName.end(), mapFileName.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    uint32_t hash = 2166136261u;

    for (const char character : mapFileName)
    {
        hash = (hash ^ static_cast<uint8_t>(character)) * 16777619u;
    }

    return 0x80000000u + (hash % 20000000u) * 100u + static_cast<uint32_t>(slot);
}

// modrt.mapNoteGet(slot, field): Active, Id, X, Y or Text of the current map's note `slot` (0 to 99).
int nativeMapNoteGet(lua_State *pLuaState)
{
    std::string mapFileName;
    const std::optional<uint32_t> noteId = modMapNoteId(pLuaState, integerArgument(pLuaState, 1), mapFileName);
    const std::string field = luaL_checkstring(pLuaState, 2);
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const EventRuntimeState *pState = pWorld != nullptr ? pWorld->eventRuntimeState() : nullptr;

    if (!noteId || pState == nullptr)
    {
        return 0;
    }

    const auto it = pState->runtimeMapNotes.find(*noteId);
    const EventRuntimeState::RuntimeMapNote note =
        it != pState->runtimeMapNotes.end() ? it->second : EventRuntimeState::RuntimeMapNote {};

    if (field == "Active")
    {
        lua_pushboolean(pLuaState, note.active);
    }
    else if (field == "Id")
    {
        // The original's number for the note (MAW writes 9999) is not kept.
        lua_pushinteger(pLuaState, 0);
    }
    else if (field == "X" || field == "Y")
    {
        lua_pushinteger(pLuaState, field == "X" ? note.x : note.y);
    }
    else if (field == "Text")
    {
        pushString(pLuaState, note.text);
    }
    else
    {
        return 0;
    }

    return 1;
}

// modrt.mapNoteSet(slot, field, value): sets Active, X, Y or Text of the current map's note `slot`, which the map
// screen shows while it is active. Id is the original's own number and is not kept.
int nativeMapNoteSet(lua_State *pLuaState)
{
    std::string mapFileName;
    const std::optional<uint32_t> noteId = modMapNoteId(pLuaState, integerArgument(pLuaState, 1), mapFileName);
    const std::string field = luaL_checkstring(pLuaState, 2);
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    EventRuntimeState *pState = pWorld != nullptr ? pWorld->eventRuntimeState() : nullptr;

    if (!noteId || pState == nullptr)
    {
        return 0;
    }

    EventRuntimeState::RuntimeMapNote &note = pState->runtimeMapNotes[*noteId];
    note.id = *noteId;
    note.mapFileName = mapFileName;

    if (field == "Active")
    {
        note.active = lua_toboolean(pLuaState, 3) != 0;
    }
    else if (field == "X" || field == "Y")
    {
        (field == "X" ? note.x : note.y) = static_cast<int32_t>(luaL_checknumber(pLuaState, 3));
    }
    else if (field == "Text")
    {
        note.text = luaL_checkstring(pLuaState, 3);
    }

    return 0;
}

// modrt.questionOpen(): whether the game's answer prompt is open for a question a mod asked (under
// ModCommandEventId).
int nativeQuestionOpen(lua_State *pLuaState)
{
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const EventRuntimeState *pState = pWorld != nullptr ? pWorld->eventRuntimeState() : nullptr;
    lua_pushboolean(
        pLuaState,
        pState != nullptr && pState->pendingInputPrompt && pState->pendingInputPrompt->eventId == ModCommandEventId);
    return 1;
}

// modrt.difficultySetting(): the difficulty the game's settings name for a new game ("" for the mod's own default).
int nativeDifficultySetting(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    pushString(pLuaState, pHost != nullptr && pHost->difficultySetting ? pHost->difficultySetting() : std::string());
    return 1;
}

// modrt.monstersInSight(): the map's monsters the party can see, by index (the original's Game.GetMonstersInSight):
// those within 5120 with a line of sight from the party's eyes; not limited to the screen's view.
int nativeMonstersInSight(lua_State *pLuaState)
{
    const IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    lua_newtable(pLuaState);

    if (pWorld == nullptr)
    {
        return 1;
    }

    const float x = pWorld->partyX();
    const float y = pWorld->partyY();
    const float eyeZ = pWorld->partyFootZ() + 96.0f;
    lua_Integer place = 1;

    for (const size_t actorIndex : pWorld->collectMapActorIndicesWithinRadius(x, y, eyeZ, 5120.0f, true, x, y, eyeZ))
    {
        lua_pushinteger(pLuaState, static_cast<lua_Integer>(actorIndex));
        lua_rawseti(pLuaState, -2, place++);
    }

    return 1;
}

// modrt.innDismiss(slot): sends party member `slot` to the Adventurer's Inn, as dismissing does. Whether it went.
int nativeInnDismiss(lua_State *pLuaState)
{
    Party *pParty = partyFromHost(pLuaState);
    const int64_t slot = integerArgument(pLuaState, 1);
    lua_pushboolean(pLuaState, pParty != nullptr && slot >= 0 && pParty->dismissMemberToAdventurersInn(static_cast<size_t>(slot)));
    return 1;
}

// modrt.inventoryCell(player, cell): the original's Inventory[cell] of the backpack (14 x 9, row by row): the item's
// number in Items (1 up) at the cell where an item's top left corner is, -(that cell + 1) in the other cells it
// covers, 0 in an empty one.
int nativeInventoryCell(lua_State *pLuaState)
{
    Character *pCharacter = playerArgument(pLuaState, 1);
    const lua_Integer cell = luaL_checkinteger(pLuaState, 2);
    lua_Integer value = 0;

    if (pCharacter != nullptr && cell >= 0 && cell < Character::InventoryWidth * Character::InventoryHeight)
    {
        const int x = static_cast<int>(cell % Character::InventoryWidth);
        const int y = static_cast<int>(cell / Character::InventoryWidth);

        for (size_t index = 0; index < pCharacter->inventory.size(); ++index)
        {
            const InventoryItem &item = pCharacter->inventory[index];

            if (x >= item.gridX && x < item.gridX + std::max<int>(1, item.width) && y >= item.gridY
                && y < item.gridY + std::max<int>(1, item.height))
            {
                value = x == item.gridX && y == item.gridY
                    ? static_cast<lua_Integer>(index + 1)
                    : -static_cast<lua_Integer>(item.gridY * Character::InventoryWidth + item.gridX + 1);
                break;
            }
        }
    }

    lua_pushinteger(pLuaState, value);
    return 1;
}

// modrt.inventoryPut(player, item[, x, y]): puts an item (a table as inventoryTake gives) in the backpack: at the
// cell when one is given and the item fits there, otherwise where it first fits. False when it fits nowhere.
int nativeInventoryPut(lua_State *pLuaState)
{
    Character *pCharacter = playerArgument(pLuaState, 1);
    luaL_checktype(pLuaState, 2, LUA_TTABLE);

    if (pCharacter == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    InventoryItem item = inventoryItemFromTable(pLuaState, 2);

    if (item.objectDescriptionId == 0)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    // An item made by a script need not say its size: its kind's.
    lua_getfield(pLuaState, 2, "Width");
    const bool sized = lua_isnumber(pLuaState, -1);
    lua_pop(pLuaState, 1);
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const ItemDefinition *pDefinition =
        !sized && pData != nullptr ? pData->getItemTable().get(item.objectDescriptionId) : nullptr;

    if (pDefinition != nullptr)
    {
        item.width = std::max<uint8_t>(1, pDefinition->inventoryWidth);
        item.height = std::max<uint8_t>(1, pDefinition->inventoryHeight);
    }

    bool placed = false;

    if (lua_isnumber(pLuaState, 3) && lua_isnumber(pLuaState, 4))
    {
        placed = pCharacter->addInventoryItemAt(
            item,
            static_cast<uint8_t>(std::clamp<lua_Integer>(lua_tointeger(pLuaState, 3), 0, 13)),
            static_cast<uint8_t>(std::clamp<lua_Integer>(lua_tointeger(pLuaState, 4), 0, 8)));
    }

    if (!placed)
    {
        placed = pCharacter->addInventoryItem(item);
    }

    lua_pushboolean(pLuaState, placed);
    return 1;
}

// modrt.heldItemSet([item]): puts an item (a table as inventoryTake gives; its kind's size when it gives none) on the
// mouse pointer in place of the one there, or takes it off when none is given.
// modrt.eventCall(path, ...): within a map event a mod's handler is running in, calls the engine's event value at
// `path` ("evt.MoveToMap"; a value that is no function is returned as it is) and returns its results.
int nativeEventCall(lua_State *pLuaState)
{
    const char *pPath = luaL_checkstring(pLuaState, 1);
    const int top = lua_gettop(pLuaState);
    const int resultCount = callActiveEventFunction(pLuaState, pPath, 2);

    if (resultCount == -1)
    {
        return luaL_error(pLuaState, "%s used outside a map event", pPath);
    }

    if (resultCount == -2)
    {
        return lua_error(pLuaState);
    }

    (void)top;
    return resultCount;
}

// modrt.runCommandEvent([eventId]): runs a map event as the game runs one (what the player clicked on, say); without
// an id, the event the mod's queued event commands are the handler of (ModCommandEventId). Whether it ran.
int nativeRunCommandEvent(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    IGameplayWorldRuntime *pWorld = pHost != nullptr && pHost->world ? pHost->world() : nullptr;
    const lua_Integer eventId = luaL_optinteger(pLuaState, 1, ModCommandEventId);

    if (pWorld == nullptr || eventId <= 0 || eventId > ModCommandEventId)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    size_t previousMessageCount = 0;
    const bool executed = pWorld->executeMapEvent(static_cast<uint16_t>(eventId), previousMessageCount);

    if (executed && pHost->handOverEventItems)
    {
        pHost->handOverEventItems();
    }

    if (executed && pHost->presentPendingDialogue)
    {
        pHost->presentPendingDialogue();
    }

    lua_pushboolean(pLuaState, executed);
    return 1;
}

int nativeHeldItemSet(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);

    if (pHost == nullptr || !pHost->setHeldItem)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    if (!lua_istable(pLuaState, 1))
    {
        pHost->setHeldItem(nullptr);
        lua_pushboolean(pLuaState, true);
        return 1;
    }

    InventoryItem item = inventoryItemFromTable(pLuaState, 1);
    lua_getfield(pLuaState, 1, "Width");
    const bool sized = lua_isnumber(pLuaState, -1);
    lua_pop(pLuaState, 1);
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const ItemDefinition *pDefinition =
        pData != nullptr ? pData->getItemTable().get(item.objectDescriptionId) : nullptr;

    if (pDefinition == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    if (!sized)
    {
        item.width = std::max<uint8_t>(1, pDefinition->inventoryWidth);
        item.height = std::max<uint8_t>(1, pDefinition->inventoryHeight);
    }

    pHost->setHeldItem(&item);
    lua_pushboolean(pLuaState, true);
    return 1;
}

// modrt.heldItemPresent(): whether the player holds an item on the mouse pointer.
int nativeHeldItemPresent(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);
    lua_pushboolean(pLuaState, pHost != nullptr && pHost->heldItem && pHost->heldItem() != nullptr);
    return 1;
}

// modrt.npcText(id): one of the game's NPC texts (Game.NPCText), nil where there is none.
int nativeNpcText(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t id = integerArgument(pLuaState, 1);
    const std::optional<std::string> text =
        pData != nullptr && id >= 0 ? pData->getNpcDialogTable().getText(static_cast<uint32_t>(id)) : std::nullopt;

    if (!text)
    {
        return 0;
    }

    pushString(pLuaState, *text);
    return 1;
}

EventRuntimeState *eventStateFromHost(lua_State *pLuaState)
{
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    return pWorld != nullptr ? pWorld->eventRuntimeState() : nullptr;
}

// The topic in slot `slot` (0 to 5) of NPC `npcId` as the game shows it: the event commands' (evt.SetNPCTopic) over
// the NPC table's.
uint32_t npcTopicInSlot(const NpcEntry &npc, const EventRuntimeState *pState, uint32_t slot)
{
    if (pState != nullptr)
    {
        const auto npcIt = pState->npcTopicOverrides.find(npc.id);

        if (npcIt != pState->npcTopicOverrides.end())
        {
            const auto slotIt = npcIt->second.find(slot);

            if (slotIt != npcIt->second.end())
            {
                return slotIt->second;
            }
        }
    }

    return slot < npc.topicIds.size() ? npc.topicIds[slot] : 0;
}

// modrt.npcGet(npc, field): Game.NPC[npc]'s Name, Pic, House, Profession, Greet, Hired, Event0 ... Event5 (its
// topics), as the game has them now; nil for an NPC the table lacks.
int nativeNpcGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t id = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);
    const NpcEntry *pNpc = pData != nullptr && id >= 0 ? pData->getNpcDialogTable().getNpc(static_cast<uint32_t>(id)) : nullptr;

    if (pNpc == nullptr)
    {
        return 0;
    }

    const EventRuntimeState *pState = eventStateFromHost(pLuaState);
    const auto overridden = [&](const std::unordered_map<uint32_t, uint32_t> EventRuntimeState::*pMap, uint32_t value)
    {
        if (pState != nullptr)
        {
            const auto it = (pState->*pMap).find(pNpc->id);

            if (it != (pState->*pMap).end())
            {
                return it->second;
            }
        }

        return value;
    };

    if (field == "Name")
    {
        const auto it = pState != nullptr ? pState->npcNameOverrides.find(pNpc->id) : decltype(pState->npcNameOverrides)::const_iterator();
        pushString(pLuaState, pState != nullptr && it != pState->npcNameOverrides.end() ? it->second : pNpc->name);
    }
    else if (field == "Pic")
    {
        lua_pushinteger(pLuaState, overridden(&EventRuntimeState::npcPictureOverrides, pNpc->pictureId));
    }
    else if (field == "House")
    {
        lua_pushinteger(pLuaState, overridden(&EventRuntimeState::npcHouseOverrides, pNpc->houseId));
    }
    else if (field == "Profession")
    {
        lua_pushinteger(pLuaState, overridden(&EventRuntimeState::npcProfessionOverrides, pNpc->professionId));
    }
    else if (field == "Greet")
    {
        lua_pushinteger(pLuaState, overridden(&EventRuntimeState::npcGreetingOverrides, pNpc->greetId));
    }
    else if (field == "Hired")
    {
        bool hired = false;

        if (pState != nullptr)
        {
            for (const auto &follower : pState->hiredNpcFollowers)
            {
                hired = hired || follower.npcId == pNpc->id;
            }
        }

        lua_pushboolean(pLuaState, hired);
    }
    else if (field.size() == 6 && field.rfind("Event", 0) == 0 && field[5] >= '0' && field[5] <= '5')
    {
        lua_pushinteger(pLuaState, npcTopicInSlot(*pNpc, pState, static_cast<uint32_t>(field[5] - '0')));
    }
    else
    {
        return 0;
    }

    return 1;
}

// modrt.npcSet(npc, field, value): sets Game.NPC[npc]'s Name, Pic, House, Greet or Event0 ... Event5 as the event
// commands do (they stay with the game). Returns whether the field could be set.
int nativeNpcSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    EventRuntimeState *pState = eventStateFromHost(pLuaState);
    const int64_t id = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);
    const NpcEntry *pNpc = pData != nullptr && id >= 0 ? pData->getNpcDialogTable().getNpc(static_cast<uint32_t>(id)) : nullptr;

    if (pNpc == nullptr || pState == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    bool set = true;

    if (field == "Name")
    {
        pState->npcNameOverrides[pNpc->id] = luaL_checkstring(pLuaState, 3);
    }
    else if (field == "Pic")
    {
        pState->npcPictureOverrides[pNpc->id] = static_cast<uint32_t>(std::max<int64_t>(0, integerArgument(pLuaState, 3)));
    }
    else if (field == "House")
    {
        pState->npcHouseOverrides[pNpc->id] = static_cast<uint32_t>(std::max<int64_t>(0, integerArgument(pLuaState, 3)));
    }
    else if (field == "Greet")
    {
        pState->npcGreetingOverrides[pNpc->id] = static_cast<uint32_t>(std::max<int64_t>(0, integerArgument(pLuaState, 3)));
    }
    else if (field.size() == 6 && field.rfind("Event", 0) == 0 && field[5] >= '0' && field[5] <= '5')
    {
        pState->npcTopicOverrides[pNpc->id][static_cast<uint32_t>(field[5] - '0')] =
            static_cast<uint32_t>(std::max<int64_t>(0, integerArgument(pLuaState, 3)));
    }
    else
    {
        set = false;
    }

    lua_pushboolean(pLuaState, set);
    return 1;
}

// modrt.npcTopicGet(event): Game.NPCTopic[event], the topic's name as the game shows it.
int nativeNpcTopicGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t id = integerArgument(pLuaState, 1);

    if (id < 0)
    {
        return 0;
    }

    if (const std::string *pLabel = modNpcTopicLabel(static_cast<uint32_t>(id)))
    {
        pushString(pLuaState, *pLabel);
        return 1;
    }

    const std::optional<NpcDialogTable::ResolvedTopic> topic =
        pData != nullptr ? pData->getNpcDialogTable().getTopicById(static_cast<uint32_t>(id)) : std::nullopt;
    pushString(pLuaState, topic ? topic->topic : std::string());
    return 1;
}

// modrt.npcTopicSet(event, name): Game.NPCTopic[event] = name; an empty name gives the topic its own back.
int nativeNpcTopicSet(lua_State *pLuaState)
{
    const int64_t id = integerArgument(pLuaState, 1);

    if (id >= 0)
    {
        setModNpcTopicLabel(static_cast<uint32_t>(id), luaL_optstring(pLuaState, 2, ""));
    }

    return 0;
}

// modrt.questTextGet(bit): Game.QuestsTxt[bit], the quest log's text for the quest bit ("" for none).
int nativeQuestTextGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t id = integerArgument(pLuaState, 1);

    if (id < 0)
    {
        return 0;
    }

    if (const std::string *pText = modQuestText(static_cast<uint32_t>(id)))
    {
        pushString(pLuaState, *pText);
        return 1;
    }

    std::string text;

    if (pData != nullptr)
    {
        for (const JournalQuestEntry &entry : pData->getJournalQuestTable().entries())
        {
            if (entry.qbitId == static_cast<uint32_t>(id))
            {
                text = entry.text;
                break;
            }
        }
    }

    pushString(pLuaState, text);
    return 1;
}

// modrt.questTextSet(bit, text): Game.QuestsTxt[bit] = text, shown in the quest log while the bit is set.
int nativeQuestTextSet(lua_State *pLuaState)
{
    const int64_t id = integerArgument(pLuaState, 1);

    if (id >= 0)
    {
        setModQuestText(static_cast<uint32_t>(id), luaL_optstring(pLuaState, 2, ""));
    }

    return 0;
}

// modrt.globalTextSet(index, text): the game shows the text where it shows that interface text (the stats page's
// labels); an empty text gives it its own back.
int nativeGlobalTextSet(lua_State *pLuaState)
{
    const int64_t index = integerArgument(pLuaState, 1);
    size_t length = 0;
    const char *pText = luaL_checklstring(pLuaState, 2, &length);
    setModGlobalText(static_cast<int>(index), std::string(pText, length));
    return 0;
}

// modrt.classSkillSet(class, skill, mastery): the highest mastery the class can reach in the skill, 0 for none to 4
// for Grandmaster. The game's teachers and the skill screen go by it from then on (MAW sets every class's limits
// from its own table this way).
int nativeClassSkillSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t classId = integerArgument(pLuaState, 1);
    const std::string skillName = canonicalSkillName(luaL_checkstring(pLuaState, 2));
    const int64_t mastery = integerArgument(pLuaState, 3);
    const std::optional<std::string> className = pData != nullptr && classId >= 0
        ? pData->getClassSkillTable().classNameForId(static_cast<uint32_t>(classId))
        : std::nullopt;

    if (!className || mastery < 0 || mastery > 4)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    const_cast<ClassSkillTable &>(pData->getClassSkillTable())
        .setClassCap(*className, skillName, static_cast<SkillMastery>(mastery));
    lua_pushboolean(pLuaState, true);
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
        {"NameId", GameplayModActorField::UniqueNameId},
        {"Ally", GameplayModActorField::Ally},
        {"AIState", GameplayModActorField::AiState},
        // The original keeps a monster's speed twice: MoveSpeed in its copy of the table row, which nothing moves
        // by, and Velocity, which it moves by. Only the second is the engine's.
        {"Velocity", GameplayModActorField::MoveSpeed},
        {"AIType", GameplayModActorField::AiType},
        {"Item", GameplayModActorField::CarriedItem},
        {"Bonus", GameplayModActorField::SpecialAttack},
        {"BonusMul", GameplayModActorField::SpecialAttackLevel},
    };
    const std::unordered_map<std::string, GameplayModActorField>::const_iterator it = fields.find(field);

    if (it != fields.end())
    {
        return it->second;
    }

    // The monster's own level and its resistances, Resistance0 to Resistance10 by const.Damage (the scripts'
    // Resistances[n]).
    if (field == "Level")
    {
        return GameplayModActorField::Level;
    }

    if (field.rfind("Resistance", 0) == 0 && field.size() > 10)
    {
        const std::string number = field.substr(10);

        if (std::all_of(number.begin(), number.end(), [](char character) { return character >= '0' && character <= '9'; })
            && number.size() <= 2 && std::stoi(number) <= 10)
        {
            return static_cast<GameplayModActorField>(
                static_cast<int>(GameplayModActorField::Resistance0) + std::stoi(number));
        }
    }

    return std::nullopt;
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

    // The original's flags (bits of the monster's record). The engine has one notion of hostility, so the flag for
    // "shown as hostile" is the same answer; "shown on the map" is every monster that is not hidden.
    const bool hostileFlag = field == "Hostile" || field == "ShowAsHostile";

    if (hostileFlag || field == "IsDead" || field == "Invisible" || field == "ShowOnMap")
    {
        GameplayRuntimeActorState state = {};

        if (actorIndex < 0 || !pWorld->actorRuntimeState(static_cast<size_t>(actorIndex), state))
        {
            return 0;
        }

        lua_pushboolean(
            pLuaState,
            hostileFlag ? state.hostileToParty
                : field == "IsDead" ? state.isDead
                : field == "Invisible" ? state.isInvisible
                : !state.isInvisible);
        return 1;
    }

    // Not the original's: what a dead monster's body holds, gold and the worth of its items together. Nothing for a
    // monster that left no body to search.
    if (field == "CorpseValue")
    {
        const std::optional<uint32_t> corpseValue =
            actorIndex >= 0 ? pWorld->corpseLootValue(static_cast<size_t>(actorIndex)) : std::nullopt;

        if (!corpseValue)
        {
            return 0;
        }

        lua_pushinteger(pLuaState, static_cast<lua_Integer>(*corpseValue));
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

    // A field the engine does not keep (a flag, say, written as true) stays with the script.
    if (!actorField.has_value() || !lua_isnumber(pLuaState, 3))
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    int64_t value = integerArgument(pLuaState, 3);

    // To the engine a speed of 0 is "its kind's speed". A script's 0 means a monster that does not move, so it is
    // kept as 1, which is as good as standing still.
    if (actorField == GameplayModActorField::MoveSpeed)
    {
        value = std::max<int64_t>(1, value);
    }

    const bool handled = pWorld != nullptr
        && actorField.has_value()
        && actorIndex >= 0
        && pWorld->setModActorField(static_cast<size_t>(actorIndex), *actorField, value);
    lua_pushboolean(pLuaState, handled);
    return 1;
}

// modrt.monsterBuffGet(actor, buff): a map monster's spell effect (const.MonsterBuff): the seconds it has left and
// its power; nothing when the engine has no such effect.
int nativeMonsterBuffGet(lua_State *pLuaState)
{
    const IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const int64_t actorIndex = integerArgument(pLuaState, 1);
    const int64_t buffId = integerArgument(pLuaState, 2);
    GameplayModActorBuff buff = {};

    if (pWorld == nullptr || actorIndex < 0
        || !pWorld->modActorBuff(static_cast<size_t>(actorIndex), static_cast<int>(buffId), buff))
    {
        return 0;
    }

    lua_pushnumber(pLuaState, buff.remainingSeconds);
    lua_pushinteger(pLuaState, buff.power);
    return 2;
}

// modrt.monsterBuffSet(actor, buff, seconds, power): sets what is given (nil leaves it). False when the engine has
// no such effect.
int nativeMonsterBuffSet(lua_State *pLuaState)
{
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const int64_t actorIndex = integerArgument(pLuaState, 1);
    const int64_t buffId = integerArgument(pLuaState, 2);
    const bool setSeconds = lua_isnumber(pLuaState, 3);
    const bool setPower = lua_isnumber(pLuaState, 4);
    GameplayModActorBuff buff = {};
    buff.remainingSeconds = setSeconds ? static_cast<float>(lua_tonumber(pLuaState, 3)) : 0.0f;
    buff.power = setPower ? static_cast<int>(lua_tointeger(pLuaState, 4)) : 0;
    lua_pushboolean(
        pLuaState,
        pWorld != nullptr && actorIndex >= 0
            && pWorld->setModActorBuff(
                static_cast<size_t>(actorIndex), static_cast<int>(buffId), buff, setSeconds, setPower));
    return 1;
}

// The texts of a Game.SpellsTxt row and where the spell entry keeps them: what the spellbook shows of a spell.
std::string *spellTextField(SpellEntry &entry, const std::string &field)
{
    if (field == "Name") return &entry.name;
    if (field == "ShortName") return &entry.shortName;
    if (field == "Description") return &entry.description;
    if (field == "Normal") return &entry.normalText;
    if (field == "Expert") return &entry.expertText;
    if (field == "Master") return &entry.masterText;
    if (field == "GM") return &entry.grandmasterText;
    return nullptr;
}

// modrt.spellTextGet(id, field): a text of a spell (see spellTextField); nothing for a spell or field there is not.
int nativeSpellTextGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t spellId = integerArgument(pLuaState, 1);
    const SpellEntry *pEntry =
        pData != nullptr ? pData->getSpellTable().findById(static_cast<int>(spellId)) : nullptr;
    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    const std::string *pText =
        pEntry != nullptr ? spellTextField(const_cast<SpellEntry &>(*pEntry), luaL_checkstring(pLuaState, 2)) : nullptr;

    if (pText == nullptr)
    {
        return 0;
    }

    pushString(pLuaState, *pText);
    return 1;
}

// modrt.spellTextSet(id, field, text): the spellbook shows the text from then on. A name is the spell's name the
// game shows; it is found by it as well as by the table's name, which quick spells are kept by.
int nativeSpellTextSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t spellId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);
    size_t length = 0;
    const char *pText = luaL_checklstring(pLuaState, 3, &length);
    const SpellEntry *pEntry =
        pData != nullptr ? pData->getSpellTable().findById(static_cast<int>(spellId)) : nullptr;
    if (pEntry != nullptr && field == "Name")
    {
        // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
        lua_pushboolean(
            pLuaState,
            const_cast<SpellTable &>(pData->getSpellTable()).renameSpell(pEntry->id, std::string(pText, length)));
        return 1;
    }

    std::string *pField = pEntry != nullptr ? spellTextField(const_cast<SpellEntry &>(*pEntry), field) : nullptr;

    if (pField == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    pField->assign(pText, length);
    lua_pushboolean(pLuaState, true);
    return 1;
}

// The four figures of a Game.Spells row by mastery, Normal to GM, and where the spell entry keeps them.
struct SpellMasteryField
{
    const char *pName;
    int SpellEntry::*pMember;
    bool SpellEntry::*pModFlag;
};

constexpr std::array<SpellMasteryField, 8> SpellMasteryFields = {{
    {"SpellPointsNormal", &SpellEntry::normalManaCost, &SpellEntry::modManaCost},
    {"SpellPointsExpert", &SpellEntry::expertManaCost, &SpellEntry::modManaCost},
    {"SpellPointsMaster", &SpellEntry::masterManaCost, &SpellEntry::modManaCost},
    {"SpellPointsGM", &SpellEntry::grandmasterManaCost, &SpellEntry::modManaCost},
    {"DelayNormal", &SpellEntry::normalRecoveryTicks, &SpellEntry::modRecovery},
    {"DelayExpert", &SpellEntry::expertRecoveryTicks, &SpellEntry::modRecovery},
    {"DelayMaster", &SpellEntry::masterRecoveryTicks, &SpellEntry::modRecovery},
    {"DelayGM", &SpellEntry::grandmasterRecoveryTicks, &SpellEntry::modRecovery},
}};

// modrt.spellTxtGet(id, field): a row of Game.Spells, the spell table: SpellPoints<Mastery> and Delay<Mastery> (the
// cost and the recovery in ticks for Normal, Expert, Master and GM), Name, ShortName. "#" is the highest spell number.
int nativeSpellTxtGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t spellId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pData == nullptr)
    {
        return 0;
    }

    if (field == "#")
    {
        int highest = 0;

        for (const SpellEntry &entry : pData->getSpellTable().entries())
        {
            highest = std::max(highest, entry.id);
        }

        lua_pushinteger(pLuaState, highest);
        return 1;
    }

    const SpellEntry *pEntry = pData->getSpellTable().findById(static_cast<int>(spellId));

    if (pEntry == nullptr)
    {
        return 0;
    }

    for (const SpellMasteryField &entry : SpellMasteryFields)
    {
        if (field == entry.pName)
        {
            lua_pushinteger(pLuaState, pEntry->*entry.pMember);
            return 1;
        }
    }

    if (field == "Name")
    {
        pushString(pLuaState, pEntry->name);
        return 1;
    }

    if (field == "ShortName")
    {
        pushString(pLuaState, pEntry->shortName);
        return 1;
    }

    return 0;
}

// modrt.spellTxtSet(id, field, value): the costs and recoveries of a spell. Casting goes by them from then on (MAW
// sets its own costs for its healing spells this way).
int nativeSpellTxtSet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t spellId = integerArgument(pLuaState, 1);
    const std::string field = luaL_checkstring(pLuaState, 2);
    const SpellEntry *pConstEntry =
        pData != nullptr ? pData->getSpellTable().findById(static_cast<int>(spellId)) : nullptr;

    if (pConstEntry == nullptr || !lua_isnumber(pLuaState, 3))
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    // The table belongs to the game's data loader, which hands it out read-only; it is not a constant object.
    SpellEntry &spell = const_cast<SpellEntry &>(*pConstEntry);

    for (const SpellMasteryField &entry : SpellMasteryFields)
    {
        if (field == entry.pName)
        {
            spell.*entry.pMember = static_cast<int>(std::clamp<int64_t>(integerArgument(pLuaState, 3), 0, 100000));
            spell.*entry.pModFlag = true;
            lua_pushboolean(pLuaState, true);
            return 1;
        }
    }

    lua_pushboolean(pLuaState, false);
    return 1;
}

// modrt.enchantTxtGet(special, index, field[, slot]): a row of the special (Game.SpcItemsTxt) or standard
// (Game.StdItemsTxt) enchant table, numbered from 0 as the original's are (an item's Bonus2 - 1, Bonus - 1).
// "#" is the number of rows. ChanceForSlot takes the slot, numbered from 0.
int nativeEnchantTxtGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const bool special = lua_toboolean(pLuaState, 1);
    const int64_t index = integerArgument(pLuaState, 2);
    const std::string field = luaL_checkstring(pLuaState, 3);
    const int64_t slot = luaL_optinteger(pLuaState, 4, -1);

    if (pData == nullptr)
    {
        return 0;
    }

    if (field == "#")
    {
        lua_pushinteger(
            pLuaState,
            static_cast<lua_Integer>(special
                ? pData->getSpecialItemEnchantTable().entries().size()
                : pData->getStandardItemEnchantTable().entries().size()));
        return 1;
    }

    if (index < 0 || index >= UINT16_MAX)
    {
        return 0;
    }

    const uint16_t enchantId = static_cast<uint16_t>(index + 1);

    if (special)
    {
        const SpecialItemEnchantEntry *pEntry = pData->getSpecialItemEnchantTable().get(enchantId);

        if (pEntry == nullptr)
        {
            return 0;
        }

        if (field == "NameAdd")
        {
            pushString(pLuaState, pEntry->suffix);
        }
        else if (field == "BonusStat")
        {
            pushString(pLuaState, pEntry->name);
        }
        else if (field == "Value")
        {
            lua_pushinteger(pLuaState, pEntry->priceModifier.amount);
        }
        else if (field == "Lvl")
        {
            // The table's letter, A to D, as the original's 0 to 3.
            lua_pushinteger(
                pLuaState,
                pEntry->rarityLevel >= 'A' && pEntry->rarityLevel <= 'D' ? pEntry->rarityLevel - 'A' : 0);
        }
        else if (field == "ChanceForSlot" && slot >= 0 && static_cast<size_t>(slot) < pEntry->slotWeights.size())
        {
            lua_pushinteger(pLuaState, pEntry->slotWeights[static_cast<size_t>(slot)]);
        }
        else
        {
            return 0;
        }

        return 1;
    }

    const StandardItemEnchantEntry *pEntry = pData->getStandardItemEnchantTable().get(enchantId);

    if (pEntry == nullptr)
    {
        return 0;
    }

    if (field == "NameAdd")
    {
        pushString(pLuaState, pEntry->suffix);
    }
    else if (field == "BonusStat")
    {
        pushString(pLuaState, pEntry->statName);
    }
    else if (field == "ChanceForSlot" && slot >= 0 && static_cast<size_t>(slot) < pEntry->slotValues.size())
    {
        lua_pushinteger(pLuaState, pEntry->slotValues[static_cast<size_t>(slot)]);
    }
    else
    {
        return 0;
    }

    return 1;
}

// modrt.itemTooltip(player, group, index, name, type, basicStat, enchantment, description): the five texts of the
// item's information box after the scripts' BuildItemInformationBox handlers, given the engine's five. The game
// asks the same of the scripts when it draws the box; this is the way to see the answer without drawing it.
int nativeItemTooltip(lua_State *pLuaState)
{
    ItemView view = {};

    if (!itemViewArgument(pLuaState, view))
    {
        return 0;
    }

    InventoryItem item = {};
    item.objectDescriptionId = *view.pItemId;
    item.identified = *view.pIdentified;
    item.broken = *view.pBroken;
    item.stolen = *view.pStolen;
    item.standardEnchantId = *view.pStandardEnchantId;
    item.standardEnchantPower = *view.pStandardEnchantPower;
    item.specialEnchantId = *view.pSpecialEnchantId;
    item.currentCharges = *view.pCurrentCharges;
    item.maxCharges = *view.pMaxCharges;
    item.modData = *view.pModData;

    ModItemTooltip texts = {};
    std::string *const fields[] = {
        &texts.name, &texts.type, &texts.basicStat, &texts.enchantment, &texts.description};

    for (int index = 0; index < 5; ++index)
    {
        size_t length = 0;
        const char *pText = luaL_optlstring(pLuaState, 4 + index, "", &length);
        fields[index]->assign(pText, length);
    }

    modItemTooltip(item, texts, true);

    for (const std::string *pField : fields)
    {
        lua_pushlstring(pLuaState, pField->data(), pField->size());
    }

    return 5;
}

// modrt.summonItem(number, x, y, z, speed): puts an item of that kind on the map at the point, as the map events'
// SummonItem does (thrown at `speed`, 0 to lie where it is). Returns whether the game placed it.
int nativeSummonItem(lua_State *pLuaState)
{
    ISceneEventContext *pScene = dynamic_cast<ISceneEventContext *>(worldFromHost(pLuaState));
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const int64_t itemId = integerArgument(pLuaState, 1);

    if (pScene == nullptr || pData == nullptr || itemId <= 0
        || pData->getItemTable().get(static_cast<uint32_t>(itemId)) == nullptr)
    {
        lua_pushboolean(pLuaState, false);
        return 1;
    }

    lua_pushboolean(
        pLuaState,
        pScene->summonEventItem(
            static_cast<uint32_t>(itemId),
            static_cast<int32_t>(luaL_checknumber(pLuaState, 2)),
            static_cast<int32_t>(luaL_checknumber(pLuaState, 3)),
            static_cast<int32_t>(luaL_checknumber(pLuaState, 4)),
            static_cast<int32_t>(luaL_optinteger(pLuaState, 5, 0)),
            1,
            false));
    return 1;
}

// modrt.summonMonster(id, x, y, z): puts one monster of that kind on the map near the point, hostile to the
// party, and returns its index in Map.Monsters, or nothing when the engine could not place one.
int nativeSummonMonster(lua_State *pLuaState)
{
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
    const int64_t monsterId = integerArgument(pLuaState, 1);

    if (pWorld == nullptr || monsterId <= 0 || monsterId > INT16_MAX)
    {
        return 0;
    }

    const size_t countBefore = pWorld->mapActorCount();

    if (!pWorld->summonHostileMonsterById(
            static_cast<int16_t>(monsterId),
            1,
            static_cast<float>(luaL_checknumber(pLuaState, 2)),
            static_cast<float>(luaL_checknumber(pLuaState, 3)),
            static_cast<float>(luaL_checknumber(pLuaState, 4)),
            0)
        || pWorld->mapActorCount() != countBefore + 1)
    {
        return 0;
    }

    lua_pushinteger(pLuaState, static_cast<lua_Integer>(countBefore));
    return 1;
}

// modrt.mapPointInfo(x, y, z): the room the point is in (0 outdoors, and indoors when it is in none) and the height
// of the floor or ground under it, or nothing for the height when there is none.
int nativeMapPointInfo(lua_State *pLuaState)
{
    const IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);

    if (pWorld == nullptr)
    {
        return 0;
    }

    const IGameplayWorldRuntime::ModPointInfo info = pWorld->modPointInfo(
        static_cast<float>(luaL_checknumber(pLuaState, 1)),
        static_cast<float>(luaL_checknumber(pLuaState, 2)),
        static_cast<float>(luaL_optnumber(pLuaState, 3, 0.0)));
    lua_pushinteger(pLuaState, info.room);

    if (info.floorZ)
    {
        lua_pushnumber(pLuaState, *info.floorZ);
    }
    else
    {
        lua_pushnil(pLuaState);
    }

    return 2;
}

// modrt.mapMonstersResetSpeed(): every map monster moves at its kind's speed again. The original engine does not
// keep a monster's speed across a map load (a real game's guards read 400 after MAW's speed-up on first arrival and
// 400 again after a save and load, where a kept 400 would have been raised to 500), so the runtime calls this on
// every arrival, before the map's events.
int nativeMapMonstersResetSpeed(lua_State *pLuaState)
{
    IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);

    if (pWorld != nullptr)
    {
        for (size_t actorIndex = 0; actorIndex < pWorld->mapActorCount(); ++actorIndex)
        {
            pWorld->setModActorField(actorIndex, GameplayModActorField::MoveSpeed, 0);
        }
    }

    return 0;
}

// --- game ------------------------------------------------------------------------------------------------------------

// modrt.houseGet(id, field): a house of the house table. "Val" its price multiplier (the original's Val), "Name".
int nativeHouseGet(lua_State *pLuaState)
{
    const GameDataLoader *pData = dataFromHost(pLuaState);
    const HouseEntry *pHouse =
        pData != nullptr ? pData->getHouseTable().get(clampedInteger<uint32_t>(integerArgument(pLuaState, 1))) : nullptr;
    const std::string field = luaL_checkstring(pLuaState, 2);

    if (pHouse == nullptr)
    {
        return 0;
    }

    if (field == "Val")
    {
        lua_pushnumber(pLuaState, pHouse->priceMultiplier);
        return 1;
    }

    if (field == "Name")
    {
        pushString(pLuaState, pHouse->name);
        return 1;
    }

    return 0;
}

// modrt.turnBasedEndWalk(): ends the party's turn-based walk (Game.TurnBasedPhase set away from 3).
int nativeTurnBasedEndWalk(lua_State *pLuaState)
{
    const ModHost *pHost = hostFromUpvalue(pLuaState);

    if (pHost != nullptr && pHost->endTurnBasedWalk)
    {
        pHost->endTurnBasedWalk();
    }

    return 0;
}

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

    if (field == "CurrentScreen")
    {
        lua_pushinteger(pLuaState, pHost->currentScreen ? pHost->currentScreen() : 0);
        return 1;
    }

    if (field == "CurrentHouse")
    {
        IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);
        const EventRuntimeState *pState = pWorld != nullptr ? pWorld->eventRuntimeState() : nullptr;
        lua_pushinteger(pLuaState, pState != nullptr ? static_cast<lua_Integer>(pState->dialogueState.hostHouseId) : 0);
        return 1;
    }

    if (field == "TurnBasedPhase")
    {
        lua_pushinteger(pLuaState, pHost->turnBasedPhase ? pHost->turnBasedPhase() : 0);
        return 1;
    }

    if (field == "CurrentCharScreen")
    {
        lua_pushinteger(pLuaState, pHost->currentCharScreen ? pHost->currentCharScreen() : 100);
        return 1;
    }

    if (field == "HasSession")
    {
        lua_pushboolean(pLuaState, partyFromHost(pLuaState) != nullptr);
        return 1;
    }

    if (field == "IsIndoor")
    {
        const IGameplayWorldRuntime *pWorld = worldFromHost(pLuaState);

        if (pWorld == nullptr)
        {
            return 0;
        }

        lua_pushboolean(pLuaState, pWorld->isIndoorMap());
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

InventoryItem *modEventItem()
{
    return g_pModEventItem;
}

InventoryItem *setModEventItem(InventoryItem *pItem)
{
    InventoryItem *pPrevious = g_pModEventItem;
    g_pModEventItem = pItem;
    return pPrevious;
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
        {"playerSpellGet", nativePlayerSpellGet},
        {"playerSpellSet", nativePlayerSpellSet},
        {"partyRefreshBonuses", nativePartyRefreshBonuses},
        {"playerSkillCap", nativePlayerSkillCap},
        {"setRule", nativeSetRule},
        {"hudTextSet", nativeHudTextSet},
        {"mercenaryRecruits", nativeMercenaryRecruits},
        {"npcGet", nativeNpcGet},
        {"npcSet", nativeNpcSet},
        {"npcTopicGet", nativeNpcTopicGet},
        {"npcTopicSet", nativeNpcTopicSet},
        {"questTextGet", nativeQuestTextGet},
        {"questTextSet", nativeQuestTextSet},
        {"questBitGet", nativeQuestBitGet},
        {"questBitSet", nativeQuestBitSet},
        {"itemGet", nativeItemGet},
        {"itemSet", nativeItemSet},
        {"itemTxtGet", nativeItemTxtGet},
        {"itemTxtSet", nativeItemTxtSet},
        {"monsterTxtGet", nativeMonsterTxtGet},
        {"monsterTxtSet", nativeMonsterTxtSet},
        {"placeMonGet", nativePlaceMonGet},
        {"placeMonSet", nativePlaceMonSet},
        {"portraitGet", nativePortraitGet},
        {"classGet", nativeClassGet},
        {"classSet", nativeClassSet},
        {"classSkillGet", nativeClassSkillGet},
        {"classSkillSet", nativeClassSkillSet},
        {"globalTextSet", nativeGlobalTextSet},
        {"npcText", nativeNpcText},
        {"inventoryTake", nativeInventoryTake},
        {"inventoryCell", nativeInventoryCell},
        {"innDismiss", nativeInnDismiss},
        {"monstersInSight", nativeMonstersInSight},
        {"difficultySetting", nativeDifficultySetting},
        {"questionOpen", nativeQuestionOpen},
        {"mapNoteGet", nativeMapNoteGet},
        {"mapNoteSet", nativeMapNoteSet},
        {"inventoryPut", nativeInventoryPut},
        {"heldItemPresent", nativeHeldItemPresent},
        {"heldItemSet", nativeHeldItemSet},
        {"eventCall", nativeEventCall},
        {"runCommandEvent", nativeRunCommandEvent},
        {"skillRegister", nativeSkillRegister},
        {"houseOfferSkill", nativeHouseOfferSkill},
        {"statDescription", nativeStatDescription},
        {"trainingLevel", nativeTrainingLevel},
        {"skillTextGet", nativeSkillTextGet},
        {"skillTextSet", nativeSkillTextSet},
        {"classDescription", nativeClassDescription},
        {"mapStatsGet", nativeMapStatsGet},
        {"mapStatsSet", nativeMapStatsSet},
        {"mapMonsterGet", nativeMapMonsterGet},
        {"mapMonsterSet", nativeMapMonsterSet},
        {"monsterBuffGet", nativeMonsterBuffGet},
        {"spellTextGet", nativeSpellTextGet},
        {"spellTextSet", nativeSpellTextSet},
        {"monsterBuffSet", nativeMonsterBuffSet},
        {"mapMonstersResetSpeed", nativeMapMonstersResetSpeed},
        {"summonMonster", nativeSummonMonster},
        {"summonItem", nativeSummonItem},
        {"itemTooltip", nativeItemTooltip},
        {"itemRandomize", nativeItemRandomize},
        {"enchantTxtGet", nativeEnchantTxtGet},
        {"spellTxtGet", nativeSpellTxtGet},
        {"spellTxtSet", nativeSpellTxtSet},
        {"mapPointInfo", nativeMapPointInfo},
        {"gameGet", nativeGameGet},
        {"turnBasedEndWalk", nativeTurnBasedEndWalk},
        {"houseGet", nativeHouseGet},
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
