#include "engine/AssetFileSystem.h"
#include "engine/ImageAssetLoader.h"
#include "game/app/GameSession.h"
#include "game/data/GameDataLoader.h"
#include "game/gameplay/GameplayInputFrame.h"
#include "game/gameplay/GameplayOverlayInputController.h"
#include "game/ui/GameplayUiRuntime.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>

TEST_CASE("dimensional travel uses original artwork and matching continent hover regions")
{
    using namespace OpenYAMM;
    const std::filesystem::path sourceRoot = OPENYAMM_SOURCE_DIR;
    Engine::AssetFileSystem assets;
    REQUIRE(assets.initialize(sourceRoot, sourceRoot / "assets_dev", Engine::AssetScaleTier::X1));
    Game::GameDataLoader loader;
    REQUIRE(loader.loadForHeadlessGameplay(assets));
    Game::GameDataRepository data;
    data.bind(loader);
    Game::GameplayUiRuntime ui;
    ui.bindDataRepository(&data);
    CHECK_FALSE(ui.ensureDimensionDoorDestinationsLoaded(1, false));
    REQUIRE(ui.ensureDimensionDoorDestinationsLoaded(1, true));
    REQUIRE_EQ(ui.townPortalDestinations().size(), 3);

    Engine::BinaryAssetCache images;
    const std::optional<Engine::ImagePixelsBgra> background =
        Engine::loadImageAssetPixelsBgra(assets, ui.townPortalBackgroundTextureName() + ".bmp", images);
    REQUIRE(background.has_value());
    CHECK_EQ(background->width, 640);
    CHECK_EQ(background->height, 480);
    CHECK(background->assetScaleTier == Engine::AssetScaleTier::X1);

    struct ExpectedDestination
    {
        const char *pLabel;
        const char *pMapName;
        int32_t hitX;
        uint32_t hitWidth;
        int iconWidth;
        int iconHeight;
    };
    const std::array<ExpectedDestination, 3> expected = {{
        {"Jadame", "out01.odm", 0, 213, 450, 360},
        {"Enroth", "oute3.odm", 213, 214, 270, 400},
        {"Antagarich", "7out01.odm", 427, 213, 460, 360},
    }};
    for (const ExpectedDestination &entry : expected)
    {
        const auto destination = std::find_if(ui.townPortalDestinations().begin(), ui.townPortalDestinations().end(),
            [&entry](const Game::GameplayTownPortalDestination &candidate) { return candidate.label == entry.pLabel; });
        REQUIRE(destination != ui.townPortalDestinations().end());
        CHECK_EQ(destination->mapName, entry.pMapName);
        CHECK_EQ(destination->hitX, entry.hitX);
        CHECK_EQ(destination->hitWidth, entry.hitWidth);
        CHECK_EQ(destination->hitY, 0);
        CHECK_EQ(destination->hitHeight, 480);
        const std::optional<Engine::ImagePixelsBgra> icon =
            Engine::loadImageAssetPixelsBgra(assets, destination->iconTextureName + ".bmp", images);
        REQUIRE(icon.has_value());
        CHECK_EQ(icon->width, entry.iconWidth);
        CHECK_EQ(icon->height, entry.iconHeight);
        CHECK(icon->assetScaleTier == Engine::AssetScaleTier::X1);
    }
}

TEST_CASE("the press that opens a chest or dialogue is consumed before the overlay reacts to it")
{
    using namespace OpenYAMM;
    Game::GameSession session;
    Game::GameplayScreenRuntime &runtime = session.gameplayScreenRuntime();
    Game::GameplayOverlayInteractionState &state = runtime.interactionState();
    state.chestClickLatch = false;
    state.chestItemClickLatch = false;
    state.lootChestItemLatch = false;
    state.dialogueClickLatch = false;

    // A frame without a loot view or dialogue: the button that is about to open one is still held.
    Game::GameplayInputFrame input;
    input.leftMouseButton.held = true;
    Game::GameplayOverlayInputController::handleLootOverlayInput(runtime, input, false);
    runtime.resetDialogueOverlayInteractionState();

    // The overlay's first frame sees these latches set, so it neither grabs the item under the cursor nor arms a
    // button or topic for the release; each latch clears once the button is up.
    CHECK(state.chestClickLatch);
    CHECK(state.chestItemClickLatch);
    CHECK(state.lootChestItemLatch);
    CHECK(state.chestPressedTarget.type == Game::GameplayChestPointerTargetType::None);
    CHECK(state.dialogueClickLatch);
    CHECK(state.dialoguePressedTarget.type == Game::GameplayDialoguePointerTargetType::None);
}
