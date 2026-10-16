#pragma once

#include "engine/AssetScaleTier.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace OpenYAMM::Engine
{
enum class WindowMode
{
    Windowed,
    WindowedFullscreen,
    Fullscreen
};

struct ApplicationConfig
{
    std::string appName;
    std::string assetRoot;
    std::string activeWorldId;
    std::string startupMapFileOverride;
    std::optional<size_t> loadUniqueActorIndex;
    AssetScaleTier assetScaleTier;
    AssetScaleProfile assetScaleProfile;
    int windowWidth;
    int windowHeight;
    WindowMode windowMode;
    bool verticalSync;
    // Upper limit of anisotropic texture filtering; 1 turns it off.
    uint8_t maxAnisotropy;
    bool fpsTrace;
    bool performanceTrace;
    bool hitchTrace;
    float hitchThresholdMilliseconds;

    static ApplicationConfig createDefault();
};
}
