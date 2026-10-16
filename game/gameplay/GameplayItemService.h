#pragma once

#include "game/ui/GameplayOverlayTypes.h"

#include <cstddef>

namespace OpenYAMM::Game
{
struct EventRuntimeState;
class GameSession;
class GameplayScreenRuntime;
class Party;

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

    void updateConnectorStoneRecharge(
        Party &party,
        EventRuntimeState &eventRuntimeState,
        float gameMinutes);

    void updateReadableScrollOverlayForHeldItem(
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
    GameSession &m_session;
};
}
