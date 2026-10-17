#pragma once

#include "game/render/WaterMath.h"

#include <bgfx/bgfx.h>

namespace OpenYAMM::Game
{
// Owns world post-processing targets; solid geometry precedes optional AO, transparencies, then grading and
// sharpening in one screen pass. With multisample anti-aliasing the world target is multisampled and that pass reads
// its resolved colour; with every effect off the world draws straight to the backbuffer.
class CinematicGrading
{
public:
    bool begin(int width, int height, bool enabled, int strength, bool ambientOcclusion = false, int aoStrength = 35,
        int sharpening = 0, int antialiasing = 0, bool bloom = false, int bloomStrength = 50);
    // Bloom (lighting plan, phase 7): the view emitters draw into again (their glow, display space, additively), or
    // UINT16_MAX without bloom. It shares the world depth, so occluded emitters do not glow.
    uint16_t glowView() const { return m_bloomActive ? WorldGlowView : UINT16_MAX; }
    // Something drew into the glow view this frame; without it the bloom passes are skipped.
    void markGlowDrawn() { m_glowDrawn = true; }
    // Whether this frame's world target is multisampled and supports alpha to coverage: cut-out draws into the world
    // views then turn their alpha test into coverage.
    bool alphaToCoverage() const { return m_alphaToCoverage; }
    void prepareWorldView(const float *pView, const float *pProjection);
    uint16_t transparentView(uint16_t worldView) const;
    void submit(bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE);
    void setOutputFrameBuffer(bgfx::FrameBufferHandle target);
    void resetViews();
    void shutdown();

private:
    bool initialize();
    bool resize(uint16_t width, uint16_t height, bool ambientOcclusion, uint8_t samples);
    bool initializeAmbientOcclusion();
    bool initializeBloom();
    bool createBloomTargets();
    void destroyBloomTargets();
    void destroyTargets();
    void submitAmbientOcclusion();
    void submitBloom();

    bgfx::FrameBufferHandle m_frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_lut = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sceneSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_lutSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_params = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoDepthProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoBlurProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoCompositeProgram = BGFX_INVALID_HANDLE;
    // The depth and composite passes reading the first sample of the unresolved multisampled depth (m_aoMsaaDepth).
    bgfx::ProgramHandle m_aoDepthMsaaProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoCompositeMsaaProgram = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_aoDepthFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_aoFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_aoBlurFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_aoCompositeFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_depthSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_aoSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_aoLinearDepthSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_aoRay = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_aoInverseProjection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_aoParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_aoDepthParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_aoTexel = BGFX_INVALID_HANDLE;
    // Bloom: the full-resolution glow (shares the world depth) and two half-resolution blur targets.
    bgfx::TextureHandle m_glowColor = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_glowFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_bloomFrameBuffers[2] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle m_bloomDownsampleProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_bloomBlurProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_bloomSourceSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_bloomTexel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_gradeBloomSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_gradeBloom = BGFX_INVALID_HANDLE;
    // A 1x1 black texture bound for the bloom sampler when bloom is off.
    bgfx::TextureHandle m_noBloom = BGFX_INVALID_HANDLE;
    float m_bloomStrength = 0.0f;
    bool m_bloomActive = false;
    bool m_glowDrawn = false;
    bool m_bloomUnsupportedLogged = false;
    float m_ray[4] = {};
    float m_inverseProjection[16] = {};
    bgfx::VertexLayout m_vertexLayout;
    bgfx::TransientVertexBuffer m_vertices = {};
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    float m_strength = 0.0f;
    float m_aoStrength = 0.0f;
    float m_sharpening = 0.0f;
    uint8_t m_samples = 0;
    uint8_t m_loggedSamples = 0;
    bool m_aoTargets = false;
    // The world depth is multisampled and sampled as such by AO, never resolved (desktop OpenGL with anti-aliasing).
    bool m_aoMsaaDepth = false;
    bool m_aoActive = false;
    bool m_aoPrepared = false;
    bool m_aoUnsupportedLogged = false;
    bool m_aoMsaaLogged = false;
    bool m_msaaUnsupportedLogged = false;
    bool m_alphaToCoverage = false;
    bool m_active = false;
    bool m_failed = false;
};
}
