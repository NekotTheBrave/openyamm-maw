#pragma once

#include <cstdint>

namespace OpenYAMM::Game
{
// Liquids drawn by the liquid shader (WaterRenderer) instead of as textured terrain or faces.
enum class LiquidKind : uint8_t
{
    None,
    Water,
    Tar,
    Lava,
};

// The shader's liquid selector (WaterVertex::liquid, v_texcoord1.z): 0 water, 1 tar, 2 lava.
inline float liquidShaderValue(LiquidKind kind)
{
    return kind == LiquidKind::Lava ? 2.0f : kind == LiquidKind::Tar ? 1.0f : 0.0f;
}

inline LiquidKind liquidFromShaderValue(float value)
{
    return value > 1.5f ? LiquidKind::Lava : value > 0.5f ? LiquidKind::Tar : LiquidKind::Water;
}

// Water and tar mirror their surroundings in a planar reflection; lava is self-lit and takes none.
inline bool liquidReflects(LiquidKind kind)
{
    return kind != LiquidKind::Lava;
}
}
