#include "game/gameplay/GameplayItemService.h"

#include "game/app/GameSession.h"
#include "game/audio/GameAudioSystem.h"
#include "game/events/EventRuntime.h"
#include "game/gameplay/GameMechanics.h"
#include "game/items/AlchemyBenchRuntime.h"
#include "game/items/ItemRuntime.h"
#include "game/gameplay/GameplayScreenRuntime.h"
#include "game/items/InventoryItemMixingRuntime.h"
#include "game/items/InventoryItemUseRuntime.h"
#include "game/party/SpellIds.h"
#include "game/tables/MergedBaseTables.h"
#include "game/tables/ObjectTable.h"
#include "game/tables/PotionNoteTable.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace OpenYAMM::Game
{
namespace
{
constexpr const char *IdentifyFailedText = "Identify Failed";
constexpr const char *RepairFailedText = "Repair Failed";
constexpr const char *NwcDungeonMapName = "7nwc.blv";
constexpr const char *TempleInBottleReturnLocationName = "TempleInABottleReturn";
constexpr const char *ConnectorStoneRechargeGlobalVar = "MMerge.CrossContinents.NextConnectorRecharge";
constexpr const char *ConnectorStoneImprovedGlobalVar = "MMerge.CrossContinents.ImprovedConnector";
constexpr const char *ConnectorStoneDivineInterventionGlobalVar = "MMerge.CrossContinents.DivineInterventionLastUsed";
constexpr uint32_t DimensionDoorScrollItemId = 190;
constexpr uint32_t ChargedConnectorStoneItemId = 624;
constexpr uint32_t DischargedConnectorStoneItemId = 625;
constexpr uint32_t VerdantNpcId = 803;
constexpr int32_t ConnectorStoneRechargeMinutes = 24 * 60;
constexpr int32_t ConnectorStoneDivineInterventionCooldownMinutes = 24 * 60;
constexpr int16_t FireballImpactObjectId = 1051;
constexpr float PotionExplosionForwardOffset = 64.0f;
constexpr float PotionExplosionHeightOffset = 96.0f;
constexpr uint32_t AutoNoteVariableTag = 0x00E1u;

enum class ActiveLootOperation
{
    ForceIdentify,
    IdentifyWithSkill,
    RepairWithSkill
};

bool applyLootOperation(
    GameplayChestItemState &lootItem,
    const ItemTable &itemTable,
    ActiveLootOperation operation,
    const Party *pParty,
    std::string &statusText)
{
    statusText.clear();

    if (lootItem.isGold)
    {
        return false;
    }

    const ItemDefinition *pItemDefinition = itemTable.get(lootItem.item.objectDescriptionId);

    if (pItemDefinition == nullptr)
    {
        statusText = "Unavailable.";
        return false;
    }

    switch (operation)
    {
        case ActiveLootOperation::ForceIdentify:
            if (lootItem.item.identified || !ItemRuntime::requiresIdentification(*pItemDefinition))
            {
                statusText = "Already identified.";
                return false;
            }

            lootItem.item.identified = true;
            statusText = "Identified " + ItemRuntime::displayName(lootItem.item, *pItemDefinition) + ".";
            return true;

        case ActiveLootOperation::IdentifyWithSkill:
            if (lootItem.item.identified || !ItemRuntime::requiresIdentification(*pItemDefinition))
            {
                statusText = "Already identified.";
                return false;
            }

            if (pParty == nullptr || !pParty->canIdentifyItem(*pItemDefinition))
            {
                statusText = IdentifyFailedText;
                return false;
            }

            lootItem.item.identified = true;
            statusText = "Identified " + ItemRuntime::displayName(lootItem.item, *pItemDefinition) + ".";
            return true;

        case ActiveLootOperation::RepairWithSkill:
        {
            if (!lootItem.item.broken)
            {
                statusText = "Nothing to repair.";
                return false;
            }

            if (pParty == nullptr || !pParty->canRepairItem(*pItemDefinition))
            {
                statusText = RepairFailedText;
                return false;
            }

            lootItem.item.broken = false;
            lootItem.item.identified = true;
            statusText = "Repaired " + ItemRuntime::displayName(lootItem.item, *pItemDefinition) + ".";
            return true;
        }
    }

    return false;
}

Party *activeParty(GameSession &session)
{
    IGameplayWorldRuntime *pWorldRuntime = session.activeWorldRuntime();
    return pWorldRuntime != nullptr ? pWorldRuntime->party() : nullptr;
}

bool applyWorldItemOperation(
    IGameplayWorldRuntime &worldRuntime,
    size_t worldItemIndex,
    const ItemTable &itemTable,
    ActiveLootOperation operation,
    const Party *pParty,
    std::string &statusText)
{
    GameplayWorldItemInspectState worldItemState = {};

    if (!worldRuntime.worldItemInspectState(worldItemIndex, worldItemState) || worldItemState.isGold)
    {
        return false;
    }

    GameplayChestItemState lootItem = {};
    lootItem.item = worldItemState.item;

    if (!applyLootOperation(lootItem, itemTable, operation, pParty, statusText))
    {
        return false;
    }

    return worldRuntime.updateWorldItemInspectState(worldItemIndex, lootItem.item);
}

std::string lowerAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool isNwcDungeonMapName(const std::string &mapFileName)
{
    const std::string normalized = lowerAscii(mapFileName);
    return normalized == NwcDungeonMapName || normalized == std::string("data/games/") + NwcDungeonMapName;
}

bool openDimensionDoorOverlay(GameplayScreenRuntime &runtime)
{
    const Party *pParty = runtime.partyReadOnly();
    const size_t casterMemberIndex = pParty != nullptr ? pParty->activeMemberIndex() : 0;
    return runtime.openDimensionDoorOverlay(casterMemberIndex, spellIdValue(SpellId::TownPortal));
}

bool connectorStoneRechargeReady(const EventRuntimeState &eventRuntimeState, int32_t currentMinutes)
{
    const auto rechargeIterator = eventRuntimeState.namedGlobalVars.find(ConnectorStoneRechargeGlobalVar);
    const int32_t rechargeMinutes = rechargeIterator != eventRuntimeState.namedGlobalVars.end()
        ? rechargeIterator->second
        : 0;

    return currentMinutes >= rechargeMinutes;
}

size_t rechargeConnectorStonesInInventory(Party &party)
{
    size_t rechargedCount = 0;

    for (size_t memberIndex = 0; memberIndex < party.memberCount(); ++memberIndex)
    {
        Character *pMember = party.member(memberIndex);

        if (pMember == nullptr)
        {
            continue;
        }

        for (InventoryItem &item : pMember->inventory)
        {
            if (item.objectDescriptionId == DischargedConnectorStoneItemId)
            {
                item.objectDescriptionId = ChargedConnectorStoneItemId;
                ++rechargedCount;
            }
        }
    }

    return rechargedCount;
}

bool rechargeHeldConnectorStone(
    GameplayUiController::HeldInventoryItemState &heldItem,
    Party &party)
{
    if (!heldItem.active || heldItem.item.objectDescriptionId != DischargedConnectorStoneItemId)
    {
        return false;
    }

    heldItem.item.objectDescriptionId = ChargedConnectorStoneItemId;
    party.setHeldItemForQueries(heldItem.item);
    return true;
}

void saveTempleInBottleReturnLocation(GameplayScreenRuntime &runtime)
{
    IGameplayWorldRuntime *pWorldRuntime = runtime.worldRuntime();
    EventRuntimeState *pEventRuntimeState =
        pWorldRuntime != nullptr ? pWorldRuntime->eventRuntimeState() : nullptr;

    if (pWorldRuntime == nullptr || pEventRuntimeState == nullptr)
    {
        return;
    }

    EventRuntimeState::SavedLocation location = {};
    location.x = static_cast<int32_t>(std::lround(runtime.partyX()));
    location.y = static_cast<int32_t>(std::lround(runtime.partyY()));
    location.z = static_cast<int32_t>(std::lround(runtime.partyFootZ()));
    location.continentId = pEventRuntimeState->activeHistoryContinentId;
    location.mapName = runtime.currentMapFileName();
    pEventRuntimeState->savedLocations[TempleInBottleReturnLocationName] = std::move(location);
}

InventoryItemUseContext buildInventoryItemUseContext(const GameplayScreenRuntime &runtime)
{
    InventoryItemUseContext context = {};
    const IGameplayWorldRuntime *pWorldRuntime = runtime.worldRuntime();

    if (pWorldRuntime != nullptr)
    {
        context.underwater = pWorldRuntime->isUnderwaterMap();
        context.gameMinutes = pWorldRuntime->gameMinutes();
    }

    return context;
}

void closeCharacterInventoryAfterPotionExplosion(GameplayScreenRuntime &runtime)
{
    GameplayUiController::CharacterScreenState &characterScreen = runtime.characterScreen();
    characterScreen.open = false;
    characterScreen.dollJewelryOverlayOpen = false;
    characterScreen.adventurersInnRosterOverlayOpen = false;
    runtime.closeInventoryNestedOverlay();
    runtime.itemInspectOverlay() = {};
    runtime.characterDetailOverlay() = {};
}

bool tryUseConnectorStone(GameplayScreenRuntime &runtime, GameplayUiController::HeldInventoryItemState &heldItem)
{
    uint32_t itemId = heldItem.item.objectDescriptionId;

    if (itemId != ChargedConnectorStoneItemId && itemId != DischargedConnectorStoneItemId)
    {
        return false;
    }

    IGameplayWorldRuntime *pWorldRuntime = runtime.worldRuntime();
    EventRuntimeState *pEventRuntimeState = pWorldRuntime != nullptr ? pWorldRuntime->eventRuntimeState() : nullptr;
    Party *pParty = runtime.party();

    if (pEventRuntimeState == nullptr || pParty == nullptr || pWorldRuntime == nullptr)
    {
        return true;
    }

    const int32_t currentMinutes = static_cast<int32_t>(std::lround(pWorldRuntime->gameMinutes()));
    if (connectorStoneRechargeReady(*pEventRuntimeState, currentMinutes))
    {
        rechargeConnectorStonesInInventory(*pParty);
        rechargeHeldConnectorStone(heldItem, *pParty);
        itemId = heldItem.item.objectDescriptionId;
    }

    if (itemId == DischargedConnectorStoneItemId)
    {
        runtime.setStatusBarEvent("The connector stone is recharging.");
        return true;
    }

    const auto improvedIterator = pEventRuntimeState->namedGlobalVars.find(ConnectorStoneImprovedGlobalVar);
    const bool improved = improvedIterator != pEventRuntimeState->namedGlobalVars.end()
        && improvedIterator->second != 0;
    const auto lastInterventionIterator =
        pEventRuntimeState->namedGlobalVars.find(ConnectorStoneDivineInterventionGlobalVar);
    const int32_t lastInterventionMinutes = lastInterventionIterator != pEventRuntimeState->namedGlobalVars.end()
        ? lastInterventionIterator->second
        : -ConnectorStoneDivineInterventionCooldownMinutes;

    if (improved && currentMinutes >= lastInterventionMinutes + ConnectorStoneDivineInterventionCooldownMinutes)
    {
        bool needsRestore = false;

        for (const Character &member : pParty->members())
        {
            if (member.health < member.maxHealth || member.spellPoints < member.maxSpellPoints)
            {
                needsRestore = true;
                break;
            }
        }

        if (needsRestore)
        {
            pParty->restoreAll();
            pEventRuntimeState->namedGlobalVars[ConnectorStoneDivineInterventionGlobalVar] = currentMinutes;
        }
    }

    heldItem.item.objectDescriptionId = DischargedConnectorStoneItemId;
    pParty->setHeldItemForQueries(heldItem.item);
    pEventRuntimeState->namedGlobalVars[ConnectorStoneRechargeGlobalVar] =
        currentMinutes + ConnectorStoneRechargeMinutes;

    const auto mainQuestIterator =
        pEventRuntimeState->namedGlobalVars.find("MMerge.CrossContinents.GotMainQuest");
    const bool hasMainQuest = mainQuestIterator != pEventRuntimeState->namedGlobalVars.end()
        && mainQuestIterator->second != 0;
    pEventRuntimeState->npcTopicOverrides[VerdantNpcId][0] = hasMainQuest ? 1788u : 1778u;

    EventRuntimeState::PendingDialogueContext dialogueContext = {};
    dialogueContext.kind = DialogueContextKind::NpcTalk;
    dialogueContext.sourceId = VerdantNpcId;
    pEventRuntimeState->pendingDialogueContext = std::move(dialogueContext);

    GameplayUiController::CharacterScreenState &characterScreen = runtime.characterScreen();
    characterScreen.open = false;
    characterScreen.dollJewelryOverlayOpen = false;
    characterScreen.adventurersInnRosterOverlayOpen = false;
    runtime.closeInventoryNestedOverlay();
    runtime.setStatusBarEvent("The connector stone calls Verdant.");
    return true;
}

void spawnPotionExplosionImpactVisual(GameSession &session, GameplayScreenRuntime &runtime)
{
    if (!session.hasDataRepository())
    {
        return;
    }

    GameplayProjectileService &projectileService = session.gameplayProjectileService();
    const ObjectTable &objectTable = session.data().objectTable();
    const std::optional<uint16_t> impactDescriptionId =
        objectTable.findDescriptionIdByObjectId(FireballImpactObjectId);

    if (!impactDescriptionId)
    {
        return;
    }

    const std::optional<GameplayProjectileService::ProjectileImpactVisualDefinition> impactDefinition =
        projectileService.buildProjectileImpactVisualDefinition(*impactDescriptionId, &objectTable, nullptr);

    if (!impactDefinition)
    {
        return;
    }

    const float yawRadians = runtime.gameplayCameraYawRadians();
    const float x = runtime.partyX() + std::cos(yawRadians) * PotionExplosionForwardOffset;
    const float y = runtime.partyY() + std::sin(yawRadians) * PotionExplosionForwardOffset;
    const float z = runtime.partyFootZ() + PotionExplosionHeightOffset;

    runtime.fxService().spawnImmediateSpellImpactVisual(
        *impactDefinition,
        static_cast<int>(spellIdValue(SpellId::Fireball)),
        "Fireball",
        "fire04",
        x,
        y,
        z,
        false,
        false);
}
}

GameplayItemService::GameplayItemService(GameSession &session)
    : m_session(session)
{
}

void GameplayItemService::updateConnectorStoneRecharge(
    Party &party,
    EventRuntimeState &eventRuntimeState,
    float gameMinutes)
{
    const int32_t currentMinutes = static_cast<int32_t>(std::lround(gameMinutes));

    if (connectorStoneRechargeReady(eventRuntimeState, currentMinutes))
    {
        rechargeConnectorStonesInInventory(party);
    }
}

bool GameplayItemService::tryUseHeldItemOnPartyMember(
    GameplayScreenRuntime &runtime,
    size_t memberIndex,
    bool keepCharacterScreenOpen)
{
    GameplayUiController &uiController = m_session.gameplayUiController();
    GameplayUiController::HeldInventoryItemState &heldItem = uiController.heldInventoryItem();
    Party *pParty = runtime.party();
    const ItemTable *pItemTable = m_session.hasDataRepository() ? &m_session.data().itemTable() : nullptr;
    const ReadableScrollTable *pReadableScrollTable =
        m_session.hasDataRepository() ? &m_session.data().readableScrollTable() : nullptr;
    const MergedPotionSettingTable *pPotionSettingTable =
        m_session.hasDataRepository() ? &m_session.data().mergedPotionSettingTable() : nullptr;
    const MergedReagentSettingTable *pReagentSettingTable =
        m_session.hasDataRepository() ? &m_session.data().mergedReagentSettingTable() : nullptr;

    if (!heldItem.active || pParty == nullptr || pItemTable == nullptr)
    {
        return false;
    }

    if (tryUseConnectorStone(runtime, heldItem))
    {
        return true;
    }

    const InventoryItemUseResult useResult =
        InventoryItemUseRuntime::useItemOnMember(
            *pParty,
            memberIndex,
            heldItem.item,
            *pItemTable,
            pReadableScrollTable,
            pPotionSettingTable,
            pReagentSettingTable,
            buildInventoryItemUseContext(runtime));

    if (!useResult.handled)
    {
        return false;
    }

    if (useResult.action == InventoryItemUseAction::CastScroll)
    {
        if (!useResult.consumed)
        {
            if (runtime.audioSystem() != nullptr)
            {
                runtime.audioSystem()->playCommonSound(SoundId::Error, GameAudioSystem::PlaybackGroup::Ui);
            }
        }
        else
        {
            if (heldItem.item.objectDescriptionId == DimensionDoorScrollItemId)
            {
                if (openDimensionDoorOverlay(runtime))
                {
                    heldItem = {};
                    pParty->clearHeldItemForQueries();
                }

                GameplayUiController::CharacterScreenState &characterScreen =
                    m_session.gameplayScreenState().characterScreen();
                characterScreen.open = false;
                characterScreen.dollJewelryOverlayOpen = false;
                return true;
            }

            const SpellTable *pSpellTable = m_session.hasDataRepository() ? &m_session.data().spellTable() : nullptr;

            if (pSpellTable == nullptr)
            {
                runtime.setStatusBarEvent("Spell data missing");
                return true;
            }

            const SpellEntry *pSpellEntry = pSpellTable->findById(useResult.spellId);

            if (pSpellEntry == nullptr)
            {
                runtime.setStatusBarEvent("Unknown scroll spell");
                return true;
            }

            PartySpellCastRequest request = {};
            request.casterMemberIndex = memberIndex;
            request.spellId = useResult.spellId;
            request.skillLevelOverride = useResult.spellSkillLevelOverride;
            request.skillMasteryOverride = useResult.spellSkillMasteryOverride;
            request.bypassRequiredMastery = useResult.spellBypassRequiredMastery;
            request.spendMana = false;
            request.applyRecovery = true;

            if (!runtime.tryCastSpellRequest(request, pSpellEntry->name))
            {
                return true;
            }

            heldItem = {};
            pParty->clearHeldItemForQueries();
        }
    }
    else if (useResult.action == InventoryItemUseAction::UseDimensionDoorScroll)
    {
        if (useResult.consumed && openDimensionDoorOverlay(runtime))
        {
            heldItem = {};
            pParty->clearHeldItemForQueries();
        }

        GameplayUiController::CharacterScreenState &characterScreen =
            m_session.gameplayScreenState().characterScreen();
        characterScreen.open = false;
        characterScreen.dollJewelryOverlayOpen = false;
        return true;
    }
    else if (useResult.action == InventoryItemUseAction::ReadMessageScroll)
    {
        GameplayUiController::ReadableScrollOverlayState &overlay = uiController.readableScrollOverlay();
        overlay.active = true;
        overlay.title = useResult.readableTitle;
        overlay.body = useResult.readableBody;
        learnRecipeScroll(runtime, *pParty, memberIndex, heldItem.item.objectDescriptionId);
    }
    else
    {
        if (useResult.consumed)
        {
            heldItem = {};
            pParty->clearHeldItemForQueries();
        }

        if (useResult.action == InventoryItemUseAction::ConsumePotion
            && useResult.consumed
            && runtime.audioSystem() != nullptr)
        {
            runtime.audioSystem()->playCommonSound(SoundId::Drink, GameAudioSystem::PlaybackGroup::Ui);
            runtime.triggerPortraitFaceAnimation(memberIndex, FaceAnimationId::DrinkPotion);
        }

        if (useResult.action == InventoryItemUseAction::UseHorseshoe && useResult.consumed)
        {
            m_session.gameplayFxService().triggerPortraitEventFxWithoutSpeech(
                runtime,
                memberIndex,
                PortraitFxEventKind::QuestComplete);
        }
        else if (useResult.action == InventoryItemUseAction::UseGenieLamp && useResult.consumed)
        {
            m_session.gameplayFxService().triggerPortraitEventFxWithoutSpeech(
                runtime,
                memberIndex,
                PortraitFxEventKind::QuestComplete);
        }
        else if (useResult.action == InventoryItemUseAction::LearnSpell
                 && !useResult.consumed
                 && useResult.alreadyKnown
                 && runtime.audioSystem() != nullptr)
        {
            runtime.audioSystem()->playCommonSound(SoundId::Error, GameAudioSystem::PlaybackGroup::Ui);
        }

        if (useResult.speechId.has_value())
        {
            runtime.playSpeechReaction(memberIndex, *useResult.speechId, true);
        }

        if (useResult.soundId.has_value() && runtime.audioSystem() != nullptr)
        {
            runtime.audioSystem()->playCommonSound(*useResult.soundId, GameAudioSystem::PlaybackGroup::Ui);
        }

        if (useResult.action == InventoryItemUseAction::UseTempleInABottle
            && !isNwcDungeonMapName(m_session.currentMapFileName()))
        {
            saveTempleInBottleReturnLocation(runtime);

            EventRuntimeState::PendingMapMove pendingMapMove = {};
            pendingMapMove.mapName = NwcDungeonMapName;
            pendingMapMove.useMapStartPosition = true;
            m_session.setPendingMapMove(std::move(pendingMapMove));
        }
    }

    if (!useResult.statusText.empty())
    {
        runtime.setStatusBarEvent(useResult.statusText);
    }

    const bool closeCharacterScreen =
        !keepCharacterScreenOpen
        || (useResult.action == InventoryItemUseAction::CastScroll && useResult.consumed)
        || useResult.action == InventoryItemUseAction::UseTempleInABottle;

    if (closeCharacterScreen)
    {
        GameplayUiController::CharacterScreenState &characterScreen = m_session.gameplayScreenState().characterScreen();
        characterScreen.open = false;
        characterScreen.dollJewelryOverlayOpen = false;
    }

    return true;
}

bool GameplayItemService::tryUseHeldItemOnInventoryItem(
    GameplayScreenRuntime &runtime,
    size_t memberIndex,
    uint8_t targetGridX,
    uint8_t targetGridY)
{
    GameplayUiController::HeldInventoryItemState &heldItem = m_session.gameplayUiController().heldInventoryItem();
    Party *pParty = runtime.party();
    const ItemTable *pItemTable = m_session.hasDataRepository() ? &m_session.data().itemTable() : nullptr;
    const PotionMixingTable *pPotionMixingTable =
        m_session.hasDataRepository() ? &m_session.data().potionMixingTable() : nullptr;
    const MergedPotionSettingTable *pPotionSettingTable =
        m_session.hasDataRepository() ? &m_session.data().mergedPotionSettingTable() : nullptr;
    const MergedReagentSettingTable *pReagentSettingTable =
        m_session.hasDataRepository() ? &m_session.data().mergedReagentSettingTable() : nullptr;
    const PotionNoteTable *pPotionNoteTable =
        m_session.hasDataRepository() ? &m_session.data().potionNoteTable() : nullptr;

    if (!heldItem.active
        || pParty == nullptr
        || pItemTable == nullptr
        || pPotionMixingTable == nullptr
        || pPotionSettingTable == nullptr
        || pReagentSettingTable == nullptr
        || pPotionNoteTable == nullptr)
    {
        return false;
    }

    const InventoryItemMixResult mixResult = InventoryItemMixingRuntime::tryApplyHeldItemToInventoryItem(
        *pParty,
        memberIndex,
        heldItem.item,
        targetGridX,
        targetGridY,
        *pItemTable,
        *pPotionMixingTable,
        *pPotionSettingTable,
        *pReagentSettingTable,
        pPotionNoteTable);

    if (!mixResult.handled)
    {
        return false;
    }

    if (mixResult.heldItemReplacement.has_value())
    {
        heldItem.item = *mixResult.heldItemReplacement;
        heldItem.grabCellOffsetX = 0;
        heldItem.grabCellOffsetY = 0;
        heldItem.grabOffsetX = 0.0f;
        heldItem.grabOffsetY = 0.0f;
        pParty->setHeldItemForQueries(heldItem.item);
    }
    else if (mixResult.heldItemConsumed)
    {
        heldItem = {};
        pParty->clearHeldItemForQueries();
    }

    applyMixResultEffects(runtime, *pParty, memberIndex, mixResult, false);
    return true;
}

void GameplayItemService::unlockAutonote(
    GameplayScreenRuntime &runtime,
    Party &party,
    size_t memberIndex,
    uint32_t autonoteId)
{
    IGameplayWorldRuntime *pWorldRuntime = runtime.worldRuntime();
    EventRuntimeState *pEventRuntimeState = pWorldRuntime != nullptr ? pWorldRuntime->eventRuntimeState() : nullptr;

    if (pEventRuntimeState != nullptr)
    {
        const uint32_t rawId = (autonoteId << 16) | AutoNoteVariableTag;
        const EventRuntime::VariableRef variable = EventRuntime::decodeVariable(rawId);
        const std::vector<size_t> targetMemberIndices = {memberIndex};
        EventRuntime::setVariableValue(*pEventRuntimeState, variable, 1, &party, targetMemberIndices);
    }
}

void GameplayItemService::learnRecipeScroll(
    GameplayScreenRuntime &runtime,
    Party &party,
    size_t memberIndex,
    uint32_t scrollItemId)
{
    if (!m_session.hasDataRepository())
    {
        return;
    }

    const GameDataRepository &data = m_session.data();
    const AlchemyBenchTables tables = {
        data.itemTable(),
        data.potionMixingTable(),
        data.potionNoteTable(),
        data.mergedPotionSettingTable(),
        data.mergedReagentSettingTable()};
    const std::optional<uint32_t> potionItemId = AlchemyBenchRuntime::recipeScrollPotionItemId(scrollItemId, tables);

    if (!potionItemId.has_value())
    {
        return;
    }

    bool learned = false;

    for (const uint32_t autonoteId : AlchemyBenchRuntime::potionNoteIds(*potionItemId, tables))
    {
        if (!runtime.isAutonoteUnlocked(autonoteId))
        {
            unlockAutonote(runtime, party, memberIndex, autonoteId);
            learned = true;
        }
    }

    if (learned)
    {
        const ItemDefinition *pPotion = tables.itemTable.get(*potionItemId);
        runtime.setStatusBarEvent("Learned recipe: " + (pPotion != nullptr ? pPotion->name : std::string("potion")));
    }
}

void GameplayItemService::applyMixResultEffects(
    GameplayScreenRuntime &runtime,
    Party &party,
    size_t memberIndex,
    const InventoryItemMixResult &mixResult,
    bool keepInventoryOpen)
{
    if (mixResult.unlockedAutonoteId != 0)
    {
        unlockAutonote(runtime, party, memberIndex, mixResult.unlockedAutonoteId);
    }

    const bool potionExplosion =
        mixResult.action == InventoryItemMixAction::PotionMix
        && !mixResult.success
        && mixResult.failureDamageLevel != 0;

    if (potionExplosion)
    {
        party.applyPotionExplosionToMember(memberIndex, mixResult.failureDamageLevel);

        if (!keepInventoryOpen)
        {
            closeCharacterInventoryAfterPotionExplosion(runtime);
        }

        spawnPotionExplosionImpactVisual(m_session, runtime);
    }

    if (runtime.audioSystem() != nullptr)
    {
        if (mixResult.success && mixResult.action == InventoryItemMixAction::PotionMix)
        {
            runtime.audioSystem()->playCommonSound(SoundId::MixPotion, GameAudioSystem::PlaybackGroup::Ui);
        }
        else if (potionExplosion)
        {
            runtime.audioSystem()->playCommonSound(SoundId::Fireball, GameAudioSystem::PlaybackGroup::Ui);
        }
        else if (!mixResult.success)
        {
            runtime.audioSystem()->playCommonSound(SoundId::Error, GameAudioSystem::PlaybackGroup::Ui);
        }
    }

    if (mixResult.action == InventoryItemMixAction::PotionMix
        || mixResult.action == InventoryItemMixAction::ReagentBottleMix)
    {
        Character *pMember = party.member(memberIndex);
        const bool canPlayPotionReaction =
            mixResult.success
            || !potionExplosion
            || (pMember != nullptr && GameMechanics::canAct(*pMember));

        if (canPlayPotionReaction)
        {
            runtime.triggerPortraitFaceAnimation(
                memberIndex,
                mixResult.success ? FaceAnimationId::MixPotion : FaceAnimationId::PotionExplode);
            runtime.playSpeechReaction(
                memberIndex,
                mixResult.success ? SpeechId::PotionSuccess : SpeechId::PotionFail,
                true);
        }
    }
    else if (mixResult.action == InventoryItemMixAction::RechargePotion && !mixResult.success)
    {
        runtime.playSpeechReaction(memberIndex, SpeechId::PotionFail, true);
    }

    if (!mixResult.statusText.empty())
    {
        runtime.setStatusBarEvent(mixResult.statusText);
    }
}

bool GameplayItemService::mixAlchemyBench(GameplayScreenRuntime &runtime, bool mixAll)
{
    Party *pParty = runtime.party();

    if (pParty == nullptr || !m_session.hasDataRepository())
    {
        return false;
    }

    const GameDataRepository &data = m_session.data();
    const AlchemyBenchTables tables = {
        data.itemTable(),
        data.potionMixingTable(),
        data.potionNoteTable(),
        data.mergedPotionSettingTable(),
        data.mergedReagentSettingTable()};
    GameplayUiController::AlchemyBenchState &bench = runtime.characterScreen().alchemyBench;
    const AlchemistInfo alchemist = AlchemyBenchRuntime::resolveAlchemist(*pParty);
    const size_t alchemistIndex = alchemist.memberIndex.value_or(pParty->activeMemberIndex());
    const uint32_t bottleItemId = tables.potionSettingTable.emptyBottleItemId();
    AlchemyRecipe recipe = {bench.targetItemId, bench.heldItemId};
    // A mix the party had not tried, that records a potion note, is a discovery.
    const bool untried = AlchemyBenchRuntime::preview(*pParty, recipe, tables,
        [&runtime](uint32_t autonoteId)
        {
            return runtime.isAutonoteUnlocked(autonoteId);
        }).outcome == AlchemyPreviewOutcome::Unknown;
    InventoryItemMixResult lastResult = {};
    InventoryItem brewedItem = {};
    uint32_t mixedCount = 0;
    bool bottleOverflow = false;

    while (true)
    {
        const AlchemyMixOutcome outcome = AlchemyBenchRuntime::mix(*pParty, recipe, tables);

        if (!outcome.result.handled)
        {
            break;
        }

        lastResult = outcome.result;
        brewedItem = outcome.resultItem;

        if (outcome.overflowItem.has_value())
        {
            // No pack had room for the empty bottle; hand it to the cursor and step back to the pack.
            GameplayUiController::HeldInventoryItemState &heldItem = runtime.heldInventoryItem();
            heldItem = {};
            heldItem.active = true;
            heldItem.item = *outcome.overflowItem;
            pParty->setHeldItemForQueries(heldItem.item);
            bench.active = false;
            bottleOverflow = true;
        }

        if (!outcome.result.success)
        {
            break;
        }

        ++mixedCount;

        if (!mixAll || bottleOverflow)
        {
            break;
        }

        const std::vector<AlchemyIngredient> ingredients = AlchemyBenchRuntime::collectIngredients(*pParty, tables);

        if (AlchemyBenchRuntime::brewableCount(recipe, ingredients) > 0)
        {
            continue;
        }

        // Filling bottles moves on to the next reagent that makes the same potion.
        const std::optional<uint32_t> reagentResult =
            tables.reagentSettingTable.resultItemIdForReagent(recipe.heldItemId);

        if (recipe.targetItemId != bottleItemId || !reagentResult.has_value())
        {
            break;
        }

        AlchemyBookEntry reagents = {};
        reagents.reagentRecipe = true;

        for (const MergedReagentSettingEntry &reagent : tables.reagentSettingTable.entries())
        {
            if (reagent.resultItemId == *reagentResult)
            {
                reagents.recipes.push_back({bottleItemId, reagent.itemId});
            }
        }

        const std::optional<AlchemyRecipe> nextRecipe = AlchemyBenchRuntime::pickRecipe(reagents, ingredients);

        if (!nextRecipe.has_value() || AlchemyBenchRuntime::brewableCount(*nextRecipe, ingredients) == 0)
        {
            break;
        }

        recipe = *nextRecipe;
    }

    if (!lastResult.handled)
    {
        runtime.setStatusBarEvent("Nothing happens.");

        if (runtime.audioSystem() != nullptr)
        {
            runtime.audioSystem()->playCommonSound(SoundId::Error, GameAudioSystem::PlaybackGroup::Ui);
        }

        return false;
    }

    const ItemDefinition *pBrewed = tables.itemTable.get(brewedItem.objectDescriptionId);
    const std::string brewedName = pBrewed != nullptr ? pBrewed->name : "potion";
    const bool discovery = untried && lastResult.success && lastResult.unlockedAutonoteId != 0;

    if (bottleOverflow)
    {
        lastResult.statusText = "No room for the empty bottle.";
    }
    else if (discovery)
    {
        lastResult.statusText = "Discovered: " + brewedName + "!";
    }
    else if (mixedCount > 1)
    {
        lastResult.statusText = "Brewed " + std::to_string(mixedCount) + " x " + brewedName;
    }
    else if (lastResult.success)
    {
        lastResult.statusText = "Brewed " + brewedName + " (power " + std::to_string(brewedItem.standardEnchantPower)
            + ")";
    }

    // The slots keep the mixed pair, faded once the party runs out, and the result slot shows what was brewed.
    bench.targetItemId = recipe.targetItemId;
    bench.heldItemId = recipe.heldItemId;
    bench.clearBrewResult();
    bench.brewedTicks = SDL_GetTicks();
    bench.brewedExploded = lastResult.failureDamageLevel != 0;

    if (lastResult.success)
    {
        bench.brewedItemId = brewedItem.objectDescriptionId;
        bench.brewedPower = brewedItem.standardEnchantPower;
        bench.brewedCount = mixedCount;
        bench.brewedDiscovery = discovery;
        bench.selectedResultItemId = brewedItem.objectDescriptionId;
        bench.brewedScrollPending = true;
    }

    if (discovery)
    {
        bench.newRecipeResultIds.push_back(brewedItem.objectDescriptionId);

        if (runtime.audioSystem() != nullptr)
        {
            runtime.audioSystem()->playCommonSound(SoundId::Quest, GameAudioSystem::PlaybackGroup::Ui);
        }
    }

    applyMixResultEffects(runtime, *pParty, alchemistIndex, lastResult, true);
    return true;
}

bool GameplayItemService::brewAlchemyChain(GameplayScreenRuntime &runtime, bool brewAll)
{
    constexpr uint32_t MaxChainBrews = 99;
    Party *pParty = runtime.party();

    if (pParty == nullptr || !m_session.hasDataRepository())
    {
        return false;
    }

    const GameDataRepository &data = m_session.data();
    const AlchemyBenchTables tables = {
        data.itemTable(),
        data.potionMixingTable(),
        data.potionNoteTable(),
        data.mergedPotionSettingTable(),
        data.mergedReagentSettingTable()};
    GameplayUiController::AlchemyBenchState &bench = runtime.characterScreen().alchemyBench;
    const AlchemistInfo alchemist = AlchemyBenchRuntime::resolveAlchemist(*pParty);
    const AlchemyBook book = AlchemyBenchRuntime::buildBook(
        tables,
        AlchemyBenchRuntime::collectIngredients(*pParty, tables),
        alchemist.mastery,
        [&runtime](uint32_t autonoteId)
        {
            return runtime.isAutonoteUnlocked(autonoteId);
        });
    const AlchemyChainPlan plan = AlchemyChainPlanner(*pParty, book, alchemist.level, tables)
        .plan(bench.selectedResultItemId, brewAll ? MaxChainBrews : 1);

    if (plan.brewCount == 0)
    {
        return false;
    }

    const AlchemyChainOutcome outcome = AlchemyBenchRuntime::brewChain(*pParty, plan, tables);
    InventoryItemMixResult result = outcome.lastMix.result;
    const ItemDefinition *pBrewed = tables.itemTable.get(outcome.lastBrewed.objectDescriptionId);
    const std::string brewedName = pBrewed != nullptr ? pBrewed->name : "potion";
    const std::string steps = std::to_string(outcome.stepsDone) + (outcome.stepsDone == 1 ? " step" : " steps");

    if (outcome.lastMix.overflowItem.has_value())
    {
        // No pack had room for an empty bottle; hand it to the cursor and step back to the pack.
        GameplayUiController::HeldInventoryItemState &heldItem = runtime.heldInventoryItem();
        heldItem = {};
        heldItem.active = true;
        heldItem.item = *outcome.lastMix.overflowItem;
        pParty->setHeldItemForQueries(heldItem.item);
        bench.active = false;
        result.statusText = "No room for the empty bottle.";
    }
    else if (outcome.brewedCount == 0)
    {
        result.statusText = "Brewing stopped after " + steps + ".";
    }
    else if (outcome.brewedCount > 1)
    {
        result.statusText = "Brewed " + std::to_string(outcome.brewedCount) + " x " + brewedName + " (" + steps + ")";
    }
    else
    {
        result.statusText = "Brewed " + brewedName + " (power "
            + std::to_string(outcome.lastBrewed.standardEnchantPower) + ", " + steps + ")";
    }

    bench.clearBrewResult();
    bench.brewedTicks = SDL_GetTicks();

    if (outcome.brewedCount > 0)
    {
        bench.brewedItemId = outcome.lastBrewed.objectDescriptionId;
        bench.brewedPower = outcome.lastBrewed.standardEnchantPower;
        bench.brewedCount = outcome.brewedCount;
        bench.brewedScrollPending = true;
    }

    applyMixResultEffects(runtime, *pParty, alchemist.memberIndex.value_or(pParty->activeMemberIndex()), result, true);
    return outcome.brewedCount > 0;
}

void GameplayItemService::updateReadableScrollOverlayForHeldItem(
    GameplayScreenRuntime &runtime,
    size_t memberIndex,
    const GameplayCharacterPointerTarget &pointerTarget,
    bool isLeftMousePressed)
{
    GameplayUiController &uiController = m_session.gameplayUiController();
    GameplayUiController::ReadableScrollOverlayState &overlay = uiController.readableScrollOverlay();
    overlay = {};

    const ItemTable *pItemTable = m_session.hasDataRepository() ? &m_session.data().itemTable() : nullptr;
    Party *pParty = activeParty(m_session);
    const GameplayUiController::HeldInventoryItemState &heldItem = uiController.heldInventoryItem();
    const ReadableScrollTable *pReadableScrollTable =
        m_session.hasDataRepository() ? &m_session.data().readableScrollTable() : nullptr;

    if (!isLeftMousePressed
        || !heldItem.active
        || pItemTable == nullptr
        || pParty == nullptr
        || (pointerTarget.type != GameplayCharacterPointerTargetType::EquipmentSlot
            && pointerTarget.type != GameplayCharacterPointerTargetType::DollPanel))
    {
        return;
    }

    const InventoryItemUseAction useAction =
        InventoryItemUseRuntime::classifyItemUse(
            heldItem.item,
            *pItemTable,
            &m_session.data().mergedPotionSettingTable(),
            &m_session.data().mergedReagentSettingTable());

    if (useAction != InventoryItemUseAction::ReadMessageScroll)
    {
        return;
    }

    const InventoryItemUseResult useResult =
        InventoryItemUseRuntime::useItemOnMember(
            *pParty,
            memberIndex,
            heldItem.item,
            *pItemTable,
            pReadableScrollTable,
            &m_session.data().mergedPotionSettingTable(),
            &m_session.data().mergedReagentSettingTable());

    if (!useResult.handled || useResult.action != InventoryItemUseAction::ReadMessageScroll)
    {
        return;
    }

    overlay.active = true;
    overlay.title = useResult.readableTitle;
    overlay.body = useResult.readableBody;
    learnRecipeScroll(runtime, *pParty, memberIndex, heldItem.item.objectDescriptionId);
}

bool GameplayItemService::identifyInspectedItem(
    const GameplayUiController::ItemInspectOverlayState &overlay,
    std::string &statusText)
{
    statusText.clear();

    Party *pParty = activeParty(m_session);
    IGameplayWorldRuntime *pWorldRuntime = m_session.activeWorldRuntime();
    const ItemTable *pItemTable = m_session.hasDataRepository() ? &m_session.data().itemTable() : nullptr;

    if (pParty == nullptr || pItemTable == nullptr)
    {
        return false;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Inventory)
    {
        return pParty->identifyMemberInventoryItem(
            overlay.sourceMemberIndex,
            overlay.sourceGridX,
            overlay.sourceGridY,
            statusText);
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Equipment)
    {
        return pParty->identifyEquippedItem(
            overlay.sourceMemberIndex,
            overlay.sourceEquipmentSlot,
            statusText);
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Chest && pWorldRuntime != nullptr)
    {
        GameplayChestViewState *pChestView = pWorldRuntime->activeChestView();

        if (pChestView == nullptr || overlay.sourceLootItemIndex >= pChestView->items.size())
        {
            return false;
        }

        if (!applyLootOperation(
                pChestView->items[overlay.sourceLootItemIndex],
                *pItemTable,
                ActiveLootOperation::ForceIdentify,
                nullptr,
                statusText))
        {
            return false;
        }

        pWorldRuntime->commitActiveChestView();
        return true;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Corpse && pWorldRuntime != nullptr)
    {
        GameplayCorpseViewState *pCorpseView = pWorldRuntime->activeCorpseView();

        if (pCorpseView == nullptr || overlay.sourceLootItemIndex >= pCorpseView->items.size())
        {
            return false;
        }

        if (!applyLootOperation(
                pCorpseView->items[overlay.sourceLootItemIndex],
                *pItemTable,
                ActiveLootOperation::ForceIdentify,
                nullptr,
                statusText))
        {
            return false;
        }

        pWorldRuntime->commitActiveCorpseView();
        return true;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::WorldItem && pWorldRuntime != nullptr)
    {
        return applyWorldItemOperation(
            *pWorldRuntime,
            overlay.sourceWorldItemIndex,
            *pItemTable,
            ActiveLootOperation::ForceIdentify,
            nullptr,
            statusText);
    }

    return false;
}

bool GameplayItemService::tryIdentifyInspectedItem(
    const GameplayUiController::ItemInspectOverlayState &overlay,
    size_t inspectorMemberIndex,
    std::string &statusText)
{
    statusText.clear();

    Party *pParty = activeParty(m_session);
    IGameplayWorldRuntime *pWorldRuntime = m_session.activeWorldRuntime();
    const ItemTable *pItemTable = m_session.hasDataRepository() ? &m_session.data().itemTable() : nullptr;

    if (pParty == nullptr || pItemTable == nullptr)
    {
        return false;
    }

    const Character *pRequestedInspector = pParty->member(inspectorMemberIndex);

    if (pRequestedInspector == nullptr)
    {
        return false;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Inventory)
    {
        return pParty->tryIdentifyMemberInventoryItem(
            overlay.sourceMemberIndex,
            overlay.sourceGridX,
            overlay.sourceGridY,
            inspectorMemberIndex,
            statusText);
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Equipment)
    {
        return pParty->tryIdentifyEquippedItem(
            overlay.sourceMemberIndex,
            overlay.sourceEquipmentSlot,
            inspectorMemberIndex,
            statusText);
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Chest && pWorldRuntime != nullptr)
    {
        GameplayChestViewState *pChestView = pWorldRuntime->activeChestView();

        if (pChestView == nullptr || overlay.sourceLootItemIndex >= pChestView->items.size())
        {
            return false;
        }

        if (!applyLootOperation(
                pChestView->items[overlay.sourceLootItemIndex],
                *pItemTable,
                ActiveLootOperation::IdentifyWithSkill,
                pParty,
                statusText))
        {
            return false;
        }

        pWorldRuntime->commitActiveChestView();
        return true;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Corpse && pWorldRuntime != nullptr)
    {
        GameplayCorpseViewState *pCorpseView = pWorldRuntime->activeCorpseView();

        if (pCorpseView == nullptr || overlay.sourceLootItemIndex >= pCorpseView->items.size())
        {
            return false;
        }

        if (!applyLootOperation(
                pCorpseView->items[overlay.sourceLootItemIndex],
                *pItemTable,
                ActiveLootOperation::IdentifyWithSkill,
                pParty,
                statusText))
        {
            return false;
        }

        pWorldRuntime->commitActiveCorpseView();
        return true;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::WorldItem && pWorldRuntime != nullptr)
    {
        return applyWorldItemOperation(
            *pWorldRuntime,
            overlay.sourceWorldItemIndex,
            *pItemTable,
            ActiveLootOperation::IdentifyWithSkill,
            pParty,
            statusText);
    }

    return false;
}

bool GameplayItemService::tryRepairInspectedItem(
    const GameplayUiController::ItemInspectOverlayState &overlay,
    size_t inspectorMemberIndex,
    std::string &statusText)
{
    statusText.clear();

    Party *pParty = activeParty(m_session);
    IGameplayWorldRuntime *pWorldRuntime = m_session.activeWorldRuntime();
    const ItemTable *pItemTable = m_session.hasDataRepository() ? &m_session.data().itemTable() : nullptr;

    if (pParty == nullptr || pItemTable == nullptr)
    {
        return false;
    }

    const Character *pRequestedInspector = pParty->member(inspectorMemberIndex);

    if (pRequestedInspector == nullptr)
    {
        return false;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Inventory)
    {
        return pParty->tryRepairMemberInventoryItem(
            overlay.sourceMemberIndex,
            overlay.sourceGridX,
            overlay.sourceGridY,
            inspectorMemberIndex,
            statusText);
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Equipment)
    {
        return pParty->tryRepairEquippedItem(
            overlay.sourceMemberIndex,
            overlay.sourceEquipmentSlot,
            inspectorMemberIndex,
            statusText);
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Chest && pWorldRuntime != nullptr)
    {
        GameplayChestViewState *pChestView = pWorldRuntime->activeChestView();

        if (pChestView == nullptr || overlay.sourceLootItemIndex >= pChestView->items.size())
        {
            return false;
        }

        if (!applyLootOperation(
                pChestView->items[overlay.sourceLootItemIndex],
                *pItemTable,
                ActiveLootOperation::RepairWithSkill,
                pParty,
                statusText))
        {
            return false;
        }

        pWorldRuntime->commitActiveChestView();
        return true;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::Corpse && pWorldRuntime != nullptr)
    {
        GameplayCorpseViewState *pCorpseView = pWorldRuntime->activeCorpseView();

        if (pCorpseView == nullptr || overlay.sourceLootItemIndex >= pCorpseView->items.size())
        {
            return false;
        }

        if (!applyLootOperation(
                pCorpseView->items[overlay.sourceLootItemIndex],
                *pItemTable,
                ActiveLootOperation::RepairWithSkill,
                pParty,
                statusText))
        {
            return false;
        }

        pWorldRuntime->commitActiveCorpseView();
        return true;
    }

    if (overlay.sourceType == GameplayUiController::ItemInspectSourceType::WorldItem && pWorldRuntime != nullptr)
    {
        return applyWorldItemOperation(
            *pWorldRuntime,
            overlay.sourceWorldItemIndex,
            *pItemTable,
            ActiveLootOperation::RepairWithSkill,
            pParty,
            statusText);
    }

    return false;
}

void GameplayItemService::closeReadableScrollOverlay()
{
    m_session.gameplayUiController().closeReadableScrollOverlay();
}
}
