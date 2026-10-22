#pragma once

#include "game/render/LiquidKind.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OpenYAMM::Game
{
// One liquid of engine/liquids/liquids.yml: which terrain tiles and face textures it covers, and its colour.
struct LiquidDefinition
{
    std::string id;
    LiquidKind kind = LiquidKind::Water;
    // ABGR: the lit body tint of water and tar, the glow tint of lava.
    uint32_t colorAbgr = 0;
    bool appliesToTerrain = false;
    bool appliesToFaces = false;
    // Terrain edge tiles over this liquid draw their overlay above it, and its alpha is the liquid's coverage.
    bool terrainTransitionOverlay = false;
    std::vector<std::string> textureNames;
    std::vector<std::string> texturePrefixes;
    uint32_t requiredFaceAttributes = 0;
    // The worlds whose maps this liquid applies to; empty: every world. A world's own liquid is matched before the
    // shared ones, so it can give a shared tile or texture another look in that world alone.
    std::vector<std::string> worlds;
    // Shallow liquids are waded through: their terrain cells are never deep water for gameplay (no drowning, the
    // party and monsters walk in them, dropped items stay), whatever the tile table's water flag says.
    bool shallow = false;
    // False: its surfaces never take a planar reflection, only the sky response (an opaque, matte liquid).
    bool reflects = true;
};

class LiquidTable
{
public:
    bool loadFromYaml(const std::string &yamlText, std::string &errorMessage);
    const LiquidDefinition *findMatch(std::string_view textureName, uint32_t faceAttributes, bool isTerrain) const;
    // The liquids that apply to one world's maps: that world's own first, then the shared ones in file order.
    LiquidTable forWorld(std::string_view worldId) const;
    bool hasShallowLiquids() const;

private:
    std::vector<LiquidDefinition> m_liquids;
};
}
