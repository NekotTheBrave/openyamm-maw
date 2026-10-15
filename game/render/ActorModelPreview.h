#pragma once

#include "engine/models/ModelInstance.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace OpenYAMM::Engine
{
class ModelRenderer;
}

namespace OpenYAMM::Game
{
class IGameplayWorldRuntime;
class WorldFxSystem;

// The actor inspect popup's 3D portrait: the inspected actor's portrait model (WorldFxSystem::syncInspectPortrait)
// drawn alone into a small offscreen target (ActorPortraitView, ordered before the HUD view that shows it). While the
// popup shows a modelled actor it costs one posed skeleton and one draw into a few hundred pixels square; otherwise
// nothing. Actors without a model keep their sprite preview.
class ActorModelPreview
{
public:
    // The active scene's actor models and their renderer (null outside a map).
    void bindScene(WorldFxSystem *pWorldFx, Engine::ModelRenderer *pModelRenderer);
    // This frame's portrait of the actor for a square of pixelSize HUD pixels, or an invalid handle when the actor
    // has no model. The texture's rows run bottom-up when the renderer's origin is bottom-left.
    bgfx::TextureHandle render(const IGameplayWorldRuntime &world, size_t actorIndex, float pixelSize,
        uint32_t animationTicks);
    void shutdown();

private:
    // Camera for a new portrait (WorldFxSystem::InspectPortrait framing), held while it plays.
    void frame(const std::vector<std::array<float, 3>> &points, size_t restPointCount);
    bool ensureTarget(uint16_t size);
    void destroyTarget();

    WorldFxSystem *m_pWorldFx = nullptr;
    Engine::ModelRenderer *m_pModelRenderer = nullptr;
    bgfx::FrameBufferHandle m_frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_colorTexture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_worldClipPlaneUniform = BGFX_INVALID_HANDLE;
    uint16_t m_size = 0;
    Engine::ModelInstanceHandle m_framedHandle;
    std::array<float, 16> m_view = {};
    std::array<float, 16> m_projection = {};
    std::array<float, 3> m_eye = {};
    bool m_failed = false;
};
}
