#include "game/items/PriceCalculator.h"

#include "game/party/CharacterState.h"
#include "game/tables/HouseTable.h"
#include "game/items/ItemEnchantRuntime.h"
#include "game/items/ItemEnchantTables.h"
#include "game/tables/ItemTable.h"
#include "game/party/Party.h"
#include "game/party/SkillData.h"

#include <algorithm>
#include <cmath>

namespace OpenYAMM::Game
{
namespace
{
constexpr float MinutesPerDay = 24.0f * 60.0f;

int masteryMerchantMultiplier(SkillMastery mastery)
{
    switch (mastery)
    {
        case SkillMastery::Normal:
            return 1;

        case SkillMastery::Expert:
            return 2;

        case SkillMastery::Master:
            return 3;

        case SkillMastery::Grandmaster:
            return 5;

        case SkillMastery::None:
        default:
            return 0;
    }
}

int conditionDaysPassed(const Character &member, CharacterCondition condition, float gameMinutes)
{
    const size_t conditionIndex = static_cast<size_t>(condition);

    if (!member.conditions.test(conditionIndex))
    {
        return 0;
    }

    const float startGameMinutes = member.conditionStartGameMinutes[conditionIndex];

    if (startGameMinutes <= 0.0f || gameMinutes < startGameMinutes)
    {
        return 1;
    }

    return static_cast<int>(std::floor((gameMinutes - startGameMinutes) / MinutesPerDay)) + 1;
}

int highestMinorConditionDaysPassed(const Character &member, float gameMinutes)
{
    int highestDaysPassed = 1;

    for (size_t conditionIndex = 0; conditionIndex <= static_cast<size_t>(CharacterCondition::Unconscious);
        ++conditionIndex)
    {
        const int daysPassed = conditionDaysPassed(
            member,
            static_cast<CharacterCondition>(conditionIndex),
            gameMinutes);
        highestDaysPassed = std::max(highestDaysPassed, daysPassed);
    }

    return highestDaysPassed;
}

int classTierForClassName(const std::string &className)
{
    const std::string canonicalClass = canonicalClassName(className);

    if (canonicalClass == "PriestLight"
        || canonicalClass == "PriestDark"
        || canonicalClass == "HighPriest"
        || canonicalClass == "MasterArcher"
        || canonicalClass == "Sniper"
        || canonicalClass == "Warlock"
        || canonicalClass == "ArchDruid"
        || canonicalClass == "BlackKnight"
        || canonicalClass == "Champion"
        || canonicalClass == "Master"
        || canonicalClass == "Ninja"
        || canonicalClass == "Hero"
        || canonicalClass == "Villain"
        || canonicalClass == "BountyHunter"
        || canonicalClass == "RangerLord"
        || canonicalClass == "Assassin"
        || canonicalClass == "Spy"
        || canonicalClass == "ArchMage"
        || canonicalClass == "DarkArchMage"
        || canonicalClass == "MasterWizard")
    {
        return 3;
    }

    if (canonicalClass == "WarriorMage"
        || canonicalClass == "Priest"
        || canonicalClass == "Patriarch"
        || canonicalClass == "GreatWyrm"
        || canonicalClass == "GreatDruid"
        || canonicalClass == "Cavalier"
        || canonicalClass == "MinotaurLord"
        || canonicalClass == "Initiate"
        || canonicalClass == "Crusader"
        || canonicalClass == "Hunter"
        || canonicalClass == "Rogue"
        || canonicalClass == "WarTroll"
        || canonicalClass == "Nosferatu"
        || canonicalClass == "Wizard"
        || canonicalClass == "Lich")
    {
        return 2;
    }

    return 1;
}
}

int PriceCalculator::playerMerchant(const Character *pCharacter, int effectiveReputation)
{
    if (pCharacter == nullptr)
    {
        return 0;
    }

    const CharacterSkill *pMerchant = pCharacter->findSkill("Merchant");
    const int merchantBonus = std::max(0, pCharacter->merchantBonus);
    const int skillBonus = pCharacter->skillBonus("Merchant");

    const bool hasOwnSkill = pMerchant != nullptr && pMerchant->mastery != SkillMastery::None;

    if (hasOwnSkill && pMerchant->mastery == SkillMastery::Grandmaster)
    {
        return 100;
    }

    // Without a Merchant skill of their own, hireling and item levels trade at Normal mastery.
    const int skillLevel = (hasOwnSkill ? static_cast<int>(pMerchant->level) : 0) + skillBonus;
    const SkillMastery mastery = hasOwnSkill ? pMerchant->mastery : SkillMastery::Normal;
    const int bonus = hasOwnSkill || skillLevel > 0
        ? skillLevel * masteryMerchantMultiplier(mastery) + merchantBonus
        : 0;

    if (bonus <= 0)
    {
        return std::min(merchantBonus + skillBonus - effectiveReputation, 100);
    }

    return std::min(bonus - effectiveReputation + 7, 100);
}

int PriceCalculator::applyMerchantDiscount(const Character *pCharacter, int goldAmount, int effectiveReputation)
{
    if (goldAmount <= 0)
    {
        return 0;
    }

    return goldAmount * (100 - playerMerchant(pCharacter, effectiveReputation)) / 100;
}

int PriceCalculator::itemBuyingPrice(
    const Character *pCharacter,
    int realValue,
    float priceMultiplier,
    int effectiveReputation)
{
    const int clampedValue = std::max(1, realValue);
    int price = applyMerchantDiscount(
        pCharacter,
        static_cast<int>(static_cast<float>(clampedValue) * priceMultiplier),
        effectiveReputation);
    return std::max(clampedValue, price);
}

int PriceCalculator::itemValue(
    const InventoryItem &item,
    const ItemDefinition &itemDefinition,
    const StandardItemEnchantTable *pStandardEnchantTable,
    const SpecialItemEnchantTable *pSpecialEnchantTable)
{
    return ItemEnchantRuntime::itemValue(item, itemDefinition, pStandardEnchantTable, pSpecialEnchantTable);
}

int PriceCalculator::itemSellingPrice(
    const Character *pCharacter,
    const InventoryItem &item,
    const ItemDefinition &itemDefinition,
    float priceMultiplier,
    const StandardItemEnchantTable *pStandardEnchantTable,
    const SpecialItemEnchantTable *pSpecialEnchantTable,
    int effectiveReputation)
{
    if (item.broken)
    {
        return 1;
    }

    const int realValue = itemValue(item, itemDefinition, pStandardEnchantTable, pSpecialEnchantTable);

    if (realValue <= 0)
    {
        return 1;
    }

    const int merchant = playerMerchant(pCharacter, effectiveReputation);
    const int result = static_cast<int>(
        static_cast<float>(realValue) / (priceMultiplier + 2.0f)
        + static_cast<float>(realValue * merchant) / 100.0f);
    return std::clamp(result, 1, realValue) * std::max(1u, item.quantity);
}

int PriceCalculator::itemIdentificationPrice(
    const Character *pCharacter,
    const InventoryItem &,
    const ItemDefinition &,
    float priceMultiplier,
    const StandardItemEnchantTable *,
    const SpecialItemEnchantTable *,
    int effectiveReputation)
{
    const int basePrice = std::max(1, static_cast<int>(priceMultiplier * 50.0f));
    const int minimumPrice = std::max(1, basePrice / 3);
    return std::max(minimumPrice, applyMerchantDiscount(pCharacter, basePrice, effectiveReputation));
}

int PriceCalculator::itemRepairPrice(
    const Character *pCharacter,
    const InventoryItem &item,
    const ItemDefinition &itemDefinition,
    float priceMultiplier,
    const StandardItemEnchantTable *pStandardEnchantTable,
    const SpecialItemEnchantTable *pSpecialEnchantTable,
    int effectiveReputation)
{
    const int realValue = std::max(1, itemValue(item, itemDefinition, pStandardEnchantTable, pSpecialEnchantTable));
    const int basePrice = std::max(1, static_cast<int>(static_cast<float>(realValue) / (6.0f - priceMultiplier)));
    const int minimumPrice = std::max(1, basePrice / 3);
    return std::max(minimumPrice, applyMerchantDiscount(pCharacter, basePrice, effectiveReputation));
}

int PriceCalculator::skillLearningPrice(
    const Character *pCharacter,
    const HouseEntry &houseEntry,
    bool isGuild,
    int effectiveReputation)
{
    const float multiplier = isGuild ? houseEntry.priceMultiplier : houseEntry.skillPriceMultiplier;
    const int basePrice = std::max(1, static_cast<int>(std::round(multiplier * 500.0f)));
    const int minimumPrice = std::max(1, basePrice / 3);
    return std::max(minimumPrice, applyMerchantDiscount(pCharacter, basePrice, effectiveReputation));
}

int PriceCalculator::transportPrice(
    const Character *pCharacter,
    const HouseEntry &houseEntry,
    bool isBoat,
    int effectiveReputation)
{
    const int basePrice = isBoat ? 50 : 25;
    return transportPrice(pCharacter, basePrice, houseEntry.priceMultiplier, effectiveReputation);
}

int PriceCalculator::transportPrice(
    const Character *pCharacter,
    int basePrice,
    float priceMultiplier,
    int effectiveReputation)
{
    basePrice = std::max(1, basePrice);
    const int scaledBasePrice =
        std::max(1, static_cast<int>(std::round(static_cast<float>(basePrice) * priceMultiplier)));
    const int minimumPrice = std::max(1, basePrice / 3);
    return std::max(minimumPrice, applyMerchantDiscount(pCharacter, scaledBasePrice, effectiveReputation));
}

int PriceCalculator::tavernRoomPrice(
    const Character *pCharacter,
    const HouseEntry &houseEntry,
    int effectiveReputation)
{
    const float houseMultiplier = houseEntry.priceMultiplier;
    const int basePrice = std::max(1, static_cast<int>(std::round(houseMultiplier * houseMultiplier / 10.0f)));
    const int minimumPrice = std::max(1, basePrice / 3);
    return std::max(minimumPrice, applyMerchantDiscount(pCharacter, basePrice, effectiveReputation));
}

int PriceCalculator::tavernFoodPrice(
    const Character *pCharacter,
    const HouseEntry &houseEntry,
    int effectiveReputation)
{
    const float houseMultiplier = houseEntry.priceMultiplier;
    const int basePrice = std::max(1, static_cast<int>(std::round(
        houseMultiplier * houseMultiplier * houseMultiplier / 100.0f)));
    const int minimumPrice = std::max(1, basePrice / 3);
    return std::max(minimumPrice, applyMerchantDiscount(pCharacter, basePrice, effectiveReputation));
}

int PriceCalculator::templeHealPrice(const Character *pCharacter, const HouseEntry &houseEntry, float gameMinutes)
{
    const Character dummy = {};
    const Character &member = pCharacter != nullptr ? *pCharacter : dummy;
    int conditionTimeMultiplier = 1;
    int baseConditionMultiplier = 1;

    if (member.conditions.test(static_cast<size_t>(CharacterCondition::Eradicated)))
    {
        conditionTimeMultiplier = conditionDaysPassed(member, CharacterCondition::Eradicated, gameMinutes);
        baseConditionMultiplier = 10;
    }
    else if (member.conditions.test(static_cast<size_t>(CharacterCondition::Petrified)))
    {
        conditionTimeMultiplier = conditionDaysPassed(member, CharacterCondition::Petrified, gameMinutes);
        baseConditionMultiplier = 5;
    }
    else if (member.conditions.test(static_cast<size_t>(CharacterCondition::Dead)))
    {
        conditionTimeMultiplier = conditionDaysPassed(member, CharacterCondition::Dead, gameMinutes);
        baseConditionMultiplier = 5;
    }
    else if (member.conditions.any())
    {
        conditionTimeMultiplier = highestMinorConditionDaysPassed(member, gameMinutes);
    }

    const int result = static_cast<int>(
        static_cast<double>(conditionTimeMultiplier)
        * static_cast<double>(baseConditionMultiplier)
        * static_cast<double>(houseEntry.priceMultiplier));
    return std::clamp(result, 1, 10000);
}

int PriceCalculator::trainingPrice(
    const Character *pCharacter,
    const HouseEntry &houseEntry,
    int effectiveReputation)
{
    return trainingPrice(pCharacter, pCharacter, houseEntry, effectiveReputation);
}

int PriceCalculator::trainingPrice(
    const Character *pCharacter,
    const Character *pMerchantCharacter,
    const HouseEntry &houseEntry,
    int effectiveReputation)
{
    if (pCharacter == nullptr)
    {
        return 0;
    }

    const uint64_t experienceForNextLevel =
        1000ull * static_cast<uint64_t>(pCharacter->level) * static_cast<uint64_t>(pCharacter->level + 1) / 2ull;

    if (pCharacter->experience < experienceForNextLevel)
    {
        return 0;
    }

    const int classTier = std::max(1, classTierForClassName(canonicalClassName(pCharacter->className)));
    const int basePrice = std::max(
            1,
            static_cast<int>(std::round(
                static_cast<float>(pCharacter->level) * houseEntry.priceMultiplier * static_cast<float>(classTier))));
    const int minimumPrice = std::max(1, basePrice / 3);
    return std::max(minimumPrice, applyMerchantDiscount(pMerchantCharacter, basePrice, effectiveReputation));
}
}
