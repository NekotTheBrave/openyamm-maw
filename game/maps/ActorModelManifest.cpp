#include "game/maps/ActorModelManifest.h"

#include "engine/AssetFileSystem.h"
#include "game/tables/MonsterTable.h"

#include <optional>
#include <unordered_set>

namespace OpenYAMM::Game
{
bool ActorModelManifest::load(const Engine::AssetFileSystem &assets, const std::vector<std::string> &paths,
    const std::string &activePath, const MonsterTable &monsters, std::string &error)
{
    m_entries.clear();
    m_boundMonsterIds.clear();
    const auto read = [&](const std::string &path, bool active)
    {
        const std::optional<std::string> text = assets.readTextFile(path);
        if (!text)
        {
            return true;
        }
        try
        {
            const YAML::Node document = YAML::Load(*text);
            if (!document["actors"].IsSequence())
            {
                error = "actor model manifest requires an actors sequence: " + path;
                return false;
            }
            std::unordered_set<std::string> fileDescriptors;
            for (const YAML::Node &entry : document["actors"])
            {
                const std::string descriptor = entry["descriptor"].as<std::string>();
                const MonsterEntry *pMonster = monsters.findByInternalName(descriptor);
                if (pMonster == nullptr || !fileDescriptors.insert(pMonster->internalName).second)
                {
                    error = path + ": unknown or duplicate actor model descriptor: " + descriptor;
                    return false;
                }
                const auto [iterator, inserted] = m_entries.try_emplace(pMonster->internalName, Entry{entry, path});
                if (!inserted)
                {
                    if (!active)
                    {
                        error = "actor model descriptor bound by " + iterator->second.manifestPath + " and " + path
                            + ": " + descriptor;
                        return false;
                    }
                    iterator->second = Entry{entry, path};
                }
            }
        }
        catch (const YAML::Exception &exception)
        {
            error = path + ": " + exception.what();
            return false;
        }
        return true;
    };
    for (const std::string &path : paths)
    {
        if (path != activePath && !read(path, false))
        {
            m_entries.clear();
            return false;
        }
    }
    if (!activePath.empty() && !read(activePath, true))
    {
        m_entries.clear();
        return false;
    }
    // Monster ids index the table's entries (MonsterTable::findById).
    const std::vector<MonsterEntry> &monsterEntries = monsters.getEntries();
    m_boundMonsterIds.resize(monsterEntries.size());
    for (size_t monsterId = 0; monsterId < monsterEntries.size(); ++monsterId)
    {
        m_boundMonsterIds[monsterId] = m_entries.contains(monsterEntries[monsterId].internalName);
    }
    return true;
}

const ActorModelManifest::Entry *ActorModelManifest::find(const std::string &descriptor) const
{
    const auto iterator = m_entries.find(descriptor);
    return iterator != m_entries.end() ? &iterator->second : nullptr;
}
}
