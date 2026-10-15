#pragma once

#include "game/maps/MapItemSourceData.h"
#include "game/party/Party.h"

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>

namespace OpenYAMM::Game
{
class ItemTable;
class StandardItemEnchantTable;
class SpecialItemEnchantTable;

struct SearchableLootPropState
{
    std::unordered_set<std::string> searchedSourceIds;
};

struct SearchableLootPropResult
{
    bool handled = false;
    bool alreadySearched = false;
    bool diseaseApplied = false;
    bool itemGenerated = false;
    bool itemAwarded = false;
    bool inventoryFull = false;
    std::optional<InventoryItem> generatedItem;
};

bool searchableLootPropHazardTriggers(const MapSearchableLootPropSource &source, uint32_t rollOneToOneHundred);
uint32_t searchableLootPropSeed(uint32_t sessionSeed, const std::string &sourceId);

SearchableLootPropResult applySearchableLootPropResult(
    const MapSearchableLootPropSource &source,
    SearchableLootPropState &state,
    uint32_t hazardRollOneToOneHundred,
    const std::optional<InventoryItem> &generatedItem,
    Party &party);

SearchableLootPropResult searchLootProp(
    const MapSearchableLootPropSource &source,
    SearchableLootPropState &state,
    const ItemTable &itemTable,
    const StandardItemEnchantTable &standardItemEnchantTable,
    const SpecialItemEnchantTable &specialItemEnchantTable,
    Party &party,
    const std::string &worldId,
    std::mt19937 &rng);
}
