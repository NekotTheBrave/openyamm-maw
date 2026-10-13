#include "game/mod/ModRuntime.h"

#include "engine/ApplicationConfig.h"
#include "engine/AssetFileSystem.h"
#include "game/mod/ModBindings.h"

#include <SDL3/SDL.h>
#include <lua.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <system_error>

extern "C"
{
// Defined by the patched copy of lobject.c (cmake/Dependencies.cmake). Zero prints whole-valued floats as "5"
// rather than "5.0", which is what scripts written for Lua 5.1 expect.
extern int luaO_floatsuffix;
}

namespace OpenYAMM::Game
{
namespace
{
constexpr const char *BootScriptPath = "scripts/modrt/boot.lua";
constexpr const char *ConfigFileName = "mods.yml";
constexpr const char *LogDirectoryName = "logs";
constexpr const char *LogFileName = "mod_runtime.log";

ModRuntime *runtimeFromUpvalue(lua_State *pLuaState)
{
    return static_cast<ModRuntime *>(lua_touserdata(pLuaState, lua_upvalueindex(1)));
}

std::filesystem::path pathFromUtf8(const std::string &text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string utf8FromPath(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string lowerGenericPath(const std::filesystem::path &path)
{
    std::error_code errorCode;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, errorCode);

    if (errorCode)
    {
        normalized = path.lexically_normal();
    }

    std::string text = normalized.generic_string();

    for (char &character : text)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    while (text.size() > 1 && text.back() == '/')
    {
        text.pop_back();
    }

    return text;
}

std::optional<std::string> readPhysicalTextFile(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);

    if (!stream)
    {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string sanitizedReportName(const std::string &name)
{
    std::string result;

    for (char character : name)
    {
        const bool allowed = std::isalnum(static_cast<unsigned char>(character)) != 0
            || character == '_'
            || character == '-'
            || character == '.';
        result.push_back(allowed ? character : '_');
    }

    if (result.empty() || result.front() == '.')
    {
        result.insert(result.begin(), '_');
    }

    return result;
}

int luaTraceback(lua_State *pLuaState)
{
    const char *pMessage = lua_tostring(pLuaState, 1);

    if (pMessage == nullptr)
    {
        pMessage = luaL_tolstring(pLuaState, 1, nullptr);
    }

    luaL_traceback(pLuaState, pLuaState, pMessage, 1);
    return 1;
}

int nativeLog(lua_State *pLuaState)
{
    ModRuntime *pRuntime = runtimeFromUpvalue(pLuaState);
    const std::string level = luaL_checkstring(pLuaState, 1);
    const std::string text = luaL_checkstring(pLuaState, 2);
    pRuntime->writeLog(level, text);
    return 0;
}

int nativeReadAsset(lua_State *pLuaState)
{
    ModRuntime *pRuntime = runtimeFromUpvalue(pLuaState);
    const std::string virtualPath = luaL_checkstring(pLuaState, 1);
    const Engine::AssetFileSystem *pAssetFileSystem = pRuntime->assetFileSystem();

    if (pAssetFileSystem == nullptr)
    {
        lua_pushnil(pLuaState);
        return 1;
    }

    const std::optional<std::string> text = pAssetFileSystem->readTextFile(virtualPath);

    if (!text)
    {
        lua_pushnil(pLuaState);
        return 1;
    }

    lua_pushlstring(pLuaState, text->data(), text->size());
    return 1;
}

int nativeReadFile(lua_State *pLuaState)
{
    ModRuntime *pRuntime = runtimeFromUpvalue(pLuaState);
    const std::filesystem::path path = pathFromUtf8(luaL_checkstring(pLuaState, 1));

    if (!pRuntime->isReadablePath(path))
    {
        lua_pushnil(pLuaState);
        lua_pushstring(pLuaState, "path is outside the configured mod roots");
        return 2;
    }

    const std::optional<std::string> text = readPhysicalTextFile(path);

    if (!text)
    {
        lua_pushnil(pLuaState);
        lua_pushstring(pLuaState, "cannot open file");
        return 2;
    }

    lua_pushlstring(pLuaState, text->data(), text->size());
    return 1;
}

int nativeFileExists(lua_State *pLuaState)
{
    ModRuntime *pRuntime = runtimeFromUpvalue(pLuaState);
    const std::filesystem::path path = pathFromUtf8(luaL_checkstring(pLuaState, 1));
    std::error_code errorCode;
    const bool exists = pRuntime->isReadablePath(path) && std::filesystem::is_regular_file(path, errorCode);
    lua_pushboolean(pLuaState, exists);
    return 1;
}

int nativeListDirectory(lua_State *pLuaState)
{
    ModRuntime *pRuntime = runtimeFromUpvalue(pLuaState);
    const std::filesystem::path path = pathFromUtf8(luaL_checkstring(pLuaState, 1));
    lua_newtable(pLuaState);

    if (!pRuntime->isReadablePath(path))
    {
        return 1;
    }

    std::error_code errorCode;
    std::filesystem::directory_iterator iterator(path, errorCode);

    if (errorCode)
    {
        return 1;
    }

    lua_Integer index = 0;

    for (const std::filesystem::directory_entry &entry : iterator)
    {
        const std::string name = utf8FromPath(entry.path().filename());
        lua_newtable(pLuaState);
        lua_pushlstring(pLuaState, name.data(), name.size());
        lua_setfield(pLuaState, -2, "name");
        lua_pushboolean(pLuaState, entry.is_directory(errorCode));
        lua_setfield(pLuaState, -2, "isDirectory");
        lua_rawseti(pLuaState, -2, ++index);
    }

    return 1;
}

int nativeWriteReport(lua_State *pLuaState)
{
    const std::string name = sanitizedReportName(luaL_checkstring(pLuaState, 1));
    size_t textLength = 0;
    const char *pText = luaL_checklstring(pLuaState, 2, &textLength);
    const std::filesystem::path logDirectory = runtimeFromUpvalue(pLuaState)->logDirectory();
    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);
    const std::filesystem::path path = logDirectory / name;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);

    if (!stream)
    {
        lua_pushnil(pLuaState);
        return 1;
    }

    stream.write(pText, static_cast<std::streamsize>(textLength));
    const std::string writtenPath = std::filesystem::absolute(path, errorCode).generic_string();
    lua_pushlstring(pLuaState, writtenPath.data(), writtenPath.size());
    return 1;
}

int nativeClock(lua_State *pLuaState)
{
    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now().time_since_epoch();
    lua_pushnumber(pLuaState, std::chrono::duration<double>(elapsed).count());
    return 1;
}
}

ModRuntime::ModRuntime() = default;

ModRuntime::~ModRuntime()
{
    shutdown();
}

bool ModRuntime::isActive() const
{
    return m_active;
}

const Engine::AssetFileSystem *ModRuntime::assetFileSystem() const
{
    return m_pAssetFileSystem;
}

const std::filesystem::path &ModRuntime::logDirectory() const
{
    return m_logDirectory;
}

std::string ModRuntime::statusDescription() const
{
    if (m_active)
    {
        return "Mod runtime is active: " + std::to_string(m_mods.size()) + " mod(s) from "
            + m_configPath.generic_string();
    }

    if (!m_configPath.empty())
    {
        return "Mod runtime is not active: " + m_configPath.generic_string()
            + " was found but is disabled, lists no mods, or failed to start (see logs/mod_runtime.log).";
    }

    std::string text = "Mod runtime is not active: no mods.yml found. Looked in:";

    for (const std::filesystem::path &path : m_searchedConfigPaths)
    {
        text += "\n  " + path.generic_string();
    }

    return text;
}

std::filesystem::path ModRuntime::findConfigPath()
{
    m_searchedConfigPaths.clear();
    std::vector<std::filesystem::path> directories;
    std::error_code errorCode;

    // OPENYAMM_MODS_CONFIG names the file outright (tests use it to run with a fixed mod set).
    const char *pConfigOverride = SDL_getenv("OPENYAMM_MODS_CONFIG");

    if (pConfigOverride != nullptr && pConfigOverride[0] != '\0')
    {
        const std::filesystem::path overridePath = std::filesystem::absolute(pathFromUtf8(pConfigOverride), errorCode);
        m_searchedConfigPaths.push_back(overridePath);
        return std::filesystem::is_regular_file(overridePath, errorCode) ? overridePath : std::filesystem::path();
    }

    directories.push_back(std::filesystem::current_path(errorCode));

    if (const char *pBasePath = SDL_GetBasePath(); pBasePath != nullptr)
    {
        directories.push_back(pathFromUtf8(pBasePath));
    }

    if (m_pAssetFileSystem != nullptr && !m_pAssetFileSystem->getDevelopmentRoot().empty())
    {
        directories.push_back(std::filesystem::absolute(m_pAssetFileSystem->getDevelopmentRoot(), errorCode).parent_path());
    }

    for (const std::filesystem::path &directory : directories)
    {
        if (directory.empty())
        {
            continue;
        }

        const std::filesystem::path candidate = directory / ConfigFileName;
        const std::string candidateKey = lowerGenericPath(candidate);
        const bool alreadySearched = std::any_of(
            m_searchedConfigPaths.begin(),
            m_searchedConfigPaths.end(),
            [&candidateKey](const std::filesystem::path &searched)
            {
                return lowerGenericPath(searched) == candidateKey;
            });

        if (alreadySearched)
        {
            continue;
        }

        m_searchedConfigPaths.push_back(candidate);

        if (std::filesystem::is_regular_file(candidate, errorCode))
        {
            return candidate;
        }
    }

    return {};
}

bool ModRuntime::loadConfig(const std::filesystem::path &configPath, std::string &error)
{
    m_enabled = false;
    m_mods.clear();
    m_readRoots.clear();
    m_mergeBasePath.clear();

    std::error_code errorCode;

    if (!std::filesystem::exists(configPath, errorCode))
    {
        return true;
    }

    try
    {
        const YAML::Node root = YAML::LoadFile(configPath.string());

        if (!root || !root.IsMap())
        {
            error = "mods.yml root must be a map";
            return false;
        }

        m_enabled = root["enabled"] && root["enabled"].as<bool>();

        if (root["merge_base"] && root["merge_base"].IsScalar())
        {
            m_mergeBasePath = pathFromUtf8(root["merge_base"].as<std::string>());
            m_readRoots.push_back(m_mergeBasePath);
        }

        const YAML::Node modsNode = root["mods"];

        if (modsNode && modsNode.IsSequence())
        {
            for (const YAML::Node &modNode : modsNode)
            {
                if (!modNode.IsMap() || !modNode["id"] || !modNode["path"])
                {
                    error = "mods.yml: each mod needs an id and a path";
                    return false;
                }

                ModDefinition mod = {};
                mod.id = modNode["id"].as<std::string>();
                mod.rootPath = pathFromUtf8(modNode["path"].as<std::string>());

                if (mod.rootPath.is_relative())
                {
                    mod.rootPath = configPath.parent_path() / mod.rootPath;
                }

                if (modNode["base_overrides"] && modNode["base_overrides"].IsScalar())
                {
                    mod.baseOverrides = modNode["base_overrides"].as<std::string>();
                }

                if (modNode["skip"] && modNode["skip"].IsSequence())
                {
                    for (const YAML::Node &skipNode : modNode["skip"])
                    {
                        mod.skippedScripts.push_back(skipNode.as<std::string>());
                    }
                }

                if (!std::filesystem::is_directory(mod.rootPath, errorCode))
                {
                    error = "mods.yml: mod '" + mod.id + "' path is not a directory: " + mod.rootPath.string();
                    return false;
                }

                if (modNode["assets"] && modNode["assets"].IsScalar())
                {
                    mod.assetsPath = pathFromUtf8(modNode["assets"].as<std::string>());

                    if (mod.assetsPath.is_relative())
                    {
                        mod.assetsPath = configPath.parent_path() / mod.assetsPath;
                    }

                    if (!std::filesystem::is_directory(mod.assetsPath, errorCode))
                    {
                        error = "mods.yml: mod '" + mod.id + "' assets is not a directory: "
                            + mod.assetsPath.string();
                        return false;
                    }
                }

                m_readRoots.push_back(mod.rootPath);
                m_mods.push_back(std::move(mod));
            }
        }
    }
    catch (const std::exception &exception)
    {
        error = std::string("mods.yml: ") + exception.what();
        return false;
    }

    return true;
}

bool ModRuntime::isReadablePath(const std::filesystem::path &path) const
{
    const std::string candidate = lowerGenericPath(path);

    for (const std::filesystem::path &root : m_readRoots)
    {
        const std::string rootText = lowerGenericPath(root);

        if (candidate == rootText
            || (candidate.size() > rootText.size()
                && candidate.compare(0, rootText.size(), rootText) == 0
                && candidate[rootText.size()] == '/'))
        {
            return true;
        }
    }

    return false;
}

void ModRuntime::writeLog(const std::string &level, const std::string &text)
{
    if (m_logFile.is_open())
    {
        m_logFile << '[' << level << "] " << text << '\n';
        m_logFile.flush();
    }

    if (level == "error" || level == "warn")
    {
        std::cerr << "ModRuntime [" << level << "] " << text << '\n';
    }
}

void ModRuntime::registerNatives()
{
    struct NativeFunction
    {
        const char *pName;
        lua_CFunction function;
    };

    const NativeFunction natives[] = {
        {"log", nativeLog},
        {"readAsset", nativeReadAsset},
        {"readFile", nativeReadFile},
        {"fileExists", nativeFileExists},
        {"listDirectory", nativeListDirectory},
        {"writeReport", nativeWriteReport},
        {"clock", nativeClock},
    };

    lua_newtable(m_pLuaState);

    for (const NativeFunction &native : natives)
    {
        lua_pushlightuserdata(m_pLuaState, this);
        lua_pushcclosure(m_pLuaState, native.function, 1);
        lua_setfield(m_pLuaState, -2, native.pName);
    }

    pushConfig();
    lua_setfield(m_pLuaState, -2, "config");
    lua_setglobal(m_pLuaState, "modrt");
}

void ModRuntime::pushConfig()
{
    lua_newtable(m_pLuaState);
    const std::string mergeBase = m_mergeBasePath.generic_string();
    lua_pushlstring(m_pLuaState, mergeBase.data(), mergeBase.size());
    lua_setfield(m_pLuaState, -2, "mergeBase");
    lua_newtable(m_pLuaState);
    lua_Integer modIndex = 0;

    for (const ModDefinition &mod : m_mods)
    {
        lua_newtable(m_pLuaState);
        lua_pushlstring(m_pLuaState, mod.id.data(), mod.id.size());
        lua_setfield(m_pLuaState, -2, "id");
        const std::string rootPath = mod.rootPath.generic_string();
        lua_pushlstring(m_pLuaState, rootPath.data(), rootPath.size());
        lua_setfield(m_pLuaState, -2, "path");
        lua_pushlstring(m_pLuaState, mod.baseOverrides.data(), mod.baseOverrides.size());
        lua_setfield(m_pLuaState, -2, "baseOverrides");
        lua_newtable(m_pLuaState);
        lua_Integer skipIndex = 0;

        for (const std::string &skipped : mod.skippedScripts)
        {
            lua_pushlstring(m_pLuaState, skipped.data(), skipped.size());
            lua_rawseti(m_pLuaState, -2, ++skipIndex);
        }

        lua_setfield(m_pLuaState, -2, "skip");
        lua_rawseti(m_pLuaState, -2, ++modIndex);
    }

    lua_setfield(m_pLuaState, -2, "mods");
}

bool ModRuntime::runAssetChunk(const std::string &virtualPath)
{
    const std::optional<std::string> source = m_pAssetFileSystem->readTextFile(virtualPath);

    if (!source)
    {
        writeLog("error", "missing runtime script: " + virtualPath);
        return false;
    }

    const std::string chunkName = "@" + virtualPath;
    lua_pushcfunction(m_pLuaState, luaTraceback);
    const int tracebackIndex = lua_gettop(m_pLuaState);

    if (luaL_loadbuffer(m_pLuaState, source->data(), source->size(), chunkName.c_str()) != LUA_OK
        || lua_pcall(m_pLuaState, 0, 0, tracebackIndex) != LUA_OK)
    {
        const char *pMessage = lua_tostring(m_pLuaState, -1);
        writeLog("error", std::string("runtime script failed: ") + (pMessage != nullptr ? pMessage : "unknown"));
        lua_settop(m_pLuaState, tracebackIndex - 1);
        return false;
    }

    lua_settop(m_pLuaState, tracebackIndex - 1);
    return true;
}

bool ModRuntime::loadConfiguration()
{
    // OPENYAMM_DISABLE_MODS=1 runs the unmodded game without touching mods.yml (for before/after comparisons).
    const char *pDisableMods = SDL_getenv("OPENYAMM_DISABLE_MODS");

    if (pDisableMods != nullptr && pDisableMods[0] != '\0' && std::string(pDisableMods) != "0")
    {
        m_configPath.clear();
        m_searchedConfigPaths.clear();
        m_enabled = false;
        m_mods.clear();
        return true;
    }

    m_configPath = findConfigPath();

    if (m_configPath.empty())
    {
        m_enabled = false;
        m_mods.clear();
        return true;
    }

    std::string error;

    if (!loadConfig(m_configPath, error))
    {
        std::cerr << "ModRuntime: " << error << '\n';
        m_enabled = false;
        m_mods.clear();
        return false;
    }

    return true;
}

bool ModRuntime::configure(Engine::AssetFileSystem &assetFileSystem)
{
    m_pAssetFileSystem = &assetFileSystem;
    m_configured = loadConfiguration();

    if (!m_configured)
    {
        return false;
    }

    if (!m_enabled)
    {
        return true;
    }

    for (const ModDefinition &mod : m_mods)
    {
        if (mod.assetsPath.empty())
        {
            continue;
        }

        if (!assetFileSystem.addOverlayRoot(mod.assetsPath))
        {
            return false;
        }

        std::cout << "ModRuntime: mod '" << mod.id << "' assets mounted from " << mod.assetsPath.generic_string()
                  << '\n';
    }

    return true;
}

bool ModRuntime::initialize(const Engine::AssetFileSystem &assetFileSystem)
{
    shutdown();

    if (!m_configured || m_pAssetFileSystem != &assetFileSystem)
    {
        m_pAssetFileSystem = &assetFileSystem;
        m_configured = loadConfiguration();

        if (!m_configured)
        {
            return false;
        }
    }

    if (!m_enabled || m_mods.empty())
    {
        return true;
    }

    std::error_code errorCode;
    m_logDirectory = m_configPath.parent_path() / LogDirectoryName;
    std::filesystem::create_directories(m_logDirectory, errorCode);
    m_logFile.open(m_logDirectory / LogFileName, std::ios::trunc);
    std::cout << "ModRuntime: " << m_mods.size() << " mod(s) enabled by " << m_configPath.generic_string() << '\n';

    m_pLuaState = luaL_newstate();

    if (m_pLuaState == nullptr)
    {
        std::cerr << "ModRuntime: could not create Lua state\n";
        return false;
    }

    luaO_floatsuffix = 0;
    luaL_openlibs(m_pLuaState);
    registerNatives();
    registerModBindings(m_pLuaState, m_host);

    if (!runAssetChunk(BootScriptPath))
    {
        shutdown();
        return false;
    }

    lua_getglobal(m_pLuaState, "ModRT");

    if (!lua_istable(m_pLuaState, -1))
    {
        writeLog("error", "boot script did not define ModRT");
        shutdown();
        return false;
    }

    lua_getfield(m_pLuaState, -1, "dispatch");

    if (!lua_isfunction(m_pLuaState, -1))
    {
        writeLog("error", "boot script did not define ModRT.dispatch");
        shutdown();
        return false;
    }

    m_dispatchReference = luaL_ref(m_pLuaState, LUA_REGISTRYINDEX);
    lua_pop(m_pLuaState, 1);
    m_active = true;

    if (beginDispatch("start"))
    {
        endDispatch(0, 0);
    }

    return true;
}

void ModRuntime::shutdown()
{
    if (m_pLuaState != nullptr)
    {
        lua_close(m_pLuaState);
        m_pLuaState = nullptr;
        luaO_floatsuffix = 1;
    }

    m_dispatchReference = -1;
    m_active = false;

    if (m_logFile.is_open())
    {
        m_logFile.close();
    }
}

bool ModRuntime::beginDispatch(const char *pKind)
{
    if (!m_active || m_pLuaState == nullptr)
    {
        return false;
    }

    lua_pushcfunction(m_pLuaState, luaTraceback);
    m_tracebackIndex = lua_gettop(m_pLuaState);
    lua_rawgeti(m_pLuaState, LUA_REGISTRYINDEX, m_dispatchReference);
    lua_pushstring(m_pLuaState, pKind);
    return true;
}

bool ModRuntime::endDispatch(int argumentCount, int resultCount)
{
    const int status = lua_pcall(m_pLuaState, argumentCount + 1, resultCount, m_tracebackIndex);

    if (status != LUA_OK)
    {
        const char *pMessage = lua_tostring(m_pLuaState, -1);
        writeLog("error", std::string("dispatch failed: ") + (pMessage != nullptr ? pMessage : "unknown"));
        lua_settop(m_pLuaState, m_tracebackIndex - 1);
        return false;
    }

    lua_remove(m_pLuaState, m_tracebackIndex);
    return true;
}

void ModRuntime::setHost(ModHost host)
{
    m_host = std::move(host);
}

std::string ModRuntime::captureState()
{
    if (!beginDispatch("captureState") || !endDispatch(0, 1))
    {
        return {};
    }

    size_t length = 0;
    const char *pText = lua_tolstring(m_pLuaState, -1, &length);
    std::string state = pText != nullptr ? std::string(pText, length) : std::string();
    lua_pop(m_pLuaState, 1);
    return state;
}

void ModRuntime::restoreState(const std::string &state)
{
    if (!beginDispatch("restoreState"))
    {
        return;
    }

    lua_pushlstring(m_pLuaState, state.data(), state.size());
    endDispatch(1, 0);
}

void ModRuntime::fireEvent(const std::string &eventName)
{
    if (!beginDispatch("event"))
    {
        return;
    }

    lua_pushlstring(m_pLuaState, eventName.data(), eventName.size());
    endDispatch(1, 0);
}

void ModRuntime::tick(float deltaSeconds)
{
    if (!beginDispatch("tick"))
    {
        return;
    }

    lua_pushnumber(m_pLuaState, deltaSeconds);
    endDispatch(1, 0);
}

void ModRuntime::keyEvent(bool pressed, int32_t sdlKeycode, bool repeat)
{
    const int virtualKey = windowsVirtualKeyFromSdlKeycode(sdlKeycode);

    if (virtualKey == 0 || !beginDispatch("key"))
    {
        return;
    }

    lua_pushboolean(m_pLuaState, pressed);
    lua_pushinteger(m_pLuaState, virtualKey);
    lua_pushboolean(m_pLuaState, repeat);
    endDispatch(3, 0);
}

bool ModRuntime::executeConsole(const std::string &code, std::string &output)
{
    if (!beginDispatch("console"))
    {
        output = statusDescription();
        return false;
    }

    lua_pushlstring(m_pLuaState, code.data(), code.size());

    if (!endDispatch(1, 2))
    {
        output = "Console dispatch failed; see logs/mod_runtime.log.";
        return false;
    }

    const bool success = lua_toboolean(m_pLuaState, -2) != 0;
    size_t length = 0;
    const char *pText = lua_tolstring(m_pLuaState, -1, &length);
    output = pText != nullptr ? std::string(pText, length) : std::string();
    lua_pop(m_pLuaState, 2);
    return success;
}

// MMExtension scripts compare key events against Windows virtual-key codes.
int ModRuntime::windowsVirtualKeyFromSdlKeycode(int32_t sdlKeycode)
{
    if (sdlKeycode >= SDLK_A && sdlKeycode <= SDLK_Z)
    {
        return 'A' + (sdlKeycode - SDLK_A);
    }

    if (sdlKeycode >= SDLK_0 && sdlKeycode <= SDLK_9)
    {
        return '0' + (sdlKeycode - SDLK_0);
    }

    if (sdlKeycode >= SDLK_F1 && sdlKeycode <= SDLK_F12)
    {
        return 0x70 + (sdlKeycode - SDLK_F1);
    }

    if (sdlKeycode >= SDLK_KP_1 && sdlKeycode <= SDLK_KP_9)
    {
        return 0x61 + (sdlKeycode - SDLK_KP_1);
    }

    switch (sdlKeycode)
    {
        case SDLK_BACKSPACE: return 0x08;
        case SDLK_TAB: return 0x09;
        case SDLK_RETURN: return 0x0D;
        case SDLK_KP_ENTER: return 0x0D;
        case SDLK_LSHIFT: return 0x10;
        case SDLK_RSHIFT: return 0x10;
        case SDLK_LCTRL: return 0x11;
        case SDLK_RCTRL: return 0x11;
        case SDLK_LALT: return 0x12;
        case SDLK_RALT: return 0x12;
        case SDLK_PAUSE: return 0x13;
        case SDLK_CAPSLOCK: return 0x14;
        case SDLK_ESCAPE: return 0x1B;
        case SDLK_SPACE: return 0x20;
        case SDLK_PAGEUP: return 0x21;
        case SDLK_PAGEDOWN: return 0x22;
        case SDLK_END: return 0x23;
        case SDLK_HOME: return 0x24;
        case SDLK_LEFT: return 0x25;
        case SDLK_UP: return 0x26;
        case SDLK_RIGHT: return 0x27;
        case SDLK_DOWN: return 0x28;
        case SDLK_INSERT: return 0x2D;
        case SDLK_DELETE: return 0x2E;
        case SDLK_KP_0: return 0x60;
        case SDLK_KP_MULTIPLY: return 0x6A;
        case SDLK_KP_PLUS: return 0x6B;
        case SDLK_KP_MINUS: return 0x6D;
        case SDLK_KP_PERIOD: return 0x6E;
        case SDLK_KP_DIVIDE: return 0x6F;
        case SDLK_SEMICOLON: return 0xBA;
        case SDLK_EQUALS: return 0xBB;
        case SDLK_COMMA: return 0xBC;
        case SDLK_MINUS: return 0xBD;
        case SDLK_PERIOD: return 0xBE;
        case SDLK_SLASH: return 0xBF;
        case SDLK_GRAVE: return 0xC0;
        case SDLK_LEFTBRACKET: return 0xDB;
        case SDLK_BACKSLASH: return 0xDC;
        case SDLK_RIGHTBRACKET: return 0xDD;
        case SDLK_APOSTROPHE: return 0xDE;
        default: return 0;
    }
}
}
