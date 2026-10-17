#include "engine/render/ModelEnvironment.h"

#include "engine/ImageMipmaps.h"
#include "engine/ParallelFor.h"

#include <bx/math.h>
#include <bx/uint32_t.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace OpenYAMM::Engine
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
constexpr uint16_t EnvironmentSize = 128;
constexpr uint16_t RoomEnvironmentSize = 32;
constexpr uint32_t SampleCount = 128;

float radicalInverse(uint32_t value)
{
    value = (value << 16) | (value >> 16);
    value = ((value & 0x55555555u) << 1) | ((value & 0xaaaaaaaau) >> 1);
    value = ((value & 0x33333333u) << 2) | ((value & 0xccccccccu) >> 2);
    value = ((value & 0x0f0f0f0fu) << 4) | ((value & 0xf0f0f0f0u) >> 4);
    value = ((value & 0x00ff00ffu) << 8) | ((value & 0xff00ff00u) >> 8);
    return float(value) * 2.3283064365386963e-10f;
}

bx::Vec3 sampleHalfDirection(uint32_t index, float roughness)
{
    const float phi = 2.0f * Pi * float(index) / SampleCount;
    const float sample = radicalInverse(index);
    const float alpha = roughness * roughness;
    const float cosine = std::sqrt((1.0f - sample) / (1.0f + (alpha * alpha - 1.0f) * sample));
    const float sine = std::sqrt(std::max(1.0f - cosine * cosine, 0.0f));
    return {std::cos(phi) * sine, std::sin(phi) * sine, cosine};
}

bx::Vec3 cubeDirection(uint8_t face, float u, float v)
{
    const std::array<bx::Vec3, 6> directions = {{
        {1, -v, -u}, {-1, -v, u}, {u, 1, v}, {u, -1, -v}, {u, -v, 1}, {-u, -v, -1}
    }};
    return bx::normalize(directions[face]);
}

// GGX-prefiltered mip chain of a radiance function (mip m = roughness m / (levels - 1)), scaled to unit average
// luminance.
std::vector<ModelEnvironmentMip> filterRadiance(uint16_t topSize, float luminance,
    const std::function<std::array<float, 3>(const bx::Vec3 &)> &radiance)
{
    // The shader reads roughness r at mip r x (levels - 1), so the levels span roughness 0..1 whatever the size.
    uint32_t levelCount = 0;
    for (uint16_t size = topSize; size > 0; size /= 2)
    {
        ++levelCount;
    }
    std::vector<ModelEnvironmentMip> mips;
    std::vector<std::vector<bx::Vec3>> halfDirectionsByMip;
    for (uint16_t size = topSize; size > 0; size /= 2)
    {
        ModelEnvironmentMip mip;
        mip.size = size;
        const float roughness = float(mips.size()) / float(levelCount - 1);
        std::vector<bx::Vec3> halfDirections;
        halfDirections.reserve(SampleCount);
        for (uint32_t sample = 0; sample < SampleCount; ++sample)
        {
            halfDirections.push_back(sampleHalfDirection(sample, roughness));
        }
        for (std::vector<uint16_t> &pixels : mip.rgbaHalfFaces)
        {
            pixels.resize(size_t(size) * size * 4);
        }
        mips.push_back(std::move(mip));
        halfDirectionsByMip.push_back(std::move(halfDirections));
    }
    // Every face of every mip is independent: filter them on the load workers.
    parallelFor(mips.size() * 6, [&](size_t task)
    {
        const size_t mipIndex = task / 6;
        const uint8_t face = uint8_t(task % 6);
        const uint16_t size = mips[mipIndex].size;
        const std::vector<bx::Vec3> &halfDirections = halfDirectionsByMip[mipIndex];
        std::vector<uint16_t> &pixels = mips[mipIndex].rgbaHalfFaces[face];
        for (uint16_t row = 0; row < size; ++row)
        {
            for (uint16_t column = 0; column < size; ++column)
            {
                const bx::Vec3 normal = cubeDirection(face,
                    2.0f * (column + 0.5f) / size - 1.0f, 2.0f * (row + 0.5f) / size - 1.0f);
                std::array<float, 3> color = {};
                if (mipIndex == 0)
                {
                    color = radiance(normal);
                }
                else
                {
                    const bx::Vec3 up = std::abs(normal.z) < 0.999f ? bx::Vec3{0, 0, 1} : bx::Vec3{1, 0, 0};
                    const bx::Vec3 tangent = bx::normalize(bx::cross(up, normal));
                    const bx::Vec3 bitangent = bx::cross(normal, tangent);
                    float weight = 0;
                    for (const bx::Vec3 &sample : halfDirections)
                    {
                        const bx::Vec3 half = bx::add(bx::add(bx::mul(tangent, sample.x),
                            bx::mul(bitangent, sample.y)), bx::mul(normal, sample.z));
                        const bx::Vec3 light = bx::sub(bx::mul(half, 2.0f * bx::dot(normal, half)), normal);
                        const float cosine = std::max(bx::dot(normal, light), 0.0f);
                        if (cosine > 0)
                        {
                            const std::array<float, 3> incoming = radiance(light);
                            for (size_t channel = 0; channel < 3; ++channel)
                            {
                                color[channel] += incoming[channel] * cosine;
                            }
                            weight += cosine;
                        }
                    }
                    for (float &channel : color)
                    {
                        channel /= std::max(weight, 0.0001f);
                    }
                }
                const size_t offset = (size_t(row) * size + column) * 4;
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    pixels[offset + channel] = bx::halfFromFloat(color[channel] / luminance);
                }
                pixels[offset + 3] = bx::halfFromFloat(1.0f);
            }
        }
    });
    return mips;
}

std::array<float, 3> sampleCloudPlane(const ModelSkyEnvironment &sky,
    const std::vector<std::array<float, 3>> &linearPixels, const std::array<float, 3> &average,
    const bx::Vec3 &direction)
{
    // Match the existing tiled cloud plane's world axes/scale. Fade its horizon singularity into sky fill.
    // ponytail: cloud drift stays fixed in the filtered environment; regenerate only if animated reflections matter.
    const float reciprocalHeight = 64.0f / std::max(direction.z, 0.15f);
    const float u = direction.x * reciprocalHeight / sky.logicalWidth;
    const float v = direction.y * reciprocalHeight / sky.logicalHeight;
    const float x = (u - std::floor(u)) * sky.width - 0.5f;
    const float y = (v - std::floor(v)) * sky.height - 0.5f;
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const float tx = x - x0;
    const float ty = y - y0;
    const auto pixel = [&](int column, int row) -> const std::array<float, 3> &
    {
        return linearPixels[size_t((row + sky.height) % sky.height) * sky.width
            + (column + sky.width) % sky.width];
    };
    const float cloudWeight = std::clamp((direction.z - 0.05f) / 0.15f, 0.0f, 1.0f);
    const float hemisphere = 0.15f + 0.85f * std::clamp(direction.z / 0.15f, 0.0f, 1.0f);
    std::array<float, 3> color;
    for (size_t channel = 0; channel < 3; ++channel)
    {
        const float top = pixel(x0, y0)[channel] * (1.0f - tx) + pixel(x0 + 1, y0)[channel] * tx;
        const float bottom = pixel(x0, y0 + 1)[channel] * (1.0f - tx) + pixel(x0 + 1, y0 + 1)[channel] * tx;
        color[channel] = average[channel] * hemisphere * (1.0f - cloudWeight)
            + (top * (1.0f - ty) + bottom * ty) * cloudWeight;
    }
    return color;
}
}

std::vector<ModelEnvironmentMip> prepareModelSkyEnvironment(const ModelSkyEnvironment &sky)
{
    if (sky.width == 0 || sky.height == 0 || sky.bgraPixels.size() != size_t(sky.width) * sky.height * 4
        || !std::isfinite(sky.logicalWidth) || !std::isfinite(sky.logicalHeight)
        || sky.logicalWidth <= 0 || sky.logicalHeight <= 0)
    {
        throw std::invalid_argument("Invalid model sky environment source");
    }
    std::array<float, 256> decode;
    for (size_t index = 0; index < decode.size(); ++index)
    {
        decode[index] = srgbToLinear(float(index) / 255.0f);
    }
    std::vector<std::array<float, 3>> linearPixels(size_t(sky.width) * sky.height);
    std::array<float, 3> average = {};
    for (size_t index = 0; index < linearPixels.size(); ++index)
    {
        for (size_t channel = 0; channel < 3; ++channel)
        {
            linearPixels[index][channel] = decode[sky.bgraPixels[index * 4 + 2 - channel]];
            average[channel] += linearPixels[index][channel] / linearPixels.size();
        }
    }
    // Probe irradiance supplies scene brightness; normalize the cloud pattern once without losing its colour.
    const float luminance = std::max(average[0] * 0.2126f + average[1] * 0.7152f + average[2] * 0.0722f, 0.01f);
    return filterRadiance(EnvironmentSize, luminance, [&](const bx::Vec3 &direction)
    {
        return sampleCloudPlane(sky, linearPixels, average, direction);
    });
}

std::vector<ModelEnvironmentMip> prepareModelRoomEnvironment()
{
    // A neutral enclosed room: dark floor, lighter walls with a few soft brighter patches (torches, openings) and a
    // dim ceiling, so metal shows a room-like reflection instead of a flat colour. Luminance averages about 1, like the
    // sky cube, because the renderer tints it per frame by the room's ambient and light colour.
    const auto radiance = [](const bx::Vec3 &direction)
    {
        const float up = direction.z;
        float level = up < 0.0f ? 0.45f + 0.35f * (1.0f + up) : 1.0f - 0.35f * up;
        const float azimuth = std::atan2(direction.y, direction.x);
        const float band = std::exp(-up * up * 18.0f);
        const float patches = std::pow(std::max(std::cos(azimuth * 3.0f + 0.7f), 0.0f), 6.0f);
        level += band * (0.25f + 1.6f * patches);
        return std::array<float, 3>{level, level, level};
    };
    float total = 0.0f;
    uint32_t samples = 0;
    for (uint8_t face = 0; face < 6; ++face)
    {
        for (int row = 0; row < 16; ++row)
        {
            for (int column = 0; column < 16; ++column)
            {
                total += radiance(cubeDirection(face, (column + 0.5f) / 8.0f - 1.0f, (row + 0.5f) / 8.0f - 1.0f))[0];
                ++samples;
            }
        }
    }
    return filterRadiance(RoomEnvironmentSize, total / samples, radiance);
}

std::vector<uint8_t> prepareModelEnvironmentBrdf()
{
    std::vector<uint8_t> pixels(size_t(ModelEnvironmentBrdfSize) * ModelEnvironmentBrdfSize * 4);
    for (uint16_t row = 0; row < ModelEnvironmentBrdfSize; ++row)
    {
        const float roughness = (row + 0.5f) / ModelEnvironmentBrdfSize;
        const float alpha = roughness * roughness;
        std::vector<bx::Vec3> halfDirections;
        halfDirections.reserve(SampleCount);
        for (uint32_t sample = 0; sample < SampleCount; ++sample)
        {
            halfDirections.push_back(sampleHalfDirection(sample, roughness));
        }
        const auto visibility = [&](float cosine)
        {
            return 2.0f * cosine / (cosine
                + std::sqrt(alpha * alpha + (1.0f - alpha * alpha) * cosine * cosine));
        };
        for (uint16_t column = 0; column < ModelEnvironmentBrdfSize; ++column)
        {
            const float nv = (column + 0.5f) / ModelEnvironmentBrdfSize;
            const bx::Vec3 view = {std::sqrt(1.0f - nv * nv), 0, nv};
            std::array<float, 2> integral = {};
            for (const bx::Vec3 &half : halfDirections)
            {
                const float vh = std::max(bx::dot(view, half), 0.0f);
                const bx::Vec3 light = bx::sub(bx::mul(half, 2.0f * vh), view);
                if (light.z > 0)
                {
                    const float weight = visibility(nv) * visibility(light.z) * vh / (half.z * nv);
                    const float fresnel = std::pow(1.0f - vh, 5.0f);
                    integral[0] += (1.0f - fresnel) * weight / SampleCount;
                    integral[1] += fresnel * weight / SampleCount;
                }
            }
            const size_t offset = (size_t(row) * ModelEnvironmentBrdfSize + column) * 4;
            pixels[offset] = uint8_t(std::lround(std::clamp(integral[0], 0.0f, 1.0f) * 255));
            pixels[offset + 1] = uint8_t(std::lround(std::clamp(integral[1], 0.0f, 1.0f) * 255));
            pixels[offset + 2] = 0;
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}
}
