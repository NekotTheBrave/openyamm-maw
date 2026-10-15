#include "game/maps/DecorationModelManifest.h"

#include "engine/AssetFileSystem.h"
#include "game/StringUtils.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <optional>

namespace OpenYAMM::Game
{
std::string decorationModelManifestPath(const std::string &worldId, const std::string &mapFile)
{
    const bool indoor = toLowerCopy(mapFile).ends_with(".blv");
    return "worlds/" + worldId + "/models/" + (indoor ? "indoor_decorations.yml" : "decorations.yml");
}

bool decorationModelBindsOnMap(const YAML::Node &entry, const std::string &mapFile)
{
    if (!entry["maps"])
    {
        return true;
    }
    const std::string lowerMap = toLowerCopy(mapFile);
    const std::vector<std::string> maps = entry["maps"].as<std::vector<std::string>>();
    return std::any_of(maps.begin(), maps.end(),
        [&](const std::string &map) { return toLowerCopy(map) == lowerMap; });
}

bool modelledDecorationNames(const Engine::AssetFileSystem &assets, const std::string &worldId,
    const std::string &mapFile, std::vector<std::string> &names, std::string &error)
{
    names.clear();
    const std::string manifestPath = decorationModelManifestPath(worldId, mapFile);
    const std::optional<std::string> text = assets.readTextFile(manifestPath);
    if (!text)
    {
        return true;
    }
    try
    {
        const YAML::Node document = YAML::Load(*text);
        if (!document["decorations"].IsSequence())
        {
            error = "decoration model manifest requires a decorations sequence: " + manifestPath;
            return false;
        }
        for (const YAML::Node &entry : document["decorations"])
        {
            if (decorationModelBindsOnMap(entry, mapFile))
            {
                names.push_back(toLowerCopy(entry["name"].as<std::string>()));
            }
        }
    }
    catch (const YAML::Exception &exception)
    {
        error = manifestPath + ": " + exception.what();
        return false;
    }
    return true;
}
}
