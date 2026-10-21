#pragma once

#include "game/party/Party.h"

#include <functional>
#include <optional>
#include <random>
#include <string>

namespace OpenYAMM::Game
{
class ItemTable;
class StandardItemEnchantTable;
class SpecialItemEnchantTable;
struct ItemDefinition;

enum class ItemGenerationMode : uint8_t
{
    Generic = 0,
    Shop,
    MonsterLoot,
    ChestLoot,
};

struct ItemGenerationRequest
{
    int treasureLevel = 1;
    ItemGenerationMode mode = ItemGenerationMode::Generic;
    bool allowRareItems = true;
    bool rareItemsOnly = false;
    // World of the map the item is generated for; world-gated items (ItemDefinition::worldIds) need a match.
    std::string worldId;
};

class ItemGenerator
{
public:
    static InventoryItem makeInventoryItem(
        uint32_t itemId,
        const ItemTable &itemTable,
        ItemGenerationMode mode);
    static std::optional<InventoryItem> generateRandomInventoryItem(
        const ItemTable &itemTable,
        const StandardItemEnchantTable &standardItemEnchantTable,
        const SpecialItemEnchantTable &specialItemEnchantTable,
        const ItemGenerationRequest &request,
        Party *pParty,
        std::mt19937 &rng,
        const std::function<bool(const ItemDefinition &)> &filter = {});
    // MM8 potion power by where the potion comes from: random treasure 2d4 x treasure level, placed on a map or in
    // a chest 5-19, carried by a monster 2-8. Each leaves anything but a potion without power untouched.
    static void assignTreasurePotionPower(
        InventoryItem &item,
        const ItemTable &itemTable,
        int treasureLevel,
        std::mt19937 &rng);
    static void assignPlacedPotionPower(InventoryItem &item, const ItemTable &itemTable, std::mt19937 &rng);
    static void assignMonsterPotionPower(InventoryItem &item, const ItemTable &itemTable, std::mt19937 &rng);
    static std::optional<uint32_t> chooseRandomBaseItem(
        const ItemTable &itemTable,
        int treasureLevel,
        std::mt19937 &rng,
        const std::function<bool(const ItemDefinition &)> &filter = {});
};
}
