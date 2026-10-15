#include "game/gameplay/ActorBehaviorCatalog.h"

#include "engine/AssetFileSystem.h"

#include <yaml-cpp/yaml.h>

#include <exception>
#include <optional>

namespace OpenYAMM::Game
{
namespace
{
constexpr const char *CatalogSchema = "openyamm.actorBehaviors.v1";
}

bool ActorBehaviorCatalog::load(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &worldId,
    std::string &error)
{
    clear();
    m_worldId = worldId;

    if (!loadDocument(assetFileSystem, "engine/behaviors/behaviors.yml", true, error))
    {
        return false;
    }

    if (!worldId.empty() && !loadDocument(assetFileSystem, "worlds/" + worldId + "/behaviors.yml", false, error))
    {
        return false;
    }

    for (const auto &[monsterId, behaviorId] : m_monsterDefaults)
    {
        if (!m_behaviors.contains(behaviorId))
        {
            error = "monster " + std::to_string(monsterId) + " uses unknown actor behaviour: " + behaviorId;
            return false;
        }
    }

    return true;
}

bool ActorBehaviorCatalog::loadDocument(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &virtualPath,
    bool required,
    std::string &error)
{
    const std::optional<std::string> text = assetFileSystem.readTextFile(virtualPath);

    if (!text)
    {
        if (required)
        {
            error = "missing actor behaviour catalogue: " + virtualPath;
            return false;
        }

        return true;
    }

    try
    {
        const YAML::Node root = YAML::Load(*text);

        if (root["schema"].as<std::string>("") != CatalogSchema)
        {
            error = virtualPath + ": expected schema " + CatalogSchema;
            return false;
        }

        const auto readSource =
            [&](const std::string &sourcePath, std::string &source)
            {
                const std::optional<std::string> sourceText = assetFileSystem.readTextFile(sourcePath);

                if (!sourceText)
                {
                    error = virtualPath + ": missing behaviour script " + sourcePath;
                    return false;
                }

                source = *sourceText;
                return true;
            };

        if (root["library"])
        {
            const std::string sourcePath = root["library"].as<std::string>();
            std::string source;

            if (!readSource(sourcePath, source))
            {
                return false;
            }

            setLibrary(sourcePath, source);
        }

        for (const auto &entry : root["behaviors"])
        {
            const std::string id = entry.first.as<std::string>();
            const std::string sourcePath = entry.second.as<std::string>();
            std::string source;

            if (id.empty() || !readSource(sourcePath, source))
            {
                if (error.empty())
                {
                    error = virtualPath + ": behaviour ids must not be empty";
                }

                return false;
            }

            addBehavior(id, sourcePath, source);
        }

        for (const auto &entry : root["monsters"])
        {
            const int monsterId = entry.first.as<int>();

            if (monsterId <= 0 || monsterId > 32767)
            {
                error = virtualPath + ": invalid monster id " + std::to_string(monsterId);
                return false;
            }

            setMonsterDefault(static_cast<int16_t>(monsterId), entry.second.as<std::string>());
        }
    }
    catch (const std::exception &exception)
    {
        error = virtualPath + ": " + exception.what();
        return false;
    }

    return true;
}

void ActorBehaviorCatalog::clear()
{
    m_worldId.clear();
    m_librarySourcePath.clear();
    m_librarySource.clear();
    m_behaviors.clear();
    m_monsterDefaults.clear();
}

void ActorBehaviorCatalog::setLibrary(const std::string &sourcePath, const std::string &source)
{
    m_librarySourcePath = sourcePath;
    m_librarySource = source;
}

void ActorBehaviorCatalog::addBehavior(const std::string &id, const std::string &sourcePath, const std::string &source)
{
    m_behaviors[id] = ActorBehaviorDefinition{id, sourcePath, source};
}

void ActorBehaviorCatalog::setMonsterDefault(int16_t monsterId, const std::string &behaviorId)
{
    m_monsterDefaults[monsterId] = behaviorId;
}

const std::string &ActorBehaviorCatalog::worldId() const
{
    return m_worldId;
}

const std::string &ActorBehaviorCatalog::librarySourcePath() const
{
    return m_librarySourcePath;
}

const std::string &ActorBehaviorCatalog::librarySource() const
{
    return m_librarySource;
}

const ActorBehaviorDefinition *ActorBehaviorCatalog::findBehavior(const std::string &id) const
{
    const auto iterator = m_behaviors.find(id);
    return iterator != m_behaviors.end() ? &iterator->second : nullptr;
}

const std::string *ActorBehaviorCatalog::defaultBehaviorForMonster(int16_t monsterId) const
{
    const auto iterator = m_monsterDefaults.find(monsterId);
    return iterator != m_monsterDefaults.end() ? &iterator->second : nullptr;
}

bool ActorBehaviorCatalog::empty() const
{
    return m_behaviors.empty();
}

bool ActorBehaviorCatalog::hasMonsterDefaults() const
{
    return !m_monsterDefaults.empty();
}
}
