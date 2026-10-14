#include "game/mod/ModHooks.h"

#include <unordered_map>

namespace OpenYAMM::Game
{
namespace
{
// One mod runtime per process; gameplay code reaches it from many layers that share no owner object.
IModHooks *g_pActiveModHooks = nullptr;

std::unordered_map<int, std::string> &modGlobalTexts()
{
    static std::unordered_map<int, std::string> texts;
    return texts;
}
}

const std::string *modGlobalText(int index)
{
    const std::unordered_map<int, std::string>::const_iterator it = modGlobalTexts().find(index);
    return it != modGlobalTexts().end() ? &it->second : nullptr;
}

void setModGlobalText(int index, const std::string &text)
{
    if (text.empty())
    {
        modGlobalTexts().erase(index);
    }
    else
    {
        modGlobalTexts()[index] = text;
    }
}

IModHooks *activeModHooks()
{
    return g_pActiveModHooks;
}

void setActiveModHooks(IModHooks *pModHooks)
{
    g_pActiveModHooks = pModHooks;
}

void modItemTooltip(const InventoryItem &item, ModItemTooltip &texts, bool fresh)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->itemTooltip(item, texts, fresh);
    }
}

int modPlayerSkillTotal(ModPlayerSkillTotal kind, const Character &character, int engineValue)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr ? pModHooks->playerSkillTotal(kind, character, engineValue) : engineValue;
}

bool modCanHandleItem(bool repair, uint32_t itemId, bool engineAnswer)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr ? pModHooks->canHandleItem(repair, itemId, engineAnswer) : engineAnswer;
}

void modCanTeachSkillMastery(ModTeachSkillMasteryEvent &event)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->canTeachSkillMastery(event);
    }
}

void modStatInformationBox(int stat, std::string &text)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->statInformationBox(stat, text);
    }
}

float modTrainingMinutes(float engineMinutes)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr ? pModHooks->trainingMinutes(engineMinutes) : engineMinutes;
}

ModPlayerActionResult modPlayerAction(int action, int param, int param2)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr ? pModHooks->playerAction(action, param, param2) : ModPlayerActionResult{};
}

MonsterTable::LootPrototype modCorpseLoot(size_t actorIndex, const MonsterTable::LootPrototype &engineLoot)
{
    IModHooks *pModHooks = activeModHooks();

    if (pModHooks == nullptr)
    {
        return engineLoot;
    }

    ModCorpseLoot loot = {
        .goldDiceCount = engineLoot.goldDiceRolls,
        .goldDiceSides = engineLoot.goldDiceSides,
        .itemPercent = engineLoot.itemChance,
        .itemLevel = engineLoot.itemLevel,
    };
    pModHooks->pickCorpse(actorIndex, loot);

    if (!loot.allowed)
    {
        return {};
    }

    MonsterTable::LootPrototype result = engineLoot;
    result.goldDiceRolls = std::max(0, loot.goldDiceCount);
    result.goldDiceSides = std::max(0, loot.goldDiceSides);
    result.itemChance = std::clamp(loot.itemPercent, 0, 100);
    result.itemLevel = std::clamp(loot.itemLevel, 0, 7);

    if (loot.anyItemKind || result.itemKind == MonsterTable::LootItemKind::None)
    {
        result.itemKind = MonsterTable::LootItemKind::Any;
    }

    return result;
}

std::string stripMm8ColorCodes(const std::string &text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] == '\f')
        {
            size_t digits = 0;

            while (digits < 5 && index + 1 + digits < text.size()
                && text[index + 1 + digits] >= '0' && text[index + 1 + digits] <= '9')
            {
                ++digits;
            }

            index += digits;
            continue;
        }

        result.push_back(text[index]);
    }

    return result;
}

int mm8AiState(const ModActorActivity &activity)
{
    if (activity.dead || activity.motion == 7)
    {
        return activity.invisible ? Mm8AiState::Removed : Mm8AiState::Dead;
    }

    if (activity.invisible)
    {
        return Mm8AiState::Invisible;
    }

    if (activity.motion == 6)
    {
        return Mm8AiState::Dying;
    }

    if (activity.paralyzed)
    {
        return Mm8AiState::Paralyzed;
    }

    switch (activity.motion)
    {
        case 1: return Mm8AiState::Active;
        case 2: return Mm8AiState::Pursue;
        case 3: return Mm8AiState::Flee;
        case 4: return Mm8AiState::Stunned;
        case 5: return activity.rangedAttack ? Mm8AiState::RangedAttack : Mm8AiState::MeleeAttack;
        default: return Mm8AiState::Stand;
    }
}

int mm8DamageKind(CombatDamageType damageType)
{
    switch (damageType)
    {
        case CombatDamageType::Fire: return 0;
        case CombatDamageType::Air: return 1;
        case CombatDamageType::Water: return 2;
        case CombatDamageType::Earth: return 3;
        case CombatDamageType::Physical: return 4;
        case CombatDamageType::Spirit: return 6;
        case CombatDamageType::Mind: return 7;
        case CombatDamageType::Body: return 8;
        case CombatDamageType::Light: return 9;
        case CombatDamageType::Dark: return 10;
        case CombatDamageType::Energy: return 12;
        case CombatDamageType::Irresistible: return 12;
    }

    return 4;
}

int modDamageToPlayer(const ModDamageToPlayerEvent &event)
{
    return g_pActiveModHooks != nullptr ? g_pActiveModHooks->calcDamageToPlayer(event) : event.resolvedDamage;
}

uint32_t modKillExperience(size_t actorIndex, uint32_t engineExperience)
{
    return g_pActiveModHooks != nullptr
        ? g_pActiveModHooks->monsterKillExperience(actorIndex, engineExperience)
        : engineExperience;
}

int modObjectDamageToMonster(
    size_t actorIndex,
    std::optional<size_t> partyMemberIndex,
    int rawDamage,
    int resolvedDamage,
    CombatDamageType damageType,
    uint32_t spellId,
    uint32_t skillLevel,
    uint32_t skillMastery,
    float objectX,
    float objectY,
    float objectZ)
{
    if (g_pActiveModHooks == nullptr)
    {
        return resolvedDamage;
    }

    ModDamageToMonsterEvent event = {};
    event.actorIndex = actorIndex;
    event.partyMemberIndex = partyMemberIndex;
    event.rawDamage = rawDamage;
    event.resolvedDamage = resolvedDamage;
    event.damageType = damageType;
    event.spellId = spellId;
    event.melee = false;
    event.fromObject = true;
    event.skillLevel = skillLevel;
    event.skillMastery = skillMastery;
    event.objectX = objectX;
    event.objectY = objectY;
    event.objectZ = objectZ;
    return g_pActiveModHooks->calcDamageToMonster(event);
}
}
