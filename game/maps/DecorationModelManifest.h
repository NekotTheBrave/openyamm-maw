#pragma once

#include <string>
#include <unordered_set>
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
// Decoration model manifests are engine content, so any world can show any world's decoration models:
// engine/models/decorations.yml (outdoor maps), indoor_decorations.yml (dungeons; decorations.yml is a dependency of
// baked outdoor maps) and decoration_overlays.yml. A world extends them with a manifest of the same name in
// worlds/<world>/models/: its entry for a name replaces every engine entry of that name, and an entry whose model (or
// overlay states) is null keeps that decoration a sprite in the world.
// The manifest paths a map reads, engine first: the outdoor or dungeon manifest, or the overlay manifest.
std::vector<std::string> decorationModelManifestPaths(const std::string &worldId, const std::string &mapFile);
std::vector<std::string> decorationOverlayManifestPaths(const std::string &worldId);
// Whether a manifest entry binds on this map: entries without maps bind on every map of their kind.
bool decorationModelBindsOnMap(const YAML::Node &entry, const std::string &mapFile);
// The effective model bindings of a map (engine entries with the world's replacements, filtered by maps).
bool decorationModelEntries(const Engine::AssetFileSystem &assets, const std::string &worldId,
    const std::string &mapFile, std::vector<YAML::Node> &entries, std::string &error);
// The effective state overlays of a world.
bool decorationOverlayEntries(const Engine::AssetFileSystem &assets, const std::string &worldId,
    std::vector<YAML::Node> &entries, std::string &error);
// Lowercase names of the decorations DecorationModelSet draws as models on this map, without loading the models: the
// map loader skips their sprites. Absent manifests bind nothing.
bool modelledDecorationNames(const Engine::AssetFileSystem &assets, const std::string &worldId,
    const std::string &mapFile, std::vector<std::string> &names, std::string &error);
// Lowercase decoration names that the map's event scripts (worlds/<world>/events/maps/<map>.lua and its overlays)
// switch decorations to with SetSprite string literals: the models a map may show beyond its own placements.
std::unordered_set<std::string> mapEventSpriteNames(const Engine::AssetFileSystem &assets, const std::string &worldId,
    const std::string &mapFile);
}
