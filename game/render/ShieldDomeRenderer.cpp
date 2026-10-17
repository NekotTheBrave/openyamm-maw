#include "game/render/ShieldDomeRenderer.h"

#include "game/render/RuntimeShader.h"

#include <bx/math.h>

#include <cmath>
#include <iostream>
#include <numbers>

namespace OpenYAMM::Game
{
namespace
{
constexpr uint32_t SphereRings = 24;
constexpr uint32_t SphereSegments = 40;

struct ShieldVertex
{
    float x;
    float y;
    float z;
    float normalX;
    float normalY;
    float normalZ;
};
}

void ShieldDomeRenderer::render(
    uint16_t viewId,
    const std::vector<ShieldDomeDraw> &domes,
    const bx::Vec3 &cameraPosition,
    float timeSeconds)
{
    if (domes.empty() || (!bgfx::isValid(m_program) && (m_initializeAttempted || !initialize())))
    {
        return;
    }

    for (const ShieldDomeDraw &dome : domes)
    {
        if (dome.radius <= 0.0f || dome.strength <= 0.001f)
        {
            continue;
        }

        float transform[16];
        bx::mtxSRT(transform, dome.radius, dome.radius, dome.radius, 0.0f, 0.0f, 0.0f,
            dome.center[0], dome.center[1], dome.center[2]);
        const float color[4] = {dome.color[0], dome.color[1], dome.color[2], dome.strength};
        const float params[4] = {dome.rimPower, dome.noise, timeSeconds, 0.0f};
        const float center[4] = {dome.center[0], dome.center[1], dome.center[2], dome.radius};
        const float camera[4] = {cameraPosition.x, cameraPosition.y, cameraPosition.z, dome.groundZ};
        bgfx::setTransform(transform);
        bgfx::setUniform(m_colorUniform, color);
        bgfx::setUniform(m_paramsUniform, params);
        bgfx::setUniform(m_centerUniform, center);
        bgfx::setUniform(m_cameraUniform, camera);
        bgfx::setUniform(m_hitsUniform, dome.hits.data(), static_cast<uint16_t>(ShieldDomeDraw::MaxHits));
        bgfx::setVertexBuffer(0, m_vertices);
        bgfx::setIndexBuffer(m_indices);
        // No culling: the far wall shows through the near one (the shader dims back faces).
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ADD | BGFX_STATE_MSAA);
        bgfx::submit(viewId, m_program);
    }
}

void ShieldDomeRenderer::shutdown(bool destroyGpu)
{
    if (destroyGpu)
    {
        for (bgfx::UniformHandle *pUniform :
             {&m_colorUniform, &m_paramsUniform, &m_centerUniform, &m_cameraUniform, &m_hitsUniform})
        {
            if (bgfx::isValid(*pUniform))
            {
                bgfx::destroy(*pUniform);
            }
        }

        if (bgfx::isValid(m_program))
        {
            bgfx::destroy(m_program);
        }

        if (bgfx::isValid(m_vertices))
        {
            bgfx::destroy(m_vertices);
        }

        if (bgfx::isValid(m_indices))
        {
            bgfx::destroy(m_indices);
        }
    }

    *this = ShieldDomeRenderer{};
}

bool ShieldDomeRenderer::initialize()
{
    m_initializeAttempted = true;

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();

    std::vector<ShieldVertex> vertices;
    std::vector<uint16_t> indices;
    vertices.reserve((SphereRings + 1) * (SphereSegments + 1));

    for (uint32_t ring = 0; ring <= SphereRings; ++ring)
    {
        const float polar = std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(SphereRings);

        for (uint32_t segment = 0; segment <= SphereSegments; ++segment)
        {
            const float azimuth =
                2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / static_cast<float>(SphereSegments);
            const float x = std::sin(polar) * std::cos(azimuth);
            const float y = std::sin(polar) * std::sin(azimuth);
            const float z = std::cos(polar);
            vertices.push_back({x, y, z, x, y, z});
        }
    }

    for (uint32_t ring = 0; ring < SphereRings; ++ring)
    {
        for (uint32_t segment = 0; segment < SphereSegments; ++segment)
        {
            const uint16_t topLeft = static_cast<uint16_t>(ring * (SphereSegments + 1) + segment);
            const uint16_t bottomLeft = static_cast<uint16_t>(topLeft + SphereSegments + 1);

            for (const uint16_t index : {topLeft, bottomLeft, uint16_t(topLeft + 1),
                     uint16_t(topLeft + 1), bottomLeft, uint16_t(bottomLeft + 1)})
            {
                indices.push_back(index);
            }
        }
    }

    m_vertices = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(ShieldVertex))), layout);
    m_indices = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint16_t))));
    m_program = loadRuntimeProgram("vs_shield_dome", "fs_shield_dome");
    m_colorUniform = bgfx::createUniform("u_shieldColor", bgfx::UniformType::Vec4);
    m_paramsUniform = bgfx::createUniform("u_shieldParams", bgfx::UniformType::Vec4);
    m_centerUniform = bgfx::createUniform("u_shieldCenter", bgfx::UniformType::Vec4);
    m_cameraUniform = bgfx::createUniform("u_shieldCamera", bgfx::UniformType::Vec4);
    m_hitsUniform = bgfx::createUniform(
        "u_shieldHits", bgfx::UniformType::Vec4, static_cast<uint16_t>(ShieldDomeDraw::MaxHits));

    if (!bgfx::isValid(m_program) || !bgfx::isValid(m_vertices) || !bgfx::isValid(m_indices))
    {
        std::cerr << "Shield dome rendering unavailable (vs/fs_shield_dome)\n";
        shutdown(true);
        m_initializeAttempted = true;
        return false;
    }

    return true;
}
}
