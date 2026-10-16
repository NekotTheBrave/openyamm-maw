#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace OpenYAMM::Game
{
// The lit water body tint from the base texture's mean colour (0-255 per channel).
inline uint32_t waterBodyColorFromMeanBgr(double blue, double green, double red)
{
    // Preserve the material's native colour without letting dark source art overwhelm the lit water body.
    const uint8_t r = uint8_t(std::lround(0.10 * 0.65 * 255.0 + red * 0.55 * 0.35));
    const uint8_t g = uint8_t(std::lround(0.26 * 0.65 * 255.0 + green * 0.55 * 0.35));
    const uint8_t b = uint8_t(std::lround(0.30 * 0.65 * 255.0 + blue * 0.55 * 0.35));
    return 0xff000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
}

// Sample the uncomposited water base once, so shoreline land and animated wave patterns cannot affect its tint.
inline uint32_t waterBodyColorFromBgra(std::span<const uint8_t> pixels)
{
    if (pixels.empty() || pixels.size() % 4 != 0)
    {
        throw std::invalid_argument("Invalid water colour pixel data");
    }
    uint64_t red = 0;
    uint64_t green = 0;
    uint64_t blue = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        blue += pixels[offset];
        green += pixels[offset + 1];
        red += pixels[offset + 2];
    }
    const double pixelCount = pixels.size() / 4;
    return waterBodyColorFromMeanBgr(blue / pixelCount, green / pixelCount, red / pixelCount);
}
}
