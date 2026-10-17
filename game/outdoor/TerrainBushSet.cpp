#include "game/outdoor/TerrainBushSet.h"

#include <algorithm>
#include <cmath>

namespace OpenYAMM::Game
{
bool TerrainBushSet::load(const Engine::AssetFileSystem &assets, const TerrainDecorationBushes &config,
    const std::vector<TerrainBushInstance> &bushes, std::string &error)
{
    clear();
    if (bushes.empty())
    {
        return true;
    }
    m_fadeDistance = config.fadeDistance;
    for (const TerrainDecorationBushModel &model : config.models)
    {
        const Engine::ModelLoadResult loaded = m_assets.load(assets, model.model);
        if (!loaded)
        {
            error = "bush model " + model.model + ": " + loaded.error;
            clear();
            return false;
        }
        m_groups.push_back({loaded.asset, 0, Engine::ModelLodPixels, {}, {}, {}, {}});
    }
    for (const TerrainBushInstance &bush : bushes)
    {
        Engine::ModelStaticGroup &group = m_groups[bush.model];
        const Engine::ModelBounds &bounds = group.asset->staticBounds;
        // The asset's Y is up (glTF); the configured height is the model's world height.
        const float scale = config.models[bush.model].height / std::max(bounds.max[1] - bounds.min[1], 0.001f)
            * bush.heightScale;
        Engine::ModelStaticPlacement placement;
        placement.matrix = Engine::composeModelTransform(Engine::gltfModelPlacement(bush.position, bush.yaw, scale));
        group.placements.push_back(placement);
        group.bounds.push_back(group.placementBounds(placement));
    }
    std::erase_if(m_groups, [](const Engine::ModelStaticGroup &group)
    {
        return group.placements.empty();
    });
    for (const Engine::ModelStaticGroup &group : m_groups)
    {
        m_probes.emplace_back(group.placements.size());
        m_probeSampled.emplace_back(group.placements.size(), 0);
    }
    return true;
}

void TerrainBushSet::clear()
{
    m_groups.clear();
    m_probes.clear();
    m_probeSampled.clear();
    m_pProbeSource = nullptr;
    m_assets.clear();
}

void TerrainBushSet::updateFade(const std::array<float, 3> &camera)
{
    for (Engine::ModelStaticGroup &group : m_groups)
    {
        for (size_t index = 0; index < group.placements.size(); ++index)
        {
            const Engine::ModelBounds &bounds = group.bounds[index];
            const float dx = (bounds.min[0] + bounds.max[0]) * 0.5f - camera[0];
            const float dy = (bounds.min[1] + bounds.max[1]) * 0.5f - camera[1];
            const float dz = (bounds.min[2] + bounds.max[2]) * 0.5f - camera[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float coverage = std::clamp((m_fadeDistance[1] - distance) / (m_fadeDistance[1] - m_fadeDistance[0]),
                0.0f, 1.0f);
            group.placements[index].coverage = coverage;
            group.placements[index].visible = coverage > 0.0f;
        }
    }
}
}
