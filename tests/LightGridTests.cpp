#include "engine/render/LightGrid.h"

#include <bx/math.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <set>

using namespace OpenYAMM::Engine;

namespace
{
struct TestView
{
    float view[16] = {};
    float projection[16] = {};
    std::array<float, 3> camera = {0.0f, 0.0f, 0.0f};
};

// The indoor renderer's camera: right-handed look-at along +y, 60 degree projection.
TestView testView()
{
    TestView result;
    bx::mtxLookAt(result.view, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, bx::Handedness::Right);
    bx::mtxProj(result.projection, 60.0f, 16.0f / 9.0f, 1.0f, 50000.0f, false, bx::Handedness::Right);
    return result;
}

LightGridLight light(float x, float y, float z, float radius, float intensity = 1.0f)
{
    LightGridLight result;
    result.position = {x, y, z};
    result.radius = radius;
    result.intensity = intensity;
    return result;
}

std::vector<uint32_t> cellLights(const LightGridData &data, int32_t cell)
{
    const uint32_t offset = uint32_t(data.cellTexels[size_t(cell) * 4]);
    const uint32_t count = uint32_t(data.cellTexels[size_t(cell) * 4 + 1]);
    std::vector<uint32_t> result;
    for (uint32_t entry = 0; entry < count; ++entry)
    {
        result.push_back(uint32_t(data.indexTexels[offset + entry]));
    }
    return result;
}
}

TEST_CASE("light grid without lights has empty cells")
{
    const TestView view = testView();
    const LightGridData data = buildLightGrid(LightGridConfig::high(4096.0f), {}, view.view, view.projection,
        view.camera);
    CHECK_EQ(data.lightCount, 0u);
    CHECK_EQ(data.stats.cellEntries, 0u);
    CHECK(std::all_of(data.cellTexels.begin(), data.cellTexels.end(), [](float value) { return value == 0.0f; }));
}

TEST_CASE("light grid bins a light into the cells around it and culls lights out of view")
{
    const TestView view = testView();
    const LightGridConfig config = LightGridConfig::high(4096.0f);
    const std::vector<LightGridLight> lights = {light(100.0f, 800.0f, 20.0f, 300.0f),
        light(0.0f, -900.0f, 0.0f, 200.0f), light(0.0f, 9000.0f, 0.0f, 200.0f)};
    const LightGridData data = buildLightGrid(config, lights, view.view, view.projection, view.camera);
    CHECK_EQ(data.lightCount, 1u);   // the second is behind the camera, the third beyond the grid's far end
    CHECK_EQ(data.stats.culledLights, 2u);
    // The light's own position and points inside its sphere find it in their cells.
    for (const std::array<float, 3> &point : {std::array<float, 3>{100.0f, 800.0f, 20.0f},
             std::array<float, 3>{100.0f, 1050.0f, 20.0f}, std::array<float, 3>{-150.0f, 750.0f, 0.0f},
             std::array<float, 3>{100.0f, 600.0f, 200.0f}})
    {
        const int32_t cell = lightGridCellForPoint(config, view.view, view.projection, point);
        REQUIRE(cell >= 0);
        const std::vector<uint32_t> found = cellLights(data, cell);
        CHECK_EQ(found.size(), 1u);
    }
    // A point far outside the sphere does not.
    const int32_t far = lightGridCellForPoint(config, view.view, view.projection, {100.0f, 3500.0f, 20.0f});
    REQUIRE(far >= 0);
    CHECK(cellLights(data, far).empty());
    // The light texels carry position, radius, colour, intensity, sector and the dynamic flag.
    CHECK_EQ(data.lightTexels[0], doctest::Approx(100.0f));
    CHECK_EQ(data.lightTexels[3], doctest::Approx(300.0f));
    CHECK_EQ(data.lightTexels[7], doctest::Approx(1.0f));
    CHECK_EQ(data.lightTexels[8], doctest::Approx(-1.0f));
    CHECK_EQ(data.lightTexels[9], doctest::Approx(0.0f));
}

TEST_CASE("light grid keeps the strongest lights of a full cell")
{
    const TestView view = testView();
    const LightGridConfig config = LightGridConfig::high(4096.0f);
    std::vector<LightGridLight> lights;
    for (int index = 0; index < 40; ++index)
    {
        lights.push_back(light(0.0f, 600.0f, 0.0f, 50.0f, 1.0f + float(index)));
    }
    const LightGridData data = buildLightGrid(config, lights, view.view, view.projection, view.camera);
    const int32_t cell = lightGridCellForPoint(config, view.view, view.projection, {0.0f, 600.0f, 0.0f});
    REQUIRE(cell >= 0);
    const std::vector<uint32_t> found = cellLights(data, cell);
    REQUIRE_EQ(found.size(), size_t(config.cellCap));
    CHECK(data.stats.overflowEntries > 0);
    // Every kept light is stronger than the 8 weakest (intensity 1..8).
    for (const uint32_t slot : found)
    {
        CHECK(data.lightTexels[size_t(slot) * LightGridTexelsPerLight * 4 + 7] > 8.5f);
    }
}

TEST_CASE("light grid keeps dynamic lights ahead of stronger static ones")
{
    const TestView view = testView();
    LightGridConfig config = LightGridConfig::high(4096.0f);
    config.cellCap = 4;
    std::vector<LightGridLight> lights;
    for (int index = 0; index < 8; ++index)
    {
        lights.push_back(light(0.0f, 600.0f, 0.0f, 200.0f, 10.0f));
    }
    LightGridLight torch = light(0.0f, 600.0f, 0.0f, 50.0f, 0.5f);
    torch.dynamic = true;
    torch.sector = 7;
    lights.push_back(torch);
    const LightGridData data = buildLightGrid(config, lights, view.view, view.projection, view.camera);
    const int32_t cell = lightGridCellForPoint(config, view.view, view.projection, {0.0f, 600.0f, 0.0f});
    REQUIRE(cell >= 0);
    const std::vector<uint32_t> found = cellLights(data, cell);
    REQUIRE_EQ(found.size(), size_t(4));
    // Slot 0 is the dynamic light, with its sector.
    CHECK(std::find(found.begin(), found.end(), 0u) != found.end());
    CHECK_EQ(data.lightTexels[8], doctest::Approx(7.0f));
    CHECK_EQ(data.lightTexels[9], doctest::Approx(1.0f));
}

TEST_CASE("light grid fades lights out near its far end")
{
    const TestView view = testView();
    const LightGridConfig config = LightGridConfig::high(4096.0f);
    const std::vector<LightGridLight> lights = {light(0.0f, 1000.0f, 0.0f, 50.0f), light(0.0f, 3900.0f, 0.0f, 50.0f)};
    const LightGridData data = buildLightGrid(config, lights, view.view, view.projection, view.camera);
    REQUIRE_EQ(data.lightCount, 2u);
    // Strongest (nearest) first: slot 0 is the near light at full intensity, slot 1 the far one, faded.
    CHECK_EQ(data.lightTexels[7], doctest::Approx(1.0f));
    CHECK(data.lightTexels[LightGridTexelsPerLight * 4 + 7] < 0.5f);
    CHECK(data.lightTexels[LightGridTexelsPerLight * 4 + 7] > 0.0f);
}

TEST_CASE("low light grid uses the smaller layout")
{
    const LightGridConfig low = LightGridConfig::low(4096.0f);
    CHECK_EQ(low.tilesX, 8);
    CHECK_EQ(low.tilesY, 5);
    CHECK_EQ(low.slices, 16);
    CHECK_EQ(low.cellCap, 16);
    const TestView view = testView();
    const LightGridData data = buildLightGrid(low, {light(0.0f, 500.0f, 0.0f, 100.0f)}, view.view, view.projection,
        view.camera);
    CHECK_EQ(data.cellTexels.size(), size_t(8 * 5 * 16 * 4));
    CHECK_EQ(data.lightCount, 1u);
}

TEST_CASE("light grid keeps the far cells of many large lights near the camera")
{
    // Fireballs bursting on the party: each light near the camera fills the screen across many depth slices, and
    // together they need more entries than a fixed 16384-entry list had (every farther slice then went dark).
    const TestView view = testView();
    std::vector<LightGridLight> lights;
    for (int index = 0; index < 20; ++index)
    {
        lights.push_back(light(float(index % 5) * 40.0f - 80.0f, 120.0f, float(index / 5) * 20.0f, 2048.0f));
    }
    const LightGridConfig config = LightGridConfig::high(4096.0f);
    const LightGridData data = buildLightGrid(config, lights, view.view, view.projection, view.camera);
    CHECK(data.stats.cellEntries > 16384u);
    // A point 1500 units ahead, beyond the slices a short list reached, still has all of them.
    const int32_t farCell = lightGridCellForPoint(config, view.view, view.projection, {0.0f, 1500.0f, 0.0f});
    REQUIRE(farCell >= 0);
    CHECK_EQ(cellLights(data, farCell).size(), 20u);
}
