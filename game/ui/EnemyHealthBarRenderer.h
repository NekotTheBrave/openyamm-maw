#pragma once

#include <bx/math.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace OpenYAMM::Game
{
class GameplayScreenRuntime;

class EnemyHealthBarRenderer
{
public:
    struct Draw
    {
        size_t actorIndex = 0;
        bx::Vec3 actorPosition = {0.0f, 0.0f, 0.0f};
        bx::Vec3 anchor = {0.0f, 0.0f, 0.0f};
        float distanceSquared = 0.0f;
        bool focused = false;
        bool attackingParty = false;
    };

    static void preload(const GameplayScreenRuntime &context);
    // The bars blend without writing depth, so they are submitted after every world model and effect;
    // anything shaded later would paint over them, however far behind the bar it lies.
    static void render(const GameplayScreenRuntime &context, const std::vector<Draw> &draws, uint16_t viewId,
        const bx::Vec3 &cameraPosition, const float *pViewMatrix, const float *pProjectionMatrix, int height);

private:
    static void render(const GameplayScreenRuntime &context, const Draw &draw, uint16_t viewId,
        const bx::Vec3 &cameraPosition, const float *pViewMatrix, const float *pProjectionMatrix, int height);
};
}
