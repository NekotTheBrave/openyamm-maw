#include "doctest/doctest.h"

#include "game/outdoor/OutdoorLightingRuntime.h"
#include "game/render/lighting/RenderLight.h"

#include <array>
#include <cstdint>
#include <vector>

using OpenYAMM::Game::OutdoorLightSelectionBounds;
using OpenYAMM::Game::OutdoorLightingRuntime;
using OpenYAMM::Game::OutdoorSelectedFxLights;
using OpenYAMM::Game::RenderLightKind;
using OpenYAMM::Game::WorldFxLightEmitter;

namespace
{
uint32_t makeAbgr(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
{
    return (static_cast<uint32_t>(alpha) << 24)
        | (static_cast<uint32_t>(blue) << 16)
        | (static_cast<uint32_t>(green) << 8)
        | static_cast<uint32_t>(red);
}

WorldFxLightEmitter makeLight(float x, float y, float z, float radius, uint32_t colorAbgr)
{
    WorldFxLightEmitter light = {};
    light.x = x;
    light.y = y;
    light.z = z;
    light.radius = radius;
    light.colorAbgr = colorAbgr;
    light.kind = RenderLightKind::Projectile;
    return light;
}
}

TEST_CASE("outdoor lighting runtime selects local lights for draw bounds")
{
    std::vector<WorldFxLightEmitter> lights;
    lights.push_back(makeLight(32.0f, 0.0f, 0.0f, 256.0f, makeAbgr(255, 32, 16)));
    lights.push_back(makeLight(5000.0f, 0.0f, 0.0f, 256.0f, makeAbgr(16, 32, 255)));

    OutdoorLightingRuntime runtime;
    runtime.build(lights);

    OutdoorLightSelectionBounds bounds = {};
    bounds.min = {-64.0f, -64.0f, -64.0f};
    bounds.max = {64.0f, 64.0f, 64.0f};
    bounds.valid = true;

    const OutdoorSelectedFxLights selected = runtime.selectForBounds({0.0f, 0.0f, 0.0f}, bounds);

    REQUIRE_EQ(selected.lightCount, 1u);
    CHECK(selected.colors[0] > 0.95f);
    CHECK(selected.colors[2] < 0.10f);
}

TEST_CASE("outdoor lighting runtime point sampling falls off with distance")
{
    std::vector<WorldFxLightEmitter> lights;
    lights.push_back(makeLight(0.0f, 0.0f, 0.0f, 256.0f, makeAbgr(255, 255, 255)));

    OutdoorLightingRuntime runtime;
    runtime.build(lights);

    const std::array<float, 3> nearSample = runtime.sampleLightingRgb({0.0f, 0.0f, 0.0f});
    const std::array<float, 3> farSample = runtime.sampleLightingRgb({1000.0f, 0.0f, 0.0f});

    CHECK(nearSample[0] > 0.5f);
    CHECK(farSample[0] == doctest::Approx(0.0f));
}

TEST_CASE("outdoor lighting broad material bounds preserve radius filtering and the strongest eight lights")
{
    std::vector<WorldFxLightEmitter> lights;
    for (uint32_t index = 0; index < 12; ++index)
    {
        WorldFxLightEmitter light = makeLight(-22000.0f + index * 4000.0f, 0.0f, 64.0f,
            128.0f + index * 16.0f, makeAbgr(255, 128, 64));
        light.kind = RenderLightKind::Static;
        lights.push_back(light);
    }
    lights.push_back(makeLight(100000.0f, 0.0f, 64.0f, 8000.0f, makeAbgr(0, 0, 255)));
    // A light's center outside the bounds can still illuminate their edge.
    lights.push_back(makeLight(0.0f, 30100.0f, 64.0f, 320.0f, makeAbgr(0, 255, 0)));

    OutdoorLightingRuntime runtime;
    runtime.build(lights);
    const OutdoorLightSelectionBounds bounds = {{-30000.0f, -30000.0f, 0.0f},
        {30000.0f, 30000.0f, 128.0f}, true};
    const OutdoorSelectedFxLights selected = runtime.selectForBounds({0.0f, 0.0f, 0.0f}, bounds);

    REQUIRE(selected.lightCount == OutdoorSelectedFxLights::MaxLights);
    CHECK(selected.rankedCandidateCount == 13);
    CHECK(selected.filteredEmitterCount == 1);
    for (size_t index = 0; index + 1 < selected.lightCount; ++index)
    {
        CHECK(selected.positions[index * 4] == doctest::Approx(-22000.0f + (11 - index) * 4000.0f));
    }
    CHECK(selected.positions[28] == doctest::Approx(0.0f));
    CHECK(selected.positions[29] == doctest::Approx(30100.0f));

    // This tiny query continues using the spatial grid and rejects the same distant lights.
    const OutdoorLightSelectionBounds local = {{1990.0f, 0.0f, 0.0f}, {2010.0f, 10.0f, 128.0f}, true};
    const OutdoorSelectedFxLights nearby = runtime.selectForBounds({0.0f, 0.0f, 0.0f}, local);
    REQUIRE(nearby.lightCount == 1);
    CHECK(nearby.positions[0] == doctest::Approx(2000.0f));
}

TEST_CASE("outdoor lighting runtime clusters dense sectorless projectile lights")
{
    std::vector<WorldFxLightEmitter> lights;

    // More than the high threshold (24): below it every light stays its own.
    for (uint32_t index = 0; index < 30; ++index)
    {
        lights.push_back(makeLight(static_cast<float>(index * 10), 32.0f, 64.0f, 128.0f, makeAbgr(255, 64, 32)));
    }

    OutdoorLightingRuntime runtime;
    runtime.build(lights);

    OutdoorLightSelectionBounds bounds = {};
    bounds.min = {-64.0f, -64.0f, 0.0f};
    bounds.max = {320.0f, 128.0f, 128.0f};
    bounds.valid = true;

    const OutdoorSelectedFxLights selected = runtime.selectForBounds({0.0f, 0.0f, 0.0f}, bounds);

    CHECK_EQ(runtime.sourceLightCount(), 30u);
    CHECK_EQ(runtime.outputClusterLightCount(), 1u);
    CHECK_EQ(selected.lightCount, 1u);
}

TEST_CASE("outdoor lighting runtime keeps spread sectorless projectile clusters represented")
{
    std::vector<WorldFxLightEmitter> lights;

    for (uint32_t index = 0; index < 80; ++index)
    {
        const float x = static_cast<float>((index % 16) * 320);
        const float y = static_cast<float>((index / 16) * 320);
        lights.push_back(makeLight(x, y, 128.0f, 128.0f, makeAbgr(255, 64, 32)));
    }

    OutdoorLightingRuntime runtime;
    runtime.build(lights);

    CHECK_EQ(runtime.sourceLightCount(), 80u);
    CHECK(runtime.outputClusterLightCount() <= OpenYAMM::Game::FxLightClusterThresholdHigh);

    const std::array<float, 3> leftSample = runtime.sampleLightingRgb({0.0f, 0.0f, 128.0f});
    const std::array<float, 3> rightSample = runtime.sampleLightingRgb({4800.0f, 1280.0f, 128.0f});
    const std::array<float, 3> farSample = runtime.sampleLightingRgb({9000.0f, 4500.0f, 128.0f});
    CHECK(leftSample[0] > 0.0f);
    CHECK(rightSample[0] > 0.0f);
    CHECK(farSample[0] == doctest::Approx(0.0f));
}
