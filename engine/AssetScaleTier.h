#pragma once

#include <optional>
#include <string>

namespace OpenYAMM::Engine
{
enum class AssetScaleTier
{
    X1 = 1,
    X2 = 2,
    X4 = 4
};

enum class AssetScaleCategory
{
    Textures,
    Sky,
    Decorations,
    Icons
};

struct AssetScaleProfile
{
    AssetScaleTier textures = AssetScaleTier::X1;
    AssetScaleTier sky = AssetScaleTier::X1;
    AssetScaleTier decorations = AssetScaleTier::X1;
    AssetScaleTier icons = AssetScaleTier::X1;
    bool preferRestoredIcons = false;
};

const char *assetScaleTierToString(AssetScaleTier assetScaleTier);
std::optional<AssetScaleTier> parseAssetScaleTier(const std::string &value);
AssetScaleTier assetScaleTierFromResolvedPath(const std::string &path);
int assetScaleTierFactor(AssetScaleTier assetScaleTier);
std::string assetScaleTierDirectorySuffix(AssetScaleTier assetScaleTier);
int scalePhysicalPixelsToLogical(int physicalPixels, AssetScaleTier assetScaleTier);
AssetScaleProfile createUniformAssetScaleProfile(AssetScaleTier assetScaleTier);
AssetScaleTier assetScaleTierForCategory(
    const AssetScaleProfile &assetScaleProfile,
    AssetScaleCategory assetScaleCategory);
void setAssetScaleTierForCategory(
    AssetScaleProfile &assetScaleProfile,
    AssetScaleCategory assetScaleCategory,
    AssetScaleTier assetScaleTier);
}
