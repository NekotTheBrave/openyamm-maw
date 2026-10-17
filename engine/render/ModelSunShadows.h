#pragma once

#include "engine/models/ModelAsset.h"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace OpenYAMM::Engine
{
constexpr size_t ModelSunShadowCascades = 3;
constexpr uint16_t ModelSunShadowSize = 2048;
// PSSM splits: this share of the logarithmic split, the rest uniform (OpenMW's split ratio). The logarithmic part
// starts at ModelSunShadowSplitStart instead of the camera's near plane, whose few units would leave the first
// cascade uselessly small.
constexpr float ModelSunShadowSplitLambda = 0.7f;
constexpr float ModelSunShadowSplitStart = 32.0f;

struct ModelShadowSettings
{
    // One cascade's square tile of the shadow atlas, and the camera distance the cascades cover.
    uint16_t size = 0;
    float distance = 0.0f;
};

inline ModelShadowSettings modelShadowSettings(int quality)
{
    switch (quality)
    {
    case 1: return {512, 1536.0f};
    case 2: return {1024, 3072.0f};
    case 3: return {ModelSunShadowSize, 4096.0f};
    default: return {};
    }
}

// Camera distances of the cascade boundaries, the first 0 and the last the shadow distance.
inline std::array<float, ModelSunShadowCascades + 1> modelSunShadowSplits(float distance)
{
    std::array<float, ModelSunShadowCascades + 1> splits = {};
    const float start = std::min(ModelSunShadowSplitStart, distance);
    for (size_t index = 1; index <= ModelSunShadowCascades; ++index)
    {
        const float fraction = float(index) / float(ModelSunShadowCascades);
        const float logarithmic = start * std::pow(distance / start, fraction);
        const float uniform = start + (distance - start) * fraction;
        splits[index] = ModelSunShadowSplitLambda * logarithmic + (1.0f - ModelSunShadowSplitLambda) * uniform;
    }
    return splits;
}

struct ModelSunShadowCamera
{
    std::array<float, 3> position = {};
    std::array<float, 3> forward = {1.0f, 0.0f, 0.0f};
    // Tangents of half the horizontal and vertical field of view.
    float tanHalfWidth = 1.0f;
    float tanHalfHeight = 1.0f;
};

struct ModelSunShadowSphere
{
    std::array<float, 3> center = {};
    float radius = 0.0f;
};

// The smallest sphere around the view frustum between two camera depths. Its radius depends only on the depths and
// the field of view, so cascades fitted to it keep their size, and their texels, while the camera turns.
inline ModelSunShadowSphere modelSunShadowSliceSphere(const ModelSunShadowCamera &camera, float nearDepth,
    float farDepth)
{
    const float spread = camera.tanHalfWidth * camera.tanHalfWidth + camera.tanHalfHeight * camera.tanHalfHeight;
    // Equally far from the near and the far corners, but not beyond the far plane (wide fields of view).
    const float depth = std::min(0.5f * (nearDepth + farDepth) * (1.0f + spread), farDepth);
    const float farRadius = std::sqrt((farDepth - depth) * (farDepth - depth) + farDepth * farDepth * spread);
    const float nearRadius = std::sqrt((depth - nearDepth) * (depth - nearDepth) + nearDepth * nearDepth * spread);
    ModelSunShadowSphere sphere;
    for (size_t axis = 0; axis < 3; ++axis)
    {
        sphere.center[axis] = camera.position[axis] + camera.forward[axis] * depth;
    }
    sphere.radius = std::max(farRadius, nearRadius);
    return sphere;
}

struct ModelSunShadowCascade
{
    ModelMatrix view = {};
    ModelMatrix projection = {};
    ModelMatrix textureMatrix = {};
};

// A square orthographic cascade of the given radius around a centre, looking along the sunlight.
inline ModelSunShadowCascade modelSunShadowCascade(const std::array<float, 3> &center,
    const std::array<float, 3> &direction, float radius, bool homogeneousDepth, bool originBottomLeft,
    uint16_t resolution = ModelSunShadowSize)
{
    const bx::Vec3 light = bx::normalize({direction[0], direction[1], direction[2]});
    const bx::Vec3 up = std::abs(light.z) > 0.95f ? bx::Vec3{0, 1, 0} : bx::Vec3{0, 0, 1};
    const bx::Vec3 right = bx::normalize(bx::cross(up, light));
    const bx::Vec3 vertical = bx::cross(light, right);
    const bx::Vec3 eye = {center[0], center[1], center[2]};
    const float texel = 2.0f * radius / resolution;
    const float x = bx::dot(eye, right);
    const float y = bx::dot(eye, vertical);
    // Fixed extents and texel-snapped light-space translation keep camera turns and subtexel movement stable.
    const bx::Vec3 snapped = bx::add(eye, bx::add(
        bx::mul(right, std::round(x / texel) * texel - x),
        bx::mul(vertical, std::round(y / texel) * texel - y)));
    constexpr float depthRadius = 8192.0f;
    ModelSunShadowCascade result;
    bx::mtxLookAt(result.view.data(), bx::add(snapped, bx::mul(light, depthRadius)), snapped,
        up, bx::Handedness::Right);
    bx::mtxOrtho(result.projection.data(), -radius, radius, -radius, radius, 0.0f, 2.0f * depthRadius,
        0.0f, homogeneousDepth, bx::Handedness::Right);
    ModelMatrix clipToTexture = identityModelMatrix();
    clipToTexture[0] = 0.5f;
    clipToTexture[5] = originBottomLeft ? 0.5f : -0.5f;
    clipToTexture[10] = homogeneousDepth ? 0.5f : 1.0f;
    clipToTexture[12] = clipToTexture[13] = 0.5f;
    clipToTexture[14] = homogeneousDepth ? 0.5f : 0.0f;
    ModelMatrix viewProjection;
    bx::mtxMul(viewProjection.data(), result.view.data(), result.projection.data());
    bx::mtxMul(result.textureMatrix.data(), viewProjection.data(), clipToTexture.data());
    return result;
}

inline bool modelSunShadowIntersects(const ModelSunShadowCascade &cascade, const ModelBounds &bounds)
{
    if (!bounds.valid)
    {
        return false;
    }
    constexpr float infinity = std::numeric_limits<float>::infinity();
    std::array<float, 3> low = {infinity, infinity, infinity};
    std::array<float, 3> high = {-infinity, -infinity, -infinity};
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        const float point[4] = {corner & 1 ? bounds.max[0] : bounds.min[0],
            corner & 2 ? bounds.max[1] : bounds.min[1], corner & 4 ? bounds.max[2] : bounds.min[2], 1.0f};
        float projected[4];
        bx::vec4MulMtx(projected, point, cascade.textureMatrix.data());
        for (size_t axis = 0; axis < 3; ++axis)
        {
            low[axis] = std::min(low[axis], projected[axis]);
            high[axis] = std::max(high[axis], projected[axis]);
        }
    }
    for (size_t axis = 0; axis < 3; ++axis)
    {
        if (low[axis] > 1.00001f || high[axis] < -0.00001f)
        {
            return false;
        }
    }
    return true;
}

// Whether the bounds lie inside the cascade's unblended centre. A shadow falls along the light, onto receivers at the
// caster's own position in the cascade, and receivers there read only this or finer cascades (sun_shadows.sh blends
// towards the next cascade beyond 0.8 of the half extent): coarser cascades can skip such a caster.
inline bool modelSunShadowInside(const ModelSunShadowCascade &cascade, const ModelBounds &bounds)
{
    if (!bounds.valid)
    {
        return false;
    }
    // Inside 0.78 of the half extent, leaving room for the receivers' normal offset.
    constexpr float low = 0.5f - 0.39f;
    constexpr float high = 0.5f + 0.39f;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        const float point[4] = {corner & 1 ? bounds.max[0] : bounds.min[0],
            corner & 2 ? bounds.max[1] : bounds.min[1], corner & 4 ? bounds.max[2] : bounds.min[2], 1.0f};
        float projected[4];
        bx::vec4MulMtx(projected, point, cascade.textureMatrix.data());
        if (projected[0] < low || projected[0] > high || projected[1] < low || projected[1] > high
            || projected[2] <= 0.0f || projected[2] >= 1.0f)
        {
            return false;
        }
    }
    return true;
}

// Whether a caster must be drawn into cascade index: it reaches the cascade and no finer cascade holds it whole.
template <size_t Count>
inline bool modelSunShadowCasts(const std::array<ModelSunShadowCascade, Count> &cascades, size_t index,
    const ModelBounds &bounds)
{
    if (!modelSunShadowIntersects(cascades[index], bounds))
    {
        return false;
    }
    for (size_t finer = 0; finer < index; ++finer)
    {
        if (modelSunShadowInside(cascades[finer], bounds))
        {
            return false;
        }
    }
    return true;
}
}
