#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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
class ModRuntime
{
public:
    struct ModDefinition
    {
        std::string id;
        std::filesystem::path rootPath;
        std::string baseOverrides = "skip";
        std::vector<std::string> skippedScripts;
    };

    ModRuntime();
    ~ModRuntime();

    ModRuntime(const ModRuntime &) = delete;
    ModRuntime &operator=(const ModRuntime &) = delete;

    // Looks for mods.yml in the working directory, next to the executable, then beside the development asset root,
    // so the game finds it however it was launched. Logs and reports go to a logs folder next to that file.
    bool initialize(const Engine::AssetFileSystem &assetFileSystem);
    void shutdown();
    bool isActive() const;

    void fireEvent(const std::string &eventName);
    void tick(float deltaSeconds);
    void keyEvent(bool pressed, int32_t sdlKeycode, bool repeat);
    bool executeConsole(const std::string &code, std::string &output);

    // One line saying whether mods are running and, if not, where mods.yml was looked for.
    std::string statusDescription() const;
    const std::filesystem::path &logDirectory() const;

    void writeLog(const std::string &level, const std::string &text);
    bool isReadablePath(const std::filesystem::path &path) const;
    const Engine::AssetFileSystem *assetFileSystem() const;

    static int windowsVirtualKeyFromSdlKeycode(int32_t sdlKeycode);

private:
    std::filesystem::path findConfigPath();
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
    std::filesystem::path m_configPath;
    std::filesystem::path m_logDirectory = "logs";
    std::vector<std::filesystem::path> m_searchedConfigPaths;
    std::vector<std::filesystem::path> m_readRoots;
    std::ofstream m_logFile;
    int m_dispatchReference = -1;
    int m_tracebackIndex = 0;
    bool m_enabled = false;
    bool m_active = false;
};

int runModRuntimeHeadlessReport(
    const std::filesystem::path &basePath,
    const Engine::ApplicationConfig &config,
    int tickCount,
    const std::string &consoleCode);
}
