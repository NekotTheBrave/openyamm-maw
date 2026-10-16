#include "game/ui/GameplayBuffHud.h"
#include "game/ui/GameplayUiSkin.h"
#include "game/gameplay/GameplayScreenRuntime.h"
#include <algorithm>
#include <cmath>

namespace OpenYAMM::Game
{
const std::array<const char *, CharacterBuffCount> GameplayPersonalBuffNames = {{
    "Bless", "Fate", "Preservation", "Regeneration", "Hammerhands", "Pain Reflection",
    "Fire Aura", "Vampiric Weapon", "Mistform", "Glamour"
}};

namespace
{
void renderBuffLayer(GameplayScreenRuntime &context, const GameplayBuffHudLayer &layer,
    const GameplayResolvedHudLayoutElement &panel)
{
    const std::optional<GameplayHudTextureHandle> texture =
        context.gameplayUiRuntime().ensureHudTextureLoaded(layer.pAsset);
    if (!texture)
    {
        return;
    }
    const float scaleX = panel.width / 128;
    const float scaleY = panel.height / 128;
    context.gameplayUiRuntime().submitHudTexturedQuad(texture->textureHandle,
        panel.x + layer.placement.x * scaleX, panel.y + layer.placement.y * scaleY,
        layer.placement.width * scaleX, layer.placement.height * scaleY,
        layer.uv.x, layer.uv.y, layer.uv.x + layer.uv.width, layer.uv.y + layer.uv.height);
}

void renderBuffPanel(GameplayScreenRuntime &context, const Party &party, GameplayBuffPresentation presentation,
    int width, int height)
{
    const char *pId = gameplayBuffPanelLayoutId(presentation);
    if (pId == nullptr)
    {
        return;
    }
    const UiLayoutManager::LayoutElement *pLayout = context.findHudLayoutElement(pId);
    const std::optional<GameplayResolvedHudLayoutElement> rect =
        context.resolveHudLayoutElement(pId, width, height, 0, 0);
    if (pLayout == nullptr || !rect)
    {
        return;
    }
    GameplayUiSkin::renderSurface(context, *rect);
    const GameplayBuffHudLayer *pBase = nullptr;
    // Light and clouds sit behind the skull; Stoneskin replaces the base without covering those effects.
    for (size_t i = 0; i < PartyBuffCount; ++i)
    {
        const GameplayBuffHudDefinition &definition = GameplayBuffHudDefinitions[i];
        if (definition.presentation != presentation || !party.hasPartyBuff(static_cast<PartyBuffId>(i)))
        {
            continue;
        }
        if (definition.pLayer != nullptr)
        {
            if (definition.pLayer->pass == GameplayBuffHudLayerPass::Underlay)
            {
                renderBuffLayer(context, *definition.pLayer, *rect);
            }
            else if (definition.pLayer->pass == GameplayBuffHudLayerPass::Base)
            {
                pBase = definition.pLayer;
            }
        }
    }
    if (pBase != nullptr)
    {
        renderBuffLayer(context, *pBase, *rect);
    }
    else
    {
        GameplayUiSkin::renderTexture(context, pLayout->primaryAsset, *rect);
    }
    for (size_t i = 0; i < PartyBuffCount; ++i)
    {
        const GameplayBuffHudDefinition &definition = GameplayBuffHudDefinitions[i];
        if (definition.presentation == presentation && definition.pLayer != nullptr
            && definition.pLayer->pass == GameplayBuffHudLayerPass::Overlay
            && party.hasPartyBuff(static_cast<PartyBuffId>(i)))
        {
            renderBuffLayer(context, *definition.pLayer, *rect);
        }
    }
    // Corner badges remain readable over the larger registered effect layers.
    for (size_t i = 0; i < PartyBuffCount; ++i)
    {
        const GameplayBuffHudDefinition &definition = GameplayBuffHudDefinitions[i];
        if (definition.presentation == presentation && definition.pLayer == nullptr
            && party.hasPartyBuff(static_cast<PartyBuffId>(i)))
        {
            const GameplayResolvedHudLayoutElement badge = {rect->x + definition.badgeX * rect->scale,
                rect->y + definition.badgeY * rect->scale, 12 * rect->scale, 12 * rect->scale, rect->scale};
            GameplayUiSkin::renderTexture(context, "obsidian_hud_buff_normal", badge);
            GameplayUiSkin::renderTexture(context, definition.pIcon, badge);
        }
    }
    const std::optional<GameplayHudPaletteColor> edge = context.gameplayUiRuntime().ensureHudPaletteColor(0xc46a7e88u);
    if (edge)
    {
        const float stroke = std::max(1.0f, rect->scale * 0.65f);
        const std::vector<GameplayHudBatchQuad> edges = {
            gameplayHudPaletteQuad(*edge, rect->x, rect->y, rect->width, stroke),
            gameplayHudPaletteQuad(*edge, rect->x, rect->y + rect->height - stroke, rect->width, stroke),
            gameplayHudPaletteQuad(*edge, rect->x, rect->y, stroke, rect->height),
            gameplayHudPaletteQuad(*edge, rect->x + rect->width - stroke, rect->y, stroke, rect->height)};
        context.submitHudQuadBatch(edges);
    }
}
}

std::vector<GameplayBuffHudEntry> gameplayBuffHudEntries(GameplayScreenRuntime &context, int width, int height)
{
    std::vector<GameplayBuffHudEntry> result;
    const Party *pParty = context.partyReadOnly();
    if (pParty == nullptr)
    {
        return result;
    }
    const std::vector<PartyBuffId> ids = sortedGameplaySpecialBuffs(*pParty);
    const std::optional<GameplayResolvedHudLayoutElement> first =
        context.resolveHudLayoutElement("ObsidianPartyBuff1", width, height, 0, 0);
    if (!first)
    {
        return result;
    }
    for (size_t i = 0; i < std::min(ids.size(), size_t{4}); ++i)
    {
        GameplayResolvedHudLayoutElement rect = *first;
        rect.x += i * 29.333333f * rect.scale;
        result.push_back({ids[i], rect});
    }
    if (context.interactionState().partyBuffPopupOpen && ids.size() > 4)
    {
        for (size_t i = 4; i < ids.size(); ++i)
        {
            GameplayResolvedHudLayoutElement rect = *first;
            rect.x += (8.0f + (i - 4) % 6 * 29.333333f) * rect.scale;
            rect.y += (45.0f + (i - 4) / 6 * 35.0f) * rect.scale;
            result.push_back({ids[i], rect});
        }
    }
    return result;
}

void renderGameplayBuffHud(GameplayScreenRuntime &context, int width, int height)
{
    const Party *pParty = context.partyReadOnly();
    if (pParty == nullptr)
    {
        return;
    }
    renderBuffPanel(context, *pParty, GameplayBuffPresentation::Hero, width, height);
    renderBuffPanel(context, *pParty, GameplayBuffPresentation::Skull, width, height);
    const std::vector<PartyBuffId> ids = sortedGameplaySpecialBuffs(*pParty);
    const std::vector<GameplayBuffHudEntry> entries = gameplayBuffHudEntries(context, width, height);
    if (entries.empty())
    {
        context.interactionState().partyBuffPopupOpen = false;
        return;
    }
    if (entries.size() > 4)
    {
        const GameplayResolvedHudLayoutElement &first = entries.front().rect;
        GameplayUiSkin::renderPanel(context, {first.x - 3 * first.scale, first.y + 34 * first.scale,
            194 * first.scale, (25 + std::ceil((ids.size() - 4) / 6.0f) * 35) * first.scale, first.scale});
    }
    for (const GameplayBuffHudEntry &entry : entries)
    {
        const GameplayResolvedHudLayoutElement &rect = entry.rect;
        const PartyBuffState *pBuff = pParty->partyBuff(entry.id);
        GameplayUiSkin::renderTexture(context, "obsidian_hud_buff_normal", rect);
        GameplayUiSkin::renderTexture(context, GameplayBuffHudDefinitions[size_t(entry.id)].pIcon,
            {rect.x + 3.2f * rect.scale, rect.y + 1.6f * rect.scale, 19.2f * rect.scale,
                19.2f * rect.scale, rect.scale});
        const int seconds = static_cast<int>(std::ceil(pBuff->remainingSeconds));
        const std::string duration = seconds >= 3600 ? std::to_string((seconds + 3599) / 3600) + "h"
            : seconds >= 60 ? std::to_string((seconds + 59) / 60) + "m" : std::to_string(seconds) + "s";
        UiLayoutManager::LayoutElement timer;
        timer.fontName = "SMALLNUM";
        timer.textColorAbgr = seconds < 30 ? GameplayUiSkin::Low : GameplayUiSkin::Ivory;
        timer.textScale = 0.34285714f;
        timer.textAlignX = UiLayoutManager::TextAlignX::Center;
        timer.textAlignY = UiLayoutManager::TextAlignY::Middle;
        context.renderLayoutLabel(timer, {rect.x, rect.y + 22.4f * rect.scale,
            rect.width, 6.4f * rect.scale, rect.scale}, duration);
    }
    if (ids.size() > 4)
    {
        const std::optional<GameplayResolvedHudLayoutElement> rect =
            context.resolveHudLayoutElement("ObsidianPartyBuffOverflow", width, height, 0, 0);
        const UiLayoutManager::LayoutElement *pLayout = context.findHudLayoutElement("ObsidianPartyBuffOverflow");
        if (rect && pLayout != nullptr)
        {
            GameplayUiSkin::renderTexture(context, pLayout->primaryAsset, *rect);
            context.renderLayoutLabel(*pLayout, *rect, "+" + std::to_string(ids.size() - 4));
        }
    }
}

std::optional<GameplayBuffHudEntry> gameplayMovementBuffAtPoint(GameplayScreenRuntime &context,
    int width, int height, float x, float y)
{
    const Party *pParty = context.partyReadOnly();
    if (pParty == nullptr)
    {
        return std::nullopt;
    }
    for (const char *pId : {"OutdoorFlyBuffIcon", "OutdoorWaterWalkBuffIcon"})
    {
        const std::optional<PartyBuffId> buff = std::string_view(pId) == "OutdoorFlyBuffIcon"
            ? gameplayFlightBuff(*pParty)
            : pParty->hasPartyBuff(PartyBuffId::WaterWalk)
                ? std::optional<PartyBuffId>(PartyBuffId::WaterWalk) : std::nullopt;
        const std::optional<GameplayResolvedHudLayoutElement> rect =
            buff ? context.resolveHudLayoutElement(pId, width, height, 0, 0) : std::nullopt;
        if (rect && GameplayHudCommon::isPointerInsideResolvedElement(*rect, x, y))
        {
            return GameplayBuffHudEntry{*buff, *rect};
        }
    }
    return std::nullopt;
}

GameplayHudPointerTarget gameplayBuffHudTarget(GameplayScreenRuntime &context,
    int width, int height, float x, float y)
{
    const Party *pParty = context.partyReadOnly();
    if (pParty == nullptr)
    {
        return {};
    }
    const bool gameplay = context.currentHudScreenState() == GameplayHudScreenState::Gameplay;
    for (size_t i = 0; i < pParty->members().size(); ++i)
    {
        const std::string id = std::string(gameplay ? "ObsidianPc" : "ObsidianOverlayPc")
            + std::to_string(i + 1) + "PersonalBuffs";
        const std::optional<GameplayResolvedHudLayoutElement> rect =
            context.resolveHudLayoutElement(id, width, height, 0, 0);
        if (rect && GameplayHudCommon::isPointerInsideResolvedElement(*rect, x, y))
        {
            for (size_t b = 0; b < CharacterBuffCount; ++b)
            {
                if (pParty->hasCharacterBuff(i, static_cast<CharacterBuffId>(b)))
                {
                    return {GameplayHudPointerTargetType::PersonalBuffs, i};
                }
            }
        }
    }
    if (gameplay)
    {
        const std::optional<GameplayBuffHudEntry> movement = gameplayMovementBuffAtPoint(context, width, height, x, y);
        if (movement)
        {
            return {GameplayHudPointerTargetType::PartyBuff, size_t(movement->id)};
        }
        for (GameplayBuffPresentation panel : {GameplayBuffPresentation::Hero, GameplayBuffPresentation::Skull})
        {
            const std::optional<GameplayResolvedHudLayoutElement> rect =
                context.resolveHudLayoutElement(gameplayBuffPanelLayoutId(panel), width, height, 0, 0);
            if (rect && GameplayHudCommon::isPointerInsideResolvedElement(*rect, x, y))
            {
                return {GameplayHudPointerTargetType::PartyBuffPanel, size_t(panel)};
            }
        }
        const std::vector<PartyBuffId> ids = sortedGameplaySpecialBuffs(*pParty);
        const std::optional<GameplayResolvedHudLayoutElement> rect =
            context.resolveHudLayoutElement("ObsidianPartyBuffOverflow", width, height, 0, 0);
        if (ids.size() > 4 && rect && GameplayHudCommon::isPointerInsideResolvedElement(*rect, x, y))
        {
            return {GameplayHudPointerTargetType::BuffOverflow};
        }
        for (const GameplayBuffHudEntry &entry : gameplayBuffHudEntries(context, width, height))
        {
            if (GameplayHudCommon::isPointerInsideResolvedElement(entry.rect, x, y))
            {
                return {GameplayHudPointerTargetType::PartyBuff, size_t(entry.id)};
            }
        }
    }
    return {};
}
}
