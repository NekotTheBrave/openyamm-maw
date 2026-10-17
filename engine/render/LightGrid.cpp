#include "engine/render/LightGrid.h"

#include <algorithm>
#include <cmath>

namespace OpenYAMM::Engine
{
namespace
{
std::array<float, 4> transformPoint(const float *pMatrix, const std::array<float, 4> &point)
{
    std::array<float, 4> result = {};
    for (size_t row = 0; row < 4; ++row)
    {
        result[row] = point[0] * pMatrix[row] + point[1] * pMatrix[4 + row] + point[2] * pMatrix[8 + row]
            + point[3] * pMatrix[12 + row];
    }
    return result;
}

float logDepthRange(const LightGridConfig &config)
{
    return std::log(std::max(config.farDistance, config.nearDistance * 1.01f) / config.nearDistance);
}

int32_t sliceForDepth(const LightGridConfig &config, float depth)
{
    const float normalized = std::log(std::max(depth, config.nearDistance) / config.nearDistance)
        / logDepthRange(config);
    return std::clamp(int32_t(std::floor(normalized * config.slices)), 0, int32_t(config.slices) - 1);
}

int32_t tileForNdc(float ndc, uint16_t tiles)
{
    return std::clamp(int32_t(std::floor((ndc * 0.5f + 0.5f) * tiles)), 0, int32_t(tiles) - 1);
}

uint16_t gridTextureRows(uint32_t texels)
{
    return uint16_t((texels + LightGridTextureWidth - 1) / LightGridTextureWidth);
}

// Index texels with room for every cell full.
uint32_t lightGridIndexTexels(const LightGridConfig &config)
{
    return (uint32_t(config.tilesX) * config.tilesY * config.slices * config.cellCap + 3) / 4;
}

float smoothstep(float edge0, float edge1, float value)
{
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

LightGridConfig LightGridConfig::high(float farDistance)
{
    LightGridConfig config;
    config.farDistance = farDistance;
    return config;
}

LightGridConfig LightGridConfig::low(float farDistance)
{
    LightGridConfig config;
    config.tilesX = 8;
    config.tilesY = 5;
    config.slices = 16;
    config.cellCap = 16;
    config.maxLights = 128;
    config.farDistance = farDistance;
    return config;
}

LightGridData buildLightGrid(const LightGridConfig &config, const std::vector<LightGridLight> &lights,
    const float *pViewMatrix, const float *pProjectionMatrix, const std::array<float, 3> &cameraPosition)
{
    LightGridData data;
    data.config = config;
    data.stats.inputLights = uint32_t(lights.size());
    const uint32_t cellCount = uint32_t(config.tilesX) * config.tilesY * config.slices;
    // Padded to whole rows of the grid texture, which the uploads copy.
    data.cellTexels.assign(size_t(gridTextureRows(cellCount)) * LightGridTextureWidth * 4, 0.0f);
    data.lightTexels.assign(size_t(gridTextureRows(uint32_t(config.maxLights) * LightGridTexelsPerLight))
        * LightGridTextureWidth * 4, 0.0f);

    // The view depth of a point is its clip w: the projection's w row, scaled per unit of view-space distance.
    const float depthScale = std::sqrt(pProjectionMatrix[3] * pProjectionMatrix[3]
        + pProjectionMatrix[7] * pProjectionMatrix[7] + pProjectionMatrix[11] * pProjectionMatrix[11]);
    const float fadeStart = config.farDistance * 0.85f;

    struct Candidate
    {
        uint32_t light = 0;
        float importance = 0.0f;
        float fade = 1.0f;
        std::array<int32_t, 6> range = {};   // tile x, y and slice: min, max
    };
    std::vector<Candidate> candidates;
    candidates.reserve(lights.size());
    for (uint32_t index = 0; index < lights.size(); ++index)
    {
        const LightGridLight &light = lights[index];
        if (!(light.radius > 0.0f) || !(light.intensity > 0.0f))
        {
            ++data.stats.culledLights;
            continue;
        }
        const float distance = std::sqrt(
            (light.position[0] - cameraPosition[0]) * (light.position[0] - cameraPosition[0])
            + (light.position[1] - cameraPosition[1]) * (light.position[1] - cameraPosition[1])
            + (light.position[2] - cameraPosition[2]) * (light.position[2] - cameraPosition[2]));
        // Fade out by the distance of the light's nearest point, so a large light reaching the camera stays lit.
        const float fade = 1.0f - smoothstep(fadeStart, config.farDistance, std::max(distance - light.radius, 0.0f));
        const std::array<float, 4> viewCentre = transformPoint(pViewMatrix,
            {light.position[0], light.position[1], light.position[2], 1.0f});
        const float centreDepth = transformPoint(pProjectionMatrix, viewCentre)[3];
        const float depthRadius = light.radius * depthScale;
        if (fade <= 0.0f || centreDepth + depthRadius < config.nearDistance
            || centreDepth - depthRadius > config.farDistance)
        {
            ++data.stats.culledLights;
            continue;
        }
        Candidate candidate;
        candidate.light = index;
        candidate.importance = light.radius * light.intensity / std::max(distance, 64.0f);
        candidate.fade = fade;
        // Screen bounds from the eight corners of the sphere's view-space box; one behind the near plane, or close to
        // the camera plane, takes the whole screen.
        bool fullScreen = false;
        std::array<float, 4> ndc = {1.0f, 1.0f, -1.0f, -1.0f};   // min x, min y, max x, max y
        for (int corner = 0; corner < 8 && !fullScreen; ++corner)
        {
            const std::array<float, 4> viewCorner = {
                viewCentre[0] + ((corner & 1) ? light.radius : -light.radius),
                viewCentre[1] + ((corner & 2) ? light.radius : -light.radius),
                viewCentre[2] + ((corner & 4) ? light.radius : -light.radius), 1.0f};
            const std::array<float, 4> clip = transformPoint(pProjectionMatrix, viewCorner);
            if (clip[3] <= config.nearDistance * 0.5f)
            {
                fullScreen = true;
                break;
            }
            ndc[0] = std::min(ndc[0], clip[0] / clip[3]);
            ndc[1] = std::min(ndc[1], clip[1] / clip[3]);
            ndc[2] = std::max(ndc[2], clip[0] / clip[3]);
            ndc[3] = std::max(ndc[3], clip[1] / clip[3]);
        }
        if (!fullScreen && (ndc[2] < -1.0f || ndc[0] > 1.0f || ndc[3] < -1.0f || ndc[1] > 1.0f))
        {
            ++data.stats.culledLights;
            continue;
        }
        candidate.range = fullScreen
            ? std::array<int32_t, 6>{0, int32_t(config.tilesX) - 1, 0, int32_t(config.tilesY) - 1, 0, 0}
            : std::array<int32_t, 6>{tileForNdc(ndc[0], config.tilesX), tileForNdc(ndc[2], config.tilesX),
                tileForNdc(ndc[1], config.tilesY), tileForNdc(ndc[3], config.tilesY), 0, 0};
        candidate.range[4] = sliceForDepth(config, centreDepth - depthRadius);
        candidate.range[5] = sliceForDepth(config, centreDepth + depthRadius);
        candidates.push_back(candidate);
    }
    // Dynamic lights first (world surfaces with baked static light take only them), then strongest first: the light
    // cap keeps them, and full cells keep them.
    std::stable_sort(candidates.begin(), candidates.end(), [&lights](const Candidate &left, const Candidate &right)
    {
        const bool leftDynamic = lights[left.light].dynamic;
        const bool rightDynamic = lights[right.light].dynamic;
        return leftDynamic != rightDynamic ? leftDynamic : left.importance > right.importance;
    });
    if (candidates.size() > config.maxLights)
    {
        data.stats.culledLights += uint32_t(candidates.size() - config.maxLights);
        candidates.resize(config.maxLights);
    }

    std::vector<uint16_t> cellLights(size_t(cellCount) * config.cellCap);
    std::vector<uint16_t> cellCounts(cellCount, 0);
    for (uint32_t slot = 0; slot < candidates.size(); ++slot)
    {
        const Candidate &candidate = candidates[slot];
        const LightGridLight &light = lights[candidate.light];
        float *pTexel = data.lightTexels.data() + size_t(slot) * LightGridTexelsPerLight * 4;
        pTexel[0] = light.position[0];
        pTexel[1] = light.position[1];
        pTexel[2] = light.position[2];
        pTexel[3] = light.radius;
        pTexel[4] = light.color[0];
        pTexel[5] = light.color[1];
        pTexel[6] = light.color[2];
        pTexel[7] = light.intensity * candidate.fade;
        pTexel[8] = float(light.sector);
        pTexel[9] = light.dynamic ? 1.0f : 0.0f;
        pTexel[10] = float(std::min<size_t>(light.reachCount, LightGridMaxReach));
        for (size_t entry = 0; entry < light.reachCount && entry < LightGridMaxReach; ++entry)
        {
            pTexel[12 + entry] = float(light.reach[entry]);
        }
        for (int32_t slice = candidate.range[4]; slice <= candidate.range[5]; ++slice)
        {
            for (int32_t tileY = candidate.range[2]; tileY <= candidate.range[3]; ++tileY)
            {
                for (int32_t tileX = candidate.range[0]; tileX <= candidate.range[1]; ++tileX)
                {
                    const uint32_t cell = (uint32_t(slice) * config.tilesY + uint32_t(tileY)) * config.tilesX
                        + uint32_t(tileX);
                    if (cellCounts[cell] >= config.cellCap)
                    {
                        ++data.stats.overflowEntries;
                        continue;
                    }
                    cellLights[size_t(cell) * config.cellCap + cellCounts[cell]++] = uint16_t(slot);
                }
            }
        }
    }
    data.lightCount = uint32_t(candidates.size());
    data.stats.binnedLights = data.lightCount;

    // The index list has room for every cell full (lightGridIndexTexels), so no cell loses its lights: a fixed list
    // used to run out with a few large lights near the camera, and every farther depth slice went dark.
    uint32_t entries = 0;
    for (uint32_t cell = 0; cell < cellCount; ++cell)
    {
        entries += cellCounts[cell];
    }
    // At least one row: the upload copies one even when the grid is empty.
    const uint16_t indexRows = std::max<uint16_t>(gridTextureRows((entries + 3) / 4), 1);
    data.indexTexels.assign(size_t(indexRows) * LightGridTextureWidth * 4, 0.0f);
    uint32_t offset = 0;
    for (uint32_t cell = 0; cell < cellCount; ++cell)
    {
        const uint32_t count = cellCounts[cell];
        data.cellTexels[size_t(cell) * 4 + 0] = float(offset);
        data.cellTexels[size_t(cell) * 4 + 1] = float(count);
        for (uint32_t entry = 0; entry < count; ++entry)
        {
            data.indexTexels[offset + entry] = float(cellLights[size_t(cell) * config.cellCap + entry]);
        }
        offset += count;
    }
    data.stats.cellEntries = offset;
    return data;
}

int32_t lightGridCellForPoint(const LightGridConfig &config, const float *pViewMatrix, const float *pProjectionMatrix,
    const std::array<float, 3> &point)
{
    const std::array<float, 4> clip = transformPoint(pProjectionMatrix,
        transformPoint(pViewMatrix, {point[0], point[1], point[2], 1.0f}));
    if (clip[3] <= 0.0f || std::abs(clip[0]) > clip[3] || std::abs(clip[1]) > clip[3])
    {
        return -1;
    }
    const int32_t tileX = tileForNdc(clip[0] / clip[3], config.tilesX);
    const int32_t tileY = tileForNdc(clip[1] / clip[3], config.tilesY);
    return (sliceForDepth(config, clip[3]) * config.tilesY + tileY) * config.tilesX + tileX;
}

namespace
{
void updateRows(bgfx::TextureHandle texture, uint16_t firstRow, const float *pTexels, uint32_t texels)
{
    const uint16_t rows = std::max<uint16_t>(gridTextureRows(texels), 1);
    bgfx::updateTexture2D(texture, 0, 0, 0, firstRow, LightGridTextureWidth, rows,
        bgfx::copy(pTexels, uint32_t(size_t(rows) * LightGridTextureWidth * 4 * sizeof(float))));
}
}

void LightGridTextures::upload(const LightGridData &data)
{
    const LightGridConfig &config = data.config;
    const uint32_t cellCount = uint32_t(config.tilesX) * config.tilesY * config.slices;
    const uint16_t cellRow = gridTextureRows(uint32_t(config.maxLights) * LightGridTexelsPerLight);
    const uint16_t indexRow = uint16_t(cellRow + gridTextureRows(cellCount));
    const uint16_t rows = uint16_t(indexRow + gridTextureRows(lightGridIndexTexels(config)));
    if (!valid() || rows != m_rows)
    {
        destroy();
        m_texture = bgfx::createTexture2D(LightGridTextureWidth, rows, false, 1, bgfx::TextureFormat::RGBA32F,
            BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        m_rows = rows;
    }
    m_lightCount = data.lightCount;
    m_uniform = {float(config.tilesX), float(config.tilesY), float(config.slices), float(cellRow),
        config.nearDistance, logDepthRange(config), float(config.cellCap), 0.0f,
        float(indexRow), 0.0f, 0.0f, 0.0f};
    // An empty grid is not bound (the shaders take the no-grid path), so it needs no upload.
    if (!valid() || m_lightCount == 0)
    {
        return;
    }
    // Only the rows in use.
    updateRows(m_texture, 0, data.lightTexels.data(), data.lightCount * LightGridTexelsPerLight);
    updateRows(m_texture, cellRow, data.cellTexels.data(), cellCount);
    updateRows(m_texture, indexRow, data.indexTexels.data(), std::max<uint32_t>((data.stats.cellEntries + 3) / 4, 1));
}

void LightGridTextures::destroy()
{
    if (bgfx::isValid(m_texture))
    {
        bgfx::destroy(m_texture);
    }
    m_texture = BGFX_INVALID_HANDLE;
    m_rows = 0;
    m_lightCount = 0;
}

bool LightGridTextures::valid() const
{
    return bgfx::isValid(m_texture);
}

bool LightGridBinding::create()
{
    m_uniform = bgfx::createUniform("u_lightGrid", bgfx::UniformType::Vec4, 3);
    m_sampler = bgfx::createUniform("s_lightGrid", bgfx::UniformType::Sampler);
    const std::array<float, 4> noLight = {};
    m_fallback = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
        BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(noLight.data(), sizeof(noLight)));
    return bgfx::isValid(m_uniform) && bgfx::isValid(m_sampler) && bgfx::isValid(m_fallback);
}

void LightGridBinding::destroy(bool destroyGpu)
{
    if (destroyGpu)
    {
        for (const bgfx::UniformHandle handle : {m_uniform, m_sampler})
        {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
        }
        if (bgfx::isValid(m_fallback))
        {
            bgfx::destroy(m_fallback);
        }
    }
    m_uniform = BGFX_INVALID_HANDLE;
    m_sampler = BGFX_INVALID_HANDLE;
    m_fallback = BGFX_INVALID_HANDLE;
}

void LightGridBinding::bind(const LightGridTextures *pGrid, float mode) const
{
    if (pGrid != nullptr && (!pGrid->valid() || pGrid->lightCount() == 0))
    {
        pGrid = nullptr;
    }
    std::array<float, 12> values = pGrid != nullptr ? pGrid->uniform() : std::array<float, 12>{};
    values[7] = pGrid != nullptr ? mode : 0.0f;
    bgfx::setUniform(m_uniform, values.data(), 3);
    bgfx::setTexture(LightGridTextureStage, m_sampler, pGrid != nullptr ? pGrid->texture() : m_fallback);
}
}
