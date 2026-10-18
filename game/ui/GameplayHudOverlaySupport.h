#pragma once

#include "game/ui/GameplayUiController.h"

#include <cstdint>

namespace OpenYAMM::Game
{
class GameplayScreenRuntime;

class GameplayHudOverlaySupport
{
public:
    static bool tryPopulateItemInspectOverlayFromRenderedHudItems(
        GameplayScreenRuntime &context,
        int width,
        int height,
        bool requireOpaqueHitTest);

    static void updateCharacterInspectOverlay(
        GameplayScreenRuntime &context,
        int width,
        int height);

    static void updateBuffInspectOverlay(
        GameplayScreenRuntime &context,
        int width,
        int height,
        bool showGameplayHud);

    static void updateCharacterDetailOverlay(
        GameplayScreenRuntime &context,
        int width,
        int height);

    static void updateSpellInspectOverlay(
        GameplayScreenRuntime &context,
        int width,
        int height);

    // MAW port: the texts of a spell's info box for the active member (title, school, cost, description, mastery
    // lines), as the spellbook shows them; a spell book's own box shows them too, as the original's. Leaves the
    // source rectangle and `active` to the caller. False when the spell is unknown to the spell table.
    static bool fillSpellInspectOverlayTexts(
        GameplayScreenRuntime &context,
        uint32_t spellId,
        GameplayUiController::SpellInspectOverlayState &overlay);

    static void renderGameplayMouseLookOverlay(
        GameplayScreenRuntime &context,
        int width,
        int height,
        bool active);
};
} // namespace OpenYAMM::Game
