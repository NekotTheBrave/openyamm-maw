#pragma once

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

namespace OpenYAMM::Game
{
class MonsterTable;

// Actor model bindings (worlds/<world>/models/actors.yml): the monster descriptors drawn as 3D models. Descriptors are
// global, so every mounted world contributes its bindings to all worlds (an MM9 map spawning an MM6 monster shows the
// MM6 model). A world binds a descriptor once; the active world's binding replaces another world's, and two other
// worlds binding one descriptor is an error. Loading only indexes the entries: WorldFxSystem builds a binding (loads
// its model) when a map first needs it, and the map loader skips the sprites of every bound monster.
class ActorModelManifest
{
public:
    struct Entry
    {
        YAML::Node node;
        std::string manifestPath;
    };

    // Reads the existing manifests among paths, then activePath (empty for none), whose bindings replace theirs.
    bool load(const Engine::AssetFileSystem &assets, const std::vector<std::string> &paths,
        const std::string &activePath, const MonsterTable &monsters, std::string &error);
    // Keyed by the monster table's internal name.
    const Entry *find(const std::string &descriptor) const;
    bool binds(const std::string &descriptor) const
    {
        return m_entries.contains(descriptor);
    }
    // The same by monster id (MonsterTable::findById), for per-frame checks.
    bool bindsMonster(int16_t monsterId) const
    {
        return monsterId >= 0 && size_t(monsterId) < m_boundMonsterIds.size() && m_boundMonsterIds[size_t(monsterId)];
    }
    const std::unordered_map<std::string, Entry> &entries() const
    {
        return m_entries;
    }

private:
    std::unordered_map<std::string, Entry> m_entries;
    std::vector<bool> m_boundMonsterIds;
};
}
