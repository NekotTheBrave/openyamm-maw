#include "game/indoor/IndoorLightReach.h"
#include "game/indoor/IndoorLightingRuntime.h"

#include <doctest/doctest.h>

using namespace OpenYAMM::Game;

namespace
{
// Four rooms along x, each 1000 units long: 0 | 1 | 2 joined by portals at x = 1000 and x = 2000; room 3 lies beside
// room 0 (y > 1000) and shares only a wall with it.
IndoorMapData testMap()
{
    IndoorMapData map;
    map.sectors.resize(4);
    const auto addPortal = [&map](int x, uint16_t front, uint16_t back)
    {
        const uint16_t first = uint16_t(map.vertices.size());
        map.vertices.push_back({x, 0, 0});
        map.vertices.push_back({x, 1000, 0});
        map.vertices.push_back({x, 1000, 500});
        map.vertices.push_back({x, 0, 500});
        IndoorFace face;
        face.isPortal = true;
        face.roomNumber = front;
        face.roomBehindNumber = back;
        face.vertexIndices = {first, uint16_t(first + 1), uint16_t(first + 2), uint16_t(first + 3)};
        const uint16_t faceId = uint16_t(map.faces.size());
        map.faces.push_back(face);
        map.sectors[front].portalFaceIds.push_back(faceId);
        map.sectors[back].portalFaceIds.push_back(faceId);
    };
    addPortal(1000, 0, 1);
    addPortal(2000, 1, 2);
    return map;
}
}

TEST_CASE("indoor light reach follows portals within the light radius")
{
    const IndoorMapData map = testMap();
    IndoorLightReach reach;
    reach.build(map, buildIndoorPortalGraph(map));
    REQUIRE_EQ(reach.sectorCount(), size_t(4));
    std::vector<int16_t> sectors;
    const int16_t room0 = 0;

    // Near the first portal: room 1 too, but the second portal is beyond the radius.
    reach.reach(std::span<const int16_t>(&room0, 1), {900.0f, 500.0f, 250.0f}, 400.0f, sectors);
    CHECK_EQ(sectors, std::vector<int16_t>{0, 1});
    // A large light reaches room 2 through room 1.
    reach.reach(std::span<const int16_t>(&room0, 1), {900.0f, 500.0f, 250.0f}, 1200.0f, sectors);
    CHECK_EQ(sectors, std::vector<int16_t>{0, 1, 2});
    // Far from every portal: its own room only, and never the room behind the wall.
    reach.reach(std::span<const int16_t>(&room0, 1), {100.0f, 900.0f, 250.0f}, 300.0f, sectors);
    CHECK_EQ(sectors, std::vector<int16_t>{0});
    CHECK_FALSE(IndoorLightReach::contains(sectors, 3));
    CHECK(IndoorLightReach::contains(sectors, 0));
}

TEST_CASE("indoor light reach without a sector is unrestricted")
{
    const IndoorMapData map = testMap();
    IndoorLightReach reach;
    reach.build(map, buildIndoorPortalGraph(map));
    std::vector<int16_t> sectors = {7};
    const int16_t noSector = -1;
    reach.reach(std::span<const int16_t>(&noSector, 1), {0.0f, 0.0f, 0.0f}, 5000.0f, sectors);
    CHECK(sectors.empty());
    // A light listed in two rooms starts from both.
    const std::vector<int16_t> rooms = {0, 2};
    reach.reach(rooms, {100.0f, 500.0f, 250.0f}, 100.0f, sectors);
    CHECK_EQ(sectors, std::vector<int16_t>{0, 2});
}

TEST_CASE("indoor light reach list selects the receivers a baked light may light")
{
    IndoorLightReachList list;
    list.assign({2, 5});
    CHECK(list.reachesReceiver(2, -1));
    CHECK(list.reachesReceiver(7, 5));   // through the back sector
    CHECK_FALSE(list.reachesReceiver(3, -1));
    CHECK(list.reachesReceiver(-1, -1)); // a face without sectors takes every light
    // More sectors than a list holds: unrestricted.
    std::vector<int16_t> many;
    for (int16_t sector = 0; sector < 40; ++sector)
    {
        many.push_back(sector);
    }
    list.assign(many);
    CHECK_EQ(list.count, 0);
    CHECK(list.reachesReceiver(99, -1));
}
