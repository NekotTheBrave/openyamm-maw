#pragma once

#include "engine/AssetFileSystem.h"
#include "engine/FontAsset.h"
#include "game/render/TextureFiltering.h"
#include "game/ui/GameplayOverlayTypes.h"
#include "game/ui/UiLayoutManager.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenYAMM::Game
{
struct GameplayActorInspectClock
{
    uint32_t ticks = 1;
    uint32_t lastUpdateTicks = 0;
    bool wasVisible = false;

    uint32_t update(bool visible, uint32_t nowTicks)
    {
        if (visible && wasVisible)
        {
            ticks += std::min(nowTicks - lastUpdateTicks, 32u);
        }
        wasVisible = visible;
        lastUpdateTicks = nowTicks;
        return ticks;
    }
};

struct GameplayAssetLoadCache
{
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> directoryAssetPathsByPath;
    std::unordered_map<std::string, std::optional<std::string>> assetPathByKey;
    std::unordered_map<std::string, std::optional<std::vector<uint8_t>>> binaryFilesByPath;
    std::unordered_map<std::string, std::optional<std::array<uint8_t, 256 * 3>>> actPalettesByKey;
};

struct GameplayHudTextureData
{
    std::string textureName;
    int width = 0;
    int height = 0;
    int physicalWidth = 0;
    int physicalHeight = 0;
    Engine::AssetScaleTier assetScaleTier = Engine::AssetScaleTier::X1;
    std::vector<uint8_t> bgraPixels;
    bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
};

using GameplayHudFontGlyphMetricsData = Engine::FontGlyphMetrics;

struct GameplayHudFontData : Engine::FontAtlas
{
    bool smooth = false;
    std::string fontName;
    bgfx::TextureHandle mainTextureHandle = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowTextureHandle = BGFX_INVALID_HANDLE;
};

struct GameplayHudFontColorTextureData
{
    std::string fontName;
    uint32_t colorAbgr = 0xffffffffu;
    bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
};

enum class GameplayHudTextureColorMode
{
    Solid,
    Modulated,
};

struct GameplayHudTextureColorTextureData
{
    std::string textureName;
    uint32_t colorAbgr = 0xffffffffu;
    GameplayHudTextureColorMode colorMode = GameplayHudTextureColorMode::Solid;
    bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
};

enum class GameplayHudBitmapTransparencyMode
{
    HudColorKey,
    ItemIcon
};

struct GameplayUiViewportRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

class GameplayHudCommon
{
public:
    // Shared logical viewport for portrait rendering and pointer hit tests.
    static constexpr float PartyPortraitNativeWidth = 60.0f;
    static constexpr float PartyPortraitNativeHeight = 80.0f;

    using SubmitTexturedQuadFn = std::function<void(
        bgfx::TextureHandle,
        float,
        float,
        float,
        float,
        float,
        float,
        float,
        float,
        TextureFilterProfile)>;
    using FindHudFontFn = std::function<const GameplayHudFontData *(const std::string &)>;
    using EnsureHudFontColorFn = std::function<bgfx::TextureHandle(const GameplayHudFontData &, uint32_t)>;
    using RenderHudFontLayerFn = std::function<void(
        const GameplayHudFontData &,
        bgfx::TextureHandle,
        const std::string &,
        float,
        float,
        float)>;

    static GameplayUiViewportRect computeUiViewportRect(int screenWidth, int screenHeight);
    static GameplayUiViewportRect computeAnchorRect(
        UiLayoutManager::LayoutAnchorSpace anchorSpace,
        int screenWidth,
        int screenHeight);
    static float snappedHudFontScale(float scale);
    static float touchPanelVerticalOffset(
        const GameplayResolvedHudLayoutElement &panel, const GameplayResolvedHudLayoutElement &basebar)
    {
        const float ornamentMargin = 22.0f * basebar.scale;
        if (panel.x + panel.width <= basebar.x - ornamentMargin
            || panel.x >= basebar.x + basebar.width + ornamentMargin)
        {
            return 0.0f;
        }
        return std::min(0.0f, basebar.y - 12.0f * panel.scale - panel.y - panel.height);
    }
    static GameplayResolvedHudLayoutElement resolveAttachedHudLayoutRect(
        UiLayoutManager::LayoutAttachMode attachTo,
        const GameplayResolvedHudLayoutElement &parent,
        float width,
        float height,
        float gapX,
        float gapY,
        float scale);
    static std::optional<GameplayResolvedHudLayoutElement> resolveHudLayoutElement(
        const UiLayoutManager &layoutManager,
        const std::unordered_map<std::string, float> &runtimeWidthOverrides,
        const std::unordered_map<std::string, float> &runtimeHeightOverrides,
        const std::string &layoutId,
        int screenWidth,
        int screenHeight,
        float fallbackWidth,
        float fallbackHeight);
    static bool isPointerInsideResolvedElement(
        const GameplayResolvedHudLayoutElement &resolved,
        float pointerX,
        float pointerY);
    static const std::string *resolveInteractiveAssetName(
        const UiLayoutManager::LayoutElement &layout,
        const GameplayResolvedHudLayoutElement &resolved,
        float pointerX,
        float pointerY,
        bool isLeftMousePressed);

    static std::optional<std::string> findCachedAssetPath(
        const Engine::AssetFileSystem *pAssetFileSystem,
        GameplayAssetLoadCache &cache,
        const std::string &directoryPath,
        const std::string &fileName);
    static std::optional<std::vector<uint8_t>> readCachedBinaryFile(
        const Engine::AssetFileSystem *pAssetFileSystem,
        GameplayAssetLoadCache &cache,
        const std::string &assetPath);
    static std::optional<std::array<uint8_t, 256 * 3>> loadCachedActPalette(
        const Engine::AssetFileSystem *pAssetFileSystem,
        GameplayAssetLoadCache &cache,
        int16_t paletteId,
        const std::string &worldId = {});
    static std::optional<std::vector<uint8_t>> loadHudBitmapPixelsBgraCached(
        const Engine::AssetFileSystem *pAssetFileSystem,
        GameplayAssetLoadCache &cache,
        const std::string &textureName,
        int &width,
        int &height,
        GameplayHudBitmapTransparencyMode transparencyMode = GameplayHudBitmapTransparencyMode::HudColorKey,
        Engine::AssetScaleTier *pLoadedTier = nullptr);
    static std::optional<std::vector<uint8_t>> loadSpriteBitmapPixelsBgraCached(
        const Engine::AssetFileSystem *pAssetFileSystem,
        GameplayAssetLoadCache &cache,
        const std::string &textureName,
        int16_t paletteId,
        int &width,
        int &height,
        const std::string &worldId = {},
        Engine::AssetScaleTier *pLoadedTier = nullptr);
    static const GameplayHudTextureData *findHudTexture(
        const std::vector<GameplayHudTextureData> &textures,
        const std::unordered_map<std::string, size_t> &textureIndexByName,
        const std::string &textureName);
    static bool loadHudTexture(
        const Engine::AssetFileSystem *pAssetFileSystem,
        GameplayAssetLoadCache &cache,
        const std::string &textureName,
        std::vector<GameplayHudTextureData> &textures,
        std::unordered_map<std::string, size_t> &textureIndexByName,
        GameplayHudBitmapTransparencyMode transparencyMode = GameplayHudBitmapTransparencyMode::HudColorKey);
    static std::optional<GameplayHudTextureHandle> ensureDynamicHudTexture(
        const std::string &textureName,
        int width,
        int height,
        const std::vector<uint8_t> &bgraPixels,
        std::vector<GameplayHudTextureData> &textures,
        std::unordered_map<std::string, size_t> &textureIndexByName);
    static const GameplayHudFontData *findHudFont(
        const std::vector<GameplayHudFontData> &fonts,
        const std::string &fontName);
    static bool loadHudFont(
        const Engine::AssetFileSystem *pAssetFileSystem,
        GameplayAssetLoadCache &cache,
        const std::string &fontName,
        std::vector<GameplayHudFontData> &fonts,
        const Engine::FontSettings &settings = {});
    static bool tryGetOpaqueHudTextureBounds(
        const GameplayHudTextureData &texture,
        Engine::AssetScaleTier assetScaleTier,
        int &width,
        int &height,
        int &opaqueMinX,
        int &opaqueMinY,
        int &opaqueMaxX,
        int &opaqueMaxY);
    static bgfx::TextureHandle ensureHudTextureColor(
        const GameplayHudTextureData &texture,
        uint32_t colorAbgr,
        std::vector<GameplayHudTextureColorTextureData> &colorTextures);
    static bgfx::TextureHandle ensureHudTextureColorModulated(
        const GameplayHudTextureData &texture,
        uint32_t colorAbgr,
        std::vector<GameplayHudTextureColorTextureData> &colorTextures);
    static bgfx::TextureHandle ensureHudFontMainTextureColor(
        const GameplayHudFontData &font,
        uint32_t colorAbgr,
        std::vector<GameplayHudFontColorTextureData> &colorTextures);
    static float measureHudTextWidth(const GameplayHudFontData &font, const std::string &text);

    // Coloured text, as the original marks it: a form feed and five digits, a 16-bit colour (5 bits red, 6 green,
    // 5 blue), with 00000 for "back to the text's own colour". The marks take no room and draw nothing.
    struct HudColorRun
    {
        std::string text;
        std::optional<uint32_t> colorAbgr;
        // The run starts at this distance from the start of the line, in the font's pixels (a tab stop, which may
        // go back).
        std::optional<int> tabStop;
    };
    // The original's tab stop: a tab and three characters read as a number, "go on from this many pixels into the
    // line", forwards or back (MAW's recount tables go back to the line's start and pad with spaces). The original reads them as a number does (spaces before the digits; all spaces is 0, which does not
    // move), and MAW's tables pad with spaces after a tab. The length of the one at `index`, 0 when there is none.
    static size_t hudTabStopLength(const std::string &text, size_t index);
    // The position the tab stop at `index` names, in the font's pixels.
    static int hudTabStopValue(const std::string &text, size_t index);
    // The length of the colour mark at `index`, 0 when there is none.
    static size_t hudColorCodeLength(const std::string &text, size_t index);
    static bool hasHudColorCodes(const std::string &text);
    // The text cut where its colour changes or a tab stop moves it on; a run without a colour is in the text's own.
    static std::vector<HudColorRun> splitHudColorRuns(const std::string &text);
    static std::string clampHudTextToWidth(const GameplayHudFontData &font, const std::string &text, float maxWidth);
    static std::vector<std::string> wrapHudTextToWidth(
        const GameplayHudFontData &font,
        const std::string &text,
        float maxWidth);
    static void renderHudFontLayer(
        const GameplayHudFontData &font,
        bgfx::TextureHandle textureHandle,
        const std::string &text,
        float textX,
        float textY,
        float fontScale,
        const SubmitTexturedQuadFn &submitTexturedQuad);
    static void renderLayoutLabel(
        const UiLayoutManager::LayoutElement &layout,
        const GameplayResolvedHudLayoutElement &resolved,
        const std::string &label,
        const FindHudFontFn &findHudFont,
        const EnsureHudFontColorFn &ensureHudFontColor,
        const RenderHudFontLayerFn &renderHudFontLayer);
};
} // namespace OpenYAMM::Game
