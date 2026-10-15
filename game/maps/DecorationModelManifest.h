#pragma once

#include <string>
#include <vector>

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

namespace YAML
{
class Node;
}

namespace OpenYAMM::Game
{
// The decoration model manifest of a map: worlds/<world>/models/decorations.yml for outdoor maps and
// indoor_decorations.yml for dungeons (decorations.yml is a dependency of every baked outdoor map).
std::string decorationModelManifestPath(const std::string &worldId, const std::string &mapFile);
// Whether a manifest entry binds on this map: entries without maps bind on every map of their kind.
bool decorationModelBindsOnMap(const YAML::Node &entry, const std::string &mapFile);
// Lowercase names of the decorations DecorationModelSet draws as models on this map, without loading the models: the
// map loader skips their sprites. Absent manifests bind nothing.
bool modelledDecorationNames(const Engine::AssetFileSystem &assets, const std::string &worldId,
    const std::string &mapFile, std::vector<std::string> &names, std::string &error);
}
