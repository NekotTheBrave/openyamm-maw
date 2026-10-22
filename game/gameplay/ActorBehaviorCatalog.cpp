#include "game/gameplay/ActorBehaviorCatalog.h"

#include "engine/AssetFileSystem.h"

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <exception>
#include <numbers>
#include <optional>
#include <vector>

namespace OpenYAMM::Game
{
namespace
{
constexpr const char *CatalogSchema = "openyamm.actorBehaviors.v1";
constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0f;
// Without fire_within_degrees a traversing actor fires once it is this close to facing its target.
constexpr float DefaultFireToleranceDegrees = 6.0f;
constexpr float MaximumTacticDistance = 10240.0f;

// One `tactics` entry: traverse_degrees_per_second, fire_within_degrees, ranged_band: [min, max] and post: {leash}.
bool parseTactics(const YAML::Node &node, ActorTacticsFacts &tactics, std::string &error)
{
    if (!node.IsMap())
    {
        error = "expected a map of tactic options";
        return false;
    }

    for (const auto &option : node)
    {
        const std::string key = option.first.as<std::string>();

        if (key != "traverse_degrees_per_second" && key != "fire_within_degrees" && key != "ranged_band"
            && key != "post")
        {
            error = "unknown tactic option " + key;
            return false;
        }
    }

    const auto finiteIn = [](float value, float low, float high)
    {
        return std::isfinite(value) && value >= low && value <= high;
    };

    if (node["traverse_degrees_per_second"])
    {
        const float degrees = node["traverse_degrees_per_second"].as<float>();
        const float tolerance = node["fire_within_degrees"].as<float>(DefaultFireToleranceDegrees);

        if (!finiteIn(degrees, 1.0f, 3600.0f) || !finiteIn(tolerance, 0.0f, 180.0f))
        {
            error = "traverse_degrees_per_second must be 1-3600 and fire_within_degrees 0-180";
            return false;
        }

        tactics.traverseRadiansPerSecond = degrees * DegreesToRadians;
        tactics.fireToleranceRadians = tolerance * DegreesToRadians;
    }
    else if (node["fire_within_degrees"])
    {
        error = "fire_within_degrees requires traverse_degrees_per_second";
        return false;
    }

    if (node["ranged_band"])
    {
        const std::vector<float> band = node["ranged_band"].as<std::vector<float>>();

        if (band.size() != 2 || !finiteIn(band[0], 0.0f, MaximumTacticDistance)
            || !finiteIn(band[1], band[0] + 1.0f, MaximumTacticDistance))
        {
            error = "ranged_band must be [min, max] edge distances with 0 <= min < max";
            return false;
        }

        tactics.rangedMinDistance = band[0];
        tactics.rangedMaxDistance = band[1];
    }

    if (const YAML::Node post = node["post"])
    {
        if (!post.IsMap() || (post.size() > 0 && (post.size() != 1 || !post["leash"])))
        {
            error = "post takes only leash";
            return false;
        }

        tactics.holdPost = true;
        tactics.postLeashRadius = post["leash"].as<float>(0.0f);

        if (!finiteIn(tactics.postLeashRadius, 0.0f, MaximumTacticDistance))
        {
            error = "post leash must be 0-10240";
            return false;
        }
    }

    return true;
}
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

        for (const auto &entry : root["tactics"])
        {
            const int monsterId = entry.first.as<int>();
            ActorTacticsFacts tactics = {};
            std::string tacticsError;

            if (monsterId <= 0 || monsterId > 32767)
            {
                error = virtualPath + ": invalid tactics monster id " + std::to_string(monsterId);
                return false;
            }

            if (!parseTactics(entry.second, tactics, tacticsError))
            {
                error = virtualPath + ": tactics of monster " + std::to_string(monsterId) + ": " + tacticsError;
                return false;
            }

            setMonsterTactics(static_cast<int16_t>(monsterId), tactics);
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
    m_monsterTactics.clear();
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

void ActorBehaviorCatalog::setMonsterTactics(int16_t monsterId, const ActorTacticsFacts &tactics)
{
    m_monsterTactics[monsterId] = tactics;
}

const ActorTacticsFacts *ActorBehaviorCatalog::tacticsForMonster(int16_t monsterId) const
{
    const auto iterator = m_monsterTactics.find(monsterId);
    return iterator != m_monsterTactics.end() ? &iterator->second : nullptr;
}

bool ActorBehaviorCatalog::hasTactics() const
{
    return !m_monsterTactics.empty();
}
}
