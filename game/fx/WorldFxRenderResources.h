#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
struct WorldFxParticleVertex
{
    float x;
    float y;
    float z;
    float u;
    float v;
    uint32_t abgr;

    static void init();

    static bgfx::VertexLayout ms_layout;
};

struct WorldFxParticleTexture
{
    std::string textureName;
    int width = 0;
    int height = 0;
    bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
};

class WorldFxRenderResources
{
public:
    static constexpr size_t ParticleMaterialCount = 6;
    static constexpr size_t ParticleBlendModeCount = 2;
    static constexpr size_t ParticleBatchCount = ParticleMaterialCount * ParticleBlendModeCount;

    void reset();
    void shutdown();

    bool isReady() const;

    bgfx::ProgramHandle particleProgramHandle() const;
    void setParticleProgramHandle(bgfx::ProgramHandle handle);

    bgfx::UniformHandle particleParamsUniformHandle() const;
    void setParticleParamsUniformHandle(bgfx::UniformHandle handle);

    bgfx::UniformHandle textureSamplerHandle() const;
    void setTextureSamplerHandle(bgfx::UniformHandle handle);

    const std::array<uint16_t, ParticleMaterialCount> &particleTextureHandleIndices() const;
    std::array<uint16_t, ParticleMaterialCount> &particleTextureHandleIndices();

    const std::array<std::vector<WorldFxParticleVertex>, ParticleBatchCount> &particleVertexBatches() const;
    std::array<std::vector<WorldFxParticleVertex>, ParticleBatchCount> &particleVertexBatches();

    const WorldFxParticleTexture *findParticleTexture(const std::string &textureName) const;
    void addParticleTexture(WorldFxParticleTexture texture);

    // Bloom (CinematicGrading::glowView): additive particles and effects draw again into this view, or UINT16_MAX.
    uint16_t glowView() const
    {
        return m_glowView;
    }
    // Also forgets whether anything glowed: set once per frame before the effects draw.
    void setGlowView(uint16_t view)
    {
        m_glowView = view;
        m_glowDrawn = false;
    }
    // Whether an effect drew into the glow view since setGlowView (the bloom chain is skipped otherwise).
    bool glowDrawn() const
    {
        return m_glowDrawn;
    }
    void markGlowDrawn()
    {
        m_glowDrawn = true;
    }

private:
    uint16_t m_glowView = UINT16_MAX;
    bool m_glowDrawn = false;
    bgfx::ProgramHandle m_particleProgramHandle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_particleParamsUniformHandle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_textureSamplerHandle = BGFX_INVALID_HANDLE;
    std::vector<WorldFxParticleTexture> m_particleTextures;
    std::array<uint16_t, ParticleMaterialCount> m_particleTextureHandleIndices = {{
        bgfx::kInvalidHandle,
        bgfx::kInvalidHandle,
        bgfx::kInvalidHandle,
        bgfx::kInvalidHandle,
        bgfx::kInvalidHandle,
        bgfx::kInvalidHandle
    }};
    std::array<std::vector<WorldFxParticleVertex>, ParticleBatchCount> m_particleVertexBatches;
};
}
