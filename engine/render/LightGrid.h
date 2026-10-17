#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <vector>

namespace OpenYAMM::Engine
{
// Clustered forward light grid (docs/LIGHTING_AND_MATERIALS_ARCHITECTURE.md, section 6). The view is split into
// tiles × logarithmic depth slices; the CPU bins each light sphere into the cells it reaches, and the shader
// (game/shaders/light_grid.sh) loops over only its own cell's lights. No compute shaders: the result is uploaded as
// one point-sampled float texture (lights, cells and the index list in row bands), which GLSL 1.50, ESSL 3.00 and
// SPIR-V all read with texelFetch.

// Sectors a light's reach list holds (indoors: the rooms it can light through portals).
constexpr size_t LightGridMaxReach = 16;

struct LightGridLight
{
    std::array<float, 3> position = {};
    float radius = 0.0f;
    // Linear colour and its intensity multiplier, as the shaders' point lights take them.
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    // The indoor sector the light is in (-1: none, outdoors).
    int16_t sector = -1;
    // The sectors it reaches (IndoorLightReach); a fragment of another sector takes none of it. 0: everywhere.
    std::array<int16_t, LightGridMaxReach> reach = {};
    uint8_t reachCount = 0;
    // Torch, spell and effect lights. World surfaces whose static lights are baked take only these; binning keeps
    // them ahead of static lights when a cell is full.
    bool dynamic = false;
};

struct LightGridConfig
{
    uint16_t tilesX = 16;
    uint16_t tilesY = 9;
    uint16_t slices = 24;
    // Lights per cell; a full cell keeps the strongest lights (radius x intensity / distance to the camera).
    uint16_t cellCap = 32;
    // Lights per frame after culling, strongest first.
    uint16_t maxLights = 256;
    // Depth slices run logarithmically from the near plane to farDistance; lights fade out over the last 15 %.
    float nearDistance = 8.0f;
    float farDistance = 4096.0f;

    static LightGridConfig high(float farDistance);
    static LightGridConfig low(float farDistance);
};

// The grid texture: LightGridTextureWidth texels per row; light data first (LightGridTexelsPerLight per light), then
// one texel per cell, then the index list (4 light indices per texel; room for every cell full, lightGridIndexTexels).
constexpr uint16_t LightGridTextureWidth = 64;
// Position + radius; colour + intensity; sector, dynamic (1 or 0), reach count, 0; the reach sectors, 4 per texel.
constexpr uint16_t LightGridTexelsPerLight = 3 + LightGridMaxReach / 4;
// The texture stage of s_lightGrid in every shader that includes light_grid.sh.
constexpr uint8_t LightGridTextureStage = 11;
// u_lightGrid[1].w: how a model takes the grid's lights (surfaces without the distinction use full).
constexpr float LightGridModeFull = 1.0f;
constexpr float LightGridModeHighlights = 2.0f;

struct LightGridStats
{
    uint32_t inputLights = 0;
    uint32_t culledLights = 0;
    uint32_t binnedLights = 0;
    uint32_t cellEntries = 0;
    // Entries dropped because a cell was full or the index texture was.
    uint32_t overflowEntries = 0;
};

// The CPU result: light data, per-cell (offset, count) and the packed index list, ready to upload.
struct LightGridData
{
    LightGridConfig config;
    // RGBA per texel: position.xyz, radius; colour.rgb, intensity (with the distance fade); sector, dynamic, reach
    // count, 0; then the reach sectors.
    std::vector<float> lightTexels;
    // RGBA per cell, cell = (slice × tilesY + tileY) × tilesX + tileX: offset, count, 0, 0.
    std::vector<float> cellTexels;
    // 4 light indices per RGBA texel.
    std::vector<float> indexTexels;
    uint32_t lightCount = 0;
    LightGridStats stats;
};

// Bins the lights for one view. viewMatrix and projectionMatrix are bx/bgfx matrices (row vector × matrix); the view
// depth of a point is its clip w, as the shader computes it from u_viewProj.
LightGridData buildLightGrid(const LightGridConfig &config, const std::vector<LightGridLight> &lights,
    const float *pViewMatrix, const float *pProjectionMatrix, const std::array<float, 3> &cameraPosition);

// Cell of a world point under the same rule as the shader (for tests and debugging); -1 outside the view.
int32_t lightGridCellForPoint(const LightGridConfig &config, const float *pViewMatrix, const float *pProjectionMatrix,
    const std::array<float, 3> &point);

// The uploaded grid of one view. Its texture is sized for a config and reused while the config stays the same.
class LightGridTextures
{
public:
    void upload(const LightGridData &data);
    void destroy();
    bool valid() const;
    // Lights binned in the last upload; a grid without any is not bound.
    uint32_t lightCount() const
    {
        return m_lightCount;
    }

    bgfx::TextureHandle texture() const
    {
        return m_texture;
    }
    // u_lightGrid[0]: tilesX, tilesY, slices, first cell row; [1]: near, log(far / near), cell cap, mode (0 here);
    // [2]: first index row, 0, 0, 0.
    const std::array<float, 12> &uniform() const
    {
        return m_uniform;
    }

private:
    bgfx::TextureHandle m_texture = BGFX_INVALID_HANDLE;
    uint16_t m_rows = 0;
    uint32_t m_lightCount = 0;
    std::array<float, 12> m_uniform = {};
};

// The shader side of the grid (light_grid.sh): s_lightGrid at LightGridTextureStage and u_lightGrid. Each renderer
// that draws grid-lit surfaces owns one.
class LightGridBinding
{
public:
    bool create();
    // destroyGpu false forgets the handles (the bgfx context is already gone).
    void destroy(bool destroyGpu = true);
    // Binds the grid for the next draw with the given mode; a null or empty grid binds mode 0 (the shaders' no-grid
    // path) and a 1×1 fallback texture, since backends need every declared sampler bound.
    void bind(const LightGridTextures *pGrid, float mode) const;

private:
    bgfx::UniformHandle m_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_fallback = BGFX_INVALID_HANDLE;
};
}
