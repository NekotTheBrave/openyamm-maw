#pragma once

namespace OpenYAMM::Game
{
struct GameplayInputFrame;
class GameplayScreenRuntime;

class GameplayAlchemyBenchInputController
{
public:
    // The bench is modal inside the character screen: it takes all character input while open.
    static void handleInput(GameplayScreenRuntime &context, const GameplayInputFrame &input);
};
}
