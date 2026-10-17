#include "game/outdoor/TerrainDecorationRenderer.h"

#include "engine/ImageAssetLoader.h"
#include "game/outdoor/OutdoorLightingRuntime.h"
#include "game/render/TextureFiltering.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace OpenYAMM::Game
{
namespace
{
struct Vertex
{
    float x, y, z, u, v;
    uint32_t color;
};

// One card, a fan over the TerrainDecorationCutoutVertices outline of its tuft layer: the vertex shader places slot x
// from the layer's cutout (u_terrainDecorationCutout) and turns the card to face the camera.
std::vector<Vertex> grassMesh()
{
    std::vector<Vertex> vertices;
    for (size_t slot = 0; slot < TerrainDecorationCutoutVertices; ++slot)
    {
        vertices.push_back({float(slot), 0.0f, 0.0f, 0.0f, 0.0f, 0xffffffff});
    }
    return vertices;
}

std::vector<uint16_t> grassIndices()
{
    std::vector<uint16_t> indices;
    for (uint16_t slot = 1; slot + 1 < TerrainDecorationCutoutVertices; ++slot)
    {
        indices.insert(indices.end(), {0, slot, uint16_t(slot + 1)});
    }
    return indices;
}

// TerrainDecorationMaxClumpCards fans like grassMesh's, vertex y = the card; a clump draws the first cards.
std::vector<Vertex> clumpMesh()
{
    std::vector<Vertex> vertices;
    for (int card = 0; card < TerrainDecorationMaxClumpCards; ++card)
    {
        for (size_t slot = 0; slot < TerrainDecorationCutoutVertices; ++slot)
        {
            vertices.push_back({float(slot), float(card), 0.0f, 0.0f, 0.0f, 0xffffffff});
        }
    }
    return vertices;
}

std::vector<uint16_t> clumpIndices()
{
    std::vector<uint16_t> indices;
    for (uint16_t card = 0; card < TerrainDecorationMaxClumpCards; ++card)
    {
        const uint16_t first = uint16_t(card * TerrainDecorationCutoutVertices);
        for (uint16_t slot = 1; slot + 1 < TerrainDecorationCutoutVertices; ++slot)
        {
            indices.insert(indices.end(), {first, uint16_t(first + slot), uint16_t(first + slot + 1)});
        }
    }
    return indices;
}

std::vector<Vertex> stoneMesh()
{
    std::vector<Vertex> vertices;
    for (int side = 0; side < 6; ++side)
    {
        const float a = float(side) * 6.2831853f / 6.0f;
        const float b = float(side + 1) * 6.2831853f / 6.0f;
        const uint32_t shade = 175 + uint32_t((side * 23) % 65);
        const uint32_t color = 0xff000000u | (shade << 16) | (shade << 8) | shade;
        const Vertex lowerA = {std::cos(a) * 0.5f, std::sin(a) * 0.38f, 0.0f, 0.0f, 0.0f, color};
        const Vertex lowerB = {std::cos(b) * 0.5f, std::sin(b) * 0.38f, 0.0f, 0.0f, 0.0f, color};
        const Vertex upperA = {std::cos(a) * 0.27f + 0.06f, std::sin(a) * 0.23f, 0.75f, 0.0f, 0.0f, color};
        const Vertex upperB = {std::cos(b) * 0.27f + 0.06f, std::sin(b) * 0.23f, 0.75f, 0.0f, 0.0f, color};
        const Vertex center = {0.04f, -0.02f, 0.95f, 0.0f, 0.0f, color};
        vertices.insert(vertices.end(), {lowerA, lowerB, upperA, upperA, lowerB, upperB, upperA, upperB, center});
    }
    return vertices;
}
}

bool TerrainDecorationRenderer::initialize(
    const Engine::AssetFileSystem &assets, const TerrainDecorationConfig &config,
    TerrainDecorationPlacement placement, bgfx::ProgramHandle program, bgfx::ProgramHandle shadowProgram)
{
    shutdown(true);
    m_program = program;
    m_shadowProgram = shadowProgram;
    Engine::BinaryAssetCache cache;
    std::optional<Engine::ImagePixelsBgra> texture =
        Engine::loadImageAssetPixelsBgra(assets, config.tuftTexture, cache);
    if (!texture || placement.instances.empty() || !bgfx::isValid(program) || !bgfx::isValid(shadowProgram))
    {
        std::cerr << "Terrain decorations could not initialize: " << config.tuftTexture << '\n';
        shutdown(true);
        return false;
    }
    const int columns = config.tuftAtlasGrid[0];
    const int rows = config.tuftAtlasGrid[1];
    if (texture->width % columns != 0 || texture->height % rows != 0 || size_t(columns * rows) > MaxTuftLayers)
    {
        std::cerr << "Terrain decoration atlas dimensions do not match its grid (at most " << MaxTuftLayers
                  << " cells): " << config.tuftTexture << '\n';
        shutdown(true);
        return false;
    }
    const uint16_t cellWidth = uint16_t(texture->width / columns);
    const uint16_t cellHeight = uint16_t(texture->height / rows);
    // bgfx uses a 2D target for one layer; reserve a spare layer to keep the array sampler valid.
    const uint16_t arrayLayers = uint16_t(std::max(2, columns * rows));
    std::vector<TerrainFoliageLayer> foliageLayers(size_t(columns * rows));
    m_texture = bgfx::createTexture2D(cellWidth, cellHeight, true, arrayLayers,
        bgraTextureUploadFormat(), BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    // Store atlas cells as array layers: independent edge bleed, alpha coverage and complete mip chains.
    for (int layer = 0; layer < columns * rows; ++layer)
    {
        std::vector<uint8_t> pixels(size_t(cellWidth) * cellHeight * 4);
        for (int y = 0; y < cellHeight; ++y)
        {
            const size_t sourceOffset = (size_t(layer / columns * cellHeight + y) * texture->width +
                                         layer % columns * cellWidth) * 4;
            std::copy_n(texture->pixels.data() + sourceOffset, size_t(cellWidth) * 4,
                        pixels.data() + size_t(y) * cellWidth * 4);
        }
        // Card outline from the cell's alpha; a low threshold keeps the soft fringe the mips spread outward.
        std::vector<uint8_t> alpha(size_t(cellWidth) * cellHeight);
        TerrainFoliageLayer &foliage = foliageLayers[size_t(layer)];
        for (size_t texel = 0; texel < alpha.size(); ++texel)
        {
            alpha[texel] = pixels[texel * 4 + 3];
            // Texels the grass shader keeps at full coverage (alpha cutoff 0.4), with its shading by height; pixels
            // are BGRA.
            if (alpha[texel] >= 102)
            {
                const std::array<float, 3> shade =
                    TerrainDecorationShade::average(1.0f - (float(texel / cellWidth) + 0.5f) / float(cellHeight));
                foliage.color[0] += pixels[texel * 4 + 2] * shade[0];
                foliage.color[1] += pixels[texel * 4 + 1] * shade[1];
                foliage.color[2] += pixels[texel * 4] * shade[2];
                foliage.fill += 1.0f;
            }
        }
        for (float &channel : foliage.color)
        {
            channel /= std::max(foliage.fill, 1.0f) * 255.0f;
        }
        foliage.fill /= float(alpha.size());
        const std::array<std::array<float, 2>, TerrainDecorationCutoutVertices> cutout =
            terrainDecorationCutout(alpha, cellWidth, cellHeight, 26);
        for (size_t slot = 0; slot < cutout.size(); ++slot)
        {
            const size_t index = size_t(layer) * TerrainDecorationCutoutVertices + slot;
            m_cutouts[index / 2][(index % 2) * 2] = cutout[slot][0];
            m_cutouts[index / 2][(index % 2) * 2 + 1] = cutout[slot][1];
        }
        prepareBgraTexturePixelsForUploadInPlace(cellWidth, cellHeight, pixels, TextureFilterProfile::Billboard);
        updateBgraTextureArrayLayer(m_texture, uint16_t(layer), cellWidth, cellHeight, pixels, 102);
    }
    bgfx::VertexLayout meshLayout;
    meshLayout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
    const std::vector<Vertex> grass = grassMesh();
    const std::vector<Vertex> stone = stoneMesh();
    m_grassMesh = bgfx::createVertexBuffer(
        bgfx::copy(grass.data(), uint32_t(grass.size() * sizeof(Vertex))), meshLayout);
    const std::vector<uint16_t> grassFan = grassIndices();
    m_grassIndices = bgfx::createIndexBuffer(
        bgfx::copy(grassFan.data(), uint32_t(grassFan.size() * sizeof(uint16_t))));
    m_stoneMesh = bgfx::createVertexBuffer(
        bgfx::copy(stone.data(), uint32_t(stone.size() * sizeof(Vertex))), meshLayout);
    const std::vector<Vertex> clump = clumpMesh();
    const std::vector<uint16_t> clumpFans = clumpIndices();
    m_clumpMesh = bgfx::createVertexBuffer(
        bgfx::copy(clump.data(), uint32_t(clump.size() * sizeof(Vertex))), meshLayout);
    m_clumpIndices = bgfx::createIndexBuffer(
        bgfx::copy(clumpFans.data(), uint32_t(clumpFans.size() * sizeof(uint16_t))));
    m_clumpUniform = bgfx::createUniform("u_terrainDecorationClump", bgfx::UniformType::Vec4);
    bgfx::VertexLayout instanceLayout;
    instanceLayout.begin().add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float).end();
    m_instances = bgfx::createVertexBuffer(bgfx::copy(placement.instances.data(),
        uint32_t(placement.instances.size() * sizeof(TerrainDecorationInstance))), instanceLayout);
    m_sampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    m_params = bgfx::createUniform("u_terrainDecorationParams", bgfx::UniformType::Vec4);
    m_lodUniform = bgfx::createUniform("u_terrainDecorationLod", bgfx::UniformType::Vec4);
    const std::vector<uint8_t> foliage = encodeTerrainFoliageMap(
        buildTerrainFoliageMap(placement, foliageLayers, config.clump));
    m_foliageMap = bgfx::createTexture2D(TerrainFoliageMapSize, TerrainFoliageMapSize, true, 1,
        bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(foliage.data(), uint32_t(foliage.size())));
    m_cutoutUniform = bgfx::createUniform("u_terrainDecorationCutout", bgfx::UniformType::Vec4,
        uint16_t(m_cutouts.size()));
    if (!bgfx::isValid(m_texture) || !bgfx::isValid(m_instances) || !bgfx::isValid(m_grassMesh) ||
        !bgfx::isValid(m_grassIndices) || !bgfx::isValid(m_cutoutUniform) ||
        !bgfx::isValid(m_stoneMesh) || !bgfx::isValid(m_sampler) || !bgfx::isValid(m_params) ||
        !bgfx::isValid(m_lodUniform) || !bgfx::isValid(m_foliageMap) || !bgfx::isValid(m_clumpMesh) ||
        !bgfx::isValid(m_clumpIndices) || !bgfx::isValid(m_clumpUniform))
    {
        shutdown(true);
        return false;
    }
    const size_t instanceCount = placement.instances.size();
    setPlacement(config, std::move(placement));
    std::cout << "Terrain decorations: " << instanceCount << " instances, " << m_nodes.size() << " nodes, "
              << instanceCount * sizeof(TerrainDecorationInstance) << " instance bytes, full density to "
              << m_lod.nearDistance << ", range " << m_lod.farDistance << " world units.\n";
    return true;
}

void TerrainDecorationRenderer::setPlacement(const TerrainDecorationConfig &config,
    TerrainDecorationPlacement placement)
{
    m_lod = config.lod;
    m_overhead = config.overhead;
    m_clump = config.clump;
    m_groundShade = config.groundShade;
    m_stoneDistance = config.stoneDistance;
    m_foliageTint = config.foliageTint;
    m_ranks.clear();
    m_ranks.reserve(placement.instances.size());
    for (const TerrainDecorationInstance &instance : placement.instances)
    {
        m_ranks.push_back(instance.sizeWindRank[3]);
    }
    m_nodes = std::move(placement.nodes);
    m_roots = std::move(placement.roots);
}

void TerrainDecorationRenderer::shutdown(bool destroyResources)
{
    if (destroyResources)
    {
        if (bgfx::isValid(m_program))
        {
            bgfx::destroy(m_program);
        }
        for (const bgfx::ProgramHandle handle : {m_shadowProgram, m_coverageProgram, m_coverageShadowProgram})
        {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
        }
        if (bgfx::isValid(m_instances))
        {
            bgfx::destroy(m_instances);
        }
        if (bgfx::isValid(m_grassMesh))
        {
            bgfx::destroy(m_grassMesh);
        }
        if (bgfx::isValid(m_stoneMesh))
        {
            bgfx::destroy(m_stoneMesh);
        }
        if (bgfx::isValid(m_grassIndices))
        {
            bgfx::destroy(m_grassIndices);
        }
        if (bgfx::isValid(m_clumpMesh))
        {
            bgfx::destroy(m_clumpMesh);
        }
        if (bgfx::isValid(m_clumpIndices))
        {
            bgfx::destroy(m_clumpIndices);
        }
        if (bgfx::isValid(m_clumpUniform))
        {
            bgfx::destroy(m_clumpUniform);
        }
        if (bgfx::isValid(m_cutoutUniform))
        {
            bgfx::destroy(m_cutoutUniform);
        }
        if (bgfx::isValid(m_texture))
        {
            bgfx::destroy(m_texture);
        }
        if (bgfx::isValid(m_sampler))
        {
            bgfx::destroy(m_sampler);
        }
        if (bgfx::isValid(m_params))
        {
            bgfx::destroy(m_params);
        }
        if (bgfx::isValid(m_lodUniform))
        {
            bgfx::destroy(m_lodUniform);
        }
        if (bgfx::isValid(m_foliageMap))
        {
            bgfx::destroy(m_foliageMap);
        }
    }
    m_program = BGFX_INVALID_HANDLE;
    m_shadowProgram = BGFX_INVALID_HANDLE;
    m_coverageProgram = BGFX_INVALID_HANDLE;
    m_coverageShadowProgram = BGFX_INVALID_HANDLE;
    m_coverageProgramsAttempted = false;
    m_instances = BGFX_INVALID_HANDLE;
    m_grassMesh = BGFX_INVALID_HANDLE;
    m_stoneMesh = BGFX_INVALID_HANDLE;
    m_grassIndices = BGFX_INVALID_HANDLE;
    m_clumpMesh = BGFX_INVALID_HANDLE;
    m_clumpIndices = BGFX_INVALID_HANDLE;
    m_clumpUniform = BGFX_INVALID_HANDLE;
    m_cutoutUniform = BGFX_INVALID_HANDLE;
    m_texture = BGFX_INVALID_HANDLE;
    m_sampler = BGFX_INVALID_HANDLE;
    m_params = BGFX_INVALID_HANDLE;
    m_lodUniform = BGFX_INVALID_HANDLE;
    m_foliageMap = BGFX_INVALID_HANDLE;
    m_nodes.clear();
    m_roots.clear();
    m_ranks.clear();
    m_foliageTint = 0.0f;
    m_viewValid = false;
}

void TerrainDecorationRenderer::setCameraAltitude(float altitude)
{
    const float progress = std::clamp((altitude - m_overhead.start) / (m_overhead.end - m_overhead.start), 0.0f, 1.0f);
    m_overheadAmount = progress * progress * (3.0f - 2.0f * progress);
}

bool TerrainDecorationRenderer::setView(
    const bx::Vec3 &camera, const bx::Vec3 &forward, const bx::Vec3 &right, const bx::Vec3 &up,
    float aspect, float verticalFovRadians)
{
    const auto sameVector = [](const bx::Vec3 &a, const bx::Vec3 &b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    if (m_viewValid && sameVector(camera, m_camera) && sameVector(forward, m_forward) &&
        sameVector(right, m_right) && sameVector(up, m_up) && aspect == m_aspect && verticalFovRadians == m_verticalFov)
    {
        return false;
    }
    m_viewValid = true;
    m_aspect = aspect;
    m_verticalFov = verticalFovRadians;
    m_camera = camera;
    m_forward = forward;
    m_right = right;
    m_up = up;
    m_tanY = std::tan(verticalFovRadians * 0.5f);
    m_tanX = m_tanY * aspect;
    m_radiusScaleX = std::sqrt(1.0f + m_tanX * m_tanX);
    m_radiusScaleY = std::sqrt(1.0f + m_tanY * m_tanY);
    return true;
}

float TerrainDecorationRenderer::distanceTo(const std::array<float, 3> &min, const std::array<float, 3> &max) const
{
    const float dx = std::max({min[0] - m_camera.x, 0.0f, m_camera.x - max[0]});
    const float dy = std::max({min[1] - m_camera.y, 0.0f, m_camera.y - max[1]});
    const float dz = std::max({min[2] - m_camera.z, 0.0f, m_camera.z - max[2]});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool TerrainDecorationRenderer::visible(const std::array<float, 3> &min, const std::array<float, 3> &max,
    float maximumDistance) const
{
    if (distanceTo(min, max) > maximumDistance)
    {
        return false;
    }
    const bx::Vec3 center = {(min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f};
    const bx::Vec3 extent = {(max[0] - min[0]) * 0.5f, (max[1] - min[1]) * 0.5f, (max[2] - min[2]) * 0.5f};
    const float radius = bx::length(extent);
    const bx::Vec3 delta = bx::sub(center, m_camera);
    const float depth = bx::dot(delta, m_forward);
    return depth >= -radius &&
        std::abs(bx::dot(delta, m_right)) <= depth * m_tanX + radius * m_radiusScaleX &&
        std::abs(bx::dot(delta, m_up)) <= depth * m_tanY + radius * m_radiusScaleY;
}

TerrainDecorationLod TerrainDecorationRenderer::lod(bool stone) const
{
    // Stones keep their count and size to their own distance, then fade.
    return stone ? TerrainDecorationLod{m_stoneDistance, m_stoneDistance, 1.0f} : m_lod;
}

void TerrainDecorationRenderer::collectDraws(std::vector<TerrainDecorationPatch> &draws) const
{
    for (uint32_t root : m_roots)
    {
        collectNode(root, draws);
    }
}

void TerrainDecorationRenderer::collectNode(uint32_t index, std::vector<TerrainDecorationPatch> &draws) const
{
    const TerrainDecorationNode &node = m_nodes[index];
    const TerrainDecorationLod nodeLod = lod(node.range.stone);
    if (!visible(node.regionMin, node.regionMax, nodeLod.farDistance))
    {
        return;
    }
    // The subtree's best case: no instance in it can have a rank below its nearest point's keep.
    const float best = terrainDecorationKeep(nodeLod, distanceTo(node.regionMin, node.regionMax));
    if (best <= node.rankBegin)
    {
        return;
    }
    if (node.range.count > 0 && visible(node.range.min, node.range.max, nodeLod.farDistance))
    {
        // The vertex shader thins the prefix further by each instance's own distance.
        const float keep = terrainDecorationKeep(nodeLod, distanceTo(node.range.min, node.range.max));
        const uint32_t count = terrainDecorationPrefix(m_ranks, node.range, keep);
        if (count > 0)
        {
            TerrainDecorationPatch draw = node.range;
            draw.count = count;
            draws.push_back(draw);
        }
    }
    if (best > node.rankEnd)
    {
        for (uint32_t child = 0; child < node.childCount; ++child)
        {
            collectNode(node.children[child], draws);
        }
    }
}

bool TerrainDecorationRenderer::canMerge(
    const TerrainDecorationPatch &batch, const OutdoorSelectedFxLights &batchLights,
    const TerrainDecorationPatch &next, const OutdoorSelectedFxLights &nextLights)
{
    return batch.count != 0 && next.count != 0 && batch.stone == next.stone && batch.clump == next.clump &&
        batch.first + batch.count == next.first && batchLights.positions == nextLights.positions &&
        batchLights.colors == nextLights.colors && batchLights.params == nextLights.params;
}

void TerrainDecorationRenderer::setCoveragePrograms(bgfx::ProgramHandle program, bgfx::ProgramHandle shadowProgram)
{
    for (const bgfx::ProgramHandle handle : {m_coverageProgram, m_coverageShadowProgram})
    {
        if (bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
    }
    m_coverageProgram = program;
    m_coverageShadowProgram = shadowProgram;
    m_coverageProgramsAttempted = true;
}

void TerrainDecorationRenderer::submit(uint16_t viewId, const TerrainDecorationPatch &patch, float elapsedTime,
    bool sunShadows, bool alphaToCoverage)
{
    const TerrainDecorationLod patchLod = lod(patch.stone);
    // w: 1 = grass cards cut to their layer's outline, 2 = clumps of them, 0 = stones (plain mesh).
    const float params[4] = {elapsedTime, patchLod.nearDistance, patchLod.farDistance,
        patch.stone ? 0.0f : patch.clump ? 2.0f : 1.0f};
    // y = layers that fade as the camera flies (bit mask), z = that fade's progress (setCameraAltitude), w = the
    // distance where clumps are down to one card.
    const float lodParams[4] = {patchLod.maxScale, float(m_overhead.layerMask), m_overheadAmount, m_clump.distance};
    bgfx::setUniform(m_params, params);
    bgfx::setUniform(m_lodUniform, lodParams);
    if (patch.stone)
    {
        bgfx::setVertexBuffer(0, m_stoneMesh);
    }
    else
    {
        bgfx::setUniform(m_cutoutUniform, m_cutouts.data(), uint16_t(m_cutouts.size()));
        if (patch.clump)
        {
            const float lean = m_clump.leanDegrees * 0.017453293f;
            const float clumpParams[4] = {float(m_clump.cards), std::sin(lean), std::cos(lean), m_clump.cardScale};
            bgfx::setUniform(m_clumpUniform, clumpParams);
            bgfx::setVertexBuffer(0, m_clumpMesh);
            bgfx::setIndexBuffer(m_clumpIndices, 0,
                uint32_t(m_clump.cards) * uint32_t(TerrainDecorationCutoutVertices - 2) * 3);
        }
        else
        {
            bgfx::setVertexBuffer(0, m_grassMesh);
            bgfx::setIndexBuffer(m_grassIndices);
        }
    }
    bgfx::setInstanceDataBuffer(m_instances, patch.first, patch.count);
    bgfx::setTexture(0, m_sampler, m_texture);
    const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA;
    if (alphaToCoverage && bgfx::isValid(m_coverageProgram) && bgfx::isValid(m_coverageShadowProgram))
    {
        bgfx::setState(state | BGFX_STATE_BLEND_ALPHA_TO_COVERAGE);
        bgfx::submit(viewId, sunShadows ? m_coverageShadowProgram : m_coverageProgram);
        return;
    }
    bgfx::setState(state);
    bgfx::submit(viewId, sunShadows ? m_shadowProgram : m_program);
}
} // namespace OpenYAMM::Game
