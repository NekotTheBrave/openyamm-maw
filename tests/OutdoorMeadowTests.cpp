#include "game/outdoor/OutdoorMeadow.h"
#include "game/outdoor/OutdoorGeometryUtils.h"
#include "game/outdoor/TerrainDecorationData.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace OpenYAMM::Game;

namespace
{
OutdoorMapData meadowMap()
{
    OutdoorMapData map;
    map.heightMap.resize(128 * 128, 20);
    map.tileMap.resize(128 * 128, 0);
    map.attributeMap.resize(128 * 128, 0);
    return map;
}

float atCell(const OutdoorMeadowField &field, float cellX, float cellY)
{
    return field.sample(outdoorGridCornerWorldX(0) + cellX * 512.0f, outdoorGridCornerWorldY(0) - cellY * 512.0f);
}

OutdoorMapData grassMap(int first, int last)
{
    OutdoorMapData map;
    map.heightMap.resize(128 * 128, 0);
    map.tileMap.resize(128 * 128, 0);
    for (int y = first; y <= last; ++y)
    {
        for (int x = first; x <= last; ++x)
        {
            map.tileMap[size_t(y) * 128 + x] = 1;
        }
    }
    return map;
}

std::array<std::string, 256> grassTextures()
{
    std::array<std::string, 256> result;
    result[1] = "test_grass";
    return result;
}

TerrainDecorationConfig grassConfig()
{
    TerrainDecorationConfig config;
    TerrainDecorationRule rule;
    rule.texture = "test_grass";
    rule.candidates = 60;
    TerrainDecorationVariant shortGrass;
    shortGrass.layer = 0;
    rule.variants.push_back(shortGrass);
    config.rules.push_back(rule);
    return config;
}

// Share of the instances within radius of each instance of the layer that have the same layer, on average.
double sameLayerNeighbours(const TerrainDecorationPlacement &placement, float layer, float radius)
{
    double total = 0.0;
    size_t counted = 0;
    for (const TerrainDecorationInstance &instance : placement.instances)
    {
        if (instance.groundNormal[3] != layer)
        {
            continue;
        }
        size_t near = 0;
        size_t same = 0;
        for (const TerrainDecorationInstance &other : placement.instances)
        {
            const float dx = other.positionYaw[0] - instance.positionYaw[0];
            const float dy = other.positionYaw[1] - instance.positionYaw[1];
            if (&other != &instance && dx * dx + dy * dy < radius * radius)
            {
                ++near;
                same += other.groundNormal[3] == layer;
            }
        }
        if (near > 0)
        {
            total += double(same) / double(near);
            ++counted;
        }
    }
    return counted > 0 ? total / double(counted) : 0.0;
}
}

TEST_CASE("Meadow field is dry on crests, lush along water and neutral on open flat ground")
{
    OutdoorMapData map = meadowMap();
    CHECK(buildOutdoorMeadowField(map).values.size() == 128 * 128);
    CHECK(atCell(buildOutdoorMeadowField(map), 64.0f, 64.0f) == doctest::Approx(0.0f).epsilon(0.001));
    // A round hill at (40, 40) and a lake at (90, 90).
    for (int y = 0; y < 128; ++y)
    {
        for (int x = 0; x < 128; ++x)
        {
            const float distance = std::hypot(float(x - 40), float(y - 40));
            map.heightMap[size_t(y) * 128 + x] = uint8_t(20 + std::max(0.0f, 60.0f - distance * 12.0f));
        }
    }
    for (int y = 86; y < 94; ++y)
    {
        for (int x = 86; x < 94; ++x)
        {
            map.attributeMap[size_t(y) * 128 + x] = 0x02; // Water.
        }
    }
    const OutdoorMeadowField field = buildOutdoorMeadowField(map);
    CHECK(atCell(field, 40.0f, 40.0f) > 0.5f);
    CHECK(atCell(field, 85.0f, 90.0f) < -0.5f);
    CHECK(std::abs(atCell(field, 64.0f, 64.0f)) < 0.1f);
    CHECK(std::abs(atCell(field, 110.0f, 20.0f)) < 0.1f);
    for (float value : field.values)
    {
        REQUIRE(value >= -1.0f);
        REQUIRE(value <= 1.0f);
    }
    CHECK(OutdoorMeadowField{}.sample(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("Meadow tint moves towards the dry or lush colour by moisture and strength")
{
    const std::array<float, 3> dry = {1.2f, 1.0f, 0.6f};
    const std::array<float, 3> lush = {0.8f, 1.0f, 0.8f};
    CHECK(outdoorMeadowTint(1.0f, dry, lush)[2] == doctest::Approx(0.6f));
    CHECK(outdoorMeadowTint(-0.5f, dry, lush)[0] == doctest::Approx(0.9f));
    CHECK(outdoorMeadowTint(-0.5f, dry, lush, 0.5f)[0] == doctest::Approx(0.95f));
    CHECK(outdoorMeadowTint(0.0f, dry, lush)[0] == doctest::Approx(1.0f));
}

TEST_CASE("Tall grass variants grow in whole patches")
{
    const OutdoorMapData map = grassMap(30, 98);
    TerrainDecorationConfig config = grassConfig();
    config.densityVariation = 0.8f;
    config.tallGrassChance = 0.1f;
    TerrainDecorationVariant tall;
    tall.layer = 5;
    tall.heightScale = 2.0f;
    config.rules[0].tallVariants.push_back(tall);
    const TerrainDecorationPlacement placement = scatterTerrainDecorations(map, grassTextures(), config);
    size_t tallCount = 0;
    for (const TerrainDecorationInstance &instance : placement.instances)
    {
        tallCount += instance.groundNormal[3] == 5.0f;
    }
    const double share = double(tallCount) / double(placement.instances.size());
    CHECK(share > 0.02);
    CHECK(share < 0.2);
    // Around a tall instance most grass is tall too.
    CHECK(sameLayerNeighbours(placement, 5.0f, 200.0f) > 0.4);
}

TEST_CASE("Wildflowers grow in single-species clusters only in families with flowers")
{
    const OutdoorMapData map = grassMap(30, 98);
    TerrainDecorationConfig config = grassConfig();
    config.flowers.coverage = 0.3f;
    config.flowers.chance = 0.6f;
    config.flowers.clusterSize = 900.0f;
    for (int layer : {6, 7})
    {
        TerrainDecorationVariant flower;
        flower.layer = layer;
        config.flowers.variants.push_back(flower);
    }
    CHECK(scatterTerrainDecorations(map, grassTextures(), config).instances.size() > 0);
    for (const TerrainDecorationInstance &instance : scatterTerrainDecorations(map, grassTextures(), config).instances)
    {
        REQUIRE(instance.groundNormal[3] == 0.0f);
    }
    config.rules[0].flowers = true;
    const TerrainDecorationPlacement placement = scatterTerrainDecorations(map, grassTextures(), config);
    size_t yellow = 0;
    size_t white = 0;
    for (const TerrainDecorationInstance &instance : placement.instances)
    {
        yellow += instance.groundNormal[3] == 6.0f;
        white += instance.groundNormal[3] == 7.0f;
    }
    CHECK(yellow + white > placement.instances.size() / 50);
    CHECK(yellow + white < placement.instances.size() / 3);
    // Flowers near a flower are mostly of its species.
    const auto speciesShare = [&](float layer)
    {
        size_t same = 0;
        size_t other = 0;
        for (const TerrainDecorationInstance &instance : placement.instances)
        {
            if (instance.groundNormal[3] != layer)
            {
                continue;
            }
            for (const TerrainDecorationInstance &neighbour : placement.instances)
            {
                const float dx = neighbour.positionYaw[0] - instance.positionYaw[0];
                const float dy = neighbour.positionYaw[1] - instance.positionYaw[1];
                if (dx * dx + dy * dy < 300.0f * 300.0f && neighbour.groundNormal[3] >= 6.0f)
                {
                    same += neighbour.groundNormal[3] == layer;
                    other += neighbour.groundNormal[3] != layer;
                }
            }
        }
        return double(same) / double(std::max<size_t>(same + other, 1));
    };
    CHECK(speciesShare(yellow > white ? 6.0f : 7.0f) > 0.8);
}

TEST_CASE("Verges grow tall grass along the grass edge and undergrowth rings tree decorations")
{
    OutdoorMapData map = grassMap(30, 98);
    // A road through the middle of the grass.
    for (int y = 30; y <= 98; ++y)
    {
        map.tileMap[size_t(y) * 128 + 64] = 0;
    }
    TerrainDecorationConfig config = grassConfig();
    TerrainDecorationVariant tall;
    tall.layer = 5;
    config.rules[0].tallVariants.push_back(tall);
    config.verges.distance = 100.0f;
    config.verges.tallChance = 0.6f;
    const TerrainDecorationPlacement verges = scatterTerrainDecorations(map, grassTextures(), config);
    size_t nearTall = 0;
    size_t nearAll = 0;
    size_t farTall = 0;
    for (const TerrainDecorationInstance &instance : verges.instances)
    {
        const float roadDistance = std::min(std::abs(instance.positionYaw[0] - outdoorGridCornerWorldX(64)),
            std::abs(instance.positionYaw[0] - outdoorGridCornerWorldX(65)));
        const bool isTall = instance.groundNormal[3] == 5.0f;
        if (roadDistance < 80.0f)
        {
            ++nearAll;
            nearTall += isTall;
        }
        else if (roadDistance > 400.0f && instance.positionYaw[1] < outdoorGridCornerWorldY(32) &&
            instance.positionYaw[1] > outdoorGridCornerWorldY(96) &&
            instance.positionYaw[0] > outdoorGridCornerWorldX(32) && instance.positionYaw[0] < outdoorGridCornerWorldX(96))
        {
            farTall += isTall;
        }
    }
    CHECK(double(nearTall) / double(nearAll) > 0.3);
    CHECK(farTall == 0);

    OutdoorMapData forest = grassMap(30, 98);
    OutdoorEntity tree;
    tree.name = "6Tree01";
    tree.x = int(outdoorGridCornerWorldX(60));
    tree.y = int(outdoorGridCornerWorldY(60));
    forest.entities.push_back(tree);
    OutdoorEntity rock = tree;
    rock.name = "6rock01";
    rock.x = int(outdoorGridCornerWorldX(70));
    forest.entities.push_back(rock);
    TerrainDecorationConfig undergrowth = grassConfig();
    undergrowth.undergrowth.decorations = {"tree"};
    undergrowth.undergrowth.candidates = 12;
    TerrainDecorationVariant fern;
    fern.layer = 11;
    undergrowth.undergrowth.variants.push_back(fern);
    size_t ferns = 0;
    for (const TerrainDecorationInstance &instance :
        scatterTerrainDecorations(forest, grassTextures(), undergrowth).instances)
    {
        if (instance.groundNormal[3] == 11.0f)
        {
            ++ferns;
            const float distance = std::hypot(instance.positionYaw[0] - float(tree.x),
                instance.positionYaw[1] - float(tree.y));
            CHECK(distance >= undergrowth.undergrowth.innerRadius - 0.01f);
            CHECK(distance <= undergrowth.undergrowth.outerRadius + 0.01f);
        }
    }
    CHECK(ferns > 6);
    CHECK(ferns <= 12);
}

TEST_CASE("Bushes group at tree lines and in lush hollows, on grass and apart")
{
    OutdoorMapData map = grassMap(30, 98);
    // A road through the middle of the grass and a lake in the corner.
    for (int y = 30; y <= 98; ++y)
    {
        map.tileMap[size_t(y) * 128 + 64] = 0;
    }
    map.attributeMap.resize(128 * 128, 0);
    for (int y = 86; y < 94; ++y)
    {
        for (int x = 86; x < 94; ++x)
        {
            map.attributeMap[size_t(y) * 128 + x] = 0x02;
        }
    }
    for (int tree = 0; tree < 20; ++tree)
    {
        OutdoorEntity entity;
        entity.name = "6Tree01";
        entity.x = int(outdoorGridCornerWorldX(36 + tree));
        entity.y = int(outdoorGridCornerWorldY(40));
        map.entities.push_back(entity);
    }
    TerrainDecorationConfig config = grassConfig();
    config.undergrowth.decorations = {"tree"};
    config.bushes.models = {{"a.glb", 120.0f, 1.0f}, {"b.glb", 80.0f, 1.0f}};
    config.bushes.edgeChance = 1.0f;
    config.bushes.meadowChance = 0.0f;
    config.bushes.lushChance = 0.0f;
    config.bushes.vergeChance = 0.0f;
    const TerrainDecorationPlacement placement = scatterTerrainDecorations(map, grassTextures(), config);
    REQUIRE(placement.bushes.size() >= 15);
    std::array<size_t, 2> perModel = {};
    for (const TerrainBushInstance &bush : placement.bushes)
    {
        ++perModel[bush.model];
        // Near the tree line, at least edge_radius minus the group spread from it.
        const float treeLine = outdoorGridCornerWorldY(40);
        CHECK(std::abs(bush.position[1] - treeLine) < config.bushes.edgeRadius[1] + config.bushes.groupSpread * 1.3f);
        // Off the road, with grass all around.
        const float road = std::min(std::abs(bush.position[0] - outdoorGridCornerWorldX(64)),
            std::abs(bush.position[0] - outdoorGridCornerWorldX(65)));
        CHECK(road > config.bushes.radius * 0.9f);
        CHECK(bush.heightScale >= 1.0f - config.bushes.heightJitter);
        CHECK(bush.heightScale <= 1.0f + config.bushes.heightJitter);
        for (const TerrainBushInstance &other : placement.bushes)
        {
            if (&other != &bush)
            {
                CHECK(std::hypot(other.position[0] - bush.position[0], other.position[1] - bush.position[1])
                    >= config.bushes.radius * 1.6f - 0.01f);
            }
        }
    }
    CHECK(perModel[0] > 0);
    CHECK(perModel[1] > 0);

    // Without trees, lush ground along the lake grows bushes and dry open ground none; max_count caps them.
    map.entities.clear();
    config.bushes.lushChance = 1.0f;
    config.bushes.spacing = 400.0f;
    const TerrainDecorationPlacement lush = scatterTerrainDecorations(map, grassTextures(), config);
    REQUIRE(!lush.bushes.empty());
    for (const TerrainBushInstance &bush : lush.bushes)
    {
        CHECK(std::hypot(bush.position[0] - outdoorGridCornerWorldX(90), bush.position[1] - outdoorGridCornerWorldY(90))
            < 6000.0f);
    }
    config.bushes.maxCount = 3;
    CHECK(scatterTerrainDecorations(map, grassTextures(), config).bushes.size() == 3);
}

TEST_CASE("Clump layers are placed in their own rank tree")
{
    const OutdoorMapData map = grassMap(30, 98);
    TerrainDecorationConfig config = grassConfig();
    TerrainDecorationVariant other;
    other.layer = 3;
    config.rules[0].variants.push_back(other);
    config.clump.layerMask = 1u << 0;
    const TerrainDecorationPlacement placement = scatterTerrainDecorations(map, grassTextures(), config);
    size_t clumps = 0;
    size_t cards = 0;
    for (const TerrainDecorationNode &node : placement.nodes)
    {
        for (uint32_t index = node.range.first; index < node.range.first + node.range.count; ++index)
        {
            const bool clumpLayer = placement.instances[index].groundNormal[3] == 0.0f;
            REQUIRE(node.range.clump == clumpLayer);
            clumps += clumpLayer;
            cards += !clumpLayer;
        }
    }
    CHECK(clumps > 0);
    CHECK(cards > 0);
    // A clump covers its cards' area in the foliage map.
    CHECK(terrainDecorationClumpArea(config.clump) == doctest::Approx(5.0f * 0.75f * 0.75f * 0.63662f));
}
