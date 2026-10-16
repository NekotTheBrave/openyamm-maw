#include "engine/ApplicationConfig.h"

#include "engine/AssetPaths.h"

namespace OpenYAMM::Engine
{
ApplicationConfig ApplicationConfig::createDefault()
{
    ApplicationConfig config;
    config.appName = "OpenYAMM";
#if defined(__ANDROID__)
    config.assetRoot = "assets";
#else
    config.assetRoot = AssetPaths::getDevelopmentAssetRoot();
#endif
    config.activeWorldId = "mm8";
    config.startupMapFileOverride.clear();
    config.loadUniqueActorIndex.reset();
    config.assetScaleTier = AssetScaleTier::X1;
    config.assetScaleProfile = createUniformAssetScaleProfile(config.assetScaleTier);
    config.windowWidth = OPENYAMM_WINDOW_WIDTH;
    config.windowHeight = OPENYAMM_WINDOW_HEIGHT;
    config.windowMode = WindowMode::Windowed;
    config.verticalSync = false;
    config.maxAnisotropy = 4;
    config.fpsTrace = false;
    config.performanceTrace = false;
    config.hitchTrace = false;
    config.hitchThresholdMilliseconds = 8.0f;
    return config;
}
}
