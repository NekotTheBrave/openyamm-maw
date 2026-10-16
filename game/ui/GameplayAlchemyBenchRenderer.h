#pragma once

namespace OpenYAMM::Game
{
class GameplayScreenRuntime;

class GameplayAlchemyBenchRenderer
{
public:
    static void render(GameplayScreenRuntime &context, int width, int height);
};
}
