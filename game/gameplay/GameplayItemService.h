#pragma once

#include "game/ui/GameplayOverlayTypes.h"

#include <cstddef>

namespace OpenYAMM::Game
{
struct EventRuntimeState;
class GameSession;
class GameplayScreenRuntime;
class Party;
struct InventoryItemMixResult;

class GameplayItemService
{
public:
    explicit GameplayItemService(GameSession &session);

    bool tryUseHeldItemOnPartyMember(
        GameplayScreenRuntime &runtime,
        size_t memberIndex,
        bool keepCharacterScreenOpen);
    // The held item, which the game itself has no use for, dropped on a character's doll: the mod's UseMouseItem
    // (the original raises it for the doll as for a portrait; MAW's Dimension Maps are used so). Whether the mod
    // took it (used it up, or kept the game from going on to put it on).
    bool tryModUseHeldItem(GameplayScreenRuntime &runtime, size_t memberIndex);

    bool tryUseHeldItemOnInventoryItem(
        GameplayScreenRuntime &runtime,
        size_t memberIndex,
        uint8_t targetGridX,
        uint8_t targetGridY);

    bool tryUseHeldItemOnEquippedItem(
        GameplayScreenRuntime &runtime,
        size_t memberIndex,
        EquipmentSlot slot);
    // Mixes the alchemy bench's two slots; mixAll repeats the recipe until the ingredients run out.
    bool mixAlchemyBench(GameplayScreenRuntime &runtime, bool mixAll);
    // Brews the bench's selected recipe, brewing missing ingredients first from known recipes.
    bool brewAlchemyChain(GameplayScreenRuntime &runtime, bool brewAll);

    void updateConnectorStoneRecharge(
        Party &party,
        EventRuntimeState &eventRuntimeState,
        float gameMinutes);

    void updateReadableScrollOverlayForHeldItem(
        GameplayScreenRuntime &runtime,
        size_t memberIndex,
        const GameplayCharacterPointerTarget &pointerTarget,
        bool isLeftMousePressed);

    bool identifyInspectedItem(
        const GameplayUiController::ItemInspectOverlayState &overlay,
        std::string &statusText);
    bool tryIdentifyInspectedItem(
        const GameplayUiController::ItemInspectOverlayState &overlay,
        size_t inspectorMemberIndex,
        std::string &statusText);
    bool tryRepairInspectedItem(
        const GameplayUiController::ItemInspectOverlayState &overlay,
        size_t inspectorMemberIndex,
        std::string &statusText);

    void closeReadableScrollOverlay();

private:
    void applyMixResultEffects(
        GameplayScreenRuntime &runtime,
        Party &party,
        size_t memberIndex,
        const InventoryItemMixResult &mixResult,
        bool keepInventoryOpen);
    void unlockAutonote(GameplayScreenRuntime &runtime, Party &party, size_t memberIndex, uint32_t autonoteId);
    // Reading a potion recipe teaches every mix that brews the potion, in the journal and at the alchemy bench.
    void learnRecipeScroll(GameplayScreenRuntime &runtime, Party &party, size_t memberIndex, uint32_t scrollItemId);

    GameSession &m_session;
};
}
