#pragma once

#include <algorithm>

namespace OpenYAMM::Game
{
// Effect sprites and particles fade out as they come to fill the view: a smoke puff or an explosion core on the
// camera would otherwise cover the screen as one translucent sheet (a fireball flying at the party drops its smoke in
// front of the eyes). Full strength while the sprite's half size spans at most 90 % of the view's half height
// (60-degree vertical field of view; the party's buff auras, 150 units ahead, grow to about 1.2 to 1.5), gone once it
// spans twice; anything within 16 units of the camera plane fades too.
inline float effectViewFade(float depth, float halfSize)
{
    constexpr float HalfFovTangent = 0.57735f;
    if (depth <= 0.0f)
    {
        return 0.0f;
    }
    const auto smoothstep = [](float edge0, float edge1, float value)
    {
        const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const float coverage = halfSize / (depth * HalfFovTangent);
    return (1.0f - smoothstep(0.9f, 2.0f, coverage)) * smoothstep(4.0f, 16.0f, depth);
}
}
