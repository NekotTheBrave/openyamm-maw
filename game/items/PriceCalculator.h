#pragma once

#include <cstdint>

namespace OpenYAMM::Game
{
struct Character;
struct HouseEntry;
struct InventoryItem;
struct ItemDefinition;
class StandardItemEnchantTable;
class SpecialItemEnchantTable;

class PriceCalculator
{
public:
    // The character's merchant total, the mod's word included (GetMerchantTotalSkill).
    static int playerMerchant(const Character *pCharacter, int effectiveReputation = 0);
    static int enginePlayerMerchant(const Character &character, int effectiveReputation = 0);
    static int applyMerchantDiscount(const Character *pCharacter, int goldAmount, int effectiveReputation = 0);
    static int itemBuyingPrice(
        const Character *pCharacter,
        int realValue,
        float priceMultiplier,
        int effectiveReputation = 0);
    static int itemValue(
        const InventoryItem &item,
        const ItemDefinition &itemDefinition,
        const StandardItemEnchantTable *pStandardEnchantTable = nullptr,
        const SpecialItemEnchantTable *pSpecialEnchantTable = nullptr);
    static int itemSellingPrice(
        const Character *pCharacter,
        const InventoryItem &item,
        const ItemDefinition &itemDefinition,
        float priceMultiplier,
        const StandardItemEnchantTable *pStandardEnchantTable = nullptr,
        const SpecialItemEnchantTable *pSpecialEnchantTable = nullptr,
        int effectiveReputation = 0);
    static int itemIdentificationPrice(
        const Character *pCharacter,
        const InventoryItem &item,
        const ItemDefinition &itemDefinition,
        float priceMultiplier,
        const StandardItemEnchantTable *pStandardEnchantTable = nullptr,
        const SpecialItemEnchantTable *pSpecialEnchantTable = nullptr,
        int effectiveReputation = 0);
    static int itemRepairPrice(
        const Character *pCharacter,
        const InventoryItem &item,
        const ItemDefinition &itemDefinition,
        float priceMultiplier,
        const StandardItemEnchantTable *pStandardEnchantTable = nullptr,
        const SpecialItemEnchantTable *pSpecialEnchantTable = nullptr,
        int effectiveReputation = 0);
    static int skillLearningPrice(
        const Character *pCharacter,
        const HouseEntry &houseEntry,
        bool isGuild,
        int effectiveReputation = 0);
    static int transportPrice(
        const Character *pCharacter,
        const HouseEntry &houseEntry,
        bool isBoat,
        int effectiveReputation = 0);
    static int transportPrice(
        const Character *pCharacter,
        int basePrice,
        float priceMultiplier,
        int effectiveReputation = 0);
    static int tavernRoomPrice(
        const Character *pCharacter,
        const HouseEntry &houseEntry,
        int effectiveReputation = 0);
    static int tavernFoodPrice(
        const Character *pCharacter,
        const HouseEntry &houseEntry,
        int effectiveReputation = 0);
    static int templeHealPrice(const Character *pCharacter, const HouseEntry &houseEntry, float gameMinutes = 0.0f);
    static int trainingPrice(
        const Character *pCharacter,
        const HouseEntry &houseEntry,
        int effectiveReputation = 0);
    static int trainingPrice(
        const Character *pCharacter,
        const Character *pMerchantCharacter,
        const HouseEntry &houseEntry,
        int effectiveReputation = 0);
};
}
