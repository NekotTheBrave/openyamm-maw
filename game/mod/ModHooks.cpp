#include "game/mod/ModHooks.h"
#include <map>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

#include "game/mod/ModBindings.h"
#include "game/party/SkillData.h"

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

std::unordered_map<uint32_t, std::string> &modNpcTopicLabels()
{
    static std::unordered_map<uint32_t, std::string> labels;
    return labels;
}

std::unordered_map<uint32_t, std::string> &modQuestTexts()
{
    static std::unordered_map<uint32_t, std::string> texts;
    return texts;
}

// The events of the topics the mod set in the dialog shown last.
std::vector<uint32_t> &modShownNpcTopicEvents()
{
    static std::vector<uint32_t> events;
    return events;
}

void setTextOverride(std::unordered_map<uint32_t, std::string> &texts, uint32_t id, const std::string &text)
{
    if (text.empty())
    {
        texts.erase(id);
    }
    else
    {
        texts[id] = text;
    }
}
}

const std::string *modNpcTopicLabel(uint32_t eventId)
{
    const auto it = modNpcTopicLabels().find(eventId);
    return it != modNpcTopicLabels().end() ? &it->second : nullptr;
}

void setModNpcTopicLabel(uint32_t eventId, const std::string &label)
{
    setTextOverride(modNpcTopicLabels(), eventId, label);
}

const std::string *modQuestText(uint32_t qbitId)
{
    const auto it = modQuestTexts().find(qbitId);
    return it != modQuestTexts().end() ? &it->second : nullptr;
}

void setModQuestText(uint32_t qbitId, const std::string &text)
{
    setTextOverride(modQuestTexts(), qbitId, text);
}

std::vector<uint32_t> modQuestTextBits()
{
    std::vector<uint32_t> bits;

    for (const auto &[qbitId, text] : modQuestTexts())
    {
        (void)text;
        bits.push_back(qbitId);
    }

    return bits;
}

std::vector<ModHudButton> modHudButtons()
{
    std::vector<ModHudButton> buttons;

    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->hudButtons(buttons);
    }

    return buttons;
}

void modHudButtonClicked(int id)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->hudButtonClicked(id);
    }
}

std::vector<ModDrawnHudButton> &modDrawnHudButtons()
{
    static std::vector<ModDrawnHudButton> buttons;
    return buttons;
}

void modQuestionAnswered(const std::string &answer)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->questionAnswered(answer);
    }
}

std::vector<std::string> modDifficultyChoices()
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr ? pModHooks->difficultyChoices() : std::vector<std::string>();
}

std::string modDifficulty()
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr ? pModHooks->difficulty() : std::string();
}

void modSetDifficulty(const std::string &name)
{
    if (IModHooks *pModHooks = activeModHooks(); pModHooks != nullptr && !name.empty())
    {
        pModHooks->setDifficulty(name);
    }
}

std::vector<ModNpcTopic> modNpcTopics(uint32_t npcId)
{
    std::vector<ModNpcTopic> topics;
    modShownNpcTopicEvents().clear();

    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->npcTopics(npcId, topics);
    }

    for (const ModNpcTopic &topic : topics)
    {
        if (topic.eventId != 0)
        {
            modShownNpcTopicEvents().push_back(topic.eventId);
        }
    }

    return topics;
}

bool modOwnsNpcTopic(uint32_t eventId)
{
    const std::vector<uint32_t> &events = modShownNpcTopicEvents();
    return std::find(events.begin(), events.end(), eventId) != events.end();
}

std::optional<std::string> modNpcGreeting(uint32_t npcId, bool seen)
{
    IModHooks *pModHooks = activeModHooks();
    std::string text;

    if (pModHooks == nullptr || !pModHooks->npcGreeting(npcId, seen, text))
    {
        return std::nullopt;
    }

    return text;
}

void modNpcDialogClosed()
{
    modShownNpcTopicEvents().clear();

    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->npcDialogClosed();
    }
}

std::vector<std::string> modQuestLogTexts()
{
    std::vector<std::string> texts;

    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->questLog(texts);
    }

    return texts;
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

void modTeacherText(ModTeachSkillMasteryEvent &event)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->teacherText(event);
    }
}

void modCanTeachSkillMastery(ModTeachSkillMasteryEvent &event)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->canTeachSkillMastery(event);
    }
}

bool modUseMouseItem(size_t memberIndex)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks == nullptr || pModHooks->useMouseItem(memberIndex);
}

namespace
{
std::unordered_map<std::string, std::string> &modRules()
{
    static std::unordered_map<std::string, std::string> rules;
    return rules;
}
}

std::map<int, ModHudText> &mutableModHudTexts()
{
    static std::map<int, ModHudText> texts;
    return texts;
}

const std::map<int, ModHudText> &modHudTexts()
{
    return mutableModHudTexts();
}

void setModHudText(int id, const ModHudText &text)
{
    mutableModHudTexts()[id] = text;
}

const std::string &modRule(const std::string &name)
{
    static const std::string empty;
    const auto iterator = modRules().find(name);
    return iterator != modRules().end() ? iterator->second : empty;
}

void setModRule(const std::string &name, const std::string &value)
{
    if (value.empty())
    {
        modRules().erase(name);
        return;
    }

    modRules()[name] = value;
}

std::optional<std::string> modMapEventHint(uint16_t eventId)
{
    IModHooks *pModHooks = activeModHooks();
    std::string hint;

    if (pModHooks == nullptr || eventId == 0 || !pModHooks->mapEventHint(eventId, hint) || hint.empty())
    {
        return std::nullopt;
    }

    return hint;
}

bool modMapEventRemoved(uint16_t eventId)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr && eventId != 0 && pModHooks->mapEventRemoved(eventId);
}

bool modPotionEffect(uint32_t potionId, int power, InventoryItem &target)
{
    IModHooks *pModHooks = activeModHooks();

    if (pModHooks == nullptr)
    {
        return false;
    }

    InventoryItem *pPreviousEventItem = setModEventItem(&target);
    const bool handled = pModHooks->potionEffect(potionId, power);
    setModEventItem(pPreviousEventItem);
    return handled;
}

float modMonsterSpriteScale(size_t actorIndex)
{
    struct CachedScale
    {
        float scale = 1.0f;
        uint64_t ticks = 0;
    };

    static std::unordered_map<size_t, CachedScale> s_scales;
    static IModHooks *s_pScalesHooks = nullptr;
    IModHooks *pModHooks = activeModHooks();

    if (pModHooks == nullptr)
    {
        return 1.0f;
    }

    if (s_pScalesHooks != pModHooks)
    {
        s_scales.clear();
        s_pScalesHooks = pModHooks;
    }

    const uint64_t nowTicks = SDL_GetTicks();
    auto iterator = s_scales.find(actorIndex);

    if (iterator == s_scales.end() || nowTicks - iterator->second.ticks >= 1000)
    {
        const float scale = pModHooks->monsterSpriteScale(actorIndex);
        CachedScale &cached = s_scales[actorIndex];
        cached.scale = std::isfinite(scale) ? std::clamp(scale, 0.05f, 20.0f) : 1.0f;
        cached.ticks = nowTicks;
        return cached.scale;
    }

    return iterator->second.scale;
}

std::optional<std::string> modTownHallBounty()
{
    IModHooks *pModHooks = activeModHooks();
    std::string text;

    if (pModHooks == nullptr || !pModHooks->townHallBounty(text))
    {
        return std::nullopt;
    }

    return text;
}

bool modMonsterDropsItem(size_t actorIndex, uint32_t itemId)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks == nullptr || !pModHooks->monsterDropItem(actorIndex, itemId);
}

void modMercenaryItemGenerated(InventoryItem &item)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->mercenaryItemGenerated(item);
    }
}

void modMonsterInformationBox(ModMonsterInfoBox &box)
{
    if (IModHooks *pModHooks = activeModHooks())
    {
        pModHooks->monsterInformationBox(box);
    }
}

bool modCanWearItem(const Character &character, uint32_t itemId)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks == nullptr || pModHooks->canWearItem(character, itemId);
}

int modCanLearnSpell(const Character &character, uint32_t spellId, int engineMastery)
{
    IModHooks *pModHooks = activeModHooks();
    return pModHooks != nullptr ? pModHooks->canLearnSpell(character, spellId, engineMastery) : engineMastery;
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

std::optional<int> modMergeSkillId(const std::string &skillName)
{
    const std::string canonicalName = canonicalSkillName(skillName);

    for (const size_t skillId : mergeSkillIds())
    {
        if (canonicalSkillName(mergeSkillName(skillId)) == canonicalName)
        {
            return static_cast<int>(skillId);
        }
    }

    return std::nullopt;
}

ModPlayerActionResult modPlayerActionWithHeldItem(int action, InventoryItem &heldItem, int param, int param2)
{
    InventoryItem *pPreviousEventItem = setModEventItem(&heldItem);
    const ModPlayerActionResult result = modPlayerAction(action, param, param2);
    setModEventItem(pPreviousEventItem);
    return result;
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
