#include "game/mod/ModHooks.h"
#include "game/gameplay/GameplayDialogUiFlow.h"

#include "game/debug/GameplayDebugTrace.h"
#include "game/gameplay/GameplayInputFrame.h"

#include <SDL3/SDL.h>

namespace OpenYAMM::Game
{
namespace
{
void resetDialogInteractionState(GameplayDialogUiFlowState &state, bool suppressInitialAccept)
{
    state.selectionIndex = 0;
    state.interactionState.eventDialogSelectUpLatch = false;
    state.interactionState.eventDialogSelectDownLatch = false;
    state.interactionState.eventDialogAcceptLatch = suppressInitialAccept;
    state.interactionState.eventDialogPartySelectLatches.fill(false);
}

bool shouldSuppressInitialAccept(const GameplayInputFrame &input)
{
    return input.isScancodeHeld(SDL_SCANCODE_SPACE)
        || input.isScancodeHeld(SDL_SCANCODE_RETURN)
        || input.isScancodeHeld(SDL_SCANCODE_KP_ENTER);
}

void prepareDialogPresentation(GameplayDialogUiFlowState &state)
{
    state.uiController.closeInventoryNestedOverlay();
    state.interactionState.inventoryNestedOverlayClickLatch = false;
    state.interactionState.inventoryNestedOverlayPressedTarget = {};
    state.interactionState.inventoryNestedOverlayItemClickLatch = false;
    state.uiController.houseShopOverlay() = {};
}
}

void presentActiveEventDialog(
    GameplayDialogUiFlowState &state,
    const GameplayDialogUiFlowPresentOptions &options)
{
    if (!state.uiController.eventDialog().content.isActive)
    {
        return;
    }

    prepareDialogPresentation(state);
    resetDialogInteractionState(
        state,
        options.suppressInitialAcceptIfActivationKeysHeld
            && options.pInputFrame != nullptr
            && shouldSuppressInitialAccept(*options.pInputFrame));
}

void presentPendingEventDialog(
    GameplayDialogUiFlowState &state,
    EventRuntimeState *pEventRuntimeState,
    GameplayDialogController::Context &context,
    size_t previousMessageCount,
    bool allowNpcFallbackContent,
    const GameplayDialogUiFlowPresentOptions &options,
    const std::function<void(const GameplayDialogController::PresentPendingDialogResult &)> &onOpened)
{
    if (pEventRuntimeState == nullptr)
    {
        return;
    }

    prepareDialogPresentation(state);

    const bool showBankInputCursor = (SDL_GetTicks() / 500u) % 2u == 0u;
    const GameplayDialogController::PresentPendingDialogResult result =
        state.dialogController.presentPendingEventDialog(
            context,
            previousMessageCount,
            allowNpcFallbackContent,
            showBankInputCursor);

    if (pEventRuntimeState->pendingInputPrompt)
    {
        GAMEPLAY_DEBUG_TRACE(
            "input_prompt_dialog_presented"
            " opened=" + std::string(result.dialogOpened ? "true" : "false")
            + " previous_message_count=" + std::to_string(previousMessageCount)
            + " total_message_count=" + std::to_string(pEventRuntimeState->messages.size())
            + " active_dialog="
            + (state.uiController.eventDialog().content.isActive ? std::string("true") : std::string("false"))
            + " dialog_line_count=" + std::to_string(state.uiController.eventDialog().content.lines.size())
            + " dialog_action_count=" + std::to_string(state.uiController.eventDialog().content.actions.size())
            + " prompt_event_id=" + std::to_string(pEventRuntimeState->pendingInputPrompt->eventId));
    }

    if (!result.dialogOpened)
    {
        return;
    }

    resetDialogInteractionState(
        state,
        options.suppressInitialAcceptIfActivationKeysHeld
            && options.pInputFrame != nullptr
            && shouldSuppressInitialAccept(*options.pInputFrame));

    if (onOpened)
    {
        onOpened(result);
    }
}

void closeActiveEventDialog(
    GameplayDialogUiFlowState &state,
    EventRuntimeState *pEventRuntimeState)
{
    if (pEventRuntimeState != nullptr)
    {
        pEventRuntimeState->pendingInputPrompt.reset();
        pEventRuntimeState->pendingDialogueContext.reset();
        pEventRuntimeState->dialogueState = {};
    }

    modNpcDialogClosed();
    state.uiController.clearEventDialog();
    resetDialogInteractionState(state, false);
    state.interactionState.dialogueClickLatch = false;
    state.interactionState.dialoguePressedTarget = {};
    state.uiController.houseShopOverlay() = {};
    state.uiController.closeInventoryNestedOverlay();
    state.interactionState.inventoryNestedOverlayClickLatch = false;
    state.interactionState.inventoryNestedOverlayPressedTarget = {};
    state.interactionState.inventoryNestedOverlayItemClickLatch = false;
    state.uiController.clearHouseBankState();
}

bool refreshHouseBankInputDialog(
    GameplayDialogUiFlowState &state,
    EventRuntimeState *pEventRuntimeState,
    GameplayDialogController::Context &context,
    bool showCursor)
{
    if (pEventRuntimeState == nullptr)
    {
        return false;
    }

    return state.dialogController.refreshHouseBankInputDialog(context, showCursor);
}

GameplayDialogController::Result returnToHouseBankMainDialog(
    GameplayDialogUiFlowState &state,
    EventRuntimeState *pEventRuntimeState,
    GameplayDialogController::Context &context)
{
    state.interactionState.houseBankDigitLatches.fill(false);
    state.interactionState.houseBankBackspaceLatch = false;
    state.interactionState.houseBankConfirmLatch = false;

    if (pEventRuntimeState == nullptr)
    {
        return {};
    }

    return state.dialogController.returnToHouseBankMainDialog(context);
}

GameplayDialogController::Result confirmHouseBankInput(
    GameplayDialogUiFlowState &state,
    EventRuntimeState *pEventRuntimeState,
    GameplayDialogController::Context &context)
{
    if (pEventRuntimeState == nullptr)
    {
        state.interactionState.houseBankDigitLatches.fill(false);
        state.interactionState.houseBankBackspaceLatch = false;
        state.interactionState.houseBankConfirmLatch = false;
        return {};
    }

    const GameplayDialogController::Result result = state.dialogController.confirmHouseBankInput(context);
    state.interactionState.houseBankDigitLatches.fill(false);
    state.interactionState.houseBankBackspaceLatch = false;
    state.interactionState.houseBankConfirmLatch = false;
    return result;
}
}
