#pragma once

#include "game/indoor/IndoorMapData.h"
#include "game/indoor/IndoorPortalGraph.h"

#include <bx/math.h>

#include <cstdint>
#include <span>
#include <vector>

namespace OpenYAMM::Game
{
// The sectors a light reaches (docs/LIGHTING_AND_MATERIALS_ARCHITECTURE.md, section 5.1): the sectors it is in, then,
// repeatedly, the sector behind every portal of a reached sector whose bounds lie within the light's radius. A room
// that only shares a wall with the light's room is never reached; light can still cross an internal wall inside one
// reached sector.
class IndoorLightReach
{
public:
    void build(const IndoorMapData &mapData, const IndoorPortalGraph &graph);
    void clear();
    size_t sectorCount() const
    {
        return m_portalsBySector.size();
    }

    // The reached sectors, sorted, into `sectors`. No valid start sector (or no map) leaves it empty: unrestricted.
    void reach(std::span<const int16_t> startSectors, const bx::Vec3 &position, float radius,
        std::vector<int16_t> &sectors) const;

    static bool contains(const std::vector<int16_t> &sectors, int16_t sector);

private:
    struct Portal
    {
        bx::Vec3 min = {0.0f, 0.0f, 0.0f};
        bx::Vec3 max = {0.0f, 0.0f, 0.0f};
        uint16_t sectorA = 0;
        uint16_t sectorB = 0;
    };

    std::vector<Portal> m_portals;
    std::vector<std::vector<uint32_t>> m_portalsBySector;
    mutable std::vector<uint8_t> m_visited;
    mutable std::vector<uint16_t> m_queue;
};
}
