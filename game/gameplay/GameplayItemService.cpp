#include "game/gameplay/GameplayItemService.h"

#include "game/app/GameSession.h"
#include "game/audio/GameAudioSystem.h"
#include "game/events/EventRuntime.h"
#include "game/gameplay/GameMechanics.h"
#include "game/mod/ModHooks.h"
#include "game/items/ItemRuntime.h"
#include "game/gameplay/GameplayScreenRuntime.h"
#include "game/items/InventoryItemMixingRuntime.h"
#include "game/items/InventoryItemUseRuntime.h"
#include "game/party/SpellIds.h"
#include "game/tables/MergedBaseTables.h"
#include "game/tables/ObjectTable.h"
#include "game/tables/PotionNoteTable.h"

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

bool GameplayItemService::tryModUseHeldItem(GameplayScreenRuntime &runtime, size_t memberIndex)
{
    GameplayUiController::HeldInventoryItemState &heldItem = m_session.gameplayUiController().heldInventoryItem();
    Party *pParty = runtime.party();

    if (!heldItem.active || pParty == nullptr || activeModHooks() == nullptr)
    {
        return false;
    }

    const bool gameMayUse = modUseMouseItem(memberIndex);

    if (heldItem.active && heldItem.item.objectDescriptionId == 0)
    {
        heldItem = {};
        pParty->clearHeldItemForQueries();
        return true;
    }

    return !gameMayUse;
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

    // The mod's word first (the original's UseMouseItem; MAW drinks its potions its own way). A held item the mod
    // used up is taken off the pointer.
    const bool gameMayUse = modUseMouseItem(memberIndex);

    if (heldItem.active && heldItem.item.objectDescriptionId == 0)
    {
        heldItem = {};
        pParty->clearHeldItemForQueries();
        return true;
    }

    if (!gameMayUse)
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

// The original shows the item's information box when the held item is used on it, and a mod's handlers of it may use
// the held item there (MAW's crafting items on a Dimension Map, its Crafting Cube on equipment): whether they did,
// which they show by using it up or changing it.
bool useHeldItemThroughItemBox(InventoryItem &heldItem, InventoryItem &target)
{
    if (activeModHooks() == nullptr || heldItem.objectDescriptionId == 0)
    {
        return false;
    }

    const InventoryItem heldBefore = heldItem;
    modItemInformationBoxForUse(target);
    return heldItem.objectDescriptionId != heldBefore.objectDescriptionId
        || heldItem.standardEnchantPower != heldBefore.standardEnchantPower;
}

const MergedPotionSettingEntry *modPotionForHeldItem(
    const MergedPotionSettingTable &potionSettingTable,
    uint32_t heldItemId)
{
    for (const MergedPotionSettingEntry &entry : potionSettingTable.entries())
    {
        if (entry.itemId == heldItemId)
        {
            return &entry;
        }
    }

    return nullptr;
}

// The original lets a potion effect (MAW's gems and crafting items) be used on a worn item as well as one in the
// backpack; the game's own mixing never touches a worn item.
bool GameplayItemService::tryUseHeldItemOnEquippedItem(
    GameplayScreenRuntime &runtime,
    size_t memberIndex,
    EquipmentSlot slot)
{
    GameplayUiController::HeldInventoryItemState &heldItem = m_session.gameplayUiController().heldInventoryItem();
    Party *pParty = runtime.party();
    const MergedPotionSettingTable *pPotionSettingTable =
        m_session.hasDataRepository() ? &m_session.data().mergedPotionSettingTable() : nullptr;

    if (!heldItem.active || pParty == nullptr || pPotionSettingTable == nullptr)
    {
        return false;
    }

    const MergedPotionSettingEntry *pPotion =
        modPotionForHeldItem(*pPotionSettingTable, heldItem.item.objectDescriptionId);
    std::optional<InventoryItem> target = pParty->equippedItem(memberIndex, slot);

    if (!target.has_value())
    {
        return false;
    }

    const bool potionEffect =
        pPotion != nullptr && modPotionEffect(pPotion->potionId, heldItem.item.standardEnchantPower, *target);
    const bool usedByItemBox = useHeldItemThroughItemBox(heldItem.item, *target);

    if (!potionEffect && !usedByItemBox)
    {
        return false;
    }

    pParty->replaceEquippedItem(memberIndex, slot, *target);

    if (heldItem.item.objectDescriptionId == 0)
    {
        heldItem = {};
        pParty->clearHeldItemForQueries();
    }
    else
    {
        pParty->setHeldItemForQueries(heldItem.item);
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

    // A potion the mod has an effect for (the original's evt.PotionEffects: MAW's gems and crafting items) is the
    // mod's to use on the item; the game's mixing does not see it. The effect uses it up by making the held item
    // nothing, or changes it (MAW's Enchanted Amulet copies the item onto the pointer).
    if (Character *pMember = pParty->member(memberIndex))
    {
        const MergedPotionSettingEntry *pPotion =
            modPotionForHeldItem(*pPotionSettingTable, heldItem.item.objectDescriptionId);

        InventoryItem *pTarget = nullptr;

        for (InventoryItem &candidate : pMember->inventory)
        {
            if (targetGridX >= candidate.gridX && targetGridX < candidate.gridX + std::max<uint8_t>(1, candidate.width)
                && targetGridY >= candidate.gridY
                && targetGridY < candidate.gridY + std::max<uint8_t>(1, candidate.height))
            {
                pTarget = &candidate;
                break;
            }
        }

        const bool potionEffect = pPotion != nullptr && pTarget != nullptr
            && modPotionEffect(pPotion->potionId, heldItem.item.standardEnchantPower, *pTarget);
        const bool usedByItemBox = pTarget != nullptr && useHeldItemThroughItemBox(heldItem.item, *pTarget);

        if (potionEffect || usedByItemBox)
        {
            if (heldItem.item.objectDescriptionId == 0)
            {
                heldItem = {};
                pParty->clearHeldItemForQueries();
            }
            else
            {
                pParty->setHeldItemForQueries(heldItem.item);
            }

            return true;
        }
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

    if (mixResult.unlockedAutonoteId != 0)
    {
        IGameplayWorldRuntime *pWorldRuntime = runtime.worldRuntime();
        EventRuntimeState *pEventRuntimeState =
            pWorldRuntime != nullptr ? pWorldRuntime->eventRuntimeState() : nullptr;

        if (pEventRuntimeState != nullptr)
        {
            const uint32_t rawId = (mixResult.unlockedAutonoteId << 16) | AutoNoteVariableTag;
            const EventRuntime::VariableRef variable = EventRuntime::decodeVariable(rawId);
            const std::vector<size_t> targetMemberIndices = {memberIndex};
            EventRuntime::setVariableValue(*pEventRuntimeState, variable, 1, pParty, targetMemberIndices);
        }
    }

    const bool potionExplosion =
        mixResult.action == InventoryItemMixAction::PotionMix
        && !mixResult.success
        && mixResult.failureDamageLevel != 0;

    if (potionExplosion)
    {
        pParty->applyPotionExplosionToMember(memberIndex, mixResult.failureDamageLevel);
        closeCharacterInventoryAfterPotionExplosion(runtime);
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
        Character *pMember = pParty->member(memberIndex);
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

    return true;
}

void GameplayItemService::updateReadableScrollOverlayForHeldItem(
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
