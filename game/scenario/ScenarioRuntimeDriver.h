#pragma once

#include "engine/ApplicationConfig.h"
#include "engine/AssetFileSystem.h"
#include "game/app/GameApplication.h"
#include "game/events/EventRuntime.h"
#include "game/gameplay/GameplayWorldInteraction.h"
#include "game/party/Party.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
struct ScenarioPose
{
    std::string mapFileName;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yawRadians = 0.0f;
    float pitchRadians = 0.0f;
};

struct ScenarioInteractionResult
{
    bool picked = false;
    bool activated = false;
    std::string mapFileName;
    GameplayWorldHit hit = {};
};

struct ScenarioMovementCommand
{
    ScenarioPose startPose = {};
    float durationSeconds = 0.0f;
    bool forward = true;
    bool backward = false;
    bool strafeLeft = false;
    bool strafeRight = false;
    bool jump = false;
    bool flyUp = false;
    bool flyDown = false;
    bool shift = false;
    bool ctrl = false;
    bool actorCollision = true;
    bool actorAi = true;
    std::optional<float> endYawRadians;
    std::optional<float> endPitchRadians;
};

struct ScenarioMovementResult
{
    ScenarioPose startPose = {};
    ScenarioPose finalPose = {};
};

struct ScenarioCombatLootRequest
{
    std::optional<size_t> actorIndex;
    std::string targetName;
    uint32_t expectedItemId = 0;
};

struct ScenarioCombatLootResult
{
    size_t actorIndex = GameplayInvalidWorldIndex;
    bool damageApplied = false;
    bool corpseActivated = false;
    bool worldItemActivated = false;
    bool expectedItemExposed = false;
    bool expectedItemAcquired = false;
};

struct ScenarioDialogActionSnapshot
{
    size_t index = 0;
    std::string kind;
    uint32_t id = 0;
    uint32_t secondaryId = 0;
    bool enabled = true;
    bool textOnly = false;
    std::string label;
};

struct ScenarioDialogSnapshot
{
    bool active = false;
    bool houseDialog = false;
    uint32_t sourceId = 0;
    std::string title;
    std::string houseTitle;
    size_t lineCount = 0;
    uint64_t textHash = 0;
    std::vector<ScenarioDialogActionSnapshot> actions;
};

struct ScenarioInputPromptSnapshot
{
    bool active = false;
    uint16_t eventId = 0;
    uint8_t continueStep = 0;
    uint8_t correctStep = 0;
    uint32_t textId = 0;
    std::string text;
    size_t answerCount = 0;
};

struct ScenarioInputPromptAnswerResult
{
    ScenarioInputPromptSnapshot prompt = {};
    bool matched = false;
    size_t matchedIndex = 0;
    uint8_t selectedContinueStep = 0;
};

class ScenarioRuntimeDriver
{
public:
    ScenarioRuntimeDriver(
        const std::filesystem::path &basePath,
        const Engine::ApplicationConfig &config,
        Engine::AssetFileSystem &assetFileSystem);
    ~ScenarioRuntimeDriver();

    bool initialize(std::string &failure);
    bool startNewGame(
        uint32_t continentId,
        const std::string &startMapFileName,
        std::string &failure,
        const PartySeed *pSeed = nullptr);
    bool holdGameplayAction(KeyboardAction action, float seconds, std::string &failure);
    bool selectPartyMember(size_t memberIndex, std::string &failure);
    bool loadMap(const std::string &mapFileName, std::string &failure);
    bool processPendingMapMove(std::string &failure);
    bool openMapTransitionPrompt(uint32_t sourceId, std::string &failure);
    bool speakNpc(uint32_t npcId, std::string &failure);
    bool saveGameToPath(const std::filesystem::path &path, std::string &failure);
    bool loadGameFromPath(const std::filesystem::path &path, std::string &failure);
    bool setPose(const ScenarioPose &pose, std::string &failure);
    ScenarioPose currentPose() const;
    bool simulateMovementSegment(
        const ScenarioMovementCommand &command,
        ScenarioMovementResult &result,
        std::string &failure);
    bool actorInspectState(
        size_t actorIndex,
        GameplayActorInspectState &inspectState,
        GameplayRuntimeActorState &runtimeState,
        bool &hasRuntimeState,
        std::string &failure) const;
    ScenarioInteractionResult pressKeyboardInteract(std::string &failure);
    ScenarioInteractionResult pressKeyboardInteractWithEventTargetActorOcclusion(
        uint32_t expectedTargetKind,
        size_t expectedTargetIndex,
        size_t expectedSecondaryIndex,
        uint16_t expectedTriggeredEventId,
        std::optional<uint16_t> expectedTrigger,
        bool &usedActorlessPick,
        std::string &failure);
    bool executeMapEvent(uint16_t eventId, std::string &failure);
    bool selectDialogAction(size_t actionIndex, std::string &failure);
    bool selectDialogActionById(uint32_t actionId, std::string &failure);
    bool answerPendingInputPrompt(
        const std::string &text,
        ScenarioInputPromptAnswerResult &result,
        std::string &failure);
    ScenarioInputPromptSnapshot activeInputPromptSnapshot() const;
    bool closeDialog(std::string &failure);
    ScenarioDialogSnapshot activeDialogSnapshot() const;
    std::string activeDialogSummary() const;
    bool advanceRuntime(float seconds, std::string &failure);
    bool advanceGameMinutes(float minutes, std::string &failure);
    bool executeCombatLoot(
        const ScenarioCombatLootRequest &request,
        ScenarioCombatLootResult &result,
        std::string &failure);
    ScenarioInteractionResult activateActor(size_t actorIndex, std::string &failure);
    // A party melee hit on a living actor through the normal damage path (it turns on the party, alerts its kin).
    bool damageActor(size_t actorIndex, int amount, std::string &failure);
    ScenarioInteractionResult activateWorldItem(
        size_t worldItemIndex,
        std::optional<uint16_t> recordedObjectDescriptionId,
        std::optional<uint16_t> recordedSpriteId,
        std::string &failure);
    bool worldItemContainsItem(
        std::optional<size_t> worldItemIndex,
        uint32_t itemId,
        bool &containsItem,
        std::string &failure) const;
    bool activeChestContainsItem(
        std::optional<uint32_t> chestId,
        uint32_t itemId,
        bool &containsItem,
        std::string &failure) const;
    bool takeActiveChestItemById(
        std::optional<uint32_t> chestId,
        uint32_t itemId,
        GameplayChestItemState &item,
        std::string &failure);
    bool takeInventoryItemToCursor(size_t memberIndex, uint8_t gridX, uint8_t gridY, std::string &failure);
    bool placeHeldItemInInventory(std::string &failure);
    bool triggerIndoorPressurePlate(size_t faceIndex, std::string &failure);
    bool triggerIndoorEventTarget(size_t faceIndex, std::string &failure);
    bool activeCorpseContainsItem(
        std::optional<uint32_t> sourceIndex,
        uint32_t itemId,
        bool &containsItem,
        std::string &failure) const;

    bool hasActiveRuntime() const;
    const std::string &currentMapFileName() const;
    float gameMinutes() const;
    void setPartyState(const Party &party);
    void setEventRuntimeState(const EventRuntimeState &eventRuntimeState);
    void setGameMinutes(float gameMinutes);
    Party *party();
    const Party *party() const;
    EventRuntimeState *eventRuntimeState();
    const EventRuntimeState *eventRuntimeState() const;

private:
    IGameplayWorldRuntime *worldRuntime();
    const IGameplayWorldRuntime *worldRuntime() const;
    bool loadSelectedMapRuntime(const std::string &mapFileName, std::string &failure);
    GameplayWorldPickRequest buildCenterPickRequest();
    ScenarioInteractionResult pickCenterInteractionTarget(bool ignoreActors, std::string &failure);
    bool activateInteractionResult(ScenarioInteractionResult &result, std::string &failure);

    std::filesystem::path m_basePath;
    Engine::ApplicationConfig m_config;
    Engine::AssetFileSystem *m_pAssetFileSystem = nullptr;
    GameApplication m_application;
    bool m_initialized = false;
};
}
