#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace OpenYAMM::Engine
{
struct ModelSkyEnvironment
{
    std::string key;
    uint16_t width = 0;
    uint16_t height = 0;
    float logicalWidth = 0;
    float logicalHeight = 0;
    std::span<const uint8_t> bgraPixels;
};

struct ModelEnvironmentMip
{
    uint16_t size = 0;
    std::array<std::vector<uint16_t>, 6> rgbaHalfFaces;
};

// Linear GGX-filtered radiance. The sky source is the game's repeating cloud plane, not a panorama.
std::vector<ModelEnvironmentMip> prepareModelSkyEnvironment(const ModelSkyEnvironment &sky);
// A neutral room (dark floor, lit walls, dim ceiling) for enclosed maps, tinted per frame like the sky; small and
// cheap to filter.
std::vector<ModelEnvironmentMip> prepareModelRoomEnvironment();
// RG = the two Fresnel terms of the integrated GGX/Smith specular response; linear RGBA8.
std::vector<uint8_t> prepareModelEnvironmentBrdf();
constexpr uint16_t ModelEnvironmentBrdfSize = 64;
}
