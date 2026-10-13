#pragma once

#include "game/mod/ModHooks.h"
#include "game/mod/ModHost.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace OpenYAMM::Engine
{
class AssetFileSystem;
struct ApplicationConfig;
}

namespace OpenYAMM::Game
{
// Hosts MMExtension-style Lua mods in their own Lua state, separate from the sandboxed map-event state.
// The host is inert unless mods.yml enables it, so an unconfigured game pays nothing for it.
class ModRuntime : public IModHooks
{
public:
    struct ModDefinition
    {
        std::string id;
        std::filesystem::path rootPath;
        // Optional folder mounted over the engine assets (table overrides built by tools/maw/convert_tables.py).
        std::filesystem::path assetsPath;
        std::string baseOverrides = "skip";
        std::vector<std::string> skippedScripts;
        // Optional folder shaped like the mod (Scripts/...): a script there replaces the mod's script of the same
        // path, or is added to the load order. For OpenYAMM versions of files that cannot run as shipped.
        std::filesystem::path portPath;
        // Free-form values from mods.yml, handed to the scripts as ModSettings.
        std::vector<std::pair<std::string, std::string>> settings;
    };

    ModRuntime();
    ~ModRuntime();

    ModRuntime(const ModRuntime &) = delete;
    ModRuntime &operator=(const ModRuntime &) = delete;

    // Looks for mods.yml in the working directory, next to the executable, then beside the development asset root,
    // so the game finds it however it was launched. Logs and reports go to a logs folder next to that file.
    bool initialize(const Engine::AssetFileSystem &assetFileSystem);
    // Reads mods.yml and mounts each enabled mod's asset overlay. Call before game tables are loaded; the Lua
    // side is started later by initialize().
    bool configure(Engine::AssetFileSystem &assetFileSystem);
    void shutdown();
    bool isActive() const;
    // Game state the bindings may reach. Set once, before initialize().
    void setHost(ModHost host);
    const ModHost &host() const;

    // Mod state that belongs to a save (Lua vars and mapvars). An empty string restores a clean state.
    std::string captureState();
    void restoreState(const std::string &state);

    int calcDamageToMonster(const ModDamageToMonsterEvent &event) override;
    void adjustCharacterBonuses(
        Party &party,
        size_t memberIndex,
        const CharacterStatBonuses &buffBonuses) override;
    int statisticEffect(int value, int engineEffect) override;
    int attackDelay(const Character &character, bool ranged, int engineTicks) override;
    int skillStatBonus(const Character &character, int statId, int engineBonus) override;

    void fireEvent(const std::string &eventName);
    void tick(float deltaSeconds);
    void keyEvent(bool pressed, int32_t sdlKeycode, bool repeat);
    bool executeConsole(const std::string &code, std::string &output);

    // One line saying whether mods are running and, if not, where mods.yml was looked for.
    std::string statusDescription() const;
    const std::filesystem::path &logDirectory() const;
    // Folder where mods may create and change files (their settings and caches); empty when no mod is enabled.
    const std::filesystem::path &dataDirectory() const;

    void writeLog(const std::string &level, const std::string &text);
    bool isReadablePath(const std::filesystem::path &path) const;
    const Engine::AssetFileSystem *assetFileSystem() const;

    static int windowsVirtualKeyFromSdlKeycode(int32_t sdlKeycode);

private:
    std::filesystem::path findConfigPath();
    bool loadConfiguration();
    bool loadConfig(const std::filesystem::path &configPath, std::string &error);
    void registerNatives();
    void pushConfig();
    bool runAssetChunk(const std::string &virtualPath);
    bool beginDispatch(const char *pKind);
    bool endDispatch(int argumentCount, int resultCount);

    lua_State *m_pLuaState = nullptr;
    const Engine::AssetFileSystem *m_pAssetFileSystem = nullptr;
    std::vector<ModDefinition> m_mods;
    std::filesystem::path m_mergeBasePath;
    ModHost m_host;
    std::filesystem::path m_configPath;
    std::filesystem::path m_logDirectory = "logs";
    std::filesystem::path m_dataDirectory;
    std::vector<std::filesystem::path> m_searchedConfigPaths;
    std::vector<std::filesystem::path> m_readRoots;
    std::ofstream m_logFile;
    int m_dispatchReference = -1;
    bool m_adjustingBonuses = false;
    // Statistic effects already asked of the scripts this frame. The engine asks for the same few values many
    // times while drawing one frame, and the scripts answer from a formula.
    std::unordered_map<int, int> m_statisticEffects;
    int m_attackDelayDepth = 0;
    int m_skillStatDepth = 0;
    bool m_enabled = false;
    bool m_configured = false;
    bool m_active = false;
};

int runModTableProbe(
    const std::filesystem::path &basePath,
    const Engine::ApplicationConfig &config,
    const std::vector<std::string> &probes);

int runModRuntimeHeadlessReport(
    const std::filesystem::path &basePath,
    const Engine::ApplicationConfig &config,
    int tickCount,
    const std::string &consoleCode);
}
