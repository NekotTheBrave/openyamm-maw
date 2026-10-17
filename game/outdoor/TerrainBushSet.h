#pragma once

#include "engine/models/GltfModelLoader.h"
#include "engine/render/ModelRenderer.h"
#include "game/outdoor/OutdoorLightingData.h"
#include "game/outdoor/TerrainDecorationData.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

namespace OpenYAMM::Game
{
// The low 3D bushes placed with the grass (TerrainDecorationBushes, level_generation/terrain/MEADOW.md): one static
// model group per bush model. Scenery only: no collision, picking, events or sun shadows.
class TerrainBushSet
{
public:
    // False with an error when a model does not load; an empty placement loads nothing.
    bool load(const Engine::AssetFileSystem &assets, const TerrainDecorationBushes &config,
        const std::vector<TerrainBushInstance> &bushes, std::string &error);
    void clear();
    bool empty() const
    {
        return m_groups.empty();
    }
    std::vector<Engine::ModelStaticGroup> &groups()
    {
        return m_groups;
    }
    const std::vector<Engine::ModelStaticGroup> &groups() const
    {
        return m_groups;
    }
    // Dissolves the bushes between the fade distances from the camera, hiding those past it.
    void updateFade(const std::array<float, 3> &camera);
    // Baked light probe of each placement, sampled once per bake (bushes never move); index [group][placement].
    std::vector<std::vector<std::optional<OutdoorLightingData::Probe>>> &probes()
    {
        return m_probes;
    }
    std::vector<std::vector<uint8_t>> &probeSampled()
    {
        return m_probeSampled;
    }
    const void *&probeSource()
    {
        return m_pProbeSource;
    }

private:
    Engine::ModelAssetCache m_assets;
    std::vector<Engine::ModelStaticGroup> m_groups;
    std::vector<std::vector<std::optional<OutdoorLightingData::Probe>>> m_probes;
    std::vector<std::vector<uint8_t>> m_probeSampled;
    const void *m_pProbeSource = nullptr;
    std::array<float, 2> m_fadeDistance = {5000.0f, 6500.0f};
};
}
