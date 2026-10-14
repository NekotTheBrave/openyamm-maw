#include "game/ui/GameplayHudCommon.h"

#include "engine/ImageAssetLoader.h"
#include "game/maps/MapIdentity.h"
#include "game/render/TextureFiltering.h"
#include "game/StringUtils.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string_view>

namespace OpenYAMM::Game
{
namespace
{
constexpr float HudReferenceWidth = 640.0f;
constexpr float HudReferenceHeight = 480.0f;
constexpr float HudFontIntegerSnapThreshold = 0.1f;
constexpr float MaxUiViewportAspect = 4.0f / 3.0f;

std::string actPaletteCacheKey(int16_t paletteId, const std::string &worldId)
{
    const std::string normalizedWorldId = worldId.empty() ? std::string() : normalizeWorldId(worldId);
    return normalizedWorldId + "|" + std::to_string(static_cast<int>(paletteId));
}

std::vector<std::string> actPaletteCandidatePaths(int16_t paletteId, const std::string &worldId)
{
    char paletteFileName[32] = {};
    std::snprintf(paletteFileName, sizeof(paletteFileName), "pal%03d.act", static_cast<int>(paletteId));

    std::vector<std::string> paths;

    if (!worldId.empty())
    {
        paths.push_back("worlds/" + normalizeWorldId(worldId) + "/textures/" + paletteFileName);
    }

    paths.push_back(std::string("Data/bitmaps/") + paletteFileName);
    return paths;
}

bool usesBlackTransparencyKey(std::string_view textureName)
{
    const std::string normalizedName = toLowerCopy(std::string(textureName));
    return normalizedName.rfind("mapdir", 0) == 0 || normalizedName.rfind("micon", 0) == 0;
}

bgfx::TextureHandle ensureHudTextureColorWithMode(
    const GameplayHudTextureData &texture,
    uint32_t colorAbgr,
    GameplayHudTextureColorMode colorMode,
    std::vector<GameplayHudTextureColorTextureData> &colorTextures)
{
    if (colorAbgr == 0xffffffffu)
    {
        return texture.textureHandle;
    }

    for (const GameplayHudTextureColorTextureData &textureHandle : colorTextures)
    {
        if (textureHandle.textureName == texture.textureName
            && textureHandle.colorAbgr == colorAbgr
            && textureHandle.colorMode == colorMode)
        {
            return textureHandle.textureHandle;
        }
    }

    if (texture.bgraPixels.empty() || texture.physicalWidth <= 0 || texture.physicalHeight <= 0)
    {
        return BGFX_INVALID_HANDLE;
    }

    std::vector<uint8_t> tintedPixels = texture.bgraPixels;
    const uint8_t red = static_cast<uint8_t>(colorAbgr & 0xff);
    const uint8_t green = static_cast<uint8_t>((colorAbgr >> 8) & 0xff);
    const uint8_t blue = static_cast<uint8_t>((colorAbgr >> 16) & 0xff);
    const uint8_t alpha = static_cast<uint8_t>((colorAbgr >> 24) & 0xff);

    for (size_t pixelIndex = 0; pixelIndex + 3 < tintedPixels.size(); pixelIndex += 4)
    {
        const uint8_t sourceAlpha = tintedPixels[pixelIndex + 3];

        if (colorMode == GameplayHudTextureColorMode::Modulated)
        {
            tintedPixels[pixelIndex + 0] = static_cast<uint8_t>(
                (static_cast<uint32_t>(tintedPixels[pixelIndex + 0]) * blue) / 255u);
            tintedPixels[pixelIndex + 1] = static_cast<uint8_t>(
                (static_cast<uint32_t>(tintedPixels[pixelIndex + 1]) * green) / 255u);
            tintedPixels[pixelIndex + 2] = static_cast<uint8_t>(
                (static_cast<uint32_t>(tintedPixels[pixelIndex + 2]) * red) / 255u);
        }
        else if (sourceAlpha != 0)
        {
            tintedPixels[pixelIndex + 0] = blue;
            tintedPixels[pixelIndex + 1] = green;
            tintedPixels[pixelIndex + 2] = red;
        }

        tintedPixels[pixelIndex + 3] = static_cast<uint8_t>(
            (static_cast<uint32_t>(sourceAlpha) * alpha) / 255u);
    }

    const bgfx::TextureHandle textureHandle = createBgraTexture2D(
        static_cast<uint16_t>(texture.physicalWidth),
        static_cast<uint16_t>(texture.physicalHeight),
        tintedPixels.data(),
        static_cast<uint32_t>(tintedPixels.size()),
        TextureFilterProfile::Ui,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    if (!bgfx::isValid(textureHandle))
    {
        return BGFX_INVALID_HANDLE;
    }

    GameplayHudTextureColorTextureData tintedTextureHandle = {};
    tintedTextureHandle.textureName = texture.textureName;
    tintedTextureHandle.colorAbgr = colorAbgr;
    tintedTextureHandle.colorMode = colorMode;
    tintedTextureHandle.textureHandle = textureHandle;
    colorTextures.push_back(std::move(tintedTextureHandle));
    return colorTextures.back().textureHandle;
}

struct ColumnSeparatedGlyphRun
{
    int startX = 0;
    int width = 0;
};

std::optional<GameplayHudFontData> buildColumnSeparatedHudFont(
    const Engine::ImagePixelsBgra &image,
    const std::string &fontName)
{
    constexpr int FirstPrintableCharacter = 33;
    constexpr int LastPrintableCharacter = 126;
    constexpr int PrintableCharacterCount = LastPrintableCharacter - FirstPrintableCharacter + 1;
    constexpr int DefaultAdvanceGlyphIndex = 'E' - FirstPrintableCharacter;

    const size_t expectedPixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4;
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() < expectedPixelCount)
    {
        return std::nullopt;
    }

    std::vector<ColumnSeparatedGlyphRun> glyphRuns;
    int glyphStartX = -1;
    for (int x = 0; x <= image.width; ++x)
    {
        bool columnHasInk = false;
        if (x < image.width)
        {
            for (int y = 0; y < image.height; ++y)
            {
                const size_t pixelOffset =
                    (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4;
                if (image.pixels[pixelOffset + 3] != 0)
                {
                    columnHasInk = true;
                    break;
                }
            }
        }

        if (columnHasInk && glyphStartX < 0)
        {
            glyphStartX = x;
        }
        else if (!columnHasInk && glyphStartX >= 0)
        {
            glyphRuns.push_back({.startX = glyphStartX, .width = x - glyphStartX});
            glyphStartX = -1;
        }
    }

    if (glyphRuns.size() < PrintableCharacterCount)
    {
        return std::nullopt;
    }

    int atlasCellWidth = 1;
    for (size_t glyphIndex = 0; glyphIndex < PrintableCharacterCount; ++glyphIndex)
    {
        atlasCellWidth = std::max(atlasCellWidth, glyphRuns[glyphIndex].width);
    }

    const int atlasWidth = atlasCellWidth * 16;
    const int atlasHeight = image.height * 16;
    std::vector<uint8_t> atlasPixels(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);

    GameplayHudFontData font = {};
    font.fontName = toLowerCopy(fontName);
    font.firstChar = 32;
    font.lastChar = LastPrintableCharacter;
    font.fontHeight = image.height;
    font.atlasCellWidth = atlasCellWidth;
    font.atlasWidth = atlasWidth;
    font.atlasHeight = atlasHeight;

    const int defaultAdvance = glyphRuns[DefaultAdvanceGlyphIndex].width + 1;
    font.glyphMetrics[32].rightSpacing = std::max(1, defaultAdvance / 2);

    for (int character = FirstPrintableCharacter; character <= LastPrintableCharacter; ++character)
    {
        const ColumnSeparatedGlyphRun &run = glyphRuns[character - FirstPrintableCharacter];
        font.glyphMetrics[character].width = run.width;
        font.glyphMetrics[character].rightSpacing = 1;

        const int targetX = (character % 16) * atlasCellWidth;
        const int targetY = (character / 16) * image.height;
        for (int y = 0; y < image.height; ++y)
        {
            const size_t sourceOffset =
                (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(run.startX)) * 4;
            const size_t targetOffset =
                (static_cast<size_t>(targetY + y) * static_cast<size_t>(atlasWidth)
                    + static_cast<size_t>(targetX)) * 4;
            std::copy_n(
                image.pixels.begin() + static_cast<ptrdiff_t>(sourceOffset),
                static_cast<size_t>(run.width) * 4,
                atlasPixels.begin() + static_cast<ptrdiff_t>(targetOffset));
        }
    }

    font.mainAtlasPixels = atlasPixels;
    font.mainTextureHandle = createBgraTexture2D(
        static_cast<uint16_t>(atlasWidth),
        static_cast<uint16_t>(atlasHeight),
        atlasPixels.data(),
        static_cast<uint32_t>(atlasPixels.size()),
        TextureFilterProfile::Text,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(font.mainTextureHandle))
    {
        return std::nullopt;
    }

    return font;
}
} // namespace

GameplayUiViewportRect GameplayHudCommon::computeUiViewportRect(int screenWidth, int screenHeight)
{
    GameplayUiViewportRect viewport = {};
    viewport.width = static_cast<float>(screenWidth);
    viewport.height = static_cast<float>(screenHeight);

    if (screenHeight > 0)
    {
        const float maxWidthForHeight = viewport.height * MaxUiViewportAspect;

        if (viewport.width > maxWidthForHeight)
        {
            viewport.width = maxWidthForHeight;
            viewport.x = (static_cast<float>(screenWidth) - viewport.width) * 0.5f;
        }
    }

    return viewport;
}

GameplayUiViewportRect GameplayHudCommon::computeAnchorRect(
    UiLayoutManager::LayoutAnchorSpace anchorSpace,
    int screenWidth,
    int screenHeight)
{
    if (anchorSpace == UiLayoutManager::LayoutAnchorSpace::Screen)
    {
        GameplayUiViewportRect rect = {};
        rect.width = static_cast<float>(screenWidth);
        rect.height = static_cast<float>(screenHeight);
        return rect;
    }

    return computeUiViewportRect(screenWidth, screenHeight);
}

float GameplayHudCommon::snappedHudFontScale(float scale)
{
    const float roundedScale = std::round(scale);

    if (std::abs(scale - roundedScale) <= HudFontIntegerSnapThreshold)
    {
        return std::max(1.0f, roundedScale);
    }

    return std::max(1.0f, scale);
}

GameplayResolvedHudLayoutElement GameplayHudCommon::resolveAttachedHudLayoutRect(
    UiLayoutManager::LayoutAttachMode attachTo,
    const GameplayResolvedHudLayoutElement &parent,
    float width,
    float height,
    float gapX,
    float gapY,
    float scale)
{
    GameplayResolvedHudLayoutElement resolved = {};
    resolved.width = width;
    resolved.height = height;
    resolved.scale = scale;

    switch (attachTo)
    {
    case UiLayoutManager::LayoutAttachMode::None:
        resolved.x = parent.x + gapX * scale;
        resolved.y = parent.y + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::RightOf:
        resolved.x = parent.x + parent.width + gapX * scale;
        resolved.y = parent.y + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::LeftOf:
        resolved.x = parent.x - resolved.width + gapX * scale;
        resolved.y = parent.y + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::Above:
        resolved.x = parent.x + gapX * scale;
        resolved.y = parent.y - resolved.height + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::Below:
        resolved.x = parent.x + gapX * scale;
        resolved.y = parent.y + parent.height + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::CenterAbove:
        resolved.x = parent.x + (parent.width - resolved.width) * 0.5f + gapX * scale;
        resolved.y = parent.y - resolved.height + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::CenterBelow:
        resolved.x = parent.x + (parent.width - resolved.width) * 0.5f + gapX * scale;
        resolved.y = parent.y + parent.height + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideLeft:
        resolved.x = parent.x + gapX * scale;
        resolved.y = parent.y + (parent.height - resolved.height) * 0.5f + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideRight:
        resolved.x = parent.x + parent.width - resolved.width + gapX * scale;
        resolved.y = parent.y + (parent.height - resolved.height) * 0.5f + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideTopCenter:
        resolved.x = parent.x + (parent.width - resolved.width) * 0.5f + gapX * scale;
        resolved.y = parent.y + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideTopLeft:
        resolved.x = parent.x + gapX * scale;
        resolved.y = parent.y + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideTopRight:
        resolved.x = parent.x + parent.width - resolved.width + gapX * scale;
        resolved.y = parent.y + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideBottomLeft:
        resolved.x = parent.x + gapX * scale;
        resolved.y = parent.y + parent.height - resolved.height + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideBottomCenter:
        resolved.x = parent.x + (parent.width - resolved.width) * 0.5f + gapX * scale;
        resolved.y = parent.y + parent.height - resolved.height + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::InsideBottomRight:
        resolved.x = parent.x + parent.width - resolved.width + gapX * scale;
        resolved.y = parent.y + parent.height - resolved.height + gapY * scale;
        break;

    case UiLayoutManager::LayoutAttachMode::CenterIn:
        resolved.x = parent.x + (parent.width - resolved.width) * 0.5f + gapX * scale;
        resolved.y = parent.y + (parent.height - resolved.height) * 0.5f + gapY * scale;
        break;
    }

    return resolved;
}

std::optional<GameplayResolvedHudLayoutElement> GameplayHudCommon::resolveHudLayoutElement(
    const UiLayoutManager &layoutManager,
    const std::unordered_map<std::string, float> &runtimeWidthOverrides,
    const std::unordered_map<std::string, float> &runtimeHeightOverrides,
    const std::string &layoutId,
    int screenWidth,
    int screenHeight,
    float fallbackWidth,
    float fallbackHeight)
{
    std::unordered_set<std::string> visited;

    std::function<std::optional<GameplayResolvedHudLayoutElement>(
        const std::string &,
        float,
        float)> resolveRecursive;
    resolveRecursive =
        [&](const std::string &currentLayoutId, float currentFallbackWidth, float currentFallbackHeight)
        -> std::optional<GameplayResolvedHudLayoutElement>
        {
            const std::string normalizedLayoutId = toLowerCopy(currentLayoutId);

            if (visited.contains(normalizedLayoutId))
            {
                std::cerr << "HUD layout cycle detected at element: " << currentLayoutId << '\n';
                return std::nullopt;
            }

            visited.insert(normalizedLayoutId);
            const UiLayoutManager::LayoutElement *pElement = layoutManager.findElement(currentLayoutId);

            if (pElement == nullptr)
            {
                visited.erase(normalizedLayoutId);
                return std::nullopt;
            }

            const UiLayoutManager::LayoutElement &element = *pElement;
            const GameplayUiViewportRect uiViewport = computeUiViewportRect(screenWidth, screenHeight);
            const GameplayUiViewportRect anchorRect = computeAnchorRect(element.anchorSpace, screenWidth, screenHeight);
            const float scaleReferenceWidth = element.scaleReferenceWidth > 0.0f
                ? element.scaleReferenceWidth
                : HudReferenceWidth;
            const float scaleReferenceHeight = element.scaleReferenceHeight > 0.0f
                ? element.scaleReferenceHeight
                : HudReferenceHeight;
            const float scaleAvailableWidth = element.scaleReferenceWidth > 0.0f
                ? anchorRect.width
                : uiViewport.width;
            const float scaleAvailableHeight = element.scaleReferenceHeight > 0.0f
                ? anchorRect.height
                : uiViewport.height;
            const float baseScale = std::min(
                scaleAvailableWidth / scaleReferenceWidth,
                scaleAvailableHeight / scaleReferenceHeight);
            const auto runtimeHeightOverrideIterator = runtimeHeightOverrides.find(normalizedLayoutId);
            const auto runtimeWidthOverrideIterator = runtimeWidthOverrides.find(normalizedLayoutId);
            const float effectiveWidth = runtimeWidthOverrideIterator != runtimeWidthOverrides.end()
                ? runtimeWidthOverrideIterator->second
                : (element.width > 0.0f ? element.width : currentFallbackWidth);
            const float effectiveHeight = runtimeHeightOverrideIterator != runtimeHeightOverrides.end()
                ? runtimeHeightOverrideIterator->second
                : (element.height > 0.0f ? element.height : currentFallbackHeight);

            GameplayResolvedHudLayoutElement resolved = {};

            if (!element.parentId.empty() && element.attachTo != UiLayoutManager::LayoutAttachMode::None)
            {
                const UiLayoutManager::LayoutElement *pParent = layoutManager.findElement(element.parentId);

                if (pParent == nullptr)
                {
                    visited.erase(normalizedLayoutId);
                    return std::nullopt;
                }

                const std::optional<GameplayResolvedHudLayoutElement> parent =
                    resolveRecursive(element.parentId, pParent->width, pParent->height);

                if (!parent)
                {
                    visited.erase(normalizedLayoutId);
                    return std::nullopt;
                }

                resolved.scale = element.hasExplicitScale
                    ? std::clamp(baseScale, element.minScale, element.maxScale)
                    : parent->scale;
                resolved.width = effectiveWidth * resolved.scale;
                resolved.height = effectiveHeight * resolved.scale;
                resolved = resolveAttachedHudLayoutRect(
                    element.attachTo,
                    *parent,
                    resolved.width,
                    resolved.height,
                    element.gapX,
                    element.gapY,
                    resolved.scale);

                if (!element.bottomToId.empty())
                {
                    const UiLayoutManager::LayoutElement *pBottomTo = layoutManager.findElement(element.bottomToId);

                    if (pBottomTo != nullptr)
                    {
                        const std::optional<GameplayResolvedHudLayoutElement> bottomTo =
                            resolveRecursive(element.bottomToId, pBottomTo->width, pBottomTo->height);

                        if (bottomTo)
                        {
                            resolved.height =
                                std::max(0.0f, bottomTo->y + element.bottomGap * resolved.scale - resolved.y);
                        }
                    }
                }

                visited.erase(normalizedLayoutId);
                return resolved;
            }

            resolved.scale = std::clamp(baseScale, element.minScale, element.maxScale);
            resolved.width = effectiveWidth * resolved.scale;
            resolved.height = effectiveHeight * resolved.scale;

            switch (element.anchor)
            {
            case UiLayoutManager::LayoutAnchor::TopLeft:
                resolved.x = anchorRect.x + element.offsetX * resolved.scale;
                resolved.y = anchorRect.y + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::TopCenter:
                resolved.x =
                    anchorRect.x + anchorRect.width * 0.5f - resolved.width * 0.5f + element.offsetX * resolved.scale;
                resolved.y = anchorRect.y + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::TopRight:
                resolved.x = anchorRect.x + anchorRect.width - resolved.width + element.offsetX * resolved.scale;
                resolved.y = anchorRect.y + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::Left:
                resolved.x = anchorRect.x + element.offsetX * resolved.scale;
                resolved.y =
                    anchorRect.y + anchorRect.height * 0.5f - resolved.height * 0.5f + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::Center:
                resolved.x =
                    anchorRect.x + anchorRect.width * 0.5f - resolved.width * 0.5f + element.offsetX * resolved.scale;
                resolved.y =
                    anchorRect.y + anchorRect.height * 0.5f - resolved.height * 0.5f + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::Right:
                resolved.x = anchorRect.x + anchorRect.width - resolved.width + element.offsetX * resolved.scale;
                resolved.y =
                    anchorRect.y + anchorRect.height * 0.5f - resolved.height * 0.5f + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::BottomLeft:
                resolved.x = anchorRect.x + element.offsetX * resolved.scale;
                resolved.y = anchorRect.y + anchorRect.height - resolved.height + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::BottomCenter:
                resolved.x =
                    anchorRect.x + anchorRect.width * 0.5f - resolved.width * 0.5f + element.offsetX * resolved.scale;
                resolved.y = anchorRect.y + anchorRect.height - resolved.height + element.offsetY * resolved.scale;
                break;

            case UiLayoutManager::LayoutAnchor::BottomRight:
                resolved.x = anchorRect.x + anchorRect.width - resolved.width + element.offsetX * resolved.scale;
                resolved.y = anchorRect.y + anchorRect.height - resolved.height + element.offsetY * resolved.scale;
                break;
            }

            if (!element.bottomToId.empty())
            {
                const UiLayoutManager::LayoutElement *pBottomTo = layoutManager.findElement(element.bottomToId);

                if (pBottomTo != nullptr)
                {
                    const std::optional<GameplayResolvedHudLayoutElement> bottomTo =
                        resolveRecursive(element.bottomToId, pBottomTo->width, pBottomTo->height);

                    if (bottomTo)
                    {
                        resolved.height =
                            std::max(0.0f, bottomTo->y + element.bottomGap * resolved.scale - resolved.y);
                    }
                }
            }

            visited.erase(normalizedLayoutId);
            return resolved;
        };

    return resolveRecursive(layoutId, fallbackWidth, fallbackHeight);
}

bool GameplayHudCommon::isPointerInsideResolvedElement(
    const GameplayResolvedHudLayoutElement &resolved,
    float pointerX,
    float pointerY)
{
    return pointerX >= resolved.x
        && pointerX < resolved.x + resolved.width
        && pointerY >= resolved.y
        && pointerY < resolved.y + resolved.height;
}

const std::string *GameplayHudCommon::resolveInteractiveAssetName(
    const UiLayoutManager::LayoutElement &layout,
    const GameplayResolvedHudLayoutElement &resolved,
    float pointerX,
    float pointerY,
    bool isLeftMousePressed)
{
    const bool isHovered = layout.interactive && isPointerInsideResolvedElement(resolved, pointerX, pointerY);
    const bool isPressed = isHovered && isLeftMousePressed;
    const std::string *pAssetName = &layout.primaryAsset;

    if (isPressed && !layout.pressedAsset.empty())
    {
        pAssetName = &layout.pressedAsset;
    }
    else if (isHovered && !layout.hoverAsset.empty())
    {
        pAssetName = &layout.hoverAsset;
    }

    return pAssetName;
}

std::optional<std::string> GameplayHudCommon::findCachedAssetPath(
    const Engine::AssetFileSystem *pAssetFileSystem,
    GameplayAssetLoadCache &cache,
    const std::string &directoryPath,
    const std::string &fileName)
{
    if (pAssetFileSystem == nullptr)
    {
        return std::nullopt;
    }

    const std::string cacheKey = directoryPath + "|" + toLowerCopy(fileName);
    const auto cachedPathIt = cache.assetPathByKey.find(cacheKey);

    if (cachedPathIt != cache.assetPathByKey.end())
    {
        return cachedPathIt->second;
    }

    const auto assetPathsIt = cache.directoryAssetPathsByPath.find(directoryPath);
    const std::unordered_map<std::string, std::string> *pAssetPaths = nullptr;

    if (assetPathsIt != cache.directoryAssetPathsByPath.end())
    {
        pAssetPaths = &assetPathsIt->second;
    }
    else
    {
        std::unordered_map<std::string, std::string> assetPaths;
        const std::vector<std::string> entries = pAssetFileSystem->enumerate(directoryPath);

        for (const std::string &entry : entries)
        {
            assetPaths.emplace(toLowerCopy(entry), directoryPath + "/" + entry);
        }

        pAssetPaths = &cache.directoryAssetPathsByPath.emplace(directoryPath, std::move(assetPaths)).first->second;
    }

    const std::string normalizedFileName = toLowerCopy(fileName);
    const auto resolvedPathIt = pAssetPaths->find(normalizedFileName);

    if (resolvedPathIt != pAssetPaths->end())
    {
        cache.assetPathByKey[cacheKey] = resolvedPathIt->second;
        return resolvedPathIt->second;
    }

    cache.assetPathByKey[cacheKey] = std::nullopt;
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> GameplayHudCommon::readCachedBinaryFile(
    const Engine::AssetFileSystem *pAssetFileSystem,
    GameplayAssetLoadCache &cache,
    const std::string &assetPath)
{
    if (pAssetFileSystem == nullptr)
    {
        return std::nullopt;
    }

    const auto cachedFileIt = cache.binaryFilesByPath.find(assetPath);

    if (cachedFileIt != cache.binaryFilesByPath.end())
    {
        return cachedFileIt->second;
    }

    const std::optional<std::vector<uint8_t>> bytes = pAssetFileSystem->readBinaryFile(assetPath);
    cache.binaryFilesByPath[assetPath] = bytes;
    return bytes;
}

std::optional<std::array<uint8_t, 256 * 3>> GameplayHudCommon::loadCachedActPalette(
    const Engine::AssetFileSystem *pAssetFileSystem,
    GameplayAssetLoadCache &cache,
    int16_t paletteId,
    const std::string &worldId)
{
    if (paletteId <= 0)
    {
        return std::nullopt;
    }

    const std::string cacheKey = actPaletteCacheKey(paletteId, worldId);
    const auto cachedPaletteIt = cache.actPalettesByKey.find(cacheKey);

    if (cachedPaletteIt != cache.actPalettesByKey.end())
    {
        return cachedPaletteIt->second;
    }

    for (const std::string &palettePath : actPaletteCandidatePaths(paletteId, worldId))
    {
        const std::optional<std::vector<uint8_t>> paletteBytes =
            readCachedBinaryFile(pAssetFileSystem, cache, palettePath);

        if (!paletteBytes || paletteBytes->size() < 256 * 3)
        {
            continue;
        }

        std::array<uint8_t, 256 * 3> palette = {};
        std::memcpy(palette.data(), paletteBytes->data(), palette.size());
        cache.actPalettesByKey[cacheKey] = palette;
        return palette;
    }

    cache.actPalettesByKey[cacheKey] = std::nullopt;
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> GameplayHudCommon::loadHudBitmapPixelsBgraCached(
    const Engine::AssetFileSystem *pAssetFileSystem,
    GameplayAssetLoadCache &cache,
    const std::string &textureName,
    int &width,
    int &height,
    GameplayHudBitmapTransparencyMode transparencyMode,
    Engine::AssetScaleTier *pLoadedTier)
{
    if (pAssetFileSystem == nullptr)
    {
        return std::nullopt;
    }

    Engine::ImageDecodeOptions decodeOptions = {};
    decodeOptions.applyPaletteZeroTransparencyKey = transparencyMode == GameplayHudBitmapTransparencyMode::ItemIcon;
    decodeOptions.applyMagentaTransparencyKey = true;
    decodeOptions.applyTealTransparencyKey = true;
    decodeOptions.applyBlackTransparencyKey = usesBlackTransparencyKey(textureName);

    std::string directoryPath = "Data/icons";
    std::string fileName = textureName;
    const size_t separator = textureName.find_last_of('/');
    if (separator != std::string::npos)
    {
        directoryPath = textureName.substr(0, separator);
        fileName = textureName.substr(separator + 1);
    }

    const std::optional<Engine::ImagePixelsBgra> image = Engine::loadImageAssetPixelsBgra(
        *pAssetFileSystem,
        directoryPath,
        fileName,
        cache.directoryAssetPathsByPath,
        cache.assetPathByKey,
        cache.binaryFilesByPath,
        decodeOptions);

    if (!image)
    {
        return std::nullopt;
    }

    width = image->width;
    height = image->height;
    if (pLoadedTier != nullptr)
    {
        *pLoadedTier = image->assetScaleTier;
    }
    return image->pixels;
}

std::optional<std::vector<uint8_t>> GameplayHudCommon::loadSpriteBitmapPixelsBgraCached(
    const Engine::AssetFileSystem *pAssetFileSystem,
    GameplayAssetLoadCache &cache,
    const std::string &textureName,
    int16_t paletteId,
    int &width,
    int &height,
    const std::string &worldId,
    Engine::AssetScaleTier *pLoadedTier)
{
    if (pAssetFileSystem == nullptr)
    {
        return std::nullopt;
    }

    Engine::ImageDecodeOptions decodeOptions = {};
    decodeOptions.overridePalette = loadCachedActPalette(pAssetFileSystem, cache, paletteId, worldId);
    decodeOptions.applyPaletteZeroTransparencyKey = true;

    const std::optional<Engine::ImagePixelsBgra> image = Engine::loadImageAssetPixelsBgra(
        *pAssetFileSystem,
        "Data/sprites",
        textureName,
        cache.directoryAssetPathsByPath,
        cache.assetPathByKey,
        cache.binaryFilesByPath,
        decodeOptions);

    if (!image)
    {
        return std::nullopt;
    }

    width = image->width;
    height = image->height;
    if (pLoadedTier != nullptr)
    {
        *pLoadedTier = image->assetScaleTier;
    }
    return image->pixels;
}

const GameplayHudTextureData *GameplayHudCommon::findHudTexture(
    const std::vector<GameplayHudTextureData> &textures,
    const std::unordered_map<std::string, size_t> &textureIndexByName,
    const std::string &textureName)
{
    auto iterator = textureIndexByName.find(textureName);
    if (iterator == textureIndexByName.end())
    {
        iterator = textureIndexByName.find(toLowerCopy(textureName));
    }

    if (iterator == textureIndexByName.end() || iterator->second >= textures.size())
    {
        return nullptr;
    }

    return &textures[iterator->second];
}

bool GameplayHudCommon::loadHudTexture(
    const Engine::AssetFileSystem *pAssetFileSystem,
    GameplayAssetLoadCache &cache,
    const std::string &textureName,
    std::vector<GameplayHudTextureData> &textures,
    std::unordered_map<std::string, size_t> &textureIndexByName,
    GameplayHudBitmapTransparencyMode transparencyMode)
{
    if (textureName.empty())
    {
        return false;
    }

    if (findHudTexture(textures, textureIndexByName, textureName) != nullptr)
    {
        return true;
    }

    int width = 0;
    int height = 0;
    Engine::AssetScaleTier loadedTier = Engine::AssetScaleTier::X1;
    const std::optional<std::vector<uint8_t>> pixels = loadHudBitmapPixelsBgraCached(
        pAssetFileSystem, cache, textureName, width, height, transparencyMode, &loadedTier);

    if (!pixels || width <= 0 || height <= 0 || pAssetFileSystem == nullptr)
    {
        return false;
    }

    GameplayHudTextureData textureHandle = {};
    textureHandle.textureName = toLowerCopy(textureName);
    textureHandle.width = Engine::scalePhysicalPixelsToLogical(width, loadedTier);
    textureHandle.height = Engine::scalePhysicalPixelsToLogical(height, loadedTier);
    textureHandle.assetScaleTier = loadedTier;
    textureHandle.physicalWidth = width;
    textureHandle.physicalHeight = height;
    textureHandle.bgraPixels = *pixels;
    textureHandle.textureHandle = createBgraTexture2D(
        static_cast<uint16_t>(width),
        static_cast<uint16_t>(height),
        pixels->data(),
        static_cast<uint32_t>(pixels->size()),
        textureHandle.textureName.starts_with("obsidian_")
            ? TextureFilterProfile::UiIllustration : TextureFilterProfile::Ui,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

    if (!bgfx::isValid(textureHandle.textureHandle))
    {
        return false;
    }

    const size_t index = textures.size();
    textures.push_back(std::move(textureHandle));
    textureIndexByName[textures.back().textureName] = index;
    return true;
}

std::optional<GameplayHudTextureHandle> GameplayHudCommon::ensureDynamicHudTexture(
    const std::string &textureName,
    int width,
    int height,
    const std::vector<uint8_t> &bgraPixels,
    std::vector<GameplayHudTextureData> &textures,
    std::unordered_map<std::string, size_t> &textureIndexByName)
{
    const size_t expectedBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

    if (textureName.empty() || width <= 0 || height <= 0 || bgraPixels.size() != expectedBytes)
    {
        return std::nullopt;
    }

    const std::string normalizedTextureName = toLowerCopy(textureName);
    GameplayHudTextureData *pTexture = nullptr;
    const auto existingIterator = textureIndexByName.find(normalizedTextureName);

    if (existingIterator != textureIndexByName.end() && existingIterator->second < textures.size())
    {
        pTexture = &textures[existingIterator->second];
    }

    if (pTexture == nullptr)
    {
        GameplayHudTextureData textureData = {};
        textureData.textureName = normalizedTextureName;
        textureData.width = width;
        textureData.height = height;
        textureData.physicalWidth = width;
        textureData.physicalHeight = height;
        textureData.bgraPixels = bgraPixels;
        textureData.textureHandle = createEmptyBgraTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            TextureFilterProfile::Ui,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_TEXTURE_BLIT_DST);

        if (!bgfx::isValid(textureData.textureHandle))
        {
            return std::nullopt;
        }

        const size_t index = textures.size();
        textures.push_back(std::move(textureData));
        textureIndexByName[normalizedTextureName] = index;
        pTexture = &textures.back();
    }
    else if (pTexture->physicalWidth != width || pTexture->physicalHeight != height)
    {
        if (bgfx::isValid(pTexture->textureHandle))
        {
            bgfx::destroy(pTexture->textureHandle);
        }

        pTexture->width = width;
        pTexture->height = height;
        pTexture->physicalWidth = width;
        pTexture->physicalHeight = height;
        pTexture->textureHandle = createEmptyBgraTexture2D(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height),
            TextureFilterProfile::Ui,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_TEXTURE_BLIT_DST);

        if (!bgfx::isValid(pTexture->textureHandle))
        {
            pTexture->textureHandle = BGFX_INVALID_HANDLE;
            return std::nullopt;
        }
    }

    pTexture->bgraPixels = bgraPixels;
    bgfx::updateTexture2D(
        pTexture->textureHandle,
        0,
        0,
        0,
        0,
        static_cast<uint16_t>(width),
        static_cast<uint16_t>(height),
        copyBgraTextureUploadMemory(bgraPixels.data(), static_cast<uint32_t>(bgraPixels.size())));

    GameplayHudTextureHandle result = {};
    result.textureName = pTexture->textureName;
    result.width = pTexture->width;
    result.height = pTexture->height;
    result.textureHandle = pTexture->textureHandle;
    return result;
}

const GameplayHudFontData *GameplayHudCommon::findHudFont(
    const std::vector<GameplayHudFontData> &fonts,
    const std::string &fontName)
{
    const std::string normalizedFontName = toLowerCopy(fontName);

    for (const GameplayHudFontData &fontHandle : fonts)
    {
        if (fontHandle.fontName == normalizedFontName)
        {
            return &fontHandle;
        }
    }

    return nullptr;
}

bool GameplayHudCommon::loadHudFont(
    const Engine::AssetFileSystem *pAssetFileSystem,
    GameplayAssetLoadCache &cache,
    const std::string &fontName,
    std::vector<GameplayHudFontData> &fonts,
    const Engine::FontSettings &settings)
{
    if (fontName.empty())
    {
        return true;
    }

    if (findHudFont(fonts, fontName) != nullptr)
    {
        return true;
    }

    std::string error;
    std::optional<Engine::FontAtlasImage> image;
    bool independentFont = false;
    const std::optional<std::string> descriptorText =
        pAssetFileSystem->readTextFile("fonts/truetype/" + toLowerCopy(fontName) + ".yml");
    if (descriptorText)
    {
        try
        {
            const YAML::Node descriptor = YAML::Load(*descriptorText);
            independentFont = descriptor["metrics"].as<std::string>("legacy") == "freetype";
        }
        catch (const YAML::Exception &exception)
        {
            std::cout << "HUD font load failed: font=\"" << fontName
                      << "\" reason=invalid-descriptor: " << exception.what() << '\n';
            return false;
        }
    }
    if (independentFont)
    {
        image = Engine::loadTrueTypeFontAtlas(*pAssetFileSystem, fontName, error);
    }
    else
    {
        std::optional<std::string> fontPath =
            findCachedAssetPath(pAssetFileSystem, cache, "Data/icons", fontName + ".fnt");

        if (!fontPath)
        {
            fontPath = findCachedAssetPath(pAssetFileSystem, cache, "Data/EnglishT", fontName + ".fnt");
        }

        if (!fontPath)
        {
            const std::optional<std::string> pcxFontPath =
                findCachedAssetPath(pAssetFileSystem, cache, "ui/dialogue", fontName + ".pcx");
            if (pcxFontPath)
            {
                Engine::ImageDecodeOptions decodeOptions = {};
                decodeOptions.applyTealTransparencyKey = true;
                const std::optional<Engine::ImagePixelsBgra> image = Engine::loadImageAssetPixelsBgra(
                    *pAssetFileSystem,
                    *pcxFontPath,
                    cache.binaryFilesByPath,
                    decodeOptions);
                const std::optional<GameplayHudFontData> bitmapFont = image
                    ? buildColumnSeparatedHudFont(*image, fontName)
                    : std::nullopt;
                if (bitmapFont)
                {
                    fonts.push_back(*bitmapFont);
                    return true;
                }

                std::cout << "HUD font load failed: font=\"" << fontName << "\" path=\"" << *pcxFontPath
                          << "\" reason=column-separated-pcx-parse-failed\n";
                return false;
            }

            std::cout << "HUD font load failed: font=\"" << fontName << "\" reason=path-not-found\n";
            return false;
        }

        const std::optional<std::vector<uint8_t>> fontBytes = readCachedBinaryFile(pAssetFileSystem, cache, *fontPath);

        if (!fontBytes || fontBytes->empty())
        {
            std::cout << "HUD font load failed: font=\"" << fontName << "\" path=\"" << *fontPath
                      << "\" reason=read-failed\n";
            return false;
        }

        image = Engine::loadFontAtlas(*pAssetFileSystem, *fontBytes, fontName, settings, error);
    }
    if (!image)
    {
        std::cout << "HUD font load failed: font=\"" << fontName << "\" reason=" << error << '\n';
        return false;
    }

    GameplayHudFontData fontHandle = {};
    static_cast<Engine::FontAtlas &>(fontHandle) = std::move(image->atlas);
    fontHandle.fontName = toLowerCopy(fontName);
    fontHandle.smooth = fontHandle.atlasScale > 1 || independentFont;
    const int atlasWidth = fontHandle.atlasWidth;
    const int atlasHeight = fontHandle.atlasHeight;
    const std::vector<uint8_t> &mainPixels = fontHandle.mainAtlasPixels;
    const std::vector<uint8_t> &shadowPixels = image->shadowPixels;
    const TextureFilterProfile filter = fontHandle.smooth
        ? TextureFilterProfile::SmoothText : TextureFilterProfile::Text;

    fontHandle.mainTextureHandle = createBgraTexture2D(
        static_cast<uint16_t>(atlasWidth),
        static_cast<uint16_t>(atlasHeight),
        mainPixels.data(),
        static_cast<uint32_t>(mainPixels.size()),
        filter);
    fontHandle.shadowTextureHandle = createBgraTexture2D(
        static_cast<uint16_t>(atlasWidth),
        static_cast<uint16_t>(atlasHeight),
        shadowPixels.data(),
        static_cast<uint32_t>(shadowPixels.size()),
        filter);

    if (!bgfx::isValid(fontHandle.mainTextureHandle) || !bgfx::isValid(fontHandle.shadowTextureHandle))
    {
        if (bgfx::isValid(fontHandle.mainTextureHandle))
        {
            bgfx::destroy(fontHandle.mainTextureHandle);
        }

        if (bgfx::isValid(fontHandle.shadowTextureHandle))
        {
            bgfx::destroy(fontHandle.shadowTextureHandle);
        }

        std::cout << "HUD font load failed: font=\"" << fontName << "\" atlas=" << atlasWidth << "x" << atlasHeight
                  << " reason=texture-create-failed\n";
        return false;
    }

    fonts.push_back(std::move(fontHandle));
    return true;
}

bool GameplayHudCommon::tryGetOpaqueHudTextureBounds(
    const GameplayHudTextureData &texture,
    Engine::AssetScaleTier assetScaleTier,
    int &width,
    int &height,
    int &opaqueMinX,
    int &opaqueMinY,
    int &opaqueMaxX,
    int &opaqueMaxY)
{
    if (texture.physicalWidth <= 0 || texture.physicalHeight <= 0 || texture.bgraPixels.empty())
    {
        return false;
    }

    width = texture.width;
    height = texture.height;
    opaqueMinX = width;
    opaqueMinY = height;
    opaqueMaxX = -1;
    opaqueMaxY = -1;

    for (int y = 0; y < texture.physicalHeight; ++y)
    {
        for (int x = 0; x < texture.physicalWidth; ++x)
        {
            const size_t pixelOffset =
                (static_cast<size_t>(y) * static_cast<size_t>(texture.physicalWidth) + static_cast<size_t>(x)) * 4;

            if (pixelOffset + 3 >= texture.bgraPixels.size() || texture.bgraPixels[pixelOffset + 3] == 0)
            {
                continue;
            }

            opaqueMinX = std::min(opaqueMinX, x / Engine::assetScaleTierFactor(assetScaleTier));
            opaqueMinY = std::min(opaqueMinY, y / Engine::assetScaleTierFactor(assetScaleTier));
            opaqueMaxX = std::max(opaqueMaxX, x / Engine::assetScaleTierFactor(assetScaleTier));
            opaqueMaxY = std::max(opaqueMaxY, y / Engine::assetScaleTierFactor(assetScaleTier));
        }
    }

    if (opaqueMaxX < opaqueMinX || opaqueMaxY < opaqueMinY)
    {
        opaqueMinX = 0;
        opaqueMinY = 0;
        opaqueMaxX = std::max(0, width - 1);
        opaqueMaxY = std::max(0, height - 1);
    }

    return true;
}

bgfx::TextureHandle GameplayHudCommon::ensureHudTextureColor(
    const GameplayHudTextureData &texture,
    uint32_t colorAbgr,
    std::vector<GameplayHudTextureColorTextureData> &colorTextures)
{
    return ensureHudTextureColorWithMode(
        texture,
        colorAbgr,
        GameplayHudTextureColorMode::Solid,
        colorTextures);
}

bgfx::TextureHandle GameplayHudCommon::ensureHudTextureColorModulated(
    const GameplayHudTextureData &texture,
    uint32_t colorAbgr,
    std::vector<GameplayHudTextureColorTextureData> &colorTextures)
{
    return ensureHudTextureColorWithMode(
        texture,
        colorAbgr,
        GameplayHudTextureColorMode::Modulated,
        colorTextures);
}

bgfx::TextureHandle GameplayHudCommon::ensureHudFontMainTextureColor(
    const GameplayHudFontData &font,
    uint32_t colorAbgr,
    std::vector<GameplayHudFontColorTextureData> &colorTextures)
{
    if (colorAbgr == 0xffffffffu)
    {
        return font.mainTextureHandle;
    }

    for (const GameplayHudFontColorTextureData &textureHandle : colorTextures)
    {
        if (textureHandle.fontName == font.fontName && textureHandle.colorAbgr == colorAbgr)
        {
            return textureHandle.textureHandle;
        }
    }

    if (font.mainAtlasPixels.empty() || font.atlasWidth <= 0 || font.atlasHeight <= 0)
    {
        return BGFX_INVALID_HANDLE;
    }

    std::vector<uint8_t> tintedPixels = font.mainAtlasPixels;
    const uint8_t red = static_cast<uint8_t>(colorAbgr & 0xff);
    const uint8_t green = static_cast<uint8_t>((colorAbgr >> 8) & 0xff);
    const uint8_t blue = static_cast<uint8_t>((colorAbgr >> 16) & 0xff);

    for (size_t pixelIndex = 0; pixelIndex + 3 < tintedPixels.size(); pixelIndex += 4)
    {
        if (tintedPixels[pixelIndex + 3] == 0 && !font.smooth)
        {
            continue;
        }

        tintedPixels[pixelIndex + 0] = blue;
        tintedPixels[pixelIndex + 1] = green;
        tintedPixels[pixelIndex + 2] = red;
    }

    const bgfx::TextureHandle textureHandle = createBgraTexture2D(
        static_cast<uint16_t>(font.atlasWidth),
        static_cast<uint16_t>(font.atlasHeight),
        tintedPixels.data(),
        static_cast<uint32_t>(tintedPixels.size()),
        font.smooth ? TextureFilterProfile::SmoothText : TextureFilterProfile::Text);

    if (!bgfx::isValid(textureHandle))
    {
        return BGFX_INVALID_HANDLE;
    }

    GameplayHudFontColorTextureData tintedTextureHandle = {};
    tintedTextureHandle.fontName = font.fontName;
    tintedTextureHandle.colorAbgr = colorAbgr;
    tintedTextureHandle.textureHandle = textureHandle;
    colorTextures.push_back(std::move(tintedTextureHandle));
    return colorTextures.back().textureHandle;
}

size_t GameplayHudCommon::hudColorCodeLength(const std::string &text, size_t index)
{
    if (index >= text.size() || text[index] != '\f')
    {
        return 0;
    }

    size_t length = 1;

    while (length < 6 && index + length < text.size() && text[index + length] >= '0' && text[index + length] <= '9')
    {
        ++length;
    }

    return length;
}

size_t GameplayHudCommon::hudTabStopLength(const std::string &text, size_t index)
{
    if (index + 3 >= text.size() || text[index] != '\t')
    {
        return 0;
    }

    for (size_t digit = 1; digit <= 3; ++digit)
    {
        const char character = text[index + digit];

        if (character != ' ' && (character < '0' || character > '9'))
        {
            return 0;
        }
    }

    return 4;
}

int GameplayHudCommon::hudTabStopValue(const std::string &text, size_t index)
{
    int value = 0;
    bool digits = false;

    for (size_t digit = 1; digit <= 3 && index + digit < text.size(); ++digit)
    {
        const char character = text[index + digit];

        if (character >= '0' && character <= '9')
        {
            value = value * 10 + (character - '0');
            digits = true;
        }
        else if (digits)
        {
            break;
        }
    }

    return value;
}

bool GameplayHudCommon::hasHudColorCodes(const std::string &text)
{
    return text.find('\f') != std::string::npos || text.find('\t') != std::string::npos;
}

std::vector<GameplayHudCommon::HudColorRun> GameplayHudCommon::splitHudColorRuns(const std::string &text)
{
    std::vector<HudColorRun> runs(1);

    for (size_t index = 0; index < text.size(); ++index)
    {
        if (const size_t tabLength = hudTabStopLength(text, index); tabLength > 0)
        {
            const std::optional<uint32_t> colorAbgr = runs.back().colorAbgr;

            if (!runs.back().text.empty())
            {
                runs.push_back({});
                runs.back().colorAbgr = colorAbgr;
            }

            runs.back().tabStop = hudTabStopValue(text, index);
            index += tabLength - 1;
            continue;
        }

        const size_t codeLength = hudColorCodeLength(text, index);

        if (codeLength == 0)
        {
            runs.back().text.push_back(text[index]);
            continue;
        }

        const int color = codeLength > 1 ? std::stoi(text.substr(index + 1, codeLength - 1)) : 0;
        std::optional<uint32_t> colorAbgr;

        if (color != 0)
        {
            const uint32_t red = ((static_cast<uint32_t>(color) >> 11) & 31u) * 255u / 31u;
            const uint32_t green = ((static_cast<uint32_t>(color) >> 5) & 63u) * 255u / 63u;
            const uint32_t blue = (static_cast<uint32_t>(color) & 31u) * 255u / 31u;
            colorAbgr = 0xff000000u | (blue << 16) | (green << 8) | red;
        }

        if (!runs.back().text.empty())
        {
            runs.push_back({});
        }

        runs.back().colorAbgr = colorAbgr;
        index += codeLength - 1;
    }

    return runs;
}

float GameplayHudCommon::measureHudTextWidth(const GameplayHudFontData &font, const std::string &text)
{
    float widthPixels = 0.0f;
    float widestPixels = 0.0f;
    uint8_t previous = 0;

    for (size_t index = 0; index < text.size(); ++index)
    {
        const unsigned char character = static_cast<unsigned char>(text[index]);

        if (character == '\r' || character == '\n')
        {
            break;
        }

        if (const size_t codeLength = hudColorCodeLength(text, index); codeLength > 0)
        {
            index += codeLength - 1;
            continue;
        }

        if (const size_t tabLength = hudTabStopLength(text, index); tabLength > 0)
        {
            // The pen goes to the stop, back as well as forwards; the width is the furthest it got.
            widestPixels = std::max(widestPixels, widthPixels);
            widthPixels = static_cast<float>(hudTabStopValue(text, index));
            index += tabLength - 1;
            previous = 0;
            continue;
        }

        if (character < font.firstChar || character > font.lastChar)
        {
            widthPixels += static_cast<float>(font.atlasCellWidth);
            continue;
        }

        const GameplayHudFontGlyphMetricsData &glyphMetrics = font.glyphMetrics[character];
        widthPixels += font.kerning(previous, character) + glyphMetrics.advance();
        previous = character;
    }

    return std::max({0.0f, widthPixels, widestPixels});
}

std::string GameplayHudCommon::clampHudTextToWidth(
    const GameplayHudFontData &font,
    const std::string &text,
    float maxWidth)
{
    std::string clampedText = text;

    while (!clampedText.empty() && measureHudTextWidth(font, clampedText) > maxWidth)
    {
        clampedText.pop_back();
    }

    return clampedText;
}

std::vector<std::string> GameplayHudCommon::wrapHudTextToWidth(
    const GameplayHudFontData &font,
    const std::string &text,
    float maxWidth)
{
    if (text.empty())
    {
        return {""};
    }

    if (maxWidth <= 0.0f)
    {
        return {text};
    }

    std::vector<std::string> lines;
    std::string currentLine;
    std::string currentWord;
    // The spaces before the word being read, kept as they are (as the original does: texts line up columns with them
    // and put two after a sentence) unless the line breaks there.
    std::string currentSpacing;

    const auto flushLine =
        [&lines, &currentLine]()
        {
            lines.push_back(currentLine);
            currentLine.clear();
        };

    const auto appendWord =
        [&font, &currentLine, &currentSpacing, &flushLine, maxWidth](const std::string &word)
        {
            if (word.empty())
            {
                return;
            }

            if (currentLine.empty())
            {
                // The start of a paragraph keeps its indent.
                currentLine = currentSpacing + word;
                currentSpacing.clear();
                return;
            }

            const std::string candidate = currentLine + (currentSpacing.empty() ? " " : currentSpacing) + word;
            currentSpacing.clear();

            if (measureHudTextWidth(font, candidate) <= maxWidth)
            {
                currentLine = candidate;
            }
            else
            {
                flushLine();
                currentLine = word;
            }
        };

    for (char character : text)
    {
        if (character == '\r')
        {
            continue;
        }

        if (character == '\n')
        {
            appendWord(currentWord);
            currentWord.clear();
            currentSpacing.clear();
            flushLine();
            continue;
        }

        if (character == ' ')
        {
            appendWord(currentWord);
            currentWord.clear();
            currentSpacing.push_back(' ');
            continue;
        }

        currentWord.push_back(character);
    }

    appendWord(currentWord);

    if (!currentLine.empty() || lines.empty())
    {
        lines.push_back(currentLine);
    }

    // A colour runs on over a line break: each line after the first starts in the colour the one before it ended in.
    std::string openColor;

    for (std::string &line : lines)
    {
        const std::string startColor = openColor;

        for (size_t index = 0; index < line.size(); ++index)
        {
            if (const size_t codeLength = hudColorCodeLength(line, index); codeLength > 0)
            {
                const std::string code = line.substr(index, codeLength);
                openColor = code == "\f00000" ? std::string() : code;
                index += codeLength - 1;
            }
        }

        if (!startColor.empty())
        {
            line.insert(0, startColor);
        }
    }

    return lines;
}

void GameplayHudCommon::renderHudFontLayer(
    const GameplayHudFontData &font,
    bgfx::TextureHandle textureHandle,
    const std::string &text,
    float textX,
    float textY,
    float fontScale,
    const SubmitTexturedQuadFn &submitTexturedQuad)
{
    if (!bgfx::isValid(textureHandle))
    {
        return;
    }

    float penX = textX;
    uint8_t previous = 0;

    for (size_t index = 0; index < text.size(); ++index)
    {
        const unsigned char character = static_cast<unsigned char>(text[index]);

        if (character == '\r' || character == '\n')
        {
            break;
        }

        if (const size_t codeLength = hudColorCodeLength(text, index); codeLength > 0)
        {
            index += codeLength - 1;
            continue;
        }

        if (const size_t tabLength = hudTabStopLength(text, index); tabLength > 0)
        {
            penX = textX + static_cast<float>(hudTabStopValue(text, index)) * fontScale;
            index += tabLength - 1;
            previous = 0;
            continue;
        }

        if (character < font.firstChar || character > font.lastChar)
        {
            penX += static_cast<float>(font.atlasCellWidth) * fontScale;
            continue;
        }

        const GameplayHudFontGlyphMetricsData &glyphMetrics = font.glyphMetrics[character];
        penX += (font.kerning(previous, character) + glyphMetrics.leftSpacing) * fontScale;

        if (glyphMetrics.width > 0)
        {
            const int cellX = (character % 16) * (font.atlasCellWidth + 2 * font.atlasPadding);
            const int cellY = (character / 16) * (font.fontHeight + 2 * font.atlasPadding);
            const float u0 = static_cast<float>(cellX) / static_cast<float>(font.atlasWidth / font.atlasScale);
            const float v0 = static_cast<float>(cellY) / static_cast<float>(font.atlasHeight / font.atlasScale);
            const float u1 = static_cast<float>(cellX + glyphMetrics.width + 2 * font.atlasPadding)
                / static_cast<float>(font.atlasWidth / font.atlasScale);
            const float v1 = static_cast<float>(cellY + font.fontHeight + 2 * font.atlasPadding)
                / static_cast<float>(font.atlasHeight / font.atlasScale);
            const float glyphWidth = static_cast<float>(glyphMetrics.width + 2 * font.atlasPadding) * fontScale;
            const float glyphHeight = static_cast<float>(font.fontHeight + 2 * font.atlasPadding) * fontScale;

            submitTexturedQuad(
                textureHandle,
                penX - font.atlasPadding * fontScale,
                textY - font.atlasPadding * fontScale,
                glyphWidth,
                glyphHeight,
                u0,
                v0,
                u1,
                v1,
                font.smooth ? TextureFilterProfile::SmoothText : TextureFilterProfile::Text);
        }

        penX += (glyphMetrics.advance() - glyphMetrics.leftSpacing) * fontScale;
        previous = character;
    }
}

void GameplayHudCommon::renderLayoutLabel(
    const UiLayoutManager::LayoutElement &layout,
    const GameplayResolvedHudLayoutElement &resolved,
    const std::string &label,
    const FindHudFontFn &findHudFontFn,
    const EnsureHudFontColorFn &ensureHudFontColorFn,
    const RenderHudFontLayerFn &renderHudFontLayerFn)
{
    if (label.empty())
    {
        return;
    }

    const GameplayHudFontData *pFont = findHudFontFn(layout.fontName);

    if (pFont == nullptr)
    {
        static std::unordered_set<std::string> missingFontLogs;
        const std::string logKey = "missing-font:" + toLowerCopy(layout.id) + ":" + toLowerCopy(layout.fontName);

        if (!missingFontLogs.contains(logKey))
        {
            std::cout << "HUD label skipped: id=" << layout.id
                      << " font=\"" << layout.fontName
                      << "\" reason=missing-font label=\"" << label << "\"\n";
            missingFontLogs.insert(logKey);
        }

        return;
    }

    float fontScale = resolved.scale * std::max(0.1f, layout.textScale);

    if (!pFont->smooth && fontScale >= 1.0f)
    {
        fontScale = snappedHudFontScale(fontScale);
    }

    const float labelHeightPixels = static_cast<float>(pFont->fontHeight) * fontScale;
    const float maxLabelWidth = std::max(0.0f, resolved.width - std::abs(layout.textPadX * fontScale) * 2.0f);
    std::string clampedLabel = clampHudTextToWidth(*pFont, label, maxLabelWidth / fontScale);

    if (clampedLabel.empty())
    {
        const GameplayHudFontData *pFallbackFont = findHudFontFn("Lucida");

        if (pFallbackFont != nullptr)
        {
            pFont = pFallbackFont;
            clampedLabel = clampHudTextToWidth(*pFont, label, maxLabelWidth / fontScale);
        }
    }

    if (clampedLabel.empty())
    {
        clampedLabel = label;
    }

    const float labelWidthPixels = measureHudTextWidth(*pFont, clampedLabel) * fontScale;
    float textX = resolved.x + layout.textPadX * resolved.scale;
    float textY = resolved.y + layout.textPadY * resolved.scale;

    switch (layout.textAlignX)
    {
    case UiLayoutManager::TextAlignX::Left:
        break;

    case UiLayoutManager::TextAlignX::Center:
        textX = resolved.x + (resolved.width - labelWidthPixels) * 0.5f + layout.textPadX * resolved.scale;
        break;

    case UiLayoutManager::TextAlignX::Right:
        textX = resolved.x + resolved.width - labelWidthPixels + layout.textPadX * resolved.scale;
        break;
    }

    switch (layout.textAlignY)
    {
    case UiLayoutManager::TextAlignY::Top:
        break;

    case UiLayoutManager::TextAlignY::Middle:
        textY = resolved.y + (resolved.height - labelHeightPixels) * 0.5f + layout.textPadY * resolved.scale;
        break;

    case UiLayoutManager::TextAlignY::Bottom:
        textY = resolved.y + resolved.height - labelHeightPixels + layout.textPadY * resolved.scale;
        break;
    }

    textX = std::round(textX);
    textY = std::round(textY);

    renderHudFontLayerFn(*pFont, pFont->shadowTextureHandle, clampedLabel, textX, textY, fontScale);
    const float lineStartX = textX;

    // Each run of the text in its own colour, the label's own where the text names none, from its tab stop if it
    // has one.
    for (const HudColorRun &run : splitHudColorRuns(clampedLabel))
    {
        if (run.tabStop)
        {
            textX = lineStartX + static_cast<float>(*run.tabStop) * fontScale;
        }

        bgfx::TextureHandle coloredMainTextureHandle =
            ensureHudFontColorFn(*pFont, run.colorAbgr.value_or(layout.textColorAbgr));

        if (!bgfx::isValid(coloredMainTextureHandle))
        {
            coloredMainTextureHandle = pFont->mainTextureHandle;
        }

        renderHudFontLayerFn(*pFont, coloredMainTextureHandle, run.text, textX, textY, fontScale);
        textX += measureHudTextWidth(*pFont, run.text) * fontScale;
    }
}
} // namespace OpenYAMM::Game
