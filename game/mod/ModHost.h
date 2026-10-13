#pragma once

#include <functional>
#include <string>

namespace OpenYAMM::Game
{
class GameDataLoader;
class IGameplayWorldRuntime;
class Party;

// What the mod runtime may reach in the running game. The application fills this in; every accessor may return
// null (or an empty value) when there is no session yet, and bindings treat that as "nothing there".
struct ModHost
{
    std::function<Party *()> party;
    std::function<IGameplayWorldRuntime *()> world;
    std::function<const GameDataLoader *()> data;
    std::function<std::string()> currentMapFileName;
    std::function<float()> gameMinutes;
    std::function<void(const std::string &)> showStatusText;
};
}
