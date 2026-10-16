#pragma once

#include "engine/AssetFileSystem.h"
#include "game/outdoor/OutdoorMapData.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
struct TerrainDecorationVariant
{
    float weight = 1.0f;
    float widthScale = 1.0f;
    float heightScale = 1.0f;
    int layer = -1; // Version 1 uses the variant's position in the atlas.
};

struct TerrainDecorationRule
{
    std::string texture;
    bool stone = false;
    int candidates = 64;
    float density = 1.0f;
    float width = 40.0f;
    float height = 28.0f;
    std::array<float, 3> tint = {1.0f, 1.0f, 1.0f};
    int maskWidth = 0;
    int maskHeight = 0;
    std::vector<uint8_t> coverage;
    std::string family;
    bool fullFootprint = false;
    float windStrength = 2.5f;
    std::vector<TerrainDecorationVariant> variants;
    // Uncut grass for tall patches and verges (MEADOW.md); without them a tall instance scales a normal variant.
    std::vector<TerrainDecorationVariant> tallVariants;
    // The family grows the library's wildflower clusters.
    bool flowers = false;
};

// Ground moisture (OutdoorMeadowField): dry ground grows yellower, sparser and shorter grass, lush ground the opposite.
struct TerrainDecorationMeadow
{
    std::array<float, 3> dryTint = {1.0f, 1.0f, 1.0f};
    std::array<float, 3> lushTint = {1.0f, 1.0f, 1.0f};
    // Fractions by which full dryness (or lushness) lowers (raises) density and height.
    float density = 0.0f;
    float height = 0.0f;
};

// Single-species wildflower clusters among the grass of families with flowers.
struct TerrainDecorationFlowers
{
    // Share of the ground inside clusters, the clusters' size in world units, and the share of grass instances
    // inside a cluster that become flowers.
    float coverage = 0.0f;
    float clusterSize = 900.0f;
    float chance = 0.0f;
    std::array<float, 3> tint = {1.0f, 1.0f, 1.0f};
    std::vector<TerrainDecorationVariant> variants;
};

// Unmown grass along walls, buildings, decorations and the edges of roads and water.
struct TerrainDecorationVerges
{
    float distance = 0.0f;
    float tallChance = 0.0f;
};

// Ferns and tall grass around the trunks of the named map decorations.
struct TerrainDecorationUndergrowth
{
    std::vector<std::string> decorations; // Substrings of the decoration names.
    float innerRadius = 90.0f;
    float outerRadius = 260.0f;
    int candidates = 0;
    std::vector<TerrainDecorationVariant> variants;
};

// Distance level of detail (level_generation/terrain/GRASS_LOD.md), mirrored by vs_terrain_decoration.sc.
struct TerrainDecorationLod
{
    float nearDistance = 2048.0f;
    float farDistance = 16384.0f;
    float maxScale = 2.0f;
};

struct TerrainDecorationConfig
{
    uint32_t seed = 1;
    // Grass thins beyond lod.nearDistance; stones keep their count to stoneDistance and then fade.
    TerrainDecorationLod lod;
    float stoneDistance = 2048.0f;
    // Strength of the terrain's grass colour where the tufts have thinned out (0 = none).
    float foliageTint = 1.0f;
    float minimumNormalZ = 0.85f;
    float densityVariation = 0.0f;
    float patchSize = 768.0f;
    float tallGrassChance = 0.0f;
    float tallGrassScale = 1.8f;
    TerrainDecorationMeadow meadow;
    TerrainDecorationFlowers flowers;
    TerrainDecorationVerges verges;
    TerrainDecorationUndergrowth undergrowth;
    // Optional 128x128 cell-centred grayscale control: black = none, white = full density.
    std::vector<uint8_t> densityMask;
    std::string tuftTexture;
    std::array<int, 2> tuftAtlasGrid = {1, 1};
    std::vector<TerrainDecorationVariant> tuftVariants = {TerrainDecorationVariant{}};
    std::vector<TerrainDecorationRule> rules;
};

// Four vec4s, matching i_data0..3 in the instanced vertex shader.
struct TerrainDecorationInstance
{
    std::array<float, 4> positionYaw;
    // w = rank in [0, 1): the instance is drawn while its rank is below terrainDecorationKeep.
    std::array<float, 4> sizeWindRank;
    std::array<float, 4> color;
    std::array<float, 4> groundNormal; // xyz = surface normal; w = tuft texture layer.
};

struct TerrainDecorationPatch
{
    uint32_t first = 0;
    uint32_t count = 0;
    bool stone = false;
    std::array<float, 3> min;
    std::array<float, 3> max;
};

// A rank quadtree node. Leaves are TerrainDecorationLeafSize squares; a node j levels above the leaves holds the
// ranks [4^-(j+1), 4^-j) of its area (the root everything below), sorted ascending, so a draw is a prefix of range.
struct TerrainDecorationNode
{
    TerrainDecorationPatch range; // Own instances, with bounds that include the largest LOD scale and wind.
    float rankBegin = 0.0f;
    float rankEnd = 1.0f; // Children hold the ranks from rankEnd up.
    std::array<float, 3> regionMin; // Bounds of the whole subtree.
    std::array<float, 3> regionMax;
    std::array<uint32_t, 4> children = {};
    uint32_t childCount = 0;
};

constexpr float TerrainDecorationLeafSize = 2048.0f;

struct TerrainDecorationPlacement
{
    std::vector<TerrainDecorationInstance> instances;
    // One quadtree per mesh kind, depth first: a node's instances directly precede its first child's.
    std::vector<TerrainDecorationNode> nodes;
    std::vector<uint32_t> roots;
};

// Share of an area's instances drawn at a camera distance: all within nearDistance, then (near / distance)^2, which
// keeps the count per screen area constant, falling to none over the last 15 % of farDistance.
float terrainDecorationKeep(const TerrainDecorationLod &lod, float distance);
// Footprint scale that keeps the thinned coverage (keep * scale^2 = 1), at most lod.maxScale; ignores the end fade.
float terrainDecorationScale(const TerrainDecorationLod &lod, float distance);
// Number of a node's leading instances (sorted by rank) whose rank is below keep; ranks holds every instance's rank.
uint32_t terrainDecorationPrefix(const std::vector<float> &ranks, const TerrainDecorationPatch &range, float keep);

// Average colour and filled share of one tuft atlas layer, for the foliage map.
struct TerrainFoliageLayer
{
    std::array<float, 3> color = {0.0f, 0.0f, 0.0f};
    float fill = 0.0f;
};

constexpr int TerrainFoliageMapSize = 256;

// The terrain's far grass colour: RGBA over the whole map (row 0 at the north edge, like the terrain weights),
// rgb = average tuft colour premultiplied by a, a = optical depth of the grass cards seen edge on (projected card
// area per ground area). Grass instances only.
std::vector<float> buildTerrainFoliageMap(const TerrainDecorationPlacement &placement,
    const std::vector<TerrainFoliageLayer> &layers);

std::optional<TerrainDecorationConfig> loadTerrainDecorationConfig(
    const Engine::AssetFileSystem &assets, const OutdoorMapData &map, std::string &error);
// terrainSmoothing: the materials terrain's smoothing (0 = native triangles), so plants stand on the drawn ground.
TerrainDecorationPlacement scatterTerrainDecorations(const OutdoorMapData &map,
    const std::array<std::string, 256> &textures, const TerrainDecorationConfig &config,
    float terrainSmoothing = 0.0f);

// Tight card outline of one tuft texture (alpha, row-major, width x height): a convex polygon of
// TerrainDecorationCutoutVertices texture coordinates in order (u right, v down; the last vertex repeats when fewer
// are needed) enclosing every texel with alpha >= threshold plus a small margin, within [0, 1]. A card cut to it skips
// the transparent area that an alpha test would otherwise shade. An empty texture gives the full quad.
constexpr size_t TerrainDecorationCutoutVertices = 8;
std::array<std::array<float, 2>, TerrainDecorationCutoutVertices> terrainDecorationCutout(
    const std::vector<uint8_t> &alpha, int width, int height, uint8_t threshold);
} // namespace OpenYAMM::Game
