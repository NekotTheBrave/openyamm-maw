#include "game/indoor/IndoorLightReach.h"

#include <algorithm>
#include <limits>

namespace OpenYAMM::Game
{
void IndoorLightReach::build(const IndoorMapData &mapData, const IndoorPortalGraph &graph)
{
    clear();
    m_portalsBySector.resize(mapData.sectors.size());
    for (const IndoorPortalLink &link : graph.portals)
    {
        if (link.faceId >= mapData.faces.size() || link.sectorA >= mapData.sectors.size()
            || link.sectorB >= mapData.sectors.size())
        {
            continue;
        }
        Portal portal;
        portal.min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        portal.max = {-portal.min.x, -portal.min.y, -portal.min.z};
        bool hasVertex = false;
        for (const uint16_t vertexIndex : mapData.faces[link.faceId].vertexIndices)
        {
            if (vertexIndex >= mapData.vertices.size())
            {
                continue;
            }
            const IndoorVertex &vertex = mapData.vertices[vertexIndex];
            const bx::Vec3 point = {float(vertex.x), float(vertex.y), float(vertex.z)};
            portal.min = bx::min(portal.min, point);
            portal.max = bx::max(portal.max, point);
            hasVertex = true;
        }
        if (!hasVertex)
        {
            continue;
        }
        portal.sectorA = link.sectorA;
        portal.sectorB = link.sectorB;
        const uint32_t index = uint32_t(m_portals.size());
        m_portals.push_back(portal);
        m_portalsBySector[link.sectorA].push_back(index);
        m_portalsBySector[link.sectorB].push_back(index);
    }
    m_visited.assign(mapData.sectors.size(), 0);
}

void IndoorLightReach::clear()
{
    m_portals.clear();
    m_portalsBySector.clear();
    m_visited.clear();
    m_queue.clear();
}

void IndoorLightReach::reach(std::span<const int16_t> startSectors, const bx::Vec3 &position, float radius,
    std::vector<int16_t> &sectors) const
{
    sectors.clear();
    m_queue.clear();
    for (const int16_t sector : startSectors)
    {
        if (sector >= 0 && size_t(sector) < m_portalsBySector.size() && m_visited[size_t(sector)] == 0)
        {
            m_visited[size_t(sector)] = 1;
            m_queue.push_back(uint16_t(sector));
        }
    }
    const float radiusSquared = radius * radius;
    for (size_t next = 0; next < m_queue.size(); ++next)
    {
        for (const uint32_t portalIndex : m_portalsBySector[m_queue[next]])
        {
            const Portal &portal = m_portals[portalIndex];
            const uint16_t other = portal.sectorA == m_queue[next] ? portal.sectorB : portal.sectorA;
            if (m_visited[other] != 0)
            {
                continue;
            }
            const bx::Vec3 closest = {std::clamp(position.x, portal.min.x, portal.max.x),
                std::clamp(position.y, portal.min.y, portal.max.y), std::clamp(position.z, portal.min.z, portal.max.z)};
            const bx::Vec3 offset = bx::sub(position, closest);
            if (bx::dot(offset, offset) <= radiusSquared)
            {
                m_visited[other] = 1;
                m_queue.push_back(other);
            }
        }
    }
    for (const uint16_t sector : m_queue)
    {
        sectors.push_back(int16_t(sector));
        m_visited[sector] = 0;
    }
    std::sort(sectors.begin(), sectors.end());
}

bool IndoorLightReach::contains(const std::vector<int16_t> &sectors, int16_t sector)
{
    return std::binary_search(sectors.begin(), sectors.end(), sector);
}
}
