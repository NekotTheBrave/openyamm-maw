#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bx
{
struct Vec3;
}

namespace OpenYAMM::Game
{
// One force-field shell for a frame: a sphere around centre, tinted and faded by strength.
struct ShieldDomeDraw
{
    static constexpr size_t MaxHits = 4;

    std::array<float, 3> center = {};
    float radius = 0.0f;
    // Height where the shell meets the ground; the shell glows there and draws nothing below it.
    float groundZ = 0.0f;
    // Linear 0-1 tint; strength folds in intensity and fade.
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float strength = 1.0f;
    float rimPower = 3.0f;
    float noise = 0.35f;
    // Unit direction from the centre to a projectile hit, and its age in seconds (negative: unused).
    std::array<std::array<float, 4>, MaxHits> hits = {{
        {0.0f, 0.0f, 0.0f, -1.0f},
        {0.0f, 0.0f, 0.0f, -1.0f},
        {0.0f, 0.0f, 0.0f, -1.0f},
        {0.0f, 0.0f, 0.0f, -1.0f},
    }};
};

// Additive fresnel shells (vs/fs_shield_dome): mostly transparent bubbles with a bright rim, drifting energy, a
// ground contact glow and hit ripples. Shared by the outdoor and indoor renderers; it draws into their transparent
// view after the world, depth-tested without depth writes. GPU resources are created on first use.
class ShieldDomeRenderer
{
public:
    void render(
        uint16_t viewId,
        const std::vector<ShieldDomeDraw> &domes,
        const bx::Vec3 &cameraPosition,
        float timeSeconds);
    void shutdown(bool destroyGpu);

private:
    bool initialize();

    bgfx::VertexBufferHandle m_vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_indices = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_colorUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_paramsUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_centerUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_cameraUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_hitsUniform = BGFX_INVALID_HANDLE;
    bool m_initializeAttempted = false;
};
}
