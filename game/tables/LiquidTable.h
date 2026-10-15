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
};

class LiquidTable
{
public:
    bool loadFromYaml(const std::string &yamlText, std::string &errorMessage);
    const LiquidDefinition *findMatch(std::string_view textureName, uint32_t faceAttributes, bool isTerrain) const;

private:
    std::vector<LiquidDefinition> m_liquids;
};
}
