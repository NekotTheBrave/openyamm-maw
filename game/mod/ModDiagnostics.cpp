#include "game/mod/ModRuntime.h"

#include "engine/ApplicationConfig.h"
#include "engine/AssetFileSystem.h"
#include "game/data/GameDataLoader.h"
#include "game/tables/ItemTable.h"
#include "game/tables/MonsterTable.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace OpenYAMM::Game
{
// Prints table rows as the game sees them once mod asset overlays are mounted, without opening a window.
// Probes are "item:<id>" and "monster:<id>". Set OPENYAMM_DISABLE_MODS=1 to see the unmodded values.
int runModTableProbe(
    const std::filesystem::path &basePath,
    const Engine::ApplicationConfig &config,
    const std::vector<std::string> &probes)
{
    Engine::AssetFileSystem assetFileSystem;

    if (!assetFileSystem.initialize(
            basePath,
            config.assetRoot,
            config.assetScaleTier,
            config.assetScaleProfile,
            config.activeWorldId))
    {
        std::cerr << "Table probe failed: could not initialize asset file system\n";
        return 1;
    }

    ModRuntime runtime;

    if (!runtime.configure(assetFileSystem))
    {
        return 1;
    }

    GameDataLoader gameDataLoader;

    if (!gameDataLoader.loadForHeadlessGameplay(assetFileSystem))
    {
        std::cerr << "Table probe failed: could not load game tables\n";
        return 1;
    }

    bool allFound = true;

    for (const std::string &probe : probes)
    {
        const size_t separator = probe.find(':');

        if (separator == std::string::npos)
        {
            std::cerr << "Table probe: expected item:<id> or monster:<id>, got " << probe << '\n';
            return 2;
        }

        const std::string kind = probe.substr(0, separator);
        const int id = std::stoi(probe.substr(separator + 1));

        if (kind == "item")
        {
            const ItemDefinition *pItem = gameDataLoader.getItemTable().get(static_cast<uint32_t>(id));

            if (pItem == nullptr)
            {
                std::cout << "item " << id << ": not found\n";
                allFound = false;
                continue;
            }

            std::cout << "item " << id << ": name=\"" << pItem->name << "\" value=" << pItem->value
                      << " equip=\"" << pItem->equipStat << "\" skill=\"" << pItem->skillGroup
                      << "\" mod1=\"" << pItem->mod1 << "\" mod2=\"" << pItem->mod2 << "\"\n";
        }
        else if (kind == "monster")
        {
            const MonsterTable::MonsterStatsEntry *pStats =
                gameDataLoader.getMonsterTable().findStatsById(static_cast<int16_t>(id));

            if (pStats == nullptr)
            {
                std::cout << "monster " << id << ": not found\n";
                allFound = false;
                continue;
            }

            std::cout << "monster " << id << ": name=\"" << pStats->name << "\" level=" << pStats->level
                      << " hp=" << pStats->hitPoints << " ac=" << pStats->armorClass
                      << " exp=" << pStats->experience << '\n';
        }
        else
        {
            std::cerr << "Table probe: unknown kind " << kind << '\n';
            return 2;
        }
    }

    return allFound ? 0 : 1;
}
}
