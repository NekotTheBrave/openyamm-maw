#include "engine/render/ModelSunShadows.h"
#include "engine/render/ModelLod.h"

#include <doctest/doctest.h>

using namespace OpenYAMM::Engine;

TEST_CASE("model LOD uses projected size with hysteresis and independent shadow thresholds")
{
    const ModelBounds bounds = {{-1, -1, -1}, {1, 1, 1}, true};
    CHECK(modelProjectedPixels(bounds, {0, 0, 20}, 900)
        == doctest::Approx(modelProjectedPixels(bounds, {0, 0, 20}, 450) * 2));
    CHECK(modelProjectedPixels(bounds, {0, 0, 40}, 900) < modelProjectedPixels(bounds, {0, 0, 20}, 900));
    CHECK(modelLodLevel(440, 0, 4) == 1);
    CHECK(modelLodLevel(490, 1, 4) == 1);
    CHECK(modelLodLevel(551, 1, 4) == 0);
    CHECK(modelLodLevel(50, 0, 4) == 3);
    CHECK(modelLodLevel(500, 3, 4) == 1);
    CHECK(modelLodLevel(250, 0, 4, true) == 0);
    CHECK(modelLodLevel(250, 0, 4) == 1);
    CHECK(modelLodLevel(1, 99, 1) == 0);
    CHECK(modelLodLevel(1, 99, 0) == 0);
    CHECK(modelProjectedPixels({}, {0, 0, 0}, 900) == 0);
    CHECK(std::isfinite(modelProjectedPixels(bounds, {0, 0, 0}, 900)));
}

TEST_CASE("model shadow quality bounds resolution coverage and projection snapping")
{
    CHECK(modelShadowSettings(0).size == 0);
    CHECK(modelShadowSettings(1).size == 512);
    CHECK(modelShadowSettings(2).size == 1024);
    CHECK(modelShadowSettings(3).size == 2048);
    CHECK(modelShadowSettings(-1).size == 0);
    CHECK(modelShadowSettings(4).size == 0);
    for (int quality = 1; quality <= 3; ++quality)
    {
        const ModelShadowSettings settings = modelShadowSettings(quality);
        const float radius = modelSunShadowSplits(settings.distance)[1];
        const ModelSunShadowCascade first = modelSunShadowCascade({0, 0, 0}, {0, 0, 1},
            radius, true, true, settings.size);
        const ModelSunShadowCascade shifted = modelSunShadowCascade({0.1f, 0, 0}, {0, 0, 1},
            radius, true, true, settings.size);
        CHECK(first.textureMatrix == shifted.textureMatrix);
        CHECK(modelSunShadowIntersects(first, {{-100, -100, 0}, {100, 100, 500}, true}));
    }
}

TEST_CASE("model sunlight cascade splits mix logarithmic and uniform steps up to the shadow distance")
{
    const std::array<float, ModelSunShadowCascades + 1> splits = modelSunShadowSplits(3072.0f);
    CHECK(splits[0] == 0.0f);
    CHECK(splits.back() == doctest::Approx(3072.0f));
    for (size_t index = 1; index < splits.size(); ++index)
    {
        CHECK(splits[index] > splits[index - 1]);
        const float fraction = float(index) / float(ModelSunShadowCascades);
        const float uniform = ModelSunShadowSplitStart + (3072.0f - ModelSunShadowSplitStart) * fraction;
        const float logarithmic = ModelSunShadowSplitStart * std::pow(3072.0f / ModelSunShadowSplitStart, fraction);
        CHECK(splits[index] <= doctest::Approx(uniform));
        CHECK(splits[index] >= doctest::Approx(logarithmic));
    }
    // The near cascade covers far less than an even third.
    CHECK(splits[1] < 3072.0f / 3.0f * 0.5f);
}

TEST_CASE("model sunlight slice sphere holds the frustum slice and keeps its radius while the camera turns")
{
    ModelSunShadowCamera camera;
    camera.position = {100.0f, -50.0f, 300.0f};
    camera.tanHalfHeight = std::tan(0.5f * 1.1f);
    camera.tanHalfWidth = camera.tanHalfHeight * 16.0f / 9.0f;
    const ModelSunShadowSphere ahead = modelSunShadowSliceSphere(camera, 200.0f, 900.0f);
    for (float depth : {200.0f, 900.0f})
    {
        for (float x : {-1.0f, 1.0f})
        {
            for (float y : {-1.0f, 1.0f})
            {
                // Forward is +X here; the corners spread over Y and Z.
                const std::array<float, 3> corner = {camera.position[0] + depth,
                    camera.position[1] + x * depth * camera.tanHalfWidth,
                    camera.position[2] + y * depth * camera.tanHalfHeight};
                float distanceSquared = 0.0f;
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    distanceSquared += (corner[axis] - ahead.center[axis]) * (corner[axis] - ahead.center[axis]);
                }
                CHECK(std::sqrt(distanceSquared) <= ahead.radius * 1.0001f);
            }
        }
    }
    camera.forward = {0.0f, std::sqrt(0.5f), -std::sqrt(0.5f)};
    const ModelSunShadowSphere turned = modelSunShadowSliceSphere(camera, 200.0f, 900.0f);
    CHECK(turned.radius == doctest::Approx(ahead.radius));
    CHECK(turned.center[0] == doctest::Approx(camera.position[0]));
    CHECK(turned.center[2] < camera.position[2]);
}

TEST_CASE("model sunlight shadow projection matches backend origins and depth conventions")
{
    const std::array<float, 3> camera = {-9728.0f, -11919.0f, 257.0f};
    const std::array<float, 3> direction = {-0.5f, -0.5f, std::sqrt(0.5f)};
    for (bool homogeneousDepth : {false, true})
    {
        for (bool bottomLeft : {false, true})
        {
            const ModelSunShadowCascade cascade = modelSunShadowCascade(
                camera, direction, 1024.0f, homogeneousDepth, bottomLeft);
            const float receiver[4] = {camera[0], camera[1], camera[2], 1.0f};
            const float caster[4] = {camera[0] + direction[0] * 100.0f,
                camera[1] + direction[1] * 100.0f, camera[2] + direction[2] * 100.0f, 1.0f};
            float receiverUv[4];
            float casterUv[4];
            bx::vec4MulMtx(receiverUv, receiver, cascade.textureMatrix.data());
            bx::vec4MulMtx(casterUv, caster, cascade.textureMatrix.data());
            CHECK(receiverUv[0] == doctest::Approx(0.5f).epsilon(0.001f));
            CHECK(receiverUv[1] == doctest::Approx(0.5f).epsilon(0.001f));
            CHECK(receiverUv[2] == doctest::Approx(0.5f).epsilon(0.001f));
            CHECK(casterUv[0] == doctest::Approx(receiverUv[0]).epsilon(0.001f));
            CHECK(casterUv[1] == doctest::Approx(receiverUv[1]).epsilon(0.001f));
            CHECK(casterUv[2] < receiverUv[2]);
            const bx::Vec3 right = bx::normalize(bx::cross({0, 0, 1},
                {direction[0], direction[1], direction[2]}));
            const bx::Vec3 up = bx::cross({direction[0], direction[1], direction[2]}, right);
            const float above[4] = {camera[0] + up.x * 100.0f, camera[1] + up.y * 100.0f,
                camera[2] + up.z * 100.0f, 1.0f};
            float aboveUv[4];
            bx::vec4MulMtx(aboveUv, above, cascade.textureMatrix.data());
            CHECK((aboveUv[1] > receiverUv[1]) == bottomLeft);
        }
    }
}

TEST_CASE("model sunlight cascades retain crossing casters and reject distant and invalid bounds")
{
    for (const std::array<float, 3> light : {std::array<float, 3>{0, 0, 1},
        std::array<float, 3>{-0.5f, -0.5f, std::sqrt(0.5f)}})
    {
        const ModelSunShadowCascade cascade = modelSunShadowCascade({0, 0, 0}, light, 1024.0f, true, true);
        CHECK(modelSunShadowIntersects(cascade, {{-50, -50, -50}, {50, 50, 180}, true}));
        CHECK(modelSunShadowIntersects(cascade, {{-2048, -2048, -100}, {2048, 2048, 180}, true}));
        CHECK_FALSE(modelSunShadowIntersects(cascade, {{10000, 10000, 0}, {10100, 10100, 180}, true}));
        CHECK_FALSE(modelSunShadowIntersects(cascade, {{-10100, -10100, 0}, {-10000, -10000, 180}, true}));
        CHECK_FALSE(modelSunShadowIntersects(cascade, {}));
        for (float component : cascade.textureMatrix)
        {
            CHECK(std::isfinite(component));
        }
    }
}

TEST_CASE("model sunlight cascade translation stays stable within a shadow texel")
{
    const std::array<float, 3> light = {-0.5f, -0.5f, std::sqrt(0.5f)};
    const ModelSunShadowCascade first = modelSunShadowCascade({0, 0, 0}, light, 1024.0f, true, true);
    const bx::Vec3 right = bx::normalize(bx::cross({0, 0, 1}, {light[0], light[1], light[2]}));
    const ModelSunShadowCascade shifted = modelSunShadowCascade(
        {right.x * 0.1f, right.y * 0.1f, 0}, light, 1024.0f, true, true);
    for (size_t index = 0; index < first.textureMatrix.size(); ++index)
    {
        CHECK(first.textureMatrix[index] == doctest::Approx(shifted.textureMatrix[index]).epsilon(0.00001f));
    }
}

TEST_CASE("model sunlight casters held whole by a finer cascade skip the coarser ones")
{
    const std::array<float, 3> light = {-0.5f, -0.5f, std::sqrt(0.5f)};
    const std::array<ModelSunShadowCascade, 2> cascades = {
        modelSunShadowCascade({0, 0, 0}, light, 512.0f, true, true, 1024),
        modelSunShadowCascade({0, 0, 0}, light, 2048.0f, true, true, 1024)};
    const ModelBounds centre = {{-50, -50, 0}, {50, 50, 200}, true};
    CHECK(modelSunShadowCasts(cascades, 0, centre));
    CHECK_FALSE(modelSunShadowCasts(cascades, 1, centre));
    // Reaching into the finer cascade's blend band: its shadow may be read from the coarser cascade.
    const ModelBounds edge = {{380, -50, 0}, {520, 50, 200}, true};
    CHECK(modelSunShadowCasts(cascades, 0, edge));
    CHECK(modelSunShadowCasts(cascades, 1, edge));
    const ModelBounds far = {{1500, 1500, 0}, {1600, 1600, 200}, true};
    CHECK_FALSE(modelSunShadowCasts(cascades, 0, far));
    CHECK(modelSunShadowCasts(cascades, 1, far));
}
