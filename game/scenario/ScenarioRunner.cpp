#include "game/scenario/ScenarioRunner.h"

#include "engine/AssetFileSystem.h"
#include "game/data/GameDataLoader.h"
#include "game/debug/GameplayDebugTrace.h"
#include "game/events/EventRuntime.h"
#include "game/gameplay/HouseInteraction.h"
#include "game/gameplay/MercenaryRecruitmentRuntime.h"
#include "game/party/Party.h"
#include "game/scenario/ScenarioRuntimeDriver.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace OpenYAMM::Game
{
namespace
{
struct ScenarioExecutionContext
{
    Engine::AssetFileSystem assetFileSystem;
    GameDataLoader gameDataLoader;
    EventRuntime eventRuntime;
    EventRuntimeState eventRuntimeState;
    Party party;
    std::unique_ptr<ScenarioRuntimeDriver> runtimeDriver;
    std::string currentMapFileName;
    std::filesystem::path basePath;
    std::filesystem::path saveGameRootPath;
    Engine::ApplicationConfig config;
    ScenarioMode mode = ScenarioMode::Hybrid;
    bool gameDataLoaded = false;
    bool eventRuntimeStateLoaded = false;
    bool traceEnabled = true;
    bool colorsEnabled = true;
    size_t totalStepCount = 0;
    float gameMinutes = 9.0f * 60.0f;
    struct Checkpoint
    {
        Party party;
        EventRuntimeState eventRuntimeState;
        std::string currentMapFileName;
        float gameMinutes = 0.0f;
        bool eventRuntimeStateLoaded = false;
    };

    std::unordered_map<std::string, Checkpoint> checkpoints;
    std::unordered_map<std::string, std::filesystem::path> saveGamePaths;
};

struct ScenarioInventoryGrid
{
    uint8_t x = 0;
    uint8_t y = 0;
};

bool ensureRuntimeDriver(ScenarioExecutionContext &context, std::vector<std::string> &failures);
std::optional<EquipmentSlot> parseScenarioEquipmentSlot(const std::string &value);

std::string lowerAscii(const std::string &value)
{
    std::string lowered;
    lowered.reserve(value.size());

    for (char character : value)
    {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }

    return lowered;
}

bool envFlagEnabled(const char *pName)
{
    const char *pValue = std::getenv(pName);

    if (pValue == nullptr)
    {
        return false;
    }

    const std::string value = lowerAscii(pValue);
    return value.empty() || value == "1" || value == "true" || value == "yes" || value == "on";
}

enum class ScenarioLogColor
{
    Default,
    Blue,
    Cyan,
    Green,
    Red,
    Yellow,
};

const char *scenarioColorCode(ScenarioLogColor color)
{
    switch (color)
    {
    case ScenarioLogColor::Blue:
        return "\033[34m";
    case ScenarioLogColor::Cyan:
        return "\033[36m";
    case ScenarioLogColor::Green:
        return "\033[32m";
    case ScenarioLogColor::Red:
        return "\033[31m";
    case ScenarioLogColor::Yellow:
        return "\033[33m";
    case ScenarioLogColor::Default:
        break;
    }

    return "";
}

std::string scenarioColored(
    const ScenarioExecutionContext &context,
    const std::string &text,
    ScenarioLogColor color)
{
    if (!context.colorsEnabled || color == ScenarioLogColor::Default)
    {
        return text;
    }

    return std::string(scenarioColorCode(color)) + text + "\033[0m";
}

void scenarioLog(
    const ScenarioExecutionContext &context,
    ScenarioLogColor color,
    const std::string &message)
{
    if (!context.traceEnabled)
    {
        return;
    }

    std::cout << scenarioColored(context, message, color) << '\n';
    std::cout.flush();
}

std::string scalarNodeText(const YAML::Node &node)
{
    if (!node || !node.IsScalar())
    {
        return "<unset>";
    }

    return node.as<std::string>();
}

std::string boolText(bool value)
{
    return value ? "true" : "false";
}

std::string replacedText(
    std::string value,
    const std::string &from,
    const std::string &to)
{
    if (from.empty())
    {
        return value;
    }

    size_t position = 0;

    while ((position = value.find(from, position)) != std::string::npos)
    {
        value.replace(position, from.size(), to);
        position += to.size();
    }

    return value;
}

std::string normalizedScenarioDialogLabel(std::string value)
{
    value = replacedText(value, "\xE2\x80\xA6", "<ellipsis>");
    value = replacedText(value, "\xEF\xBF\xBD", "<ellipsis>");
    value = replacedText(value, std::string(1, static_cast<char>(0x85)), "<ellipsis>");
    return value;
}

bool parseUnsignedText(const std::string &text, uint32_t &value)
{
    if (text.empty())
    {
        return false;
    }

    for (char character : text)
    {
        if (!std::isdigit(static_cast<unsigned char>(character)))
        {
            return false;
        }
    }

    try
    {
        value = static_cast<uint32_t>(std::stoul(text));
    }
    catch (const std::exception &)
    {
        return false;
    }

    return true;
}

std::optional<ScenarioInventoryGrid> parseInventoryGridText(const std::string &text)
{
    const size_t open = text.find('(');
    const size_t comma = text.find(',', open == std::string::npos ? 0 : open + 1);
    const size_t close = text.find(')', comma == std::string::npos ? 0 : comma + 1);

    if (open == std::string::npos || comma == std::string::npos || close == std::string::npos)
    {
        return std::nullopt;
    }

    uint32_t x = 0;
    uint32_t y = 0;

    if (!parseUnsignedText(text.substr(open + 1, comma - open - 1), x)
        || !parseUnsignedText(text.substr(comma + 1, close - comma - 1), y)
        || x > std::numeric_limits<uint8_t>::max()
        || y > std::numeric_limits<uint8_t>::max())
    {
        return std::nullopt;
    }

    ScenarioInventoryGrid grid = {};
    grid.x = static_cast<uint8_t>(x);
    grid.y = static_cast<uint8_t>(y);
    return grid;
}

std::optional<bx::Vec3> parseFloatTripletText(const std::string &text)
{
    const size_t open = text.find('(');
    const size_t firstComma = text.find(',', open == std::string::npos ? 0 : open + 1);
    const size_t secondComma = text.find(',', firstComma == std::string::npos ? 0 : firstComma + 1);
    const size_t close = text.find(')', secondComma == std::string::npos ? 0 : secondComma + 1);

    if (open == std::string::npos
        || firstComma == std::string::npos
        || secondComma == std::string::npos
        || close == std::string::npos)
    {
        return std::nullopt;
    }

    try
    {
        return bx::Vec3{
            std::stof(text.substr(open + 1, firstComma - open - 1)),
            std::stof(text.substr(firstComma + 1, secondComma - firstComma - 1)),
            std::stof(text.substr(secondComma + 1, close - secondComma - 1)),
        };
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

std::string compactSemanticLabel(const std::string &text)
{
    std::string result;
    result.reserve(text.size());
    bool lastWasSpace = false;

    for (char character : text)
    {
        if (std::isspace(static_cast<unsigned char>(character)))
        {
            if (!result.empty() && !lastWasSpace)
            {
                result.push_back(' ');
                lastWasSpace = true;
            }

            continue;
        }

        result.push_back(character == '"' ? '\'' : character);
        lastWasSpace = false;
    }

    if (!result.empty() && result.back() == ' ')
    {
        result.pop_back();
    }

    constexpr size_t MaxLabelLength = 96;
    if (result.size() > MaxLabelLength)
    {
        result.resize(MaxLabelLength - 3);
        result += "...";
    }

    return result;
}

std::string semanticSuffix(const std::string &label)
{
    const std::string compact = compactSemanticLabel(label);
    return compact.empty() ? std::string() : " \"" + compact + '"';
}

std::string mapSemanticSuffix(const ScenarioExecutionContext &context, const std::string &mapFileName)
{
    const MapStatsEntry *pMap = context.gameDataLoader.getMapStats().findByFileName(mapFileName);
    return pMap != nullptr ? semanticSuffix(pMap->name) : std::string();
}

std::string qbitSemanticSuffix(const ScenarioExecutionContext &context, uint32_t qbitId)
{
    for (const JournalQuestEntry &entry : context.gameDataLoader.getJournalQuestTable().entries())
    {
        if (entry.qbitId != qbitId)
        {
            continue;
        }

        if (!entry.text.empty())
        {
            return semanticSuffix(entry.text);
        }

        if (!entry.notes.empty())
        {
            return semanticSuffix(entry.notes);
        }

        return semanticSuffix(entry.owner);
    }

    return std::string();
}

std::string itemSemanticSuffix(const ScenarioExecutionContext &context, uint32_t itemId)
{
    const ItemDefinition *pItem = context.gameDataLoader.getItemTable().get(itemId);

    if (pItem == nullptr)
    {
        return std::string();
    }

    return semanticSuffix(!pItem->name.empty() ? pItem->name : pItem->unidentifiedName);
}

std::string houseSemanticSuffix(const ScenarioExecutionContext &context, uint32_t houseId)
{
    const HouseEntry *pHouse = context.gameDataLoader.getHouseTable().get(houseId);

    if (pHouse == nullptr)
    {
        return std::string();
    }

    if (!pHouse->name.empty())
    {
        return semanticSuffix(pHouse->name);
    }

    if (!pHouse->buildingName.empty())
    {
        return semanticSuffix(pHouse->buildingName);
    }

    return semanticSuffix(pHouse->type);
}

std::string npcSemanticSuffix(const ScenarioExecutionContext &context, uint32_t npcId)
{
    const NpcEntry *pNpc = context.gameDataLoader.getNpcDialogTable().getNpc(npcId);
    return pNpc != nullptr ? semanticSuffix(pNpc->name) : std::string();
}

std::string professionSemanticSuffix(const ScenarioExecutionContext &context, uint32_t professionId)
{
    const MergedNpcProfessionEntry *pProfession =
        context.gameDataLoader.getMergedNpcProfessionTable().get(professionId);
    return pProfession != nullptr ? semanticSuffix(pProfession->profession) : std::string();
}

std::string monsterSemanticSuffix(const ScenarioExecutionContext &context, uint32_t monsterId)
{
    const MonsterTable::MonsterStatsEntry *pMonster =
        context.gameDataLoader.getMonsterTable().findStatsById(static_cast<int16_t>(monsterId));
    return pMonster != nullptr ? semanticSuffix(pMonster->name) : std::string();
}

std::string topicSemanticSuffix(const ScenarioExecutionContext &context, uint32_t topicId)
{
    const std::optional<NpcDialogTable::ResolvedTopic> topic =
        context.gameDataLoader.getNpcDialogTable().getTopicById(topicId);
    return topic.has_value() ? semanticSuffix(topic->topic) : std::string();
}

std::string semanticValue(
    const ScenarioExecutionContext &context,
    const std::string &fieldName,
    const std::string &value)
{
    uint32_t unsignedValue = 0;

    if (fieldName == "map"
        || fieldName == "map_loaded"
        || fieldName == "destination_map"
        || fieldName == "previous_map")
    {
        return value + mapSemanticSuffix(context, value);
    }

    if ((fieldName == "item_id" || fieldName == "expected_item_id" || fieldName == "held_item_id")
        && parseUnsignedText(value, unsignedValue))
    {
        return value + itemSemanticSuffix(context, unsignedValue);
    }

    if ((fieldName == "npc_id" || fieldName == "active_source_id") && parseUnsignedText(value, unsignedValue))
    {
        return value + npcSemanticSuffix(context, unsignedValue);
    }

    if ((fieldName == "house_id" || fieldName == "host_house_id") && parseUnsignedText(value, unsignedValue))
    {
        return value + houseSemanticSuffix(context, unsignedValue);
    }

    if (fieldName == "topic_id" && parseUnsignedText(value, unsignedValue))
    {
        return value + topicSemanticSuffix(context, unsignedValue);
    }

    if (fieldName == "profession_id" && parseUnsignedText(value, unsignedValue))
    {
        return value + professionSemanticSuffix(context, unsignedValue);
    }

    return value;
}

std::string semanticFieldName(
    const ScenarioExecutionContext &context,
    const std::string &fieldName)
{
    const bool qbitField =
        fieldName.rfind("qbit_", 0) == 0
        || fieldName.rfind("quest_bit", 0) == 0;
    const size_t qbitOpen = qbitField ? fieldName.find('[') : std::string::npos;
    const size_t qbitClose = qbitField ? fieldName.find(']') : std::string::npos;

    if (qbitField && qbitOpen != std::string::npos && qbitClose != std::string::npos && qbitClose > qbitOpen + 1)
    {
        uint32_t qbitId = 0;
        if (parseUnsignedText(fieldName.substr(qbitOpen + 1, qbitClose - qbitOpen - 1), qbitId))
        {
            return fieldName + qbitSemanticSuffix(context, qbitId);
        }
    }

    return fieldName;
}

std::string stepLocation(const ScenarioStep &step)
{
    std::ostringstream stream;
    stream << step.sourcePath.string() << ':' << step.sourceLine << " step " << step.stepIndex
           << " (" << step.kindName << ')';

    if (step.traceSourceLine > 0)
    {
        stream << " trace_line=" << step.traceSourceLine;
    }

    return stream.str();
}

void appendPayloadScalarSummary(
    const ScenarioExecutionContext &context,
    std::ostringstream &stream,
    const YAML::Node &payload,
    const char *pFieldName)
{
    if (payload[pFieldName] && payload[pFieldName].IsScalar())
    {
        const std::string value = payload[pFieldName].as<std::string>();
        stream << ' ' << pFieldName << '=' << semanticValue(context, pFieldName, value);
    }
}

void appendSelectTopicSummary(
    const ScenarioExecutionContext &context,
    std::ostringstream &stream,
    const YAML::Node &payload)
{
    if (!payload["kind"] || !payload["kind"].IsScalar())
    {
        return;
    }

    const std::string kind = payload["kind"].as<std::string>();
    uint32_t id = 0;

    if (payload["source_id"] && payload["source_id"].IsScalar()
        && parseUnsignedText(payload["source_id"].as<std::string>(), id))
    {
        stream << " source_id=" << id;

        if (kind == "house_service" || kind == "house_resident")
        {
            stream << houseSemanticSuffix(context, id);
        }
        else
        {
            stream << npcSemanticSuffix(context, id);
        }
    }

    if (payload["action_id"] && payload["action_id"].IsScalar()
        && parseUnsignedText(payload["action_id"].as<std::string>(), id))
    {
        stream << " action_id=" << id;

        if (kind == "npc_topic")
        {
            stream << topicSemanticSuffix(context, id);
        }
        else if (kind == "house_resident")
        {
            stream << npcSemanticSuffix(context, id);
        }
    }

    if (payload["label"] && payload["label"].IsScalar())
    {
        stream << " label=" << semanticSuffix(payload["label"].as<std::string>());
    }
}

void appendTransitionSummary(
    const ScenarioExecutionContext &context,
    std::ostringstream &stream,
    const YAML::Node &transitionNode,
    const char *pName)
{
    if (!transitionNode || !transitionNode.IsMap())
    {
        return;
    }

    stream << ' ' << pName << '=';

    if (transitionNode["source_kind"] && transitionNode["source_kind"].IsScalar())
    {
        stream << transitionNode["source_kind"].as<std::string>();
    }

    if (transitionNode["source_id"] && transitionNode["source_id"].IsScalar())
    {
        const std::string sourceText = transitionNode["source_id"].as<std::string>();
        stream << ':' << sourceText;

        uint32_t sourceId = 0;
        if (parseUnsignedText(sourceText, sourceId))
        {
            stream << houseSemanticSuffix(context, sourceId);
        }
    }

    if (transitionNode["destination_map"] && transitionNode["destination_map"].IsScalar())
    {
        const std::string mapFileName = transitionNode["destination_map"].as<std::string>();
        stream << " -> " << mapFileName << mapSemanticSuffix(context, mapFileName);
    }

    if (transitionNode["destination_name"] && transitionNode["destination_name"].IsScalar())
    {
        stream << " destination=" << semanticSuffix(transitionNode["destination_name"].as<std::string>());
    }
}

void appendFollowerSummary(
    const ScenarioExecutionContext &context,
    std::ostringstream &stream,
    const YAML::Node &payload,
    const char *pFieldName)
{
    const YAML::Node followersNode = payload[pFieldName];

    if (!followersNode || !followersNode.IsSequence())
    {
        return;
    }

    stream << ' ' << pFieldName << '=';
    bool first = true;

    for (const YAML::Node &entryNode : followersNode)
    {
        if (!entryNode.IsMap() || !entryNode["npc_id"] || !entryNode["npc_id"].IsScalar())
        {
            continue;
        }

        uint32_t npcId = 0;
        if (!parseUnsignedText(entryNode["npc_id"].as<std::string>(), npcId))
        {
            continue;
        }

        if (!first)
        {
            stream << ',';
        }

        first = false;
        stream << npcId << npcSemanticSuffix(context, npcId);

        if (entryNode["profession_id"] && entryNode["profession_id"].IsScalar())
        {
            uint32_t professionId = 0;
            if (parseUnsignedText(entryNode["profession_id"].as<std::string>(), professionId))
            {
                stream << " profession=" << professionId << professionSemanticSuffix(context, professionId);
            }
        }
    }
}

void appendNestedItemSummary(
    const ScenarioExecutionContext &context,
    std::ostringstream &stream,
    const YAML::Node &payload,
    const char *pFieldName)
{
    const YAML::Node itemNode = payload[pFieldName];

    if (!itemNode || !itemNode.IsMap() || !itemNode["item_id"] || !itemNode["item_id"].IsScalar())
    {
        return;
    }

    uint32_t itemId = 0;
    if (!parseUnsignedText(itemNode["item_id"].as<std::string>(), itemId))
    {
        return;
    }

    stream << ' ' << pFieldName << '=' << itemId << itemSemanticSuffix(context, itemId);
}

void appendInventoryItemSummary(
    const ScenarioExecutionContext &context,
    std::ostringstream &stream,
    const YAML::Node &payload,
    const char *pFieldName)
{
    const YAML::Node inventoryNode = payload[pFieldName];

    if (!inventoryNode || !inventoryNode.IsSequence())
    {
        return;
    }

    stream << ' ' << pFieldName << '=';
    bool first = true;

    for (const YAML::Node &entryNode : inventoryNode)
    {
        if (!entryNode.IsMap() || !entryNode["item_id"] || !entryNode["item_id"].IsScalar())
        {
            continue;
        }

        uint32_t itemId = 0;
        if (!parseUnsignedText(entryNode["item_id"].as<std::string>(), itemId))
        {
            continue;
        }

        if (!first)
        {
            stream << ',';
        }

        first = false;
        stream << itemId << itemSemanticSuffix(context, itemId);
    }
}

void appendActorSummary(
    const ScenarioExecutionContext &context,
    std::ostringstream &stream,
    const YAML::Node &payload)
{
    const YAML::Node actorNode = payload["actor_visible"];

    if (!actorNode || !actorNode.IsMap())
    {
        return;
    }

    stream << " actor_visible=";

    if (actorNode["actor_index"] && actorNode["actor_index"].IsScalar())
    {
        stream << "index:" << actorNode["actor_index"].as<std::string>();
    }

    if (actorNode["name"] && actorNode["name"].IsScalar())
    {
        stream << semanticSuffix(actorNode["name"].as<std::string>());
    }
    else if (actorNode["monster_id"] && actorNode["monster_id"].IsScalar())
    {
        uint32_t monsterId = 0;
        if (parseUnsignedText(actorNode["monster_id"].as<std::string>(), monsterId))
        {
            stream << monsterSemanticSuffix(context, monsterId);
        }
    }

    if (actorNode["monster_id"] && actorNode["monster_id"].IsScalar())
    {
        stream << " monster_id=" << actorNode["monster_id"].as<std::string>();
    }
}

std::string stepSummary(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step)
{
    std::ostringstream stream;
    stream << "step " << step.stepIndex << '/' << context.totalStepCount << ' ' << step.kindName;

    if (step.traceSourceLine > 0)
    {
        stream << " trace_line=" << step.traceSourceLine;
    }

    stream << " yaml_line=" << step.sourceLine;

    if (step.payload && step.payload.IsMap())
    {
        appendPayloadScalarSummary(context, stream, step.payload, "map");
        appendPayloadScalarSummary(context, stream, step.payload, "kind");
        appendPayloadScalarSummary(context, stream, step.payload, "target_index");
        appendPayloadScalarSummary(context, stream, step.payload, "face_index");
        appendPayloadScalarSummary(context, stream, step.payload, "triggered_event");
        appendPayloadScalarSummary(context, stream, step.payload, "event_id");
        appendPayloadScalarSummary(context, stream, step.payload, "actor_index");
        appendPayloadScalarSummary(context, stream, step.payload, "item_id");
        appendPayloadScalarSummary(context, stream, step.payload, "name");
        appendPayloadScalarSummary(context, stream, step.payload, "text");

        if (step.kindName == "select_topic")
        {
            appendSelectTopicSummary(context, stream, step.payload);
        }

        appendTransitionSummary(context, stream, step.payload["map_transition_requested"], "requested");
        appendTransitionSummary(context, stream, step.payload["map_transition_confirmed"], "confirmed");
        appendTransitionSummary(context, stream, step.payload["map_transition_canceled"], "canceled");
        appendFollowerSummary(context, stream, step.payload, "follower_hired");
        appendFollowerSummary(context, stream, step.payload, "follower_left");
        appendNestedItemSummary(context, stream, step.payload, "item_received");
        appendNestedItemSummary(context, stream, step.payload, "item_visible");
        appendNestedItemSummary(context, stream, step.payload, "world_item_spawned");
        appendNestedItemSummary(context, stream, step.payload, "chest_contains_quest_item");
        appendNestedItemSummary(context, stream, step.payload, "corpse_contains_quest_item");
        appendInventoryItemSummary(context, stream, step.payload, "inventory");
        appendInventoryItemSummary(context, stream, step.payload, "inventory_any_member");
        appendInventoryItemSummary(context, stream, step.payload, "equipped_item");
        appendActorSummary(context, stream, step.payload);

        const YAML::Node poseNode = step.payload["pose"];
        if (poseNode && poseNode.IsMap())
        {
            stream << " pose=(" << scalarNodeText(poseNode["x"]) << ','
                   << scalarNodeText(poseNode["y"]) << ','
                   << scalarNodeText(poseNode["z"]) << ')';

            if (poseNode["map"] && poseNode["map"].IsScalar())
            {
                stream << " pose_map=" << poseNode["map"].as<std::string>();
            }
        }
    }

    return stream.str();
}

void scenarioLogAssert(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const std::string &fieldName,
    const std::string &expected,
    const std::string &actual,
    bool success)
{
    std::ostringstream stream;
    stream << (success ? "ASSERT pass " : "ASSERT fail ")
           << "step=" << step.stepIndex;

    if (step.traceSourceLine > 0)
    {
        stream << " trace_line=" << step.traceSourceLine;
    }

    stream << ' ' << semanticFieldName(context, fieldName)
           << " expected=" << semanticValue(context, fieldName, expected)
           << " actual=" << semanticValue(context, fieldName, actual);
    scenarioLog(context, success ? ScenarioLogColor::Green : ScenarioLogColor::Red, stream.str());
}

bool recordScenarioAssertion(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const std::string &fieldName,
    const std::string &expected,
    const std::string &actual,
    std::vector<std::string> &failures)
{
    const bool success = expected == actual;
    scenarioLogAssert(context, step, fieldName, expected, actual, success);

    if (!success)
    {
        failures.push_back(stepLocation(step) + ": expected " + fieldName + "=" + expected
            + " but current " + fieldName + "=" + actual);
    }

    return success;
}

bool assertDialogScalar(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &dialogNode,
    const std::string &fieldName,
    const std::string &actual,
    std::vector<std::string> &failures)
{
    const YAML::Node expectedNode = dialogNode[fieldName];

    if (!expectedNode || !expectedNode.IsScalar())
    {
        return true;
    }

    return recordScenarioAssertion(
        context,
        step,
        "dialog." + fieldName,
        expectedNode.as<std::string>(),
        actual,
        failures);
}

bool assertDialogActionScalar(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &actionNode,
    size_t actionIndex,
    const std::string &fieldName,
    const std::string &actual,
    std::vector<std::string> &failures)
{
    const YAML::Node expectedNode = actionNode[fieldName];

    if (!expectedNode || !expectedNode.IsScalar())
    {
        return true;
    }

    if (fieldName == "label"
        && actionNode["kind"]
        && actionNode["kind"].IsScalar()
        && actionNode["kind"].as<std::string>() == "house_resident"
        && actionNode["id"]
        && actionNode["id"].IsScalar()
        && isGeneratedMercenaryNpcId(actionNode["id"].as<uint32_t>()))
    {
        return true;
    }

    const std::string expected = expectedNode.as<std::string>();

    if (fieldName == "label")
    {
        const std::string normalizedExpected = normalizedScenarioDialogLabel(expected);
        const std::string normalizedActual = normalizedScenarioDialogLabel(actual);

        if (expected != actual && normalizedExpected == normalizedActual)
        {
            std::ostringstream field;
            field << "dialog.actions[" << actionIndex << "]." << fieldName;
            scenarioLogAssert(context, step, field.str(), expected, actual + " (encoding-normalized)", true);
            return true;
        }
    }

    std::ostringstream field;
    field << "dialog.actions[" << actionIndex << "]." << fieldName;
    return recordScenarioAssertion(
        context,
        step,
        field.str(),
        expected,
        actual,
        failures);
}

bool assertDialogState(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &dialogNode,
    const std::string &phase,
    std::vector<std::string> &failures)
{
    if (!dialogNode)
    {
        return true;
    }

    if (!dialogNode.IsMap())
    {
        failures.push_back(stepLocation(step) + ": " + phase + " dialog expectation must be a mapping");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    const ScenarioDialogSnapshot actual = context.runtimeDriver->activeDialogSnapshot();
    bool success = true;

    success = assertDialogScalar(context, step, dialogNode, "active", boolText(actual.active), failures) && success;
    success = assertDialogScalar(
        context,
        step,
        dialogNode,
        "house_dialog",
        boolText(actual.houseDialog),
        failures) && success;
    success = assertDialogScalar(
        context,
        step,
        dialogNode,
        "source_id",
        std::to_string(actual.sourceId),
        failures) && success;
    success = assertDialogScalar(context, step, dialogNode, "title", actual.title, failures) && success;
    success = assertDialogScalar(context, step, dialogNode, "house_title", actual.houseTitle, failures) && success;
    success = assertDialogScalar(
        context,
        step,
        dialogNode,
        "line_count",
        std::to_string(actual.lineCount),
        failures) && success;
    success = assertDialogScalar(
        context,
        step,
        dialogNode,
        "text_hash",
        std::to_string(actual.textHash),
        failures) && success;
    success = assertDialogScalar(
        context,
        step,
        dialogNode,
        "action_count",
        std::to_string(actual.actions.size()),
        failures) && success;

    const YAML::Node actionsNode = dialogNode["actions"];

    if (actionsNode)
    {
        if (!actionsNode.IsSequence())
        {
            failures.push_back(stepLocation(step) + ": " + phase + " dialog actions expectation must be a sequence");
            return false;
        }

        success = recordScenarioAssertion(
            context,
            step,
            "dialog.actions.size",
            std::to_string(actionsNode.size()),
            std::to_string(actual.actions.size()),
            failures) && success;

        const size_t checkedCount = std::min(actionsNode.size(), actual.actions.size());

        for (size_t actionIndex = 0; actionIndex < checkedCount; ++actionIndex)
        {
            const YAML::Node actionNode = actionsNode[actionIndex];

            if (!actionNode || !actionNode.IsMap())
            {
                failures.push_back(stepLocation(step) + ": " + phase + " dialog action expectation must be a mapping");
                success = false;
                continue;
            }

            const ScenarioDialogActionSnapshot &action = actual.actions[actionIndex];
            success = assertDialogActionScalar(
                context,
                step,
                actionNode,
                actionIndex,
                "index",
                std::to_string(action.index),
                failures) && success;
            success = assertDialogActionScalar(
                context,
                step,
                actionNode,
                actionIndex,
                "kind",
                action.kind,
                failures) && success;
            success = assertDialogActionScalar(
                context,
                step,
                actionNode,
                actionIndex,
                "id",
                std::to_string(action.id),
                failures) && success;
            success = assertDialogActionScalar(
                context,
                step,
                actionNode,
                actionIndex,
                "secondary_id",
                std::to_string(action.secondaryId),
                failures) && success;
            success = assertDialogActionScalar(
                context,
                step,
                actionNode,
                actionIndex,
                "enabled",
                boolText(action.enabled),
                failures) && success;
            success = assertDialogActionScalar(
                context,
                step,
                actionNode,
                actionIndex,
                "text_only",
                boolText(action.textOnly),
                failures) && success;
            success = assertDialogActionScalar(
                context,
                step,
                actionNode,
                actionIndex,
                "label",
                action.label,
                failures) && success;
        }
    }

    scenarioLog(
        context,
        success ? ScenarioLogColor::Green : ScenarioLogColor::Red,
        std::string(success ? "DIALOG assert pass " : "DIALOG assert fail ")
            + phase + " step=" + std::to_string(step.stepIndex)
            + " " + context.runtimeDriver->activeDialogSummary());

    return context.mode != ScenarioMode::Faithful || success;
}

bool assertInputPromptState(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &promptNode,
    const std::string &phase,
    std::vector<std::string> &failures)
{
    if (!promptNode)
    {
        return true;
    }

    if (!promptNode.IsMap())
    {
        failures.push_back(
            stepLocation(step) + ": " + phase + " input_prompt expectation must be a mapping");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    const ScenarioInputPromptSnapshot actual = context.runtimeDriver->activeInputPromptSnapshot();
    bool success = true;

    success = recordScenarioAssertion(
        context,
        step,
        "input_prompt.active",
        promptNode["active"] && promptNode["active"].IsScalar()
            ? boolText(promptNode["active"].as<bool>())
            : "true",
        boolText(actual.active),
        failures) && success;

    if (actual.active)
    {
        success = assertDialogScalar(
            context,
            step,
            promptNode,
            "event_id",
            std::to_string(actual.eventId),
            failures) && success;
        success = assertDialogScalar(
            context,
            step,
            promptNode,
            "continue_step",
            std::to_string(actual.continueStep),
            failures) && success;
        success = assertDialogScalar(
            context,
            step,
            promptNode,
            "correct_step",
            std::to_string(actual.correctStep),
            failures) && success;
        success = assertDialogScalar(
            context,
            step,
            promptNode,
            "text_id",
            std::to_string(actual.textId),
            failures) && success;
        success = assertDialogScalar(context, step, promptNode, "prompt", actual.text, failures) && success;
        success = assertDialogScalar(
            context,
            step,
            promptNode,
            "answer_count",
            std::to_string(actual.answerCount),
            failures) && success;
    }

    scenarioLog(
        context,
        success ? ScenarioLogColor::Green : ScenarioLogColor::Red,
        std::string(success ? "INPUT_PROMPT assert pass " : "INPUT_PROMPT assert fail ")
            + phase + " step=" + std::to_string(step.stepIndex)
            + " active=" + boolText(actual.active)
            + " event_id=" + std::to_string(actual.eventId)
            + " prompt=\"" + actual.text + "\"");

    return context.mode != ScenarioMode::Faithful || success;
}

void applyPartyStateToRuntimeDriver(ScenarioExecutionContext &context)
{
    if (context.runtimeDriver)
    {
        context.runtimeDriver->setPartyState(context.party);
    }
}

bool applyRecordedNewGamePartyMember(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node memberNode = step.payload["new_game_party_member"];

    if (!memberNode)
    {
        return true;
    }

    if (!memberNode.IsMap())
    {
        failures.push_back(stepLocation(step) + ": new_game_party_member assertion payload must be a mapping");
        return false;
    }

    if (!memberNode["member_index"] || !memberNode["member_index"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": new_game_party_member requires scalar field 'member_index'");
        return false;
    }

    const size_t memberIndex = memberNode["member_index"].as<size_t>();
    Character *pMember = context.party.member(memberIndex);

    if (pMember == nullptr)
    {
        failures.push_back(stepLocation(step) + ": new_game_party_member member_index "
            + std::to_string(memberIndex) + " does not exist");
        return false;
    }

    if (memberNode["name"] && memberNode["name"].IsScalar())
    {
        pMember->name = memberNode["name"].as<std::string>();
    }

    if (memberNode["class"] && memberNode["class"].IsScalar())
    {
        context.party.setMemberClassName(memberIndex, memberNode["class"].as<std::string>());
        pMember = context.party.member(memberIndex);

        if (pMember == nullptr)
        {
            failures.push_back(stepLocation(step) + ": new_game_party_member member_index "
                + std::to_string(memberIndex) + " disappeared after class assignment");
            return false;
        }
    }

    if (memberNode["role"] && memberNode["role"].IsScalar())
    {
        pMember->role = memberNode["role"].as<std::string>();
    }

    if (memberNode["race_id"] && memberNode["race_id"].IsScalar())
    {
        pMember->raceId = memberNode["race_id"].as<uint32_t>();
    }

    if (memberNode["sex_id"] && memberNode["sex_id"].IsScalar())
    {
        pMember->sexId = memberNode["sex_id"].as<uint32_t>();
    }

    if (memberNode["portrait_id"] && memberNode["portrait_id"].IsScalar())
    {
        pMember->portraitPictureId = memberNode["portrait_id"].as<uint32_t>();
    }

    if (memberNode["voice_id"] && memberNode["voice_id"].IsScalar())
    {
        pMember->voiceId = memberNode["voice_id"].as<int32_t>();
    }

    if (memberNode["level"] && memberNode["level"].IsScalar())
    {
        pMember->level = std::max<uint32_t>(1, memberNode["level"].as<uint32_t>());
    }

    if (memberNode["might"] && memberNode["might"].IsScalar())
    {
        pMember->might = memberNode["might"].as<uint32_t>();
    }

    if (memberNode["intellect"] && memberNode["intellect"].IsScalar())
    {
        pMember->intellect = memberNode["intellect"].as<uint32_t>();
    }

    if (memberNode["personality"] && memberNode["personality"].IsScalar())
    {
        pMember->personality = memberNode["personality"].as<uint32_t>();
    }

    if (memberNode["endurance"] && memberNode["endurance"].IsScalar())
    {
        pMember->endurance = memberNode["endurance"].as<uint32_t>();
    }

    if (memberNode["accuracy"] && memberNode["accuracy"].IsScalar())
    {
        pMember->accuracy = memberNode["accuracy"].as<uint32_t>();
    }

    if (memberNode["speed"] && memberNode["speed"].IsScalar())
    {
        pMember->speed = memberNode["speed"].as<uint32_t>();
    }

    if (memberNode["luck"] && memberNode["luck"].IsScalar())
    {
        pMember->luck = memberNode["luck"].as<uint32_t>();
    }

    applyPartyStateToRuntimeDriver(context);
    return true;
}

bool applyRecordedNewGamePartySkill(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node skillNode = step.payload["new_game_party_skill"];

    if (!skillNode)
    {
        return true;
    }

    if (!skillNode.IsMap())
    {
        failures.push_back(stepLocation(step) + ": new_game_party_skill assertion payload must be a mapping");
        return false;
    }

    if (!skillNode["member_index"] || !skillNode["member_index"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": new_game_party_skill requires scalar field 'member_index'");
        return false;
    }

    if (!skillNode["name"] || !skillNode["name"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": new_game_party_skill requires scalar field 'name'");
        return false;
    }

    const size_t memberIndex = skillNode["member_index"].as<size_t>();
    Character *pMember = context.party.member(memberIndex);

    if (pMember == nullptr)
    {
        failures.push_back(stepLocation(step) + ": new_game_party_skill member_index "
            + std::to_string(memberIndex) + " does not exist");
        return false;
    }

    CharacterSkill skill = {};
    skill.name = canonicalSkillName(skillNode["name"].as<std::string>());

    if (skill.name.empty())
    {
        failures.push_back(stepLocation(step) + ": new_game_party_skill has unknown skill '"
            + skillNode["name"].as<std::string>() + "'");
        return false;
    }

    if (skillNode["level"] && skillNode["level"].IsScalar())
    {
        skill.level = skillNode["level"].as<uint32_t>();
    }

    if (skillNode["mastery"] && skillNode["mastery"].IsScalar())
    {
        const uint32_t mastery = skillNode["mastery"].as<uint32_t>();
        skill.mastery =
            static_cast<SkillMastery>(std::min<uint32_t>(mastery, static_cast<uint32_t>(SkillMastery::Grandmaster)));
    }

    pMember->skills[skill.name] = skill;
    applyPartyStateToRuntimeDriver(context);
    return true;
}

void synchronizeRuntimeClock(ScenarioExecutionContext &context)
{
    static constexpr float MinutesPerDay = 24.0f * 60.0f;

    const int totalMinutes = std::max(0, static_cast<int>(std::floor(context.gameMinutes)));
    const int hour = (totalMinutes / 60) % 24;
    const int dayOfYear = std::max(1, static_cast<int>(totalMinutes / MinutesPerDay) + 1);

    context.eventRuntimeState.variables[static_cast<uint32_t>(EvtVariable::Hour)] = hour;
    context.eventRuntimeState.variables[static_cast<uint32_t>(EvtVariable::DayOfYear)] = dayOfYear;
}

bool payloadIsMap(
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (step.payload && step.payload.IsMap())
    {
        return true;
    }

    failures.push_back(stepLocation(step) + ": step payload must be a mapping");
    return false;
}

bool readScalarString(
    const ScenarioStep &step,
    const char *pFieldName,
    std::string &value,
    std::vector<std::string> &failures,
    bool required)
{
    if (!step.payload[pFieldName])
    {
        if (required)
        {
            failures.push_back(stepLocation(step) + ": missing scalar field '" + pFieldName + "'");
        }

        return !required;
    }

    if (!step.payload[pFieldName].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": field '" + pFieldName + "' must be scalar");
        return false;
    }

    value = step.payload[pFieldName].as<std::string>();
    return true;
}

bool readScalarFloat(
    const ScenarioStep &step,
    const char *pFieldName,
    float &value,
    std::vector<std::string> &failures,
    bool required)
{
    if (!step.payload[pFieldName])
    {
        if (required)
        {
            failures.push_back(stepLocation(step) + ": missing scalar field '" + pFieldName + "'");
        }

        return !required;
    }

    if (!step.payload[pFieldName].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": field '" + pFieldName + "' must be scalar");
        return false;
    }

    value = step.payload[pFieldName].as<float>();
    return true;
}

float degreesToRadians(float degrees)
{
    return degrees * 3.14159265358979323846f / 180.0f;
}

float normalizedAngleDifference(float left, float right)
{
    constexpr float Pi = 3.14159265358979323846f;
    float difference = left - right;

    while (difference > Pi)
    {
        difference -= Pi * 2.0f;
    }

    while (difference < -Pi)
    {
        difference += Pi * 2.0f;
    }

    return std::abs(difference);
}

std::optional<ScenarioPose> readLoadMapPoseOverride(
    const ScenarioStep &step,
    const std::string &mapFileName,
    std::vector<std::string> &failures)
{
    if (!step.payload["pos"])
    {
        return std::nullopt;
    }

    if (!step.payload["pos"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": field 'pos' must be scalar");
        return std::nullopt;
    }

    std::string text = step.payload["pos"].as<std::string>();
    if (lowerAscii(text) == "none" || lowerAscii(text) == "null" || text.empty())
    {
        return std::nullopt;
    }

    text.erase(std::remove(text.begin(), text.end(), '('), text.end());
    text.erase(std::remove(text.begin(), text.end(), ')'), text.end());

    std::stringstream stream(text);
    std::string xText;
    std::string yText;
    std::string zText;

    if (!std::getline(stream, xText, ',')
        || !std::getline(stream, yText, ',')
        || !std::getline(stream, zText, ','))
    {
        failures.push_back(stepLocation(step) + ": field 'pos' must contain three comma-separated coordinates");
        return std::nullopt;
    }

    ScenarioPose pose = {};
    pose.mapFileName = mapFileName;

    try
    {
        pose.x = std::stof(xText);
        pose.y = std::stof(yText);
        pose.z = std::stof(zText);
    }
    catch (const std::exception &)
    {
        failures.push_back(stepLocation(step) + ": field 'pos' contains a non-numeric coordinate");
        return std::nullopt;
    }

    if (step.payload["direction_degrees"] && step.payload["direction_degrees"].IsScalar())
    {
        const std::string directionText = step.payload["direction_degrees"].as<std::string>();
        if (directionText != "none")
        {
            try
            {
                pose.yawRadians = degreesToRadians(std::stof(directionText));
            }
            catch (const std::exception &)
            {
                failures.push_back(stepLocation(step) + ": field 'direction_degrees' must be numeric or 'none'");
                return std::nullopt;
            }
        }
    }

    return pose;
}

void syncContextFromRuntime(ScenarioExecutionContext &context)
{
    if (!context.runtimeDriver)
    {
        return;
    }

    if (const Party *pParty = context.runtimeDriver->party())
    {
        context.party = *pParty;
    }

    if (const EventRuntimeState *pEventRuntimeState = context.runtimeDriver->eventRuntimeState())
    {
        context.eventRuntimeState = *pEventRuntimeState;
        context.eventRuntimeStateLoaded = true;
    }

    context.currentMapFileName = context.runtimeDriver->currentMapFileName();
    context.gameMinutes = context.runtimeDriver->gameMinutes();
    synchronizeRuntimeClock(context);
}

std::optional<float> recordedGameMinutesForStep(const ScenarioStep &step)
{
    if (!step.payload || !step.payload.IsMap())
    {
        return std::nullopt;
    }

    if (step.kind == ScenarioStepKind::AdvanceGameTime
        && step.payload["before_game_minutes"] && step.payload["before_game_minutes"].IsScalar())
    {
        return step.payload["before_game_minutes"].as<float>();
    }

    if (step.payload["game_minutes"] && step.payload["game_minutes"].IsScalar())
    {
        return step.payload["game_minutes"].as<float>();
    }

    return std::nullopt;
}

bool syncRecordedGameTimeBeforeStep(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const std::optional<float> recordedGameMinutes = recordedGameMinutesForStep(step);

    if (!recordedGameMinutes.has_value())
    {
        return true;
    }

    if (!std::isfinite(*recordedGameMinutes))
    {
        failures.push_back(stepLocation(step) + ": recorded game_minutes is not finite");
        return false;
    }

    const float deltaMinutes = *recordedGameMinutes - context.gameMinutes;
    if (std::abs(deltaMinutes) > 0.001f)
    {
        scenarioLog(
            context,
            ScenarioLogColor::Cyan,
            "TIME sync before step=" + std::to_string(step.stepIndex)
                + " trace_line=" + std::to_string(step.sourceLine)
                + " previous_game_minutes=" + std::to_string(context.gameMinutes)
                + " recorded_game_minutes=" + std::to_string(*recordedGameMinutes)
                + " delta_minutes=" + std::to_string(deltaMinutes));
    }

    context.gameMinutes = *recordedGameMinutes;
    synchronizeRuntimeClock(context);

    if (context.runtimeDriver)
    {
        context.runtimeDriver->setGameMinutes(context.gameMinutes);
    }

    return true;
}

bool ensureRuntimeDriver(
    ScenarioExecutionContext &context,
    std::vector<std::string> &failures)
{
    if (!context.runtimeDriver)
    {
        context.runtimeDriver = std::make_unique<ScenarioRuntimeDriver>(
            context.basePath,
            context.config,
            context.assetFileSystem);
    }

    std::string failure;

    if (!context.runtimeDriver->initialize(failure))
    {
        failures.push_back(failure);
        return false;
    }

    return true;
}

void seedRuntimeDriverFromContext(ScenarioExecutionContext &context)
{
    if (!context.runtimeDriver)
    {
        return;
    }

    context.runtimeDriver->setPartyState(context.party);
    context.runtimeDriver->setGameMinutes(context.gameMinutes);

    if (context.eventRuntimeStateLoaded)
    {
        context.runtimeDriver->setEventRuntimeState(context.eventRuntimeState);
    }
}

std::optional<ScenarioPose> readScenarioPose(
    const ScenarioStep &step,
    std::vector<std::string> &failures,
    bool required)
{
    const YAML::Node poseNode = step.payload["pose"];
    const YAML::Node sourceNode = poseNode ? poseNode : step.payload;

    if (!sourceNode || !sourceNode.IsMap())
    {
        if (required)
        {
            failures.push_back(stepLocation(step) + ": pose payload must be a mapping");
        }

        return std::nullopt;
    }

    if (!sourceNode["x"] || !sourceNode["y"] || !sourceNode["z"])
    {
        if (required)
        {
            failures.push_back(stepLocation(step) + ": pose requires scalar fields 'x', 'y', and 'z'");
        }

        return std::nullopt;
    }

    ScenarioPose pose = {};

    if (sourceNode["map"] && sourceNode["map"].IsScalar())
    {
        pose.mapFileName = sourceNode["map"].as<std::string>();
    }

    pose.x = sourceNode["x"].as<float>();
    pose.y = sourceNode["y"].as<float>();
    pose.z = sourceNode["z"].as<float>();

    if (sourceNode["yaw"] && sourceNode["yaw"].IsScalar())
    {
        pose.yawRadians = sourceNode["yaw"].as<float>();
    }
    else if (sourceNode["yaw_degrees"] && sourceNode["yaw_degrees"].IsScalar())
    {
        pose.yawRadians = degreesToRadians(sourceNode["yaw_degrees"].as<float>());
    }

    if (sourceNode["pitch"] && sourceNode["pitch"].IsScalar())
    {
        pose.pitchRadians = sourceNode["pitch"].as<float>();
    }
    else if (sourceNode["pitch_degrees"] && sourceNode["pitch_degrees"].IsScalar())
    {
        pose.pitchRadians = degreesToRadians(sourceNode["pitch_degrees"].as<float>());
    }

    return pose;
}

std::optional<ScenarioPose> readScenarioPoseNode(
    const ScenarioStep &step,
    const YAML::Node &sourceNode,
    const char *pFieldName,
    std::vector<std::string> &failures,
    bool required)
{
    if (!sourceNode || !sourceNode.IsMap())
    {
        if (required)
        {
            failures.push_back(stepLocation(step) + ": field '" + pFieldName + "' must be a pose mapping");
        }

        return std::nullopt;
    }

    if (!sourceNode["x"] || !sourceNode["y"] || !sourceNode["z"])
    {
        if (required)
        {
            failures.push_back(stepLocation(step) + ": field '" + pFieldName
                + "' requires scalar fields 'x', 'y', and 'z'");
        }

        return std::nullopt;
    }

    ScenarioPose pose = {};

    if (sourceNode["map"] && sourceNode["map"].IsScalar())
    {
        pose.mapFileName = sourceNode["map"].as<std::string>();
    }

    pose.x = sourceNode["x"].as<float>();
    pose.y = sourceNode["y"].as<float>();
    pose.z = sourceNode["z"].as<float>();

    if (sourceNode["yaw"] && sourceNode["yaw"].IsScalar())
    {
        pose.yawRadians = sourceNode["yaw"].as<float>();
    }
    else if (sourceNode["yaw_degrees"] && sourceNode["yaw_degrees"].IsScalar())
    {
        pose.yawRadians = degreesToRadians(sourceNode["yaw_degrees"].as<float>());
    }

    if (sourceNode["pitch"] && sourceNode["pitch"].IsScalar())
    {
        pose.pitchRadians = sourceNode["pitch"].as<float>();
    }
    else if (sourceNode["pitch_degrees"] && sourceNode["pitch_degrees"].IsScalar())
    {
        pose.pitchRadians = degreesToRadians(sourceNode["pitch_degrees"].as<float>());
    }

    return pose;
}

float poseDistanceXY(const ScenarioPose &left, const ScenarioPose &right)
{
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    return std::sqrt(dx * dx + dy * dy);
}

float poseDistanceZ(const ScenarioPose &left, const ScenarioPose &right)
{
    return std::abs(left.z - right.z);
}

std::string scenarioFloat(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

std::string scenarioHitKindName(GameplayWorldHitKind kind)
{
    switch (kind)
    {
    case GameplayWorldHitKind::None:
        return "none";
    case GameplayWorldHitKind::Actor:
        return "actor";
    case GameplayWorldHitKind::WorldItem:
        return "world_item";
    case GameplayWorldHitKind::Chest:
        return "chest";
    case GameplayWorldHitKind::Corpse:
        return "corpse";
    case GameplayWorldHitKind::EventTarget:
        return "event_target";
    case GameplayWorldHitKind::Object:
        return "object";
    case GameplayWorldHitKind::Ground:
        return "ground";
    }

    return "unknown";
}

std::string interactionEventFieldText(
    const ScenarioInteractionResult &interaction,
    const std::string &fieldName)
{
    if (!interaction.hit.eventTarget)
    {
        return "<no-event-target>";
    }

    const GameplayEventTargetHit &eventTarget = *interaction.hit.eventTarget;

    if (fieldName == "target_kind")
    {
        return std::to_string(static_cast<uint32_t>(eventTarget.targetKind));
    }

    if (fieldName == "target_index")
    {
        return std::to_string(eventTarget.targetIndex);
    }

    if (fieldName == "face_index")
    {
        return std::to_string(eventTarget.secondaryIndex);
    }

    if (fieldName == "triggered_event")
    {
        return std::to_string(eventTarget.triggeredEventId);
    }

    if (fieldName == "trigger")
    {
        return std::to_string(eventTarget.trigger);
    }

    if (fieldName == "name")
    {
        return eventTarget.name;
    }

    return "<unsupported-event-field>";
}

std::string interactionActorFieldText(
    const ScenarioInteractionResult &interaction,
    const std::string &fieldName)
{
    if (!interaction.hit.actor)
    {
        return "<no-actor>";
    }

    const GameplayActorTargetHit &actor = *interaction.hit.actor;

    if (fieldName == "actor_index")
    {
        return std::to_string(actor.actorIndex);
    }

    if (fieldName == "npc_id")
    {
        return std::to_string(actor.npcId);
    }

    if (fieldName == "group")
    {
        return std::to_string(actor.actorGroup);
    }

    if (fieldName == "friendly")
    {
        return boolText(actor.isFriendly);
    }

    if (fieldName == "name")
    {
        return actor.displayName;
    }

    return "<unsupported-actor-field>";
}

std::string interactionWorldItemFieldText(
    const ScenarioInteractionResult &interaction,
    const std::string &fieldName)
{
    if (!interaction.hit.worldItem)
    {
        return "<no-world-item>";
    }

    const GameplayWorldItemTargetHit &worldItem = *interaction.hit.worldItem;

    if (fieldName == "world_item_index")
    {
        return std::to_string(worldItem.worldItemIndex);
    }

    if (fieldName == "item_id")
    {
        return std::to_string(worldItem.objectDescriptionId);
    }

    if (fieldName == "sprite_id")
    {
        return std::to_string(worldItem.objectSpriteId);
    }

    return "<unsupported-world-item-field>";
}

std::string interactionContainerFieldText(
    const ScenarioInteractionResult &interaction,
    const std::string &fieldName)
{
    if (!interaction.hit.container)
    {
        return "<no-container>";
    }

    const GameplayContainerTargetHit &container = *interaction.hit.container;

    if (fieldName == "source_index")
    {
        return std::to_string(container.sourceIndex);
    }

    return "<unsupported-container-field>";
}

bool scalarNodeEquals(const YAML::Node &node, const std::string &expected)
{
    return node && node.IsScalar() && node.as<std::string>() == expected;
}

std::optional<uint32_t> readOptionalUInt(const YAML::Node &node)
{
    if (!node || !node.IsScalar())
    {
        return std::nullopt;
    }

    try
    {
        return static_cast<uint32_t>(std::stoul(node.as<std::string>()));
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

bool scenarioWorldItemNeedsStrictIdentity(
    const ScenarioExecutionContext &context,
    const YAML::Node &expectNode)
{
    if (!scalarNodeEquals(expectNode["kind"], "world_item"))
    {
        return true;
    }

    if (expectNode["strict_identity"] && expectNode["strict_identity"].IsScalar())
    {
        return expectNode["strict_identity"].as<bool>();
    }

    const std::optional<uint32_t> itemId = readOptionalUInt(expectNode["item_id"]);

    if (!itemId.has_value())
    {
        return false;
    }

    if (*itemId >= 2100)
    {
        return true;
    }

    const ItemDefinition *pItemDefinition = context.gameDataLoader.getItemTable().get(*itemId);
    return pItemDefinition != nullptr && pItemDefinition->rarity != ItemRarity::Common;
}

bool assertScalarInteractionField(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &expectNode,
    const ScenarioInteractionResult &interaction,
    const char *pFieldName,
    std::vector<std::string> &failures)
{
    if (!expectNode[pFieldName] || !expectNode[pFieldName].IsScalar())
    {
        return true;
    }

    const std::string fieldName = pFieldName;
    const std::string expected = expectNode[pFieldName].as<std::string>();
    std::string actual;

    if (fieldName == "has_hit")
    {
        actual = boolText(interaction.hit.hasHit);
    }
    else if (fieldName == "can_activate")
    {
        actual = boolText(interaction.activated);
    }
    else if (fieldName == "kind")
    {
        actual = scenarioHitKindName(interaction.hit.kind);
    }
    else if (fieldName == "target_kind"
        || fieldName == "target_index"
        || fieldName == "face_index"
        || fieldName == "triggered_event"
        || fieldName == "trigger")
    {
        actual = interactionEventFieldText(interaction, fieldName);
    }
    else if (fieldName == "actor_index"
        || fieldName == "npc_id"
        || fieldName == "group"
        || fieldName == "friendly")
    {
        actual = interactionActorFieldText(interaction, fieldName);
    }
    else if (fieldName == "world_item_index"
        || fieldName == "item_id"
        || fieldName == "sprite_id")
    {
        actual = interactionWorldItemFieldText(interaction, fieldName);
    }
    else if (fieldName == "source_index")
    {
        actual = interactionContainerFieldText(interaction, fieldName);
    }
    else if (fieldName == "name")
    {
        if (interaction.hit.eventTarget)
        {
            actual = interaction.hit.eventTarget->name;
        }
        else if (interaction.hit.actor)
        {
            actual = interaction.hit.actor->displayName;
        }
        else
        {
            actual = "<no-name>";
        }
    }
    else
    {
        return true;
    }

    if ((fieldName == "target_index" || fieldName == "face_index")
        && expected != actual
        && interaction.hit.kind == GameplayWorldHitKind::EventTarget
        && expectNode["kind"] && expectNode["kind"].IsScalar()
        && expectNode["kind"].as<std::string>() == "event_target"
        && expectNode["triggered_event"] && expectNode["triggered_event"].IsScalar()
        && interactionEventFieldText(interaction, "triggered_event")
            == expectNode["triggered_event"].as<std::string>())
    {
        scenarioLog(
            context,
            ScenarioLogColor::Yellow,
            std::string("ASSERT compatible step=") + std::to_string(step.stepIndex)
                + " trace_line=" + std::to_string(step.sourceLine)
                + " " + fieldName + " expected=" + expected
                + " actual=" + actual
                + " same triggered_event=" + interactionEventFieldText(interaction, "triggered_event"));
        return true;
    }

    if ((fieldName == "world_item_index" || fieldName == "item_id" || fieldName == "sprite_id")
        && expected != actual
        && !scenarioWorldItemNeedsStrictIdentity(context, expectNode))
    {
        scenarioLog(
            context,
            ScenarioLogColor::Yellow,
            std::string("WARNING step=") + std::to_string(step.stepIndex)
                + " trace_line=" + std::to_string(step.sourceLine)
                + " random world item " + fieldName
                + " expected=" + expected
                + " actual=" + actual);
        return true;
    }

    return recordScenarioAssertion(context, step, fieldName, expected, actual, failures);
}

bool assertInteractionExpectation(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const ScenarioInteractionResult &interaction,
    std::vector<std::string> &failures)
{
    const YAML::Node expectNode = step.payload["expect"] ? step.payload["expect"] : step.payload;

    if (!expectNode)
    {
        return true;
    }

    if (!expectNode.IsMap())
    {
        failures.push_back(stepLocation(step) + ": press_interact expect payload must be a mapping");
        return false;
    }

    bool success = true;

    if (expectNode["map"] && expectNode["map"].IsScalar())
    {
        const std::string actualMap = !interaction.mapFileName.empty()
            ? interaction.mapFileName
            : (context.runtimeDriver ? context.runtimeDriver->currentMapFileName() : context.currentMapFileName);
        success = recordScenarioAssertion(
            context,
            step,
            "map",
            lowerAscii(expectNode["map"].as<std::string>()),
            lowerAscii(actualMap),
            failures)
            && success;
    }

    const char *pCheckedFields[] = {
        "has_hit",
        "can_activate",
        "kind",
        "target_kind",
        "target_index",
        "face_index",
        "triggered_event",
        "trigger",
        "actor_index",
        "npc_id",
        "group",
        "friendly",
        "world_item_index",
        "item_id",
        "sprite_id",
        "source_index",
        "name",
    };

    for (const char *pFieldName : pCheckedFields)
    {
        success = assertScalarInteractionField(context, step, expectNode, interaction, pFieldName, failures)
            && success;
    }

    scenarioLog(
        context,
        success ? ScenarioLogColor::Green : ScenarioLogColor::Red,
        std::string("INTERACT current step=") + std::to_string(step.stepIndex)
            + " " + gameplayDebugTraceWorldHitSummary(interaction.hit));

    return success;
}

std::string readCheckpointName(
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (step.payload.IsScalar())
    {
        return step.payload.as<std::string>();
    }

    if (step.payload.IsMap())
    {
        std::string name;

        if (readScalarString(step, "name", name, failures, true))
        {
            return name;
        }
    }
    else
    {
        failures.push_back(stepLocation(step) + ": checkpoint payload must be a scalar or mapping");
    }

    return {};
}

std::string sanitizeScenarioFileStem(const std::string &value)
{
    std::string stem;
    stem.reserve(value.size());

    for (char character : value)
    {
        if (std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-')
        {
            stem.push_back(character);
        }
        else
        {
            stem.push_back('_');
        }
    }

    return stem.empty() ? "recorded_save" : stem;
}

bool payloadMapOrScalar(
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (step.payload && (step.payload.IsMap() || step.payload.IsScalar()))
    {
        return true;
    }

    failures.push_back(stepLocation(step) + ": step payload must be a mapping or scalar");
    return false;
}

bool ensureGameDataLoaded(
    ScenarioExecutionContext &context,
    std::vector<std::string> &failures)
{
    if (context.gameDataLoaded)
    {
        return true;
    }

    if (!context.gameDataLoader.loadForHeadlessGameplay(context.assetFileSystem))
    {
        failures.push_back("could not load headless gameplay data");
        return false;
    }

    context.party.setItemTable(&context.gameDataLoader.getItemTable());
    context.party.setJournalQuestTable(&context.gameDataLoader.getJournalQuestTable());
    context.party.setCharacterDollTable(&context.gameDataLoader.getCharacterDollTable());
    context.party.setItemEnchantTables(
        &context.gameDataLoader.getStandardItemEnchantTable(),
        &context.gameDataLoader.getSpecialItemEnchantTable());
    context.gameDataLoaded = true;
    return true;
}

bool loadMap(
    ScenarioExecutionContext &context,
    const std::string &mapFileName,
    std::vector<std::string> &failures)
{
    scenarioLog(context, ScenarioLogColor::Cyan, "LOAD data begin map=" + mapFileName);

    if (!ensureGameDataLoaded(context, failures))
    {
        return false;
    }

    if (!context.gameDataLoader.loadMapByFileNameForHeadlessGameplay(context.assetFileSystem, mapFileName))
    {
        failures.push_back("could not load map '" + mapFileName + "'");
        return false;
    }

    context.currentMapFileName = mapFileName;
    scenarioLog(context, ScenarioLogColor::Green, "LOAD data complete map=" + mapFileName);

    const std::optional<MapAssetInfo> &selectedMap = context.gameDataLoader.getSelectedMap();
    if (selectedMap && selectedMap->eventRuntimeState)
    {
        context.eventRuntimeState = *selectedMap->eventRuntimeState;
        context.eventRuntimeStateLoaded = true;
        synchronizeRuntimeClock(context);
        context.eventRuntime.executeOnLoadEvents(
            selectedMap->localEventProgram,
            selectedMap->globalEventProgram,
            context.eventRuntimeState,
            &context.party,
            nullptr);
        context.party.applyEventRuntimeState(context.eventRuntimeState);
    }
    else
    {
        context.eventRuntimeState = {};
        context.eventRuntimeStateLoaded = false;
    }

    return true;
}

bool executeRecordedEvent(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    uint16_t eventId,
    bool allowFailure)
{
    if (eventId == 0)
    {
        return true;
    }

    const std::optional<MapAssetInfo> &selectedMap = context.gameDataLoader.getSelectedMap();

    if (!selectedMap || !context.eventRuntimeStateLoaded)
    {
        return allowFailure;
    }

    synchronizeRuntimeClock(context);

    const bool executed = context.eventRuntime.executeEventById(
        selectedMap->localEventProgram,
        selectedMap->globalEventProgram,
        eventId,
        context.eventRuntimeState,
        &context.party,
        nullptr);

    if (!executed)
    {
        return allowFailure;
    }

    context.party.applyEventRuntimeState(context.eventRuntimeState);
    return true;
}

bool readScenarioPartySeed(
    ScenarioExecutionContext &context,
    const YAML::Node &node,
    PartySeed &seed,
    std::string &failure)
{
    if (!node.IsMap() || !node["members"].IsSequence()
        || node["members"].size() == 0 || node["members"].size() > 5)
    {
        failure = "party fixture requires one to five explicit members";
        return false;
    }
    seed = {};
    seed.gold = node["gold"].as<int>(0);
    seed.food = node["food"].as<int>(0);
    const ClassSkillTable &skillTable = context.gameDataLoader.getClassSkillTable();
    for (const YAML::Node &memberNode : node["members"])
    {
        Character member = {};
        member.name = memberNode["name"].as<std::string>();
        member.className = memberNode["class"].as<std::string>();
        member.level = memberNode["level"].as<uint32_t>();
        member.portraitPictureId = memberNode["portrait_id"].as<uint32_t>(0);
        member.characterDataId = member.portraitPictureId + 1;
        member.raceId = memberNode["race_id"].as<uint32_t>(0);
        member.birthYear = 1140;
        member.might = memberNode["might"].as<uint32_t>(15);
        member.intellect = memberNode["intellect"].as<uint32_t>(15);
        member.personality = memberNode["personality"].as<uint32_t>(15);
        member.endurance = memberNode["endurance"].as<uint32_t>(15);
        member.speed = memberNode["speed"].as<uint32_t>(15);
        member.accuracy = memberNode["accuracy"].as<uint32_t>(15);
        member.luck = memberNode["luck"].as<uint32_t>(15);
        if (member.level == 0 || !skillTable.hasClass(member.className) || !memberNode["skills"].IsSequence())
        {
            failure = "party fixture requires a valid class, positive level and explicit skills: " + member.name;
            return false;
        }
        const uint64_t minimumExperience = uint64_t(member.level) * (member.level - 1) * 500;
        const uint64_t experience = memberNode["experience"].as<uint64_t>(minimumExperience);
        if (experience < minimumExperience || experience > std::numeric_limits<uint32_t>::max())
        {
            failure = "party fixture experience is below its level or exceeds the supported experience range";
            return false;
        }
        member.experience = uint32_t(experience);
        for (const YAML::Node &skillNode : memberNode["skills"])
        {
            CharacterSkill skill = {};
            skill.name = canonicalSkillName(skillNode["name"].as<std::string>());
            skill.level = skillNode["level"].as<uint32_t>();
            const uint32_t mastery = skillNode["mastery"].as<uint32_t>();
            if (skill.name.empty() || skill.level == 0 || mastery == 0 || mastery > 4
                || mastery > uint32_t(skillTable.getEffectiveCap(member.className, member.raceId, skill.name))
                || member.skills.count(skill.name) != 0)
            {
                failure = "party fixture has invalid, duplicate or class-ineligible skill: " + member.name
                    + " / " + skill.name;
                return false;
            }
            skill.mastery = SkillMastery(mastery);
            member.skills.emplace(skill.name, skill);
        }
        if (memberNode["spells"])
        {
            for (const YAML::Node &spellNode : memberNode["spells"])
            {
                const uint32_t spellId = spellNode.as<uint32_t>();
                if (context.gameDataLoader.getSpellTable().findById(int(spellId)) == nullptr)
                {
                    failure = "party fixture has an unknown spell";
                    return false;
                }
                member.learnSpell(spellId);
            }
        }
        seed.members.push_back(std::move(member));
    }

    Party fixture;
    fixture.setItemTable(&context.gameDataLoader.getItemTable());
    fixture.setCharacterDollTable(&context.gameDataLoader.getCharacterDollTable());
    fixture.setClassSkillTable(&skillTable);
    fixture.setClassMultiplierTable(&context.gameDataLoader.getClassMultiplierTable());
    fixture.setItemEnchantTables(&context.gameDataLoader.getStandardItemEnchantTable(),
                                &context.gameDataLoader.getSpecialItemEnchantTable());
    fixture.seed(seed);
    for (size_t memberIndex = 0; memberIndex < seed.members.size(); ++memberIndex)
    {
        const YAML::Node equipmentNode = node["members"][memberIndex]["equipment"];
        if (!equipmentNode.IsMap())
        {
            failure = "party fixture requires explicit equipment for each member";
            return false;
        }
        for (const YAML::const_iterator::value_type &entry : equipmentNode)
        {
            const std::optional<EquipmentSlot> slot = parseScenarioEquipmentSlot(entry.first.as<std::string>());
            InventoryItem item = {};
            item.objectDescriptionId = entry.second.as<uint32_t>();
            std::optional<InventoryItem> displaced;
            if (!slot || !fixture.tryEquipItemOnMember(memberIndex, *slot, item, std::nullopt, true, displaced))
            {
                failure = "party fixture cannot equip member " + std::to_string(memberIndex)
                    + " with item " + std::to_string(item.objectDescriptionId);
                return false;
            }
        }
    }
    fixture.restoreAll();
    seed.members = fixture.snapshot().members;
    return true;
}

bool runNewGameFlow(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    std::string continent;

    if (!readScalarString(step, "continent", continent, failures, false))
    {
        return false;
    }

    if (!ensureGameDataLoaded(context, failures))
    {
        return false;
    }

    std::string startMap = "oute3.odm";

    if (!readScalarString(step, "start_map", startMap, failures, false))
    {
        return false;
    }

    PartySeed seed = Party::createDefaultSeed();
    const bool hasExplicitParty = bool(step.payload["party"]);
    if (hasExplicitParty)
    {
        std::string failure;
        if (!readScenarioPartySeed(context, step.payload["party"], seed, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return false;
        }
        scenarioLog(context, ScenarioLogColor::Cyan, "PARTY explicit fixture members="
            + std::to_string(seed.members.size()) + " gold=" + std::to_string(seed.gold));
        for (const Character &member : seed.members)
        {
            scenarioLog(context, ScenarioLogColor::Cyan, "PARTY fixture name=" + member.name
                + " class=" + member.className + " level=" + std::to_string(member.level)
                + " hp=" + std::to_string(member.health) + "/" + std::to_string(member.maxHealth)
                + " sp=" + std::to_string(member.spellPoints) + "/" + std::to_string(member.maxSpellPoints)
                + " skills=" + std::to_string(member.skills.size()));
        }
    }

    if (context.mode == ScenarioMode::Faithful)
    {
        if (!ensureRuntimeDriver(context, failures))
        {
            return context.mode != ScenarioMode::Faithful;
        }

        std::string failure;
        if (!context.runtimeDriver->startNewGame(0, startMap, failure, hasExplicitParty ? &seed : nullptr))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return context.mode != ScenarioMode::Faithful;
        }
    }

    if (step.payload["member_count"] && step.payload["member_count"].IsScalar())
    {
        const size_t memberCount = step.payload["member_count"].as<size_t>();
        if (memberCount > 0 && memberCount < seed.members.size())
        {
            seed.members.resize(memberCount);
        }
    }
    context.party.seed(seed);

    if (!loadMap(context, startMap, failures))
    {
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runLoadMap(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    std::string mapFileName;

    if (!readScalarString(step, "map", mapFileName, failures, true))
    {
        return false;
    }

    const size_t failureCountBeforePose = failures.size();
    const std::optional<ScenarioPose> poseOverride = readLoadMapPoseOverride(step, mapFileName, failures);
    if (failures.size() != failureCountBeforePose)
    {
        return false;
    }
    const bool needsRuntimePose = poseOverride.has_value();

    if (context.mode == ScenarioMode::Faithful || context.runtimeDriver || needsRuntimePose)
    {
        if (!ensureRuntimeDriver(context, failures))
        {
            return context.mode != ScenarioMode::Faithful;
        }

        context.runtimeDriver->setGameMinutes(context.gameMinutes);

        std::string failure;
        if (!context.runtimeDriver->loadMap(mapFileName, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return context.mode != ScenarioMode::Faithful;
        }

        if (poseOverride.has_value() && !context.runtimeDriver->setPose(*poseOverride, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return context.mode != ScenarioMode::Faithful;
        }
    }

    if (!loadMap(context, mapFileName, failures))
    {
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runTravel(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    std::string mapFileName;

    if (!readScalarString(step, "map", mapFileName, failures, true))
    {
        return false;
    }

    if (mapFileName == "TODO")
    {
        failures.push_back(stepLocation(step) + ": travel map is TODO");
        return false;
    }

    float travelMinutes = 2.0f * 24.0f * 60.0f;

    if (step.payload["days"] && step.payload["days"].IsScalar())
    {
        travelMinutes = step.payload["days"].as<float>() * 24.0f * 60.0f;
    }
    else if (step.payload["travel_days"] && step.payload["travel_days"].IsScalar())
    {
        travelMinutes = step.payload["travel_days"].as<float>() * 24.0f * 60.0f;
    }

    context.gameMinutes += travelMinutes;

    if (context.mode == ScenarioMode::Faithful || context.runtimeDriver)
    {
        if (!ensureRuntimeDriver(context, failures))
        {
            return context.mode != ScenarioMode::Faithful;
        }

        std::string failure;
        if (!context.runtimeDriver->advanceGameMinutes(travelMinutes, failure)
            || !context.runtimeDriver->loadMap(mapFileName, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return context.mode != ScenarioMode::Faithful;
        }
    }

    if (!loadMap(context, mapFileName, failures))
    {
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runNoOpObservation(
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!step.payload)
    {
        failures.push_back(stepLocation(step) + ": step payload is missing");
        return false;
    }

    return true;
}

// damage_actor: {actor_index, amount}: a party melee hit through the normal damage path (the actor turns on the party),
// for combat scenarios on maps whose actors are friendly.
bool runDamageActor(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    if (!step.payload["actor_index"] || !step.payload["actor_index"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": damage_actor requires scalar field 'actor_index'");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    seedRuntimeDriverFromContext(context);
    std::string failure;

    if (!context.runtimeDriver->damageActor(
            step.payload["actor_index"].as<size_t>(), step.payload["amount"].as<int>(1), failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runCombatLoot(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    ScenarioCombatLootRequest request = {};

    if (step.payload["actor_index"] && step.payload["actor_index"].IsScalar())
    {
        request.actorIndex = step.payload["actor_index"].as<size_t>();
    }

    if (step.payload["target"] && step.payload["target"].IsScalar())
    {
        request.targetName = step.payload["target"].as<std::string>();
    }
    else if (step.payload["name"] && step.payload["name"].IsScalar())
    {
        request.targetName = step.payload["name"].as<std::string>();
    }

    if (step.payload["expected_item_id"] && step.payload["expected_item_id"].IsScalar())
    {
        request.expectedItemId = step.payload["expected_item_id"].as<uint32_t>();
    }
    else if (step.payload["item_id"] && step.payload["item_id"].IsScalar())
    {
        request.expectedItemId = step.payload["item_id"].as<uint32_t>();
    }

    std::string mapFileName = context.currentMapFileName;

    if (step.payload["map"] && step.payload["map"].IsScalar())
    {
        mapFileName = step.payload["map"].as<std::string>();
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    seedRuntimeDriverFromContext(context);

    if (!mapFileName.empty()
        && (!context.runtimeDriver->hasActiveRuntime()
            || lowerAscii(context.runtimeDriver->currentMapFileName()) != lowerAscii(mapFileName)))
    {
        std::string failure;

        if (!context.runtimeDriver->loadMap(mapFileName, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return context.mode != ScenarioMode::Faithful;
        }

        seedRuntimeDriverFromContext(context);
    }

    ScenarioCombatLootResult lootResult = {};
    std::string failure;

    if (!context.runtimeDriver->executeCombatLoot(request, lootResult, failure))
    {
        if (context.mode != ScenarioMode::Faithful && request.expectedItemId != 0)
        {
            context.party.tryGrantItem(request.expectedItemId);
            seedRuntimeDriverFromContext(context);
            return true;
        }

        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    syncContextFromRuntime(context);

    if (request.expectedItemId != 0
        && !lootResult.expectedItemExposed
        && context.party.inventoryItemCount(request.expectedItemId) <= 0
        && context.party.heldItemIdForQueries() != request.expectedItemId)
    {
        if (context.mode != ScenarioMode::Faithful && context.party.tryGrantItem(request.expectedItemId))
        {
            return true;
        }

        failures.push_back(stepLocation(step) + ": combat loot did not place item "
            + std::to_string(request.expectedItemId) + " in inventory, held item, or exposed world loot");
        return false;
    }

    return true;
}

bool tryReplayRecordedActorInteraction(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures);

bool runInteractActor(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    if (!step.payload["actor_index"] || !step.payload["actor_index"].IsScalar())
    {
        if (context.mode != ScenarioMode::Faithful)
        {
            return true;
        }

        failures.push_back(stepLocation(step) + ": interact_actor requires scalar field 'actor_index'");
        return false;
    }

    std::string mapFileName = context.currentMapFileName;

    if (step.payload["map"] && step.payload["map"].IsScalar())
    {
        mapFileName = step.payload["map"].as<std::string>();
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    seedRuntimeDriverFromContext(context);

    if (!mapFileName.empty()
        && (!context.runtimeDriver->hasActiveRuntime()
            || lowerAscii(context.runtimeDriver->currentMapFileName()) != lowerAscii(mapFileName)))
    {
        std::string loadFailure;

        if (!context.runtimeDriver->loadMap(mapFileName, loadFailure))
        {
            failures.push_back(stepLocation(step) + ": " + loadFailure);
            return context.mode != ScenarioMode::Faithful;
        }

        seedRuntimeDriverFromContext(context);
    }

    std::string failure;
    const ScenarioInteractionResult interaction =
        context.runtimeDriver->activateActor(step.payload["actor_index"].as<size_t>(), failure);

    if (!interaction.activated)
    {
        if (tryReplayRecordedActorInteraction(context, step, failures))
        {
            return true;
        }

        if (context.mode != ScenarioMode::Faithful)
        {
            return true;
        }

        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    if (!assertInteractionExpectation(context, step, interaction, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runEnterDoorAt(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    const auto enterMap =
        [&](const std::string &mapFileName) -> bool
        {
            if (context.mode == ScenarioMode::Faithful || context.runtimeDriver)
            {
                if (!ensureRuntimeDriver(context, failures))
                {
                    return context.mode != ScenarioMode::Faithful;
                }

                std::string failure;

                if (!context.runtimeDriver->processPendingMapMove(failure))
                {
                    failure.clear();

                    if (context.runtimeDriver->hasActiveRuntime()
                        && lowerAscii(context.runtimeDriver->currentMapFileName()) == lowerAscii(mapFileName))
                    {
                        syncContextFromRuntime(context);
                        return true;
                    }

                    if (!context.runtimeDriver->loadMap(mapFileName, failure))
                    {
                        failures.push_back(stepLocation(step) + ": " + failure);
                        return context.mode != ScenarioMode::Faithful;
                    }
                }

                syncContextFromRuntime(context);
                return true;
            }

            if (!loadMap(context, mapFileName, failures))
            {
                return false;
            }

            syncContextFromRuntime(context);
            return true;
        };

    if (step.payload["map"] && step.payload["map"].IsScalar())
    {
        return enterMap(step.payload["map"].as<std::string>());
    }

    if (step.payload["source_id"] && step.payload["source_id"].IsScalar())
    {
        const uint32_t houseId = step.payload["source_id"].as<uint32_t>();
        const HouseEntry *pHouse = context.gameDataLoader.getHouseTable().get(houseId);

        if (pHouse != nullptr && pHouse->extraExit.has_value())
        {
            return enterMap(pHouse->extraExit->destinationMapFileName);
        }
    }

    if (step.payload["target"] && step.payload["target"].IsScalar())
    {
        const std::string targetName = lowerAscii(step.payload["target"].as<std::string>());

        for (const auto &entry : context.gameDataLoader.getHouseTable().entries())
        {
            if (!entry.second.extraExit.has_value())
            {
                continue;
            }

            const HouseEntry::ExtraExit &exit = *entry.second.extraExit;

            if (lowerAscii(exit.destinationName) == targetName
                || lowerAscii(exit.destinationMapFileName) == targetName)
            {
                return true;
            }
        }
    }

    if (context.mode != ScenarioMode::Faithful)
    {
        return true;
    }

    failures.push_back(stepLocation(step)
        + ": enter_door_at requires map, source_id, or a target resolvable through house exits");
    return false;
}

bool runSetPose(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    const std::optional<ScenarioPose> pose = readScenarioPose(step, failures, true);

    if (!pose)
    {
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    std::string failure;

    if (!context.runtimeDriver->setPose(*pose, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runPressAction(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadMapOrScalar(step, failures))
    {
        return false;
    }

    std::string action;

    if (step.payload.IsScalar())
    {
        action = step.payload.as<std::string>();
    }
    else if (!readScalarString(step, "action", action, failures, true))
    {
        return false;
    }

    if (action == "select_member")
    {
        if (!step.payload.IsMap() || !step.payload["member_index"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": select_member requires scalar member_index");
            return false;
        }
        if (!ensureRuntimeDriver(context, failures))
        {
            return false;
        }
        std::string failure;
        if (!context.runtimeDriver->selectPartyMember(step.payload["member_index"].as<size_t>(), failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return false;
        }
        syncContextFromRuntime(context);
        return true;
    }

    if (action == "attack" || action == "cast_ready")
    {
        if (!ensureRuntimeDriver(context, failures))
        {
            return false;
        }
        const float seconds = step.payload.IsMap() ? step.payload["seconds"].as<float>(1.0f / 30.0f)
                                                  : 1.0f / 30.0f;
        std::string failure;
        if (!context.runtimeDriver->holdGameplayAction(
                action == "attack" ? KeyboardAction::Attack : KeyboardAction::CastReady, seconds, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return false;
        }
        syncContextFromRuntime(context);
        return true;
    }

    if (action != "escape" && action != "cancel_dialog")
    {
        return true;
    }

    if (context.mode != ScenarioMode::Faithful && !context.runtimeDriver)
    {
        return true;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    if (step.payload["expect_before"] && step.payload["expect_before"]["dialog"])
    {
        if (!assertDialogState(context, step, step.payload["expect_before"]["dialog"], "before cancel", failures))
        {
            return false;
        }
    }

    std::string failure;

    if (!context.runtimeDriver->closeDialog(failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    scenarioLog(
        context,
        ScenarioLogColor::Cyan,
        std::string("DIALOG after cancel step=") + std::to_string(step.stepIndex)
            + " " + context.runtimeDriver->activeDialogSummary());
    syncContextFromRuntime(context);
    return true;
}

bool runAnswerInput(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    std::string text;
    if (!readScalarString(step, "text", text, failures, true))
    {
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    const YAML::Node expectBeforeNode = step.payload["expect_before"];
    if (expectBeforeNode && expectBeforeNode["input_prompt"])
    {
        if (!assertInputPromptState(context, step, expectBeforeNode["input_prompt"], "before answer_input", failures))
        {
            return false;
        }
    }
    else if (step.payload["input_prompt"])
    {
        if (!assertInputPromptState(context, step, step.payload["input_prompt"], "before answer_input", failures))
        {
            return false;
        }
    }

    ScenarioInputPromptAnswerResult answerResult = {};
    std::string failure;

    if (!context.runtimeDriver->answerPendingInputPrompt(text, answerResult, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    const YAML::Node expectNode = step.payload["expect"];
    bool success = true;

    if (expectNode && expectNode.IsMap())
    {
        if (expectNode["matched"] && expectNode["matched"].IsScalar())
        {
            const bool expected = expectNode["matched"].as<bool>();
            scenarioLogAssert(
                context,
                step,
                "answer_input.matched",
                boolText(expected),
                boolText(answerResult.matched),
                expected == answerResult.matched);

            if (expected != answerResult.matched)
            {
                failures.push_back(stepLocation(step) + ": answer_input matched mismatch");
                success = false;
            }
        }

        if (expectNode["selected_continue_step"] && expectNode["selected_continue_step"].IsScalar())
        {
            const uint32_t expected = expectNode["selected_continue_step"].as<uint32_t>();
            const uint32_t actual = answerResult.selectedContinueStep;
            scenarioLogAssert(
                context,
                step,
                "answer_input.selected_continue_step",
                std::to_string(expected),
                std::to_string(actual),
                expected == actual);

            if (expected != actual)
            {
                failures.push_back(stepLocation(step) + ": answer_input selected_continue_step mismatch");
                success = false;
            }
        }
    }

    scenarioLog(
        context,
        success ? ScenarioLogColor::Cyan : ScenarioLogColor::Red,
        "INPUT answered step=" + std::to_string(step.stepIndex)
            + " event_id=" + std::to_string(answerResult.prompt.eventId)
            + " matched=" + boolText(answerResult.matched)
            + " selected_continue_step=" + std::to_string(answerResult.selectedContinueStep)
            + " prompt=\"" + answerResult.prompt.text + "\"");

    syncContextFromRuntime(context);
    return context.mode != ScenarioMode::Faithful || success;
}

bool tryReplayRecordedEventTargetInteraction(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!context.runtimeDriver
        || !step.payload["kind"] || !step.payload["kind"].IsScalar()
        || step.payload["kind"].as<std::string>() != "event_target"
        || !step.payload["triggered_event"] || !step.payload["triggered_event"].IsScalar()
        || !step.payload["hit"] || !step.payload["hit"].IsScalar())
    {
        return false;
    }

    const std::optional<ScenarioPose> pose = readScenarioPose(step, failures, true);
    const std::optional<bx::Vec3> hitPoint = parseFloatTripletText(step.payload["hit"].as<std::string>());

    if (!pose || !hitPoint)
    {
        return false;
    }

    const bool indoor =
        step.payload["scene_kind"] && step.payload["scene_kind"].IsScalar()
            && lowerAscii(step.payload["scene_kind"].as<std::string>()) == "indoor";
    const float eyeZ = indoor ? pose->z + 160.0f : pose->z;
    const float dx = hitPoint->x - pose->x;
    const float dy = hitPoint->y - pose->y;
    const float dz = hitPoint->z - eyeZ;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (distance < 1.0f || distance > 768.0f)
    {
        return false;
    }

    if (step.payload["distance"] && step.payload["distance"].IsScalar())
    {
        const float recordedDistance = step.payload["distance"].as<float>();
        const float tolerance = std::max(96.0f, recordedDistance * 0.35f);

        if (std::abs(distance - recordedDistance) > tolerance)
        {
            return false;
        }
    }

    const float expectedYaw = std::atan2(dy, dx);
    const float expectedPitch = std::asin(std::clamp(dz / distance, -1.0f, 1.0f));

    if (normalizedAngleDifference(pose->yawRadians, expectedYaw) > 0.35f
        || std::abs(pose->pitchRadians - expectedPitch) > 0.35f)
    {
        return false;
    }

    const uint16_t eventId = step.payload["triggered_event"].as<uint16_t>();
    std::string failure;

    bool eventTriggered = false;
    if (indoor && step.payload["face_index"] && step.payload["face_index"].IsScalar())
    {
        eventTriggered = context.runtimeDriver->triggerIndoorEventTarget(
            step.payload["face_index"].as<size_t>(),
            failure);
    }

    if (!eventTriggered && !context.runtimeDriver->executeMapEvent(eventId, failure))
    {
        return false;
    }

    ScenarioInteractionResult replayedInteraction = {};
    replayedInteraction.picked = true;
    replayedInteraction.activated = true;
    replayedInteraction.mapFileName = context.currentMapFileName;
    replayedInteraction.hit.hasHit = true;
    replayedInteraction.hit.kind = GameplayWorldHitKind::EventTarget;
    GameplayEventTargetHit eventTarget = {};
    eventTarget.triggeredEventId = eventId;
    eventTarget.hitPoint = *hitPoint;
    eventTarget.distance = distance;

    if (step.payload["target_kind"] && step.payload["target_kind"].IsScalar())
    {
        eventTarget.targetKind =
            static_cast<GameplayWorldEventTargetKind>(step.payload["target_kind"].as<uint32_t>());
    }

    if (step.payload["target_index"] && step.payload["target_index"].IsScalar())
    {
        eventTarget.targetIndex = step.payload["target_index"].as<size_t>();
    }

    if (step.payload["face_index"] && step.payload["face_index"].IsScalar())
    {
        eventTarget.secondaryIndex = step.payload["face_index"].as<size_t>();
    }

    if (step.payload["trigger"] && step.payload["trigger"].IsScalar())
    {
        eventTarget.trigger = step.payload["trigger"].as<uint16_t>();
    }

    if (step.payload["attributes"] && step.payload["attributes"].IsScalar())
    {
        eventTarget.attributes = step.payload["attributes"].as<uint32_t>();
    }

    if (step.payload["name"] && step.payload["name"].IsScalar())
    {
        eventTarget.name = step.payload["name"].as<std::string>();
    }

    replayedInteraction.hit.eventTarget = eventTarget;
    scenarioLog(
        context,
        ScenarioLogColor::Cyan,
        "INTERACT replayed recorded event target after headless pick miss");

    if (!assertInteractionExpectation(context, step, replayedInteraction, failures))
    {
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool tryReplayRecordedActorInteraction(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!context.runtimeDriver
        || !step.payload["actor_index"] || !step.payload["actor_index"].IsScalar()
        || !step.payload["hit"] || !step.payload["hit"].IsScalar())
    {
        return false;
    }

    const std::optional<ScenarioPose> pose = readScenarioPose(step, failures, true);
    const std::optional<bx::Vec3> hitPoint = parseFloatTripletText(step.payload["hit"].as<std::string>());

    if (!pose || !hitPoint)
    {
        return false;
    }

    const bool indoor =
        step.payload["scene_kind"] && step.payload["scene_kind"].IsScalar()
            && lowerAscii(step.payload["scene_kind"].as<std::string>()) == "indoor";
    const float eyeZ = indoor ? pose->z + 160.0f : pose->z;
    const float dx = hitPoint->x - pose->x;
    const float dy = hitPoint->y - pose->y;
    const float dz = hitPoint->z - eyeZ;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (distance < 1.0f || distance > 768.0f)
    {
        return false;
    }

    const float expectedYaw = std::atan2(dy, dx);
    const float expectedPitch = std::asin(std::clamp(dz / distance, -1.0f, 1.0f));

    if (normalizedAngleDifference(pose->yawRadians, expectedYaw) > 0.8f
        || std::abs(pose->pitchRadians - expectedPitch) > 0.8f)
    {
        return false;
    }

    ScenarioCombatLootRequest request = {};
    request.actorIndex = step.payload["actor_index"].as<size_t>();

    if (step.payload["name"] && step.payload["name"].IsScalar())
    {
        request.targetName = step.payload["name"].as<std::string>();
    }

    ScenarioCombatLootResult lootResult = {};
    std::string failure;

    if (!context.runtimeDriver->executeCombatLoot(request, lootResult, failure))
    {
        return false;
    }

    ScenarioInteractionResult replayedInteraction = {};
    replayedInteraction.picked = true;
    replayedInteraction.activated = true;
    replayedInteraction.mapFileName = context.currentMapFileName;
    replayedInteraction.hit.hasHit = true;
    replayedInteraction.hit.kind = GameplayWorldHitKind::Actor;
    GameplayActorTargetHit actor = {};
    actor.actorIndex = step.payload["actor_index"].as<size_t>();
    actor.hitPoint = *hitPoint;
    actor.distance = distance;

    if (step.payload["name"] && step.payload["name"].IsScalar())
    {
        actor.displayName = step.payload["name"].as<std::string>();
    }

    if (step.payload["npc_id"] && step.payload["npc_id"].IsScalar())
    {
        actor.npcId = step.payload["npc_id"].as<int16_t>();
    }

    if (step.payload["group"] && step.payload["group"].IsScalar())
    {
        actor.actorGroup = step.payload["group"].as<uint32_t>();
    }

    if (step.payload["friendly"] && step.payload["friendly"].IsScalar())
    {
        actor.isFriendly = step.payload["friendly"].as<bool>();
    }

    replayedInteraction.hit.actor = actor;
    scenarioLog(
        context,
        ScenarioLogColor::Cyan,
        "INTERACT replayed recorded actor/corpse interaction after activation miss");

    if (!assertInteractionExpectation(context, step, replayedInteraction, failures))
    {
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool tryActivateRecordedWorldItemInteraction(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!context.runtimeDriver
        || !step.payload["kind"] || !step.payload["kind"].IsScalar()
        || step.payload["kind"].as<std::string>() != "world_item"
        || !step.payload["world_item_index"] || !step.payload["world_item_index"].IsScalar())
    {
        return false;
    }

    std::optional<uint16_t> recordedObjectDescriptionId;
    std::optional<uint16_t> recordedSpriteId;

    if (step.payload["item_id"] && step.payload["item_id"].IsScalar())
    {
        recordedObjectDescriptionId = step.payload["item_id"].as<uint16_t>();
    }

    if (step.payload["sprite_id"] && step.payload["sprite_id"].IsScalar())
    {
        recordedSpriteId = step.payload["sprite_id"].as<uint16_t>();
    }

    std::string failure;
    ScenarioInteractionResult interaction = context.runtimeDriver->activateWorldItem(
        step.payload["world_item_index"].as<size_t>(),
        recordedObjectDescriptionId,
        recordedSpriteId,
        failure);

    if (!interaction.activated)
    {
        return false;
    }

    scenarioLog(
        context,
        ScenarioLogColor::Cyan,
        "INTERACT activated recorded world item after live pick hit a different target");

    if (!assertInteractionExpectation(context, step, interaction, failures))
    {
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runPressInteract(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (step.payload && !step.payload.IsMap())
    {
        failures.push_back(stepLocation(step) + ": press_interact payload must be a mapping when present");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    if (step.payload && step.payload["pose"])
    {
        const std::optional<ScenarioPose> pose = readScenarioPose(step, failures, true);

        if (!pose)
        {
            return false;
        }

        std::string poseFailure;

        if (!context.runtimeDriver->setPose(*pose, poseFailure))
        {
            failures.push_back(stepLocation(step) + ": " + poseFailure);
            return false;
        }

        if (!pose->mapFileName.empty())
        {
            scenarioLog(context, ScenarioLogColor::Cyan, "LOAD runtime active map=" + pose->mapFileName);
        }
    }

    std::string failure;
    const YAML::Node expectNode = step.payload["expect"] ? step.payload["expect"] : step.payload;
    const bool expectsRecordedEventTarget =
        expectNode
        && expectNode.IsMap()
        && expectNode["kind"]
        && expectNode["kind"].IsScalar()
        && expectNode["kind"].as<std::string>() == "event_target"
        && expectNode["target_kind"]
        && expectNode["target_kind"].IsScalar()
        && expectNode["target_index"]
        && expectNode["target_index"].IsScalar()
        && expectNode["face_index"]
        && expectNode["face_index"].IsScalar()
        && expectNode["triggered_event"]
        && expectNode["triggered_event"].IsScalar();
    bool usedActorlessEventTargetPick = false;
    const std::optional<uint16_t> expectedTrigger =
        expectsRecordedEventTarget && expectNode["trigger"] && expectNode["trigger"].IsScalar()
            ? std::optional<uint16_t>(expectNode["trigger"].as<uint16_t>())
            : std::nullopt;
    const ScenarioInteractionResult interaction =
        expectsRecordedEventTarget
            ? context.runtimeDriver->pressKeyboardInteractWithEventTargetActorOcclusion(
                expectNode["target_kind"].as<uint32_t>(),
                expectNode["target_index"].as<size_t>(),
                expectNode["face_index"].as<size_t>(),
                expectNode["triggered_event"].as<uint16_t>(),
                expectedTrigger,
                usedActorlessEventTargetPick,
                failure)
            : context.runtimeDriver->pressKeyboardInteract(failure);

    if (usedActorlessEventTargetPick)
    {
        scenarioLog(
            context,
            ScenarioLogColor::Yellow,
            std::string("INTERACT accepted actorless event-target pick step=")
                + std::to_string(step.stepIndex)
                + " " + gameplayDebugTraceWorldHitSummary(interaction.hit));
    }

    if (!interaction.activated)
    {
        if (tryActivateRecordedWorldItemInteraction(context, step, failures))
        {
            return true;
        }

        if (tryReplayRecordedEventTargetInteraction(context, step, failures))
        {
            return true;
        }

        scenarioLog(
            context,
            ScenarioLogColor::Red,
            std::string("INTERACT failed step=") + std::to_string(step.stepIndex)
                + " " + gameplayDebugTraceWorldHitSummary(interaction.hit));
        failures.push_back(stepLocation(step) + ": " + failure
            + "; recorded event-target replay was not valid for the recorded pose/hit");
        return false;
    }

    if (!assertInteractionExpectation(context, step, interaction, failures))
    {
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

bool runMovementSegment(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    const std::optional<ScenarioPose> startPose =
        readScenarioPoseNode(step, step.payload["start"], "start", failures, true);
    const std::optional<ScenarioPose> expectedStopPose =
        readScenarioPoseNode(step, step.payload["expect_stop"], "expect_stop", failures, true);

    if (!startPose || !expectedStopPose)
    {
        return false;
    }

    float durationSeconds = 0.0f;

    if (step.payload["duration_ms"] && step.payload["duration_ms"].IsScalar())
    {
        durationSeconds = step.payload["duration_ms"].as<float>() / 1000.0f;
    }
    else if (step.payload["seconds"] && step.payload["seconds"].IsScalar())
    {
        durationSeconds = step.payload["seconds"].as<float>();
    }
    else
    {
        failures.push_back(stepLocation(step) + ": movement_segment requires duration_ms or seconds");
        return false;
    }

    ScenarioMovementCommand command = {};
    command.startPose = *startPose;
    command.durationSeconds = durationSeconds;
    command.endYawRadians = expectedStopPose->yawRadians;
    command.endPitchRadians = expectedStopPose->pitchRadians;

    const std::string input = step.payload["input"] && step.payload["input"].IsScalar()
        ? lowerAscii(step.payload["input"].as<std::string>())
        : "forward";

    command.forward = input.empty() || input == "forward" || input == "w";
    command.backward = input == "backward" || input == "back" || input == "s";
    command.strafeLeft = input == "left" || input == "strafe_left" || input == "a";
    command.strafeRight = input == "right" || input == "strafe_right" || input == "d";

    if (!command.forward && !command.backward && !command.strafeLeft && !command.strafeRight && input != "none")
    {
        failures.push_back(stepLocation(step) + ": unsupported movement_segment input '" + input + "'");
        return false;
    }

    const YAML::Node rawStart = step.payload["raw_start"];
    const YAML::Node modifierSource = rawStart && rawStart.IsMap() ? rawStart : step.payload;
    command.shift =
        (modifierSource["shift"] && modifierSource["shift"].IsScalar() && modifierSource["shift"].as<bool>())
        || (modifierSource["run_walk_modifier"] && modifierSource["run_walk_modifier"].IsScalar()
            && modifierSource["run_walk_modifier"].as<bool>());
    command.ctrl =
        (modifierSource["ctrl"] && modifierSource["ctrl"].IsScalar() && modifierSource["ctrl"].as<bool>())
        || (modifierSource["turbo"] && modifierSource["turbo"].IsScalar()
            && modifierSource["turbo"].as<bool>());
    command.actorCollision =
        !step.payload["actor_collision"]
        || !step.payload["actor_collision"].IsScalar()
        || step.payload["actor_collision"].as<bool>();
    command.actorAi =
        !step.payload["actor_ai"]
        || !step.payload["actor_ai"].IsScalar()
        || step.payload["actor_ai"].as<bool>();

    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    std::string failure;
    ScenarioMovementResult movementResult = {};

    if (!context.runtimeDriver->simulateMovementSegment(command, movementResult, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    float toleranceXY = step.payload["tolerance_xy"] && step.payload["tolerance_xy"].IsScalar()
        ? step.payload["tolerance_xy"].as<float>()
        : 16.0f;
    float toleranceZ = step.payload["tolerance_z"] && step.payload["tolerance_z"].IsScalar()
        ? step.payload["tolerance_z"].as<float>()
        : 16.0f;
    const bool looseTolerance =
        step.payload["loose_tolerance"] && step.payload["loose_tolerance"].IsScalar()
            && step.payload["loose_tolerance"].as<bool>();

    if (context.mode == ScenarioMode::Faithful && !looseTolerance)
    {
        toleranceXY = std::min(toleranceXY, 16.0f);
        toleranceZ = std::min(toleranceZ, 16.0f);
    }

    bool success = true;
    const bool cameraMovedDuringSegment =
        normalizedAngleDifference(startPose->yawRadians, expectedStopPose->yawRadians) > 0.001f
        || std::abs(startPose->pitchRadians - expectedStopPose->pitchRadians) > 0.001f;

    if (!expectedStopPose->mapFileName.empty()
        && lowerAscii(movementResult.finalPose.mapFileName) != lowerAscii(expectedStopPose->mapFileName))
    {
        scenarioLogAssert(
            context,
            step,
            "movement.map",
            lowerAscii(expectedStopPose->mapFileName),
            lowerAscii(movementResult.finalPose.mapFileName),
            false);
        failures.push_back(stepLocation(step) + ": movement_segment expected map '"
            + expectedStopPose->mapFileName + "' but ended on '" + movementResult.finalPose.mapFileName + "'");
        success = false;
    }
    else if (!expectedStopPose->mapFileName.empty())
    {
        scenarioLogAssert(
            context,
            step,
            "movement.map",
            lowerAscii(expectedStopPose->mapFileName),
            lowerAscii(movementResult.finalPose.mapFileName),
            true);
    }

    const float recordedWalkDistanceXY = poseDistanceXY(*startPose, *expectedStopPose);
    const float replayWalkDistanceXY = poseDistanceXY(movementResult.startPose, movementResult.finalPose);
    const float walkDistanceDeltaXY = std::abs(replayWalkDistanceXY - recordedWalkDistanceXY);
    if (walkDistanceDeltaXY > toleranceXY)
    {
        scenarioLogAssert(
            context,
            step,
            "movement.walk_xy_distance",
            scenarioFloat(recordedWalkDistanceXY) + " +/- " + scenarioFloat(toleranceXY),
            scenarioFloat(replayWalkDistanceXY) + " delta=" + scenarioFloat(walkDistanceDeltaXY),
            false);
        failures.push_back(stepLocation(step) + ": movement_segment replay walked "
            + scenarioFloat(replayWalkDistanceXY) + " XY units, recorded walk was "
            + scenarioFloat(recordedWalkDistanceXY) + ", tolerance " + scenarioFloat(toleranceXY));
        success = false;
    }
    else
    {
        scenarioLogAssert(
            context,
            step,
            "movement.walk_xy_distance",
            scenarioFloat(recordedWalkDistanceXY) + " +/- " + scenarioFloat(toleranceXY),
            scenarioFloat(replayWalkDistanceXY) + " delta=" + scenarioFloat(walkDistanceDeltaXY),
            true);
    }

    const float recordedWalkDistanceZ = poseDistanceZ(*startPose, *expectedStopPose);
    const float replayWalkDistanceZ = poseDistanceZ(movementResult.startPose, movementResult.finalPose);
    const float walkDistanceDeltaZ = std::abs(replayWalkDistanceZ - recordedWalkDistanceZ);
    if (walkDistanceDeltaZ > toleranceZ)
    {
        scenarioLogAssert(
            context,
            step,
            "movement.walk_z_distance",
            scenarioFloat(recordedWalkDistanceZ) + " +/- " + scenarioFloat(toleranceZ),
            scenarioFloat(replayWalkDistanceZ) + " delta=" + scenarioFloat(walkDistanceDeltaZ),
            false);
        failures.push_back(stepLocation(step) + ": movement_segment replay walked "
            + scenarioFloat(replayWalkDistanceZ) + " Z units, recorded walk was "
            + scenarioFloat(recordedWalkDistanceZ) + ", tolerance " + scenarioFloat(toleranceZ));
        success = false;
    }
    else
    {
        scenarioLogAssert(
            context,
            step,
            "movement.walk_z_distance",
            scenarioFloat(recordedWalkDistanceZ) + " +/- " + scenarioFloat(toleranceZ),
            scenarioFloat(replayWalkDistanceZ) + " delta=" + scenarioFloat(walkDistanceDeltaZ),
            true);
    }

    const float xyDistance = poseDistanceXY(movementResult.finalPose, *expectedStopPose);
    const float zDistance = poseDistanceZ(movementResult.finalPose, *expectedStopPose);
    if (cameraMovedDuringSegment)
    {
        scenarioLog(
            context,
            ScenarioLogColor::Yellow,
            std::string("WARNING movement_segment step=") + std::to_string(step.stepIndex)
                + " trace_line=" + std::to_string(step.sourceLine)
                + " camera changed while recording; final-position assertion is skipped"
                + " yaw_delta=" + scenarioFloat(normalizedAngleDifference(
                    startPose->yawRadians,
                    expectedStopPose->yawRadians))
                + " pitch_delta=" + scenarioFloat(expectedStopPose->pitchRadians - startPose->pitchRadians)
                + " xy_drift=" + scenarioFloat(xyDistance)
                + " z_drift=" + scenarioFloat(zDistance));
    }
    else if (xyDistance > toleranceXY)
    {
        scenarioLogAssert(
            context,
            step,
            "movement.stop_xy_drift",
            "<= " + scenarioFloat(toleranceXY),
            scenarioFloat(xyDistance),
            false);
        failures.push_back(stepLocation(step) + ": movement_segment final XY was "
            + scenarioFloat(xyDistance) + " units from expected, tolerance "
            + scenarioFloat(toleranceXY));
        success = false;
    }
    else if (!cameraMovedDuringSegment)
    {
        scenarioLogAssert(
            context,
            step,
            "movement.stop_xy_drift",
            "<= " + scenarioFloat(toleranceXY),
            scenarioFloat(xyDistance),
            true);
    }

    if (!cameraMovedDuringSegment && zDistance > toleranceZ)
    {
        scenarioLogAssert(
            context,
            step,
            "movement.stop_z_drift",
            "<= " + scenarioFloat(toleranceZ),
            scenarioFloat(zDistance),
            false);
        failures.push_back(stepLocation(step) + ": movement_segment final Z was "
            + scenarioFloat(zDistance) + " units from expected, tolerance "
            + scenarioFloat(toleranceZ));
        success = false;
    }
    else if (!cameraMovedDuringSegment)
    {
        scenarioLogAssert(
            context,
            step,
            "movement.stop_z_drift",
            "<= " + scenarioFloat(toleranceZ),
            scenarioFloat(zDistance),
            true);
    }

    if (step.payload["delta_game_minutes"] && step.payload["delta_game_minutes"].IsScalar())
    {
        context.gameMinutes += step.payload["delta_game_minutes"].as<float>();
        synchronizeRuntimeClock(context);
    }

    syncContextFromRuntime(context);
    return success;
}

bool runAdvanceRuntime(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    float seconds = 0.0f;

    if (step.payload["seconds"] && step.payload["seconds"].IsScalar())
    {
        seconds = step.payload["seconds"].as<float>();
    }
    else if (step.payload["milliseconds"] && step.payload["milliseconds"].IsScalar())
    {
        seconds = step.payload["milliseconds"].as<float>() / 1000.0f;
    }
    else if (!readScalarFloat(step, "seconds", seconds, failures, true))
    {
        return false;
    }

    if (context.mode != ScenarioMode::Faithful && !context.runtimeDriver)
    {
        return true;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    std::string failure;

    if (!context.runtimeDriver->advanceRuntime(seconds, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    syncContextFromRuntime(context);
    return true;
}

std::optional<uint16_t> parseScenarioMechanismState(const YAML::Node &node)
{
    if (!node || !node.IsScalar())
    {
        return std::nullopt;
    }

    const std::string text = lowerAscii(node.as<std::string>());

    if (text == "open")
    {
        return static_cast<uint16_t>(EvtMechanismState::Open);
    }
    if (text == "closing")
    {
        return static_cast<uint16_t>(EvtMechanismState::Closing);
    }
    if (text == "closed")
    {
        return static_cast<uint16_t>(EvtMechanismState::Closed);
    }
    if (text == "opening")
    {
        return static_cast<uint16_t>(EvtMechanismState::Opening);
    }

    return node.as<uint16_t>();
}

std::string scenarioMechanismStateText(uint16_t state)
{
    switch (static_cast<EvtMechanismState>(state))
    {
        case EvtMechanismState::Open:
            return "open";
        case EvtMechanismState::Closing:
            return "closing";
        case EvtMechanismState::Closed:
            return "closed";
        case EvtMechanismState::Opening:
            return "opening";
    }

    return std::to_string(state);
}

bool assertMechanismPayload(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &mechanismNode,
    std::vector<std::string> &failures)
{
    if (!mechanismNode || !mechanismNode.IsMap())
    {
        failures.push_back(stepLocation(step) + ": mechanism assertion payload must be a mapping");
        return false;
    }

    if (!mechanismNode["id"] || !mechanismNode["id"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": mechanism assertion requires scalar field 'id'");
        return false;
    }

    if (context.runtimeDriver)
    {
        syncContextFromRuntime(context);
    }

    if (!context.eventRuntimeStateLoaded)
    {
        return context.mode != ScenarioMode::Faithful;
    }

    const uint32_t mechanismId = mechanismNode["id"].as<uint32_t>();
    const auto mechanismIt = context.eventRuntimeState.mechanisms.find(mechanismId);

    if (mechanismIt == context.eventRuntimeState.mechanisms.end())
    {
        if (context.mode != ScenarioMode::Faithful)
        {
            return true;
        }

        failures.push_back(stepLocation(step) + ": mechanism " + std::to_string(mechanismId)
            + " does not exist in current runtime state");
        return false;
    }

    const RuntimeMechanismState &mechanism = mechanismIt->second;
    bool success = true;
    const YAML::Node expectedStateNode =
        mechanismNode["state"] ? mechanismNode["state"] : mechanismNode["new_state"];

    if (expectedStateNode)
    {
        const std::optional<uint16_t> expectedState = parseScenarioMechanismState(expectedStateNode);

        if (expectedState)
        {
            const bool stateMatches = mechanism.state == *expectedState;
            scenarioLogAssert(
                context,
                step,
                "mechanism.state",
                scenarioMechanismStateText(*expectedState),
                scenarioMechanismStateText(mechanism.state),
                stateMatches);

            if (!stateMatches)
            {
                if (context.mode != ScenarioMode::Faithful)
                {
                    context.eventRuntimeState.mechanisms[mechanismId].state = *expectedState;
                }
                else if (*expectedState == static_cast<uint16_t>(EvtMechanismState::Closing)
                    || *expectedState == static_cast<uint16_t>(EvtMechanismState::Opening))
                {
                    scenarioLog(
                        context,
                        ScenarioLogColor::Yellow,
                        "WARNING step=" + std::to_string(step.stepIndex)
                            + " trace_line=" + std::to_string(step.sourceLine)
                            + " mechanism " + std::to_string(mechanismId)
                            + " transient state expected="
                            + scenarioMechanismStateText(*expectedState)
                            + " actual=" + scenarioMechanismStateText(mechanism.state));
                }
                else
                {
                    failures.push_back(stepLocation(step) + ": mechanism " + std::to_string(mechanismId)
                        + " state mismatch");
                    success = false;
                }
            }
        }
    }

    if (mechanismNode["moving"] && mechanismNode["moving"].IsScalar())
    {
        const bool expectedMoving = mechanismNode["moving"].as<bool>();
        const bool movingMatches = mechanism.isMoving == expectedMoving;
        scenarioLogAssert(
            context,
            step,
            "mechanism.moving",
            boolText(expectedMoving),
            boolText(mechanism.isMoving),
            movingMatches);

        if (!movingMatches)
        {
            if (context.mode != ScenarioMode::Faithful)
            {
                context.eventRuntimeState.mechanisms[mechanismId].isMoving = expectedMoving;
            }
            else if (expectedMoving)
            {
                scenarioLog(
                    context,
                    ScenarioLogColor::Yellow,
                    "WARNING step=" + std::to_string(step.stepIndex)
                        + " trace_line=" + std::to_string(step.sourceLine)
                        + " mechanism " + std::to_string(mechanismId)
                        + " transient moving expected=true actual=" + boolText(mechanism.isMoving));
            }
            else
            {
                failures.push_back(stepLocation(step) + ": mechanism " + std::to_string(mechanismId)
                    + " moving flag mismatch");
                success = false;
            }
        }
    }

    return success;
}

bool runAssertMechanism(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    return assertMechanismPayload(context, step, step.payload, failures);
}

bool runWaitUntil(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    float timeoutSeconds = 0.0f;

    if (step.payload["timeout_runtime_seconds"] && step.payload["timeout_runtime_seconds"].IsScalar())
    {
        timeoutSeconds = step.payload["timeout_runtime_seconds"].as<float>();
    }
    else if (step.payload["seconds"] && step.payload["seconds"].IsScalar())
    {
        timeoutSeconds = step.payload["seconds"].as<float>();
    }

    if (timeoutSeconds > 0.0f
        && (context.mode == ScenarioMode::Faithful
            || (context.runtimeDriver && context.runtimeDriver->hasActiveRuntime())))
    {
        if (!ensureRuntimeDriver(context, failures))
        {
            return context.mode != ScenarioMode::Faithful;
        }

        std::string failure;
        if (!context.runtimeDriver->advanceRuntime(timeoutSeconds, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return false;
        }

        syncContextFromRuntime(context);
    }

    const YAML::Node assertNode = step.payload["assert"];

    if (assertNode && assertNode.IsMap() && assertNode["mechanism"])
    {
        return assertMechanismPayload(context, step, assertNode["mechanism"], failures);
    }

    return true;
}

bool runAdvanceGameTime(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    float minutes = 0.0f;

    if (step.payload["minutes"] && step.payload["minutes"].IsScalar())
    {
        minutes = step.payload["minutes"].as<float>();
    }
    else if (step.payload["hours"] && step.payload["hours"].IsScalar())
    {
        minutes = step.payload["hours"].as<float>() * 60.0f;
    }
    else if (step.payload["days"] && step.payload["days"].IsScalar())
    {
        minutes = step.payload["days"].as<float>() * 24.0f * 60.0f;
    }
    else
    {
        if (context.mode != ScenarioMode::Faithful)
        {
            return true;
        }

        failures.push_back(stepLocation(step) + ": advance_game_time requires minutes, hours, or days");
        return false;
    }

    context.gameMinutes += minutes;

    if (context.runtimeDriver)
    {
        std::string failure;

        if (!context.runtimeDriver->advanceGameMinutes(minutes, failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return false;
        }

        syncContextFromRuntime(context);
    }

    if (step.payload["after_game_minutes"] && step.payload["after_game_minutes"].IsScalar())
    {
        const float expectedGameMinutes = step.payload["after_game_minutes"].as<float>();
        const float deltaMinutes = expectedGameMinutes - context.gameMinutes;

        if (std::abs(deltaMinutes) > 0.001f)
        {
            scenarioLog(
                context,
                ScenarioLogColor::Yellow,
                "WARNING advance_game_time step=" + std::to_string(step.stepIndex)
                    + " ended at game_minutes=" + std::to_string(context.gameMinutes)
                    + " expected_after_game_minutes=" + std::to_string(expectedGameMinutes)
                    + " delta_minutes=" + std::to_string(deltaMinutes)
                    + "; syncing to recorded clock");
            context.gameMinutes = expectedGameMinutes;
            if (context.runtimeDriver)
            {
                context.runtimeDriver->setGameMinutes(context.gameMinutes);
            }
        }
    }

    synchronizeRuntimeClock(context);
    return true;
}

bool runInteractTarget(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    if (context.mode == ScenarioMode::Faithful || step.payload["pose"])
    {
        if (!step.payload["pose"])
        {
            failures.push_back(stepLocation(step)
                + ": faithful interact_target requires a pose so the runtime can pick from party/camera state");
            return false;
        }

        return runPressInteract(context, step, failures);
    }

    if (!step.payload["triggered_event"] || !step.payload["triggered_event"].IsScalar())
    {
        return true;
    }

    const uint16_t eventId = step.payload["triggered_event"].as<uint16_t>();
    executeRecordedEvent(context, step, eventId, true);
    return true;
}

bool runSpeakNpc(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    if (!step.payload["source_id"] || !step.payload["source_id"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": speak_npc requires scalar field 'source_id'");
        return false;
    }

    if (!context.runtimeDriver)
    {
        return true;
    }

    std::string failure;
    const uint32_t npcId = step.payload["source_id"].as<uint32_t>();

    if (!context.runtimeDriver->speakNpc(npcId, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    scenarioLog(
        context,
        ScenarioLogColor::Cyan,
        std::string("DIALOG after speak_npc step=") + std::to_string(step.stepIndex)
            + " " + context.runtimeDriver->activeDialogSummary());
    syncContextFromRuntime(context);
    return true;
}

bool runSelectTopic(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    const std::string kind = step.payload["kind"] && step.payload["kind"].IsScalar()
        ? step.payload["kind"].as<std::string>()
        : "";

    if (!step.payload["action_id"] || !step.payload["action_id"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": select_topic requires scalar field 'action_id'");
        return false;
    }

    uint16_t eventId = step.payload["action_id"].as<uint16_t>();

    if (context.runtimeDriver)
    {
        if (step.payload["expect_before"] && step.payload["expect_before"]["dialog"])
        {
            if (!assertDialogState(
                    context,
                    step,
                    step.payload["expect_before"]["dialog"],
                    "before select_topic",
                    failures))
            {
                return false;
            }
        }

        std::string failure;
        bool selected = false;

        if (step.payload["match_label"])
        {
            if (!step.payload["match_label"].IsScalar() || step.payload["match_label"].as<std::string>().empty())
            {
                failures.push_back(stepLocation(step) + ": match_label must be a nonempty scalar");
                return false;
            }
            const std::string label = step.payload["match_label"].as<std::string>();
            const ScenarioDialogSnapshot dialog = context.runtimeDriver->activeDialogSnapshot();
            std::optional<size_t> matchingIndex;
            for (const ScenarioDialogActionSnapshot &action : dialog.actions)
            {
                if (action.id == eventId && action.label == label)
                {
                    if (matchingIndex)
                    {
                        failures.push_back(stepLocation(step) + ": dialog action label is ambiguous: " + label);
                        return false;
                    }
                    matchingIndex = action.index;
                }
            }
            if (!matchingIndex)
            {
                failures.push_back(stepLocation(step) + ": matching dialog action is unavailable: " + label);
                return false;
            }
            selected = context.runtimeDriver->selectDialogAction(*matchingIndex, failure);
        }
        else
        {
            selected = context.runtimeDriver->selectDialogActionById(eventId, failure);
        }

        if (!selected
            && context.mode != ScenarioMode::Faithful
            && step.payload["selection_index"]
            && step.payload["selection_index"].IsScalar())
        {
            const size_t rawIndex = step.payload["selection_index"].as<size_t>();

            selected =
                context.runtimeDriver->selectDialogAction(rawIndex, failure)
                || (rawIndex > 0 && context.runtimeDriver->selectDialogAction(rawIndex - 1, failure));
        }

        if (selected)
        {
            scenarioLog(
                context,
                ScenarioLogColor::Cyan,
                std::string("DIALOG after select_topic step=") + std::to_string(step.stepIndex)
                    + " " + context.runtimeDriver->activeDialogSummary());
            syncContextFromRuntime(context);
            return true;
        }

        if (context.mode == ScenarioMode::Faithful)
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return false;
        }
    }

    if (kind == "house_service"
        && step.payload["source_id"] && step.payload["source_id"].IsScalar()
        && eventId == 23)
    {
        const uint32_t houseId = step.payload["source_id"].as<uint32_t>();
        const HouseEntry *pHouse = context.gameDataLoader.getHouseTable().get(houseId);

        if (pHouse != nullptr && pHouse->extraExit.has_value())
        {
            return loadMap(context, pHouse->extraExit->destinationMapFileName, failures);
        }
    }

    if (kind == "house_service"
        && step.payload["source_id"] && step.payload["source_id"].IsScalar())
    {
        const uint32_t houseId = step.payload["source_id"].as<uint32_t>();
        const HouseEntry *pHouse = context.gameDataLoader.getHouseTable().get(houseId);

        if (pHouse != nullptr)
        {
            HouseActionOption option = {};
            option.id = static_cast<HouseActionId>(eventId);
            option.enabled = true;

            if (step.payload["label"] && step.payload["label"].IsScalar())
            {
                option.label = step.payload["label"].as<std::string>();
            }

            performHouseAction(
                option,
                *pHouse,
                context.party,
                &context.gameDataLoader.getClassSkillTable(),
                nullptr);
        }
    }

    if (kind == "house_resident")
    {
        const std::vector<NpcDialogTable::ResolvedTopic> topics =
            context.gameDataLoader.getNpcDialogTable().getTopicsForNpc(eventId);

        if (!topics.empty() && topics[0].id <= UINT16_MAX)
        {
            eventId = static_cast<uint16_t>(topics[0].id);
        }
    }

    executeRecordedEvent(context, step, eventId, true);
    return true;
}

bool runSaveCheckpoint(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadMapOrScalar(step, failures))
    {
        return false;
    }

    const std::string name = readCheckpointName(step, failures);

    if (name.empty())
    {
        return false;
    }

    ScenarioExecutionContext::Checkpoint checkpoint = {};
    checkpoint.party = context.party;
    checkpoint.eventRuntimeState = context.eventRuntimeState;
    checkpoint.currentMapFileName = context.currentMapFileName;
    checkpoint.gameMinutes = context.gameMinutes;
    checkpoint.eventRuntimeStateLoaded = context.eventRuntimeStateLoaded;
    context.checkpoints[name] = std::move(checkpoint);
    return true;
}

bool runLoadCheckpoint(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadMapOrScalar(step, failures))
    {
        return false;
    }

    const std::string name = readCheckpointName(step, failures);

    if (name.empty())
    {
        return false;
    }

    const auto checkpointIt = context.checkpoints.find(name);

    if (checkpointIt == context.checkpoints.end())
    {
        failures.push_back(stepLocation(step) + ": checkpoint '" + name + "' does not exist");
        return false;
    }

    context.party = checkpointIt->second.party;
    context.eventRuntimeState = checkpointIt->second.eventRuntimeState;
    context.gameMinutes = checkpointIt->second.gameMinutes;
    context.eventRuntimeStateLoaded = checkpointIt->second.eventRuntimeStateLoaded;
    const std::string mapFileName = checkpointIt->second.currentMapFileName;

    if (!mapFileName.empty())
    {
        return loadMap(context, mapFileName, failures);
    }

    context.currentMapFileName.clear();
    return true;
}

std::filesystem::path scenarioSavePathForName(
    ScenarioExecutionContext &context,
    const std::string &name)
{
    const std::string stem = sanitizeScenarioFileStem(name);
    return context.saveGameRootPath / (stem + ".oysav");
}

std::filesystem::path resolveScenarioExternalPath(
    const ScenarioExecutionContext &context,
    const std::string &pathText)
{
    std::filesystem::path path(pathText);

    if (path.is_relative())
    {
        std::error_code errorCode;
        const std::filesystem::path workingPath = (std::filesystem::current_path(errorCode) / path).lexically_normal();

        if (!errorCode && std::filesystem::exists(workingPath, errorCode))
        {
            return workingPath;
        }

        path = context.basePath / path;
    }

    return path.lexically_normal();
}

bool readLoadGameRequest(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::string &name,
    std::filesystem::path &explicitPath,
    std::vector<std::string> &failures)
{
    explicitPath.clear();

    if (step.payload.IsScalar())
    {
        name = step.payload.as<std::string>();
        return !name.empty();
    }

    if (!step.payload.IsMap())
    {
        failures.push_back(stepLocation(step) + ": load_game payload must be a scalar or mapping");
        return false;
    }

    std::string pathText;

    if (!readScalarString(step, "name", name, failures, false)
        || !readScalarString(step, "path", pathText, failures, false))
    {
        return false;
    }

    if (!pathText.empty())
    {
        explicitPath = resolveScenarioExternalPath(context, pathText);
    }

    if (name.empty() && !pathText.empty())
    {
        name = sanitizeScenarioFileStem(std::filesystem::path(pathText).stem().string());
    }

    if (name.empty())
    {
        failures.push_back(stepLocation(step) + ": load_game requires scalar payload, 'name', or 'path'");
        return false;
    }

    return true;
}

bool ensureRuntimeReadyForCurrentContext(
    ScenarioExecutionContext &context,
    std::vector<std::string> &failures)
{
    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    seedRuntimeDriverFromContext(context);

    if (!context.currentMapFileName.empty()
        && (!context.runtimeDriver->hasActiveRuntime()
            || lowerAscii(context.runtimeDriver->currentMapFileName()) != lowerAscii(context.currentMapFileName)))
    {
        std::string failure;

        if (!context.runtimeDriver->loadMap(context.currentMapFileName, failure))
        {
            failures.push_back(failure);
            return false;
        }

        seedRuntimeDriverFromContext(context);
    }

    return true;
}

bool runSaveGame(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadMapOrScalar(step, failures))
    {
        return false;
    }

    const std::string name = readCheckpointName(step, failures);

    if (name.empty())
    {
        return false;
    }

    if (!ensureRuntimeReadyForCurrentContext(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(context.saveGameRootPath, errorCode);

    if (errorCode)
    {
        failures.push_back(stepLocation(step) + ": could not create scenario save directory '"
            + context.saveGameRootPath.string() + "': " + errorCode.message());
        return false;
    }

    const std::filesystem::path path = scenarioSavePathForName(context, name);
    std::string failure;

    if (!context.runtimeDriver->saveGameToPath(path, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return context.mode != ScenarioMode::Faithful;
    }

    context.saveGamePaths[name] = path;
    syncContextFromRuntime(context);
    return true;
}

bool runLoadGame(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadMapOrScalar(step, failures))
    {
        return false;
    }

    std::string name;
    std::filesystem::path path;

    if (!readLoadGameRequest(context, step, name, path, failures))
    {
        return false;
    }

    const auto saveIt = context.saveGamePaths.find(name);

    if (saveIt != context.saveGamePaths.end())
    {
        path = saveIt->second;
    }
    else if (path.empty())
    {
        failures.push_back(stepLocation(step) + ": save_game '" + name + "' does not exist");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    std::string failure;

    if (!context.runtimeDriver->loadGameFromPath(path, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return context.mode != ScenarioMode::Faithful;
    }

    context.saveGamePaths[name] = path;
    syncContextFromRuntime(context);
    return true;
}

bool assertCurrentMap(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const std::string &expectedMap,
    std::vector<std::string> &failures)
{
    if (context.currentMapFileName.empty())
    {
        scenarioLogAssert(context, step, "map", expectedMap, "<none>", false);
        failures.push_back(stepLocation(step) + ": no map is currently loaded");
        return false;
    }

    if (lowerAscii(context.currentMapFileName) != lowerAscii(expectedMap))
    {
        scenarioLogAssert(context, step, "map", expectedMap, context.currentMapFileName, false);
        failures.push_back(stepLocation(step) + ": expected map '" + expectedMap
            + "' but current map is '" + context.currentMapFileName + "'");
        return false;
    }

    scenarioLogAssert(context, step, "map", expectedMap, context.currentMapFileName, true);
    return true;
}

bool assertInventoryAnyMember(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node inventoryNode = step.payload["inventory_any_member"];

    if (!inventoryNode)
    {
        return true;
    }

    if (!inventoryNode.IsSequence())
    {
        failures.push_back(stepLocation(step) + ": field 'inventory_any_member' must be a sequence");
        return false;
    }

    bool success = true;

    for (const YAML::Node &entryNode : inventoryNode)
    {
        if (!entryNode.IsMap() || !entryNode["item_id"] || !entryNode["item_id"].IsScalar())
        {
            failures.push_back(stepLocation(step)
                + ": inventory_any_member entries must contain scalar field 'item_id'");
            success = false;
            continue;
        }

        const uint32_t itemId = entryNode["item_id"].as<uint32_t>();
        const int count = context.party.inventoryItemCount(itemId);
        const bool itemFound = count > 0;
        scenarioLogAssert(
            context,
            step,
            "item_id",
            std::to_string(itemId),
            itemFound ? std::to_string(itemId) : "<missing>",
            itemFound);

        if (!itemFound)
        {
            failures.push_back(stepLocation(step) + ": item " + std::to_string(itemId)
                + " was not found in any party member inventory");
            success = false;
        }
    }

    return success;
}

bool assertInventoryList(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node inventoryNode = step.payload["inventory"];

    if (!inventoryNode)
    {
        return true;
    }

    if (!inventoryNode.IsSequence())
    {
        failures.push_back(stepLocation(step) + ": field 'inventory' must be a sequence");
        return false;
    }

    bool success = true;

    for (const YAML::Node &entryNode : inventoryNode)
    {
        if (!entryNode.IsMap() || !entryNode["item_id"] || !entryNode["item_id"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": inventory entries must contain scalar field 'item_id'");
            success = false;
            continue;
        }

        const uint32_t itemId = entryNode["item_id"].as<uint32_t>();
        bool itemFound = context.party.inventoryItemCount(itemId) > 0;

        if (!itemFound)
        {
            if (context.mode != ScenarioMode::Faithful && context.party.tryGrantItem(itemId))
            {
                itemFound = true;
            }
            else
            {
                failures.push_back(stepLocation(step) + ": item " + std::to_string(itemId)
                    + " was not found in party inventory");
                success = false;
            }
        }

        scenarioLogAssert(
            context,
            step,
            "item_id",
            std::to_string(itemId),
            itemFound ? std::to_string(itemId) : "<missing>",
            itemFound);
    }

    return success;
}

bool assertAwards(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node awardsNode = step.payload["award_acquired"];

    if (!awardsNode)
    {
        return true;
    }

    if (!awardsNode.IsSequence())
    {
        failures.push_back(stepLocation(step) + ": field 'award_acquired' must be a sequence");
        return false;
    }

    bool success = true;

    for (const YAML::Node &entryNode : awardsNode)
    {
        if (!entryNode.IsMap() || !entryNode["id"] || !entryNode["id"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": award_acquired entries must contain scalar field 'id'");
            success = false;
            continue;
        }

        const uint32_t awardId = entryNode["id"].as<uint32_t>();
        bool acquired = false;

        if (entryNode["member_index"] && entryNode["member_index"].IsScalar())
        {
            acquired = context.party.hasAward(entryNode["member_index"].as<size_t>(), awardId);
        }
        else
        {
            acquired = context.party.hasAward(awardId);
        }

        if (!acquired)
        {
            if (context.mode == ScenarioMode::Faithful)
            {
                failures.push_back(stepLocation(step) + ": award " + std::to_string(awardId)
                    + " was not acquired");
                success = false;
                continue;
            }

            if (entryNode["member_index"] && entryNode["member_index"].IsScalar())
            {
                context.party.addAward(entryNode["member_index"].as<size_t>(), awardId);
            }
            else
            {
                context.party.addAward(awardId);
            }
        }
    }

    return success;
}

std::optional<EquipmentSlot> parseScenarioEquipmentSlot(const std::string &value)
{
    const std::string normalized = lowerAscii(value);

    if (normalized == "offhand" || normalized == "off_hand")
    {
        return EquipmentSlot::OffHand;
    }
    if (normalized == "mainhand" || normalized == "main_hand")
    {
        return EquipmentSlot::MainHand;
    }
    if (normalized == "bow")
    {
        return EquipmentSlot::Bow;
    }
    if (normalized == "armor")
    {
        return EquipmentSlot::Armor;
    }
    if (normalized == "helm")
    {
        return EquipmentSlot::Helm;
    }
    if (normalized == "belt")
    {
        return EquipmentSlot::Belt;
    }
    if (normalized == "cloak")
    {
        return EquipmentSlot::Cloak;
    }
    if (normalized == "gauntlets")
    {
        return EquipmentSlot::Gauntlets;
    }
    if (normalized == "boots")
    {
        return EquipmentSlot::Boots;
    }
    if (normalized == "amulet")
    {
        return EquipmentSlot::Amulet;
    }
    if (normalized == "ring1")
    {
        return EquipmentSlot::Ring1;
    }
    if (normalized == "ring2")
    {
        return EquipmentSlot::Ring2;
    }
    if (normalized == "ring3")
    {
        return EquipmentSlot::Ring3;
    }
    if (normalized == "ring4")
    {
        return EquipmentSlot::Ring4;
    }
    if (normalized == "ring5")
    {
        return EquipmentSlot::Ring5;
    }
    if (normalized == "ring6")
    {
        return EquipmentSlot::Ring6;
    }

    return std::nullopt;
}

bool assertAwardCleared(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node awardsNode = step.payload["award_cleared"];

    if (!awardsNode)
    {
        return true;
    }

    if (!awardsNode.IsSequence())
    {
        failures.push_back(stepLocation(step) + ": field 'award_cleared' must be a sequence");
        return false;
    }

    bool success = true;

    for (const YAML::Node &entryNode : awardsNode)
    {
        if (!entryNode.IsMap() || !entryNode["id"] || !entryNode["id"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": award_cleared entries must contain scalar field 'id'");
            success = false;
            continue;
        }

        const uint32_t awardId = entryNode["id"].as<uint32_t>();
        const bool stillAcquired = entryNode["member_index"] && entryNode["member_index"].IsScalar()
            ? context.party.hasAward(entryNode["member_index"].as<size_t>(), awardId)
            : context.party.hasAward(awardId);

        if (stillAcquired)
        {
            failures.push_back(stepLocation(step) + ": award " + std::to_string(awardId)
                + " was expected to be cleared");
            success = false;
        }
    }

    return success;
}

bool takePartyOverlayHeldItem(
    ScenarioExecutionContext &context,
    const YAML::Node &heldNode,
    uint32_t expectedItemId)
{
    if (expectedItemId == 0)
    {
        return false;
    }

    std::vector<size_t> memberIndices;

    if (heldNode["member_index"] && heldNode["member_index"].IsScalar())
    {
        memberIndices.push_back(heldNode["member_index"].as<size_t>());
    }
    else
    {
        const size_t activeMemberIndex = context.party.activeMemberIndex();

        if (context.party.member(activeMemberIndex) != nullptr)
        {
            memberIndices.push_back(activeMemberIndex);
        }

        for (size_t memberIndex = 0; memberIndex < context.party.memberCount(); ++memberIndex)
        {
            if (std::find(memberIndices.begin(), memberIndices.end(), memberIndex) == memberIndices.end())
            {
                memberIndices.push_back(memberIndex);
            }
        }
    }

    const std::optional<ScenarioInventoryGrid> grid =
        heldNode["grid"] && heldNode["grid"].IsScalar()
            ? parseInventoryGridText(heldNode["grid"].as<std::string>())
            : std::nullopt;

    const auto takeFromCell = [&context](size_t memberIndex, uint8_t x, uint8_t y)
    {
        if (context.runtimeDriver)
        {
            std::string failure;
            if (!context.runtimeDriver->takeInventoryItemToCursor(memberIndex, x, y, failure))
            {
                return false;
            }
            syncContextFromRuntime(context);
            return true;
        }
        InventoryItem heldItem = {};
        if (!context.party.takeItemFromMemberInventoryCell(memberIndex, x, y, heldItem))
        {
            return false;
        }
        context.party.setHeldItemForQueries(heldItem);
        return true;
    };

    if (grid)
    {
        for (size_t memberIndex : memberIndices)
        {
            const Character *pMember = context.party.member(memberIndex);
            const InventoryItem *pItem = pMember != nullptr ? pMember->inventoryItemAt(grid->x, grid->y) : nullptr;

            if (pItem == nullptr || pItem->objectDescriptionId != expectedItemId)
            {
                continue;
            }

            if (takeFromCell(memberIndex, grid->x, grid->y))
            {
                return true;
            }
        }
    }

    for (size_t memberIndex : memberIndices)
    {
        const Character *pMember = context.party.member(memberIndex);

        if (pMember == nullptr)
        {
            continue;
        }

        for (const InventoryItem &item : pMember->inventory)
        {
            if (item.objectDescriptionId != expectedItemId)
            {
                continue;
            }

            if (takeFromCell(memberIndex, item.gridX, item.gridY))
            {
                return true;
            }
        }
    }

    return false;
}

bool assertHeldItem(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node heldNode = step.payload["held_item"];

    if (!heldNode)
    {
        return true;
    }

    if (!heldNode.IsMap() || !heldNode["active"] || !heldNode["active"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": held_item assertion must contain scalar field 'active'");
        return false;
    }

    const bool expectedActive = heldNode["active"].as<bool>();
    const uint32_t expectedItemId =
        heldNode["item_id"] && heldNode["item_id"].IsScalar() ? heldNode["item_id"].as<uint32_t>() : 0;
    const std::string source =
        heldNode["source"] && heldNode["source"].IsScalar() ? heldNode["source"].as<std::string>() : std::string();
    const uint32_t actualItemId = context.party.heldItemIdForQueries();

    if (!expectedActive)
    {
        if (actualItemId != 0)
        {
            if (expectedItemId == 0 || actualItemId == expectedItemId)
            {
                if (context.runtimeDriver)
                {
                    std::string failure;
                    if (!context.runtimeDriver->placeHeldItemInInventory(failure))
                    {
                        failures.push_back(stepLocation(step) + ": " + failure);
                        return false;
                    }
                    syncContextFromRuntime(context);
                    return true;
                }
                if (context.party.inventoryItemCount(actualItemId) <= 0)
                {
                    context.party.tryGrantItem(actualItemId);
                }

                context.party.clearHeldItemForQueries();

                if (context.runtimeDriver)
                {
                    context.runtimeDriver->setPartyState(context.party);
                }

                return true;
            }

            if (context.mode != ScenarioMode::Faithful)
            {
                context.party.clearHeldItemForQueries();
                return true;
            }

            failures.push_back(stepLocation(step) + ": expected no held item but party holds item "
                + std::to_string(actualItemId));
            return false;
        }

        return true;
    }

    if (actualItemId != expectedItemId)
    {
        if (source == "party_overlay" && takePartyOverlayHeldItem(context, heldNode, expectedItemId))
        {
            return true;
        }

        if (context.mode != ScenarioMode::Faithful && expectedItemId != 0)
        {
            InventoryItem heldItem = {};
            heldItem.objectDescriptionId = expectedItemId;
            heldItem.quantity = 1;
            context.party.setHeldItemForQueries(heldItem);
            return true;
        }

        failures.push_back(stepLocation(step) + ": expected held item " + std::to_string(expectedItemId)
            + " but party holds item " + std::to_string(actualItemId));
        return false;
    }

    return true;
}

bool assertEquippedItems(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node equippedNode = step.payload["equipped_item"];

    if (!equippedNode)
    {
        return true;
    }

    if (!equippedNode.IsSequence())
    {
        failures.push_back(stepLocation(step) + ": field 'equipped_item' must be a sequence");
        return false;
    }

    bool success = true;

    for (const YAML::Node &entryNode : equippedNode)
    {
        if (!entryNode.IsMap()
            || !entryNode["item_id"] || !entryNode["item_id"].IsScalar()
            || !entryNode["member_index"] || !entryNode["member_index"].IsScalar()
            || !entryNode["slot"] || !entryNode["slot"].IsScalar())
        {
            failures.push_back(stepLocation(step)
                + ": equipped_item entries must contain scalar item_id, member_index, and slot");
            success = false;
            continue;
        }

        const std::optional<EquipmentSlot> slot = parseScenarioEquipmentSlot(entryNode["slot"].as<std::string>());

        if (!slot)
        {
            failures.push_back(stepLocation(step) + ": unsupported equipped_item slot '"
                + entryNode["slot"].as<std::string>() + "'");
            success = false;
            continue;
        }

        const size_t memberIndex = entryNode["member_index"].as<size_t>();
        const uint32_t expectedItemId = entryNode["item_id"].as<uint32_t>();
        const uint32_t actualItemId = context.party.equippedItemId(memberIndex, *slot);

        if (actualItemId != expectedItemId)
        {
            failures.push_back(stepLocation(step) + ": expected member " + std::to_string(memberIndex)
                + " slot '" + entryNode["slot"].as<std::string>() + "' to hold item "
                + std::to_string(expectedItemId) + " but found " + std::to_string(actualItemId));
            success = false;
        }
    }

    return success;
}

bool assertMapObservation(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (step.payload["map_loaded"] && step.payload["map_loaded"].IsScalar())
    {
        const std::string mapFileName = step.payload["map_loaded"].as<std::string>();

        if (lowerAscii(context.currentMapFileName) != lowerAscii(mapFileName))
        {
            if (context.mode == ScenarioMode::Faithful)
            {
                failures.push_back(stepLocation(step) + ": expected observed map '" + mapFileName
                    + "' but current map is '" + context.currentMapFileName + "'");
                return false;
            }

            return loadMap(context, mapFileName, failures);
        }

        return assertCurrentMap(context, step, mapFileName, failures);
    }

    if (step.payload["map_arrived"])
    {
        const YAML::Node arrivedNode = step.payload["map_arrived"];

        if (!arrivedNode.IsMap() || !arrivedNode["map"] || !arrivedNode["map"].IsScalar())
        {
            failures.push_back(stepLocation(step)
                + ": map_arrived assertion must contain scalar field 'map'");
            return false;
        }

        const std::string mapFileName = arrivedNode["map"].as<std::string>();

        if (lowerAscii(context.currentMapFileName) != lowerAscii(mapFileName))
        {
            if (context.mode == ScenarioMode::Faithful)
            {
                failures.push_back(stepLocation(step) + ": expected arrival map '" + mapFileName
                    + "' but current map is '" + context.currentMapFileName + "'");
                return false;
            }

            return loadMap(context, mapFileName, failures);
        }

        return assertCurrentMap(context, step, mapFileName, failures);
    }

    return true;
}

bool mapMatchesCurrentObservation(
    const ScenarioExecutionContext &context,
    const YAML::Node &node)
{
    if (!node["map"] || !node["map"].IsScalar())
    {
        return true;
    }

    return lowerAscii(node["map"].as<std::string>()) == lowerAscii(context.currentMapFileName);
}

bool assertMapVarNode(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &node,
    std::vector<std::string> &failures)
{
    if (!node.IsMap()
        || !((node["current"] && node["current"].IsScalar())
            || (node["value"] && node["value"].IsScalar())))
    {
        failures.push_back(stepLocation(step) + ": map variable assertion requires scalar field 'current' or 'value'");
        return false;
    }

    if (!context.eventRuntimeStateLoaded || !mapMatchesCurrentObservation(context, node))
    {
        return true;
    }

    int32_t actualValue = 0;

    if (node["index"] && node["index"].IsScalar())
    {
        const size_t index = node["index"].as<size_t>();

        if (index >= context.eventRuntimeState.mapVars.size())
        {
            failures.push_back(stepLocation(step) + ": map variable index out of range: "
                + std::to_string(index));
            return false;
        }

        actualValue = context.eventRuntimeState.mapVars[index];
    }
    else if (node["name"] && node["name"].IsScalar())
    {
        const std::string name = node["name"].as<std::string>();
        const auto iterator = context.eventRuntimeState.namedMapVars.find(name);
        actualValue = iterator != context.eventRuntimeState.namedMapVars.end() ? iterator->second : 0;
    }
    else
    {
        failures.push_back(stepLocation(step) + ": map variable assertion requires index or name");
        return false;
    }

    const int32_t expectedValue =
        node["current"] && node["current"].IsScalar() ? node["current"].as<int32_t>() : node["value"].as<int32_t>();

    if (actualValue != expectedValue)
    {
        failures.push_back(stepLocation(step) + ": expected map variable value "
            + std::to_string(expectedValue) + " but found " + std::to_string(actualValue));
        return false;
    }

    return true;
}

bool isMmergeCrossContinentScrollVariableName(const std::string &name)
{
    const std::string key = lowerAscii(name);
    return key == "mmerge.crosscontinents.scrollgenerated"
        || key == "mmerge.crosscontinents.scrollitemid";
}

bool assertNamedGlobalVarNode(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const YAML::Node &node,
    std::vector<std::string> &failures)
{
    if (!node.IsMap()
        || !node["name"] || !node["name"].IsScalar()
        || !((node["current"] && node["current"].IsScalar())
            || (node["value"] && node["value"].IsScalar())))
    {
        failures.push_back(stepLocation(step)
            + ": named global variable assertion requires scalar fields 'name' and 'current' or 'value'");
        return false;
    }

    if (!context.eventRuntimeStateLoaded)
    {
        return true;
    }

    const std::string name = node["name"].as<std::string>();

    if (isMmergeCrossContinentScrollVariableName(name))
    {
        return true;
    }

    const auto iterator = context.eventRuntimeState.namedGlobalVars.find(name);
    const int32_t actualValue = iterator != context.eventRuntimeState.namedGlobalVars.end() ? iterator->second : 0;
    const int32_t expectedValue =
        node["current"] && node["current"].IsScalar() ? node["current"].as<int32_t>() : node["value"].as<int32_t>();

    if (actualValue != expectedValue)
    {
        failures.push_back(stepLocation(step) + ": expected named global var '" + name + "' value "
            + std::to_string(expectedValue) + " but found " + std::to_string(actualValue));
        return false;
    }

    return true;
}

bool assertVariableObservations(
    const ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    bool success = true;

    for (const char *pFieldName : {"map_var_changed", "decor_var_changed", "state_dump_map_var"})
    {
        if (step.payload[pFieldName])
        {
            success = assertMapVarNode(context, step, step.payload[pFieldName], failures) && success;
        }
    }

    for (const char *pFieldName : {"named_map_var_changed", "state_dump_named_map_var"})
    {
        if (step.payload[pFieldName])
        {
            success = assertMapVarNode(context, step, step.payload[pFieldName], failures) && success;
        }
    }

    for (const char *pFieldName : {"named_global_var_changed", "state_dump_named_global_var"})
    {
        if (step.payload[pFieldName])
        {
            success = assertNamedGlobalVarNode(context, step, step.payload[pFieldName], failures) && success;
        }
    }

    return success;
}

bool assertActorVisible(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node actorNode = step.payload["actor_visible"];

    if (!actorNode)
    {
        return true;
    }

    if (!actorNode.IsMap() || !actorNode["actor_index"] || !actorNode["actor_index"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": actor_visible assertion requires scalar field 'actor_index'");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    if (actorNode["map"] && actorNode["map"].IsScalar())
    {
        const std::string mapFileName = actorNode["map"].as<std::string>();

        if (lowerAscii(context.runtimeDriver->currentMapFileName()) != lowerAscii(mapFileName))
        {
            std::string loadFailure;
            if (!context.runtimeDriver->loadMap(mapFileName, loadFailure))
            {
                failures.push_back(stepLocation(step) + ": " + loadFailure);
                return false;
            }
        }
    }

    GameplayActorInspectState inspectState = {};
    GameplayRuntimeActorState runtimeState = {};
    bool hasRuntimeState = false;
    std::string failure;
    const size_t actorIndex = actorNode["actor_index"].as<size_t>();

    if (!context.runtimeDriver->actorInspectState(actorIndex, inspectState, runtimeState, hasRuntimeState, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    if (actorNode["report_state"].as<bool>(false))
    {
        std::ostringstream message;
        message << "ACTOR index=" << actorIndex << " name=" << inspectState.displayName
                << " hp=" << inspectState.currentHp << '/' << inspectState.maxHp
                << " dead=" << boolText(inspectState.isDead);
        if (hasRuntimeState)
        {
            message << " position=" << runtimeState.preciseX << ',' << runtimeState.preciseY
                    << ',' << runtimeState.preciseZ
                    << " yaw_degrees=" << runtimeState.yawRadians * 180.0f / 3.14159265f
                    << " animation=" << static_cast<int>(runtimeState.animationState)
                    << " hostile=" << boolText(runtimeState.hostileToParty)
                    << " targeting_party=" << boolText(runtimeState.combatTargetingParty)
                    << " aim_target=" << boolText(runtimeState.hasAimTarget);
        }
        scenarioLog(context, ScenarioLogColor::Default, message.str());
    }

    bool success = true;

    if (actorNode["monster_id"] && actorNode["monster_id"].IsScalar()
        && inspectState.monsterId != actorNode["monster_id"].as<uint32_t>())
    {
        failures.push_back(stepLocation(step) + ": actor_visible monster_id mismatch");
        success = false;
    }

    if (actorNode["name"] && actorNode["name"].IsScalar()
        && inspectState.displayName != actorNode["name"].as<std::string>())
    {
        failures.push_back(stepLocation(step) + ": actor_visible name mismatch");
        success = false;
    }

    if (actorNode["dead"] && actorNode["dead"].IsScalar()
        && inspectState.isDead != actorNode["dead"].as<bool>())
    {
        failures.push_back(stepLocation(step) + ": actor_visible dead state mismatch");
        success = false;
    }

    const auto checkRuntimeField = [&](const char *pKey, const std::string &actual)
    {
        const YAML::Node expected = actorNode[pKey];

        if (!expected)
        {
            return true;
        }

        if (!expected.IsScalar())
        {
            failures.push_back(stepLocation(step) + ": actor_visible requires scalar field " + pKey);
            return false;
        }

        return recordScenarioAssertion(
            context, step, std::string("actor_visible.") + pKey, expected.as<std::string>(), actual, failures);
    };
    success = checkRuntimeField("current_hp", std::to_string(inspectState.currentHp)) && success;
    success = checkRuntimeField("max_hp", std::to_string(inspectState.maxHp)) && success;

    for (const char *pKey : {"hostile_to_party", "has_detected_party", "combat_targeting_party"})
    {
        if (!actorNode[pKey])
        {
            continue;
        }

        if (!hasRuntimeState)
        {
            failures.push_back(stepLocation(step) + ": actor_visible requires live runtime state for " + pKey);
            success = false;
            continue;
        }

        const bool value = std::string(pKey) == "hostile_to_party" ? runtimeState.hostileToParty
            : std::string(pKey) == "has_detected_party" ? runtimeState.hasDetectedParty
            : runtimeState.combatTargetingParty;
        success = checkRuntimeField(pKey, boolText(value)) && success;
    }

    // near: {x, y, max_distance}: the actor stands within max_distance of a point (horizontally); far: {x, y,
    // min_distance}: at least that far from it; faces: {x, y, within_degrees}: its yaw points at it.
    for (const char *pKey : {"near", "far", "faces"})
    {
        const YAML::Node pointNode = actorNode[pKey];

        if (!pointNode)
        {
            continue;
        }

        const char *pLimitKey = std::string(pKey) == "near" ? "max_distance"
            : std::string(pKey) == "far" ? "min_distance" : "within_degrees";

        if (!hasRuntimeState || !pointNode.IsMap() || !pointNode["x"] || !pointNode["y"] || !pointNode[pLimitKey])
        {
            failures.push_back(stepLocation(step) + ": actor_visible " + pKey + " needs live state, x, y and "
                + pLimitKey);
            success = false;
            continue;
        }

        const float dx = pointNode["x"].as<float>() - runtimeState.preciseX;
        const float dy = pointNode["y"].as<float>() - runtimeState.preciseY;
        const float limit = pointNode[pLimitKey].as<float>();
        const float distance = std::hypot(dx, dy);
        const float offDegrees = std::abs(std::remainder(std::atan2(dy, dx) - runtimeState.yawRadians,
            2.0f * 3.14159265f)) * 180.0f / 3.14159265f;
        const bool passed = std::string(pKey) == "near" ? distance <= limit
            : std::string(pKey) == "far" ? distance >= limit : offDegrees <= limit;

        if (!passed)
        {
            std::ostringstream message;
            message << stepLocation(step) << ": actor_visible " << pKey << " failed: distance=" << distance
                    << " off_degrees=" << offDegrees << " limit=" << limit;
            failures.push_back(message.str());
            success = false;
        }
    }

    if (actorNode["has_aim_target"])
    {
        success = hasRuntimeState
            && checkRuntimeField("has_aim_target", boolText(runtimeState.hasAimTarget)) && success;
    }
    syncContextFromRuntime(context);
    return success;
}

bool partyHasScenarioItem(const Party &party, uint32_t itemId)
{
    return itemId != 0 && (party.inventoryItemCount(itemId) > 0 || party.heldItemIdForQueries() == itemId);
}

bool isMmergeCrossContinentScrollItemId(uint32_t itemId)
{
    return itemId >= 770 && itemId <= 772;
}

bool isMmergeCrossContinentScrollItemNode(const YAML::Node &node)
{
    if (!node || !node.IsMap())
    {
        return false;
    }

    if (node["item_id"] && node["item_id"].IsScalar()
        && isMmergeCrossContinentScrollItemId(node["item_id"].as<uint32_t>()))
    {
        return true;
    }

    return node["item_name"] && node["item_name"].IsScalar()
        && node["item_name"].as<std::string>() == "Letter for YOU.";
}

bool assertQuestItemLocationObservation(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const char *pFieldName,
    std::vector<std::string> &failures)
{
    const YAML::Node observationNode = step.payload[pFieldName];

    if (!observationNode)
    {
        return true;
    }

    if (!observationNode.IsMap() || !observationNode["item_id"] || !observationNode["item_id"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": " + pFieldName
            + " observation must contain scalar field 'item_id'");
        return false;
    }

    const uint32_t itemId = observationNode["item_id"].as<uint32_t>();

    if (std::string(pFieldName) == "chest_contains_quest_item"
        && isMmergeCrossContinentScrollItemNode(observationNode))
    {
        return true;
    }

    if (partyHasScenarioItem(context.party, itemId))
    {
        scenarioLogAssert(
            context,
            step,
            "item_id",
            std::to_string(itemId),
            std::to_string(itemId),
            true);
        return true;
    }

    std::string mapFileName = context.currentMapFileName;

    if (observationNode["map"] && observationNode["map"].IsScalar())
    {
        mapFileName = observationNode["map"].as<std::string>();
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    seedRuntimeDriverFromContext(context);

    if (!mapFileName.empty()
        && (!context.runtimeDriver->hasActiveRuntime()
            || lowerAscii(context.runtimeDriver->currentMapFileName()) != lowerAscii(mapFileName)))
    {
        std::string loadFailure;

        if (!context.runtimeDriver->loadMap(mapFileName, loadFailure))
        {
            failures.push_back(stepLocation(step) + ": " + loadFailure);
            return context.mode != ScenarioMode::Faithful;
        }

        seedRuntimeDriverFromContext(context);
    }

    bool containsItem = false;
    std::string failure;
    bool inspected = false;

    if (std::string(pFieldName) == "world_item_spawned")
    {
        std::optional<size_t> worldItemIndex;

        if (observationNode["world_item_index"] && observationNode["world_item_index"].IsScalar())
        {
            worldItemIndex = observationNode["world_item_index"].as<size_t>();
        }

        inspected = context.runtimeDriver->worldItemContainsItem(worldItemIndex, itemId, containsItem, failure);
    }
    else if (std::string(pFieldName) == "chest_contains_quest_item")
    {
        std::optional<uint32_t> chestId;

        if (observationNode["chest_id"] && observationNode["chest_id"].IsScalar())
        {
            chestId = observationNode["chest_id"].as<uint32_t>();
        }

        inspected = context.runtimeDriver->activeChestContainsItem(chestId, itemId, containsItem, failure);
    }
    else if (std::string(pFieldName) == "corpse_contains_quest_item")
    {
        std::optional<uint32_t> corpseIndex;

        if (observationNode["corpse_index"] && observationNode["corpse_index"].IsScalar())
        {
            corpseIndex = observationNode["corpse_index"].as<uint32_t>();
        }
        else if (observationNode["actor_index"] && observationNode["actor_index"].IsScalar())
        {
            corpseIndex = observationNode["actor_index"].as<uint32_t>();
        }

        inspected = context.runtimeDriver->activeCorpseContainsItem(corpseIndex, itemId, containsItem, failure);
    }

    syncContextFromRuntime(context);

    if (containsItem || partyHasScenarioItem(context.party, itemId))
    {
        scenarioLogAssert(
            context,
            step,
            "item_id",
            std::to_string(itemId),
            std::to_string(itemId),
            true);
        return true;
    }

    if (context.mode != ScenarioMode::Faithful && context.party.tryGrantItem(itemId))
    {
        scenarioLogAssert(
            context,
            step,
            "item_id",
            std::to_string(itemId),
            std::to_string(itemId),
            true);
        return true;
    }

    scenarioLogAssert(
        context,
        step,
        "item_id",
        std::to_string(itemId),
        "<missing>",
        false);

    if (!inspected)
    {
        failures.push_back(stepLocation(step) + ": " + failure);
    }
    else
    {
        failures.push_back(stepLocation(step) + ": " + pFieldName + " did not expose item "
            + std::to_string(itemId));
    }

    return false;
}

bool assertItemVisible(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node itemNode = step.payload["item_visible"];

    if (!itemNode)
    {
        return true;
    }

    if (!itemNode.IsMap() || !itemNode["item_id"] || !itemNode["item_id"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": item_visible assertion requires scalar field 'item_id'");
        return false;
    }

    const uint32_t itemId = itemNode["item_id"].as<uint32_t>();
    const std::string source = itemNode["source"] && itemNode["source"].IsScalar()
        ? lowerAscii(itemNode["source"].as<std::string>())
        : "";

    if (source == "chest" && isMmergeCrossContinentScrollItemNode(itemNode))
    {
        return true;
    }

    if (partyHasScenarioItem(context.party, itemId))
    {
        return true;
    }

    if (source == "inventory" || source == "equipment" || source == "held")
    {
        if (context.mode != ScenarioMode::Faithful && context.party.tryGrantItem(itemId))
        {
            return true;
        }

        failures.push_back(stepLocation(step) + ": item_visible did not find item "
            + std::to_string(itemId) + " in party-visible item state");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return context.mode != ScenarioMode::Faithful;
    }

    seedRuntimeDriverFromContext(context);

    bool containsItem = false;
    std::string failure;
    bool inspected = true;

    if (source == "world_item")
    {
        std::optional<size_t> worldItemIndex;

        if (itemNode["world_item_index"] && itemNode["world_item_index"].IsScalar())
        {
            worldItemIndex = itemNode["world_item_index"].as<size_t>();
        }

        inspected = context.runtimeDriver->worldItemContainsItem(worldItemIndex, itemId, containsItem, failure);
    }
    else if (source == "chest")
    {
        std::optional<uint32_t> chestId;

        if (itemNode["chest_id"] && itemNode["chest_id"].IsScalar())
        {
            chestId = itemNode["chest_id"].as<uint32_t>();
        }

        inspected = context.runtimeDriver->activeChestContainsItem(chestId, itemId, containsItem, failure);
    }
    else if (source == "corpse")
    {
        std::optional<uint32_t> corpseIndex;

        if (itemNode["corpse_index"] && itemNode["corpse_index"].IsScalar())
        {
            corpseIndex = itemNode["corpse_index"].as<uint32_t>();
        }

        inspected = context.runtimeDriver->activeCorpseContainsItem(corpseIndex, itemId, containsItem, failure);
    }
    else if (context.mode != ScenarioMode::Faithful)
    {
        return true;
    }

    syncContextFromRuntime(context);

    if (containsItem || partyHasScenarioItem(context.party, itemId))
    {
        return true;
    }

    if (context.mode != ScenarioMode::Faithful)
    {
        return true;
    }

    failures.push_back(stepLocation(step) + ": item_visible did not expose item "
        + std::to_string(itemId) + (inspected ? "" : ": " + failure));
    return false;
}

bool appendStringMismatch(
    const ScenarioStep &step,
    const char *pAssertionName,
    const char *pFieldName,
    const std::string &actual,
    const YAML::Node &node,
    std::vector<std::string> &failures)
{
    if (!node[pFieldName] || !node[pFieldName].IsScalar())
    {
        return true;
    }

    const std::string expected = node[pFieldName].as<std::string>();
    if (lowerAscii(actual) == lowerAscii(expected))
    {
        return true;
    }

    failures.push_back(stepLocation(step) + ": " + pAssertionName + ' ' + pFieldName
        + " expected '" + expected + "' but found '" + actual + "'");
    return false;
}

bool appendUIntMismatch(
    const ScenarioStep &step,
    const char *pAssertionName,
    const char *pFieldName,
    uint32_t actual,
    const YAML::Node &node,
    std::vector<std::string> &failures)
{
    if (!node[pFieldName] || !node[pFieldName].IsScalar())
    {
        return true;
    }

    const uint32_t expected = node[pFieldName].as<uint32_t>();
    if (actual == expected)
    {
        return true;
    }

    failures.push_back(stepLocation(step) + ": " + pAssertionName + ' ' + pFieldName
        + " expected " + std::to_string(expected) + " but found " + std::to_string(actual));
    return false;
}

bool appendBoolMismatch(
    const ScenarioStep &step,
    const char *pAssertionName,
    const char *pFieldName,
    bool actual,
    const YAML::Node &node,
    std::vector<std::string> &failures)
{
    if (!node[pFieldName] || !node[pFieldName].IsScalar())
    {
        return true;
    }

    const bool expected = node[pFieldName].as<bool>();
    if (actual == expected)
    {
        return true;
    }

    failures.push_back(stepLocation(step) + ": " + pAssertionName + ' ' + pFieldName
        + " expected " + (expected ? "true" : "false") + " but found " + (actual ? "true" : "false"));
    return false;
}

bool assertMapTransitionTrace(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const char *pFieldName,
    const std::optional<EventRuntimeState::MapTransitionTrace> EventRuntimeState::*pMember,
    std::vector<std::string> &failures)
{
    const YAML::Node transitionNode = step.payload[pFieldName];

    if (!transitionNode)
    {
        return true;
    }

    if (context.runtimeDriver)
    {
        syncContextFromRuntime(context);
    }

    if (!context.eventRuntimeStateLoaded || !(context.eventRuntimeState.*pMember).has_value())
    {
        if (context.mode == ScenarioMode::Faithful)
        {
            failures.push_back(stepLocation(step) + ": expected " + pFieldName
                + " trace but no transition trace was recorded");
        }

        return context.mode != ScenarioMode::Faithful;
    }

    const EventRuntimeState::MapTransitionTrace &trace = *(context.eventRuntimeState.*pMember);
    bool success = true;
    success = appendStringMismatch(step, pFieldName, "source_kind", trace.sourceKind, transitionNode, failures)
        && success;
    success = appendUIntMismatch(step, pFieldName, "source_id", trace.sourceId, transitionNode, failures)
        && success;
    success = appendUIntMismatch(step, pFieldName, "action_id", trace.actionId, transitionNode, failures)
        && success;
    success = appendUIntMismatch(step, pFieldName, "event_id", trace.eventId, transitionNode, failures)
        && success;
    success = appendBoolMismatch(
        step,
        pFieldName,
        "confirmation_required",
        trace.confirmationRequired,
        transitionNode,
        failures) && success;
    success = appendStringMismatch(step, pFieldName, "destination_map", trace.destinationMap, transitionNode, failures)
        && success;
    success =
        appendStringMismatch(step, pFieldName, "destination_name", trace.destinationName, transitionNode, failures)
        && success;
    success = appendUIntMismatch(step, pFieldName, "travel_days", trace.travelDays, transitionNode, failures)
        && success;
    if (transitionNode["use_start_position"] && transitionNode["use_start_position"].IsScalar())
    {
        const bool expectedUseStartPosition = transitionNode["use_start_position"].as<bool>();
        if (trace.useStartPosition != expectedUseStartPosition)
        {
            scenarioLog(
                context,
                ScenarioLogColor::Yellow,
                "WARNING step=" + std::to_string(step.stepIndex)
                    + " trace_line=" + std::to_string(step.sourceLine)
                    + " " + pFieldName + ".use_start_position expected="
                    + boolText(expectedUseStartPosition)
                    + " actual=" + boolText(trace.useStartPosition));
        }
    }

    if (transitionNode["route_index"] && transitionNode["route_index"].IsScalar())
    {
        const uint32_t expected = transitionNode["route_index"].as<uint32_t>();
        if (!trace.routeIndex.has_value() || *trace.routeIndex != expected)
        {
            failures.push_back(stepLocation(step) + ": " + pFieldName + " route_index mismatch");
            success = false;
        }
    }

    if (transitionNode["direction_degrees"] && transitionNode["direction_degrees"].IsScalar())
    {
        const std::string expectedText = transitionNode["direction_degrees"].as<std::string>();
        if (expectedText == "none")
        {
            if (trace.directionDegrees.has_value())
            {
                failures.push_back(stepLocation(step) + ": " + pFieldName + " direction_degrees mismatch");
                success = false;
            }
        }
        else if (!trace.directionDegrees.has_value()
                 || *trace.directionDegrees != transitionNode["direction_degrees"].as<int32_t>())
        {
            failures.push_back(stepLocation(step) + ": " + pFieldName + " direction_degrees mismatch");
            success = false;
        }
    }

    if (transitionNode["pos"] && transitionNode["pos"].IsScalar())
    {
        const std::string actualPos = "(" + std::to_string(trace.x) + "," + std::to_string(trace.y)
            + "," + std::to_string(trace.z) + ")";
        if (transitionNode["pos"].as<std::string>() != actualPos)
        {
            failures.push_back(stepLocation(step) + ": " + pFieldName + " pos mismatch");
            success = false;
        }
    }

    return context.mode != ScenarioMode::Faithful || success;
}

bool assertChestOpened(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node chestNode = step.payload["chest_opened"];

    if (!chestNode)
    {
        return true;
    }

    if (context.runtimeDriver)
    {
        syncContextFromRuntime(context);
    }

    if (!context.eventRuntimeStateLoaded || !context.eventRuntimeState.lastChestOpened.has_value())
    {
        return context.mode != ScenarioMode::Faithful;
    }

    const EventRuntimeState::ChestOpenedTrace &trace = *context.eventRuntimeState.lastChestOpened;
    bool success = true;
    success = appendStringMismatch(step, "chest_opened", "scene_kind", trace.sceneKind, chestNode, failures)
        && success;
    success = appendStringMismatch(step, "chest_opened", "map", trace.map, chestNode, failures) && success;
    success = appendUIntMismatch(step, "chest_opened", "chest_id", trace.chestId, chestNode, failures) && success;
    success = appendUIntMismatch(
        step,
        "chest_opened",
        "item_count",
        static_cast<uint32_t>(trace.itemCount),
        chestNode,
        failures) && success;
    success = appendUIntMismatch(
        step,
        "chest_opened",
        "hidden_item_count",
        static_cast<uint32_t>(trace.hiddenItemCount),
        chestNode,
        failures) && success;
    return context.mode != ScenarioMode::Faithful || success;
}

bool assertActorDialogStarted(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node dialogNode = step.payload["actor_dialog_started"];

    if (!dialogNode)
    {
        return true;
    }

    if (context.runtimeDriver)
    {
        syncContextFromRuntime(context);
    }

    if (!context.eventRuntimeStateLoaded || !context.eventRuntimeState.lastActorDialogStarted.has_value())
    {
        return context.mode != ScenarioMode::Faithful;
    }

    const EventRuntimeState::ActorDialogStartedTrace &trace =
        *context.eventRuntimeState.lastActorDialogStarted;
    bool success = true;
    success = appendStringMismatch(step, "actor_dialog_started", "kind", trace.kind, dialogNode, failures)
        && success;
    success = appendStringMismatch(step, "actor_dialog_started", "map", trace.map, dialogNode, failures) && success;
    success = appendUIntMismatch(step, "actor_dialog_started", "npc_id", trace.npcId, dialogNode, failures)
        && success;
    success = appendUIntMismatch(step, "actor_dialog_started", "source_id", trace.sourceId, dialogNode, failures)
        && success;
    success = appendUIntMismatch(
        step,
        "actor_dialog_started",
        "host_house_id",
        trace.hostHouseId,
        dialogNode,
        failures) && success;

    if (dialogNode["actor_index"] && dialogNode["actor_index"].IsScalar())
    {
        const std::string expectedText = dialogNode["actor_index"].as<std::string>();
        if (expectedText == "none")
        {
            if (trace.actorIndex.has_value())
            {
                failures.push_back(stepLocation(step) + ": actor_dialog_started actor_index mismatch");
                success = false;
            }
        }
        else if (!trace.actorIndex.has_value() || *trace.actorIndex != dialogNode["actor_index"].as<uint32_t>())
        {
            failures.push_back(stepLocation(step) + ": actor_dialog_started actor_index mismatch");
            success = false;
        }
    }

    return context.mode != ScenarioMode::Faithful || success;
}

bool assertPressurePlateTriggered(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node pressureNode = step.payload["pressure_plate_triggered"];

    if (!pressureNode)
    {
        return true;
    }

    if (context.runtimeDriver)
    {
        syncContextFromRuntime(context);
    }

    bool shouldTriggerExpectedFace = !context.eventRuntimeStateLoaded
        || !context.eventRuntimeState.lastPressurePlateTrigger.has_value();

    if (pressureNode["force_replay"] && pressureNode["force_replay"].IsScalar()
        && pressureNode["force_replay"].as<bool>())
    {
        shouldTriggerExpectedFace = true;
    }

    if (!shouldTriggerExpectedFace && pressureNode["face_index"] && pressureNode["face_index"].IsScalar())
    {
        const EventRuntimeState::PressurePlateTrigger &trigger =
            *context.eventRuntimeState.lastPressurePlateTrigger;

        if (trigger.faceIndex != pressureNode["face_index"].as<size_t>())
        {
            shouldTriggerExpectedFace = true;
        }
    }

    if (!shouldTriggerExpectedFace && pressureNode["event_id"] && pressureNode["event_id"].IsScalar())
    {
        const EventRuntimeState::PressurePlateTrigger &trigger =
            *context.eventRuntimeState.lastPressurePlateTrigger;

        if (trigger.eventId != pressureNode["event_id"].as<uint32_t>())
        {
            shouldTriggerExpectedFace = true;
        }
    }

    if (!shouldTriggerExpectedFace && pressureNode["world"] && pressureNode["world"].IsScalar())
    {
        const EventRuntimeState::PressurePlateTrigger &trigger =
            *context.eventRuntimeState.lastPressurePlateTrigger;

        if (lowerAscii(trigger.world) != lowerAscii(pressureNode["world"].as<std::string>()))
        {
            shouldTriggerExpectedFace = true;
        }
    }

    if (shouldTriggerExpectedFace)
    {
        if (pressureNode["world"] && pressureNode["world"].IsScalar()
            && lowerAscii(pressureNode["world"].as<std::string>()) == "indoor"
            && pressureNode["face_index"] && pressureNode["face_index"].IsScalar())
        {
            if (!ensureRuntimeDriver(context, failures))
            {
                return context.mode != ScenarioMode::Faithful;
            }

            if (!context.currentMapFileName.empty()
                && (!context.runtimeDriver->hasActiveRuntime()
                    || lowerAscii(context.runtimeDriver->currentMapFileName()) != lowerAscii(context.currentMapFileName)))
            {
                std::string loadFailure;

                if (!context.runtimeDriver->loadMap(context.currentMapFileName, loadFailure))
                {
                    failures.push_back(stepLocation(step) + ": " + loadFailure);
                    return context.mode != ScenarioMode::Faithful;
                }
            }

            seedRuntimeDriverFromContext(context);

            std::string failure;
            const size_t faceIndex = pressureNode["face_index"].as<size_t>();

            if (context.runtimeDriver->triggerIndoorPressurePlate(faceIndex, failure))
            {
                syncContextFromRuntime(context);
                std::cout << "PRESSURE replayed indoor face_index=" << faceIndex << "\n";
            }
            else if (pressureNode["event_id"] && pressureNode["event_id"].IsScalar())
            {
                const uint16_t eventId = pressureNode["event_id"].as<uint16_t>();
                std::string eventFailure;

                if (!context.runtimeDriver->executeMapEvent(eventId, eventFailure))
                {
                    if (context.mode == ScenarioMode::Faithful)
                    {
                        failures.push_back(stepLocation(step) + ": " + failure + "; fallback event "
                            + std::to_string(eventId) + " also failed: " + eventFailure);
                        return false;
                    }
                }
                else
                {
                    syncContextFromRuntime(context);

                    EventRuntimeState::PressurePlateTrigger trigger = {};
                    trigger.world = pressureNode["world"] && pressureNode["world"].IsScalar()
                        ? pressureNode["world"].as<std::string>()
                        : "indoor";
                    trigger.eventId = eventId;
                    trigger.faceIndex = faceIndex;
                    if (pressureNode["attributes"] && pressureNode["attributes"].IsScalar())
                    {
                        trigger.attributes = pressureNode["attributes"].as<uint32_t>();
                    }
                    context.eventRuntimeState.lastPressurePlateTrigger = trigger;
                    context.eventRuntimeStateLoaded = true;
                    if (context.runtimeDriver->eventRuntimeState() != nullptr)
                    {
                        context.runtimeDriver->eventRuntimeState()->lastPressurePlateTrigger = trigger;
                    }

                    scenarioLog(
                        context,
                        ScenarioLogColor::Yellow,
                        "WARNING pressure_plate step=" + std::to_string(step.stepIndex)
                            + " face_index=" + std::to_string(faceIndex)
                            + " could not be retriggered directly; replayed event_id="
                            + std::to_string(eventId));
                }
            }
            else if (context.mode == ScenarioMode::Faithful)
            {
                failures.push_back(stepLocation(step) + ": " + failure);
                return false;
            }
        }
    }

    if (!context.eventRuntimeStateLoaded || !context.eventRuntimeState.lastPressurePlateTrigger.has_value())
    {
        if (context.mode == ScenarioMode::Faithful)
        {
            failures.push_back(stepLocation(step) + ": expected pressure_plate_triggered event but none was recorded");
        }

        return context.mode != ScenarioMode::Faithful;
    }

    const EventRuntimeState::PressurePlateTrigger &trigger =
        *context.eventRuntimeState.lastPressurePlateTrigger;
    bool success = true;

    if (pressureNode["world"] && pressureNode["world"].IsScalar()
        && lowerAscii(trigger.world) != lowerAscii(pressureNode["world"].as<std::string>()))
    {
        failures.push_back(stepLocation(step) + ": pressure_plate_triggered world mismatch");
        success = false;
    }

    if (pressureNode["event_id"] && pressureNode["event_id"].IsScalar()
        && trigger.eventId != pressureNode["event_id"].as<uint32_t>())
    {
        failures.push_back(stepLocation(step) + ": pressure_plate_triggered event_id mismatch");
        success = false;
    }

    if (pressureNode["face_index"] && pressureNode["face_index"].IsScalar()
        && trigger.faceIndex != pressureNode["face_index"].as<size_t>())
    {
        failures.push_back(stepLocation(step) + ": pressure_plate_triggered face_index mismatch");
        success = false;
    }

    if (pressureNode["bmodel_index"] && pressureNode["bmodel_index"].IsScalar()
        && trigger.bmodelIndex != pressureNode["bmodel_index"].as<size_t>())
    {
        failures.push_back(stepLocation(step) + ": pressure_plate_triggered bmodel_index mismatch");
        success = false;
    }

    if (pressureNode["attributes"] && pressureNode["attributes"].IsScalar()
        && trigger.attributes != pressureNode["attributes"].as<uint32_t>())
    {
        failures.push_back(stepLocation(step) + ": pressure_plate_triggered attributes mismatch");
        success = false;
    }

    return context.mode != ScenarioMode::Faithful || success;
}

bool assertDialogueCanceled(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node cancelNode = step.payload["dialogue_canceled"];

    if (!cancelNode)
    {
        return true;
    }

    if (context.runtimeDriver)
    {
        syncContextFromRuntime(context);
    }

    if (!context.eventRuntimeStateLoaded || !context.eventRuntimeState.lastDialogueCanceled.has_value())
    {
        if (context.mode == ScenarioMode::Faithful)
        {
            scenarioLogAssert(context, step, "dialogue_canceled", "present", "<none>", false);
            failures.push_back(stepLocation(step) + ": expected dialogue_canceled event but none was recorded");
        }

        return context.mode != ScenarioMode::Faithful;
    }

    const EventRuntimeState::DialogueCanceled &canceled = *context.eventRuntimeState.lastDialogueCanceled;
    bool success = true;

    if (cancelNode["kind"] && cancelNode["kind"].IsScalar()
        && canceled.kind != cancelNode["kind"].as<std::string>())
    {
        failures.push_back(stepLocation(step) + ": dialogue_canceled kind mismatch");
        success = false;
    }

    if (cancelNode["active_source_id"] && cancelNode["active_source_id"].IsScalar()
        && canceled.activeSourceId != cancelNode["active_source_id"].as<uint32_t>())
    {
        failures.push_back(stepLocation(step) + ": dialogue_canceled active_source_id mismatch");
        success = false;
    }

    if (cancelNode["source_id"] && cancelNode["source_id"].IsScalar()
        && canceled.sourceId != cancelNode["source_id"].as<uint32_t>())
    {
        failures.push_back(stepLocation(step) + ": dialogue_canceled source_id mismatch");
        success = false;
    }

    if (cancelNode["house_dialog"] && cancelNode["house_dialog"].IsScalar()
        && canceled.houseDialog != cancelNode["house_dialog"].as<bool>())
    {
        failures.push_back(stepLocation(step) + ": dialogue_canceled house_dialog mismatch");
        success = false;
    }

    if (cancelNode["action_count"] && cancelNode["action_count"].IsScalar()
        && canceled.actionCount != cancelNode["action_count"].as<size_t>())
    {
        failures.push_back(stepLocation(step) + ": dialogue_canceled action_count mismatch");
        success = false;
    }

    return context.mode != ScenarioMode::Faithful || success;
}

bool applyItemReceivedObservation(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node itemNode = step.payload["item_received"];

    if (!itemNode)
    {
        return true;
    }

    if (!itemNode.IsMap() || !itemNode["item_id"] || !itemNode["item_id"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": item_received assertion must contain scalar field 'item_id'");
        return false;
    }

    const uint32_t itemId = itemNode["item_id"].as<uint32_t>();
    const bool receivedToHeld =
        itemNode["destination"] && itemNode["destination"].IsScalar()
        && itemNode["destination"].as<std::string>() == "held";
    const std::string source = itemNode["source"] && itemNode["source"].IsScalar()
        ? lowerAscii(itemNode["source"].as<std::string>())
        : "";

    if (receivedToHeld)
    {
        if (context.party.heldItemIdForQueries() != itemId)
        {
            if (source == "chest")
            {
                if (!ensureRuntimeDriver(context, failures))
                {
                    return context.mode != ScenarioMode::Faithful;
                }

                seedRuntimeDriverFromContext(context);

                std::optional<uint32_t> chestId;

                if (itemNode["chest_id"] && itemNode["chest_id"].IsScalar())
                {
                    chestId = itemNode["chest_id"].as<uint32_t>();
                }

                GameplayChestItemState chestItem = {};
                std::string failure;

                if (context.runtimeDriver->takeActiveChestItemById(chestId, itemId, chestItem, failure))
                {
                    syncContextFromRuntime(context);
                }
                else if (context.mode == ScenarioMode::Faithful)
                {
                    scenarioLogAssert(
                        context,
                        step,
                        "item_id",
                        std::to_string(itemId),
                        "<missing>",
                        false);
                    failures.push_back(stepLocation(step) + ": " + failure);
                    return false;
                }
            }

            if (context.party.heldItemIdForQueries() == itemId)
            {
                scenarioLogAssert(
                    context,
                    step,
                    "item_id",
                    std::to_string(itemId),
                    std::to_string(itemId),
                    true);
                return true;
            }

            if (context.mode == ScenarioMode::Faithful)
            {
                scenarioLogAssert(
                    context,
                    step,
                    "item_id",
                    std::to_string(itemId),
                    "<missing>",
                    false);
                failures.push_back(stepLocation(step) + ": observed held item " + std::to_string(itemId)
                    + " was not held by the party");
                return false;
            }

            InventoryItem heldItem = {};
            heldItem.objectDescriptionId = itemId;
            heldItem.quantity = 1;
            context.party.setHeldItemForQueries(heldItem);
        }

        scenarioLogAssert(
            context,
            step,
            "item_id",
            std::to_string(itemId),
            std::to_string(itemId),
            true);
        return true;
    }

    bool itemFound = context.party.inventoryItemCount(itemId) > 0;

    if (!itemFound)
    {
        if (context.mode != ScenarioMode::Faithful && context.party.tryGrantItem(itemId))
        {
            itemFound = true;
        }
        else
        {
            scenarioLogAssert(
                context,
                step,
                "item_id",
                std::to_string(itemId),
                "<missing>",
                false);
            failures.push_back(stepLocation(step) + ": observed item " + std::to_string(itemId)
                + " was not in party inventory");
            return false;
        }
    }

    scenarioLogAssert(
        context,
        step,
        "item_id",
        std::to_string(itemId),
        itemFound ? std::to_string(itemId) : "<missing>",
        itemFound);
    return true;
}

bool assertQuestBits(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    const char *pFieldName,
    bool expectedValue,
    std::vector<std::string> &failures)
{
    const YAML::Node qbitNode = step.payload[pFieldName];

    if (!qbitNode)
    {
        return true;
    }

    if (!qbitNode.IsSequence())
    {
        failures.push_back(stepLocation(step) + ": field '" + pFieldName + "' must be a sequence");
        return false;
    }

    bool success = true;

    for (const YAML::Node &entryNode : qbitNode)
    {
        if (!entryNode.IsScalar())
        {
            failures.push_back(stepLocation(step) + ": qbit entries must be scalar");
            success = false;
            continue;
        }

        const uint32_t qbitId = entryNode.as<uint32_t>();
        const bool actualValue = context.party.hasQuestBit(qbitId);
        scenarioLogAssert(
            context,
            step,
            std::string(pFieldName) + "[" + std::to_string(qbitId) + "]",
            boolText(expectedValue),
            boolText(actualValue),
            actualValue == expectedValue);

        if (actualValue != expectedValue)
        {
            if (context.mode == ScenarioMode::Faithful)
            {
                failures.push_back(stepLocation(step) + ": qbit " + std::to_string(qbitId)
                    + " expected " + (expectedValue ? "set" : "clear"));
                success = false;
                continue;
            }

            context.party.setQuestBit(qbitId, expectedValue);
        }
    }

    return success;
}

bool assertRuntimeWorldItemState(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node node = step.payload["world_item_state"];

    if (!node)
    {
        return true;
    }

    if (!node.IsMap() || !node["item_id"] || !node["item_id"].IsScalar()
        || !node["present"] || !node["present"].IsScalar())
    {
        failures.push_back(stepLocation(step) + ": world_item_state requires scalar item_id and present");
        return false;
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    bool present = false;
    std::string failure;
    const uint32_t itemId = node["item_id"].as<uint32_t>();

    if (!context.runtimeDriver->worldItemContainsItem(std::nullopt, itemId, present, failure))
    {
        failures.push_back(stepLocation(step) + ": " + failure);
        return false;
    }

    return recordScenarioAssertion(
        context, step, "world_item_state[" + std::to_string(itemId) + "]",
        boolText(node["present"].as<bool>()), boolText(present), failures);
}

bool assertRuntimeEventVariable(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    const YAML::Node node = step.payload["runtime_event_variable"];

    if (!node)
    {
        return true;
    }

    if (!node.IsMap() || !node["id"].IsScalar()
        || (!node["value"] && !node["min"] && !node["max"]))
    {
        failures.push_back(stepLocation(step) + ": runtime_event_variable requires id and value, min or max");
        return false;
    }

    for (const char *pKey : {"value", "min", "max"})
    {
        if (node[pKey] && !node[pKey].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": runtime_event_variable requires scalar " + pKey);
            return false;
        }
    }

    if (!ensureRuntimeDriver(context, failures))
    {
        return false;
    }

    const Party *pParty = context.runtimeDriver->party();
    const EventRuntimeState *pRuntimeState = context.runtimeDriver->eventRuntimeState();

    if (pParty == nullptr || pRuntimeState == nullptr)
    {
        failures.push_back(stepLocation(step) + ": runtime_event_variable requires loaded party and event state");
        return false;
    }

    std::optional<size_t> memberIndex;

    if (node["member_index"])
    {
        if (!node["member_index"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": runtime_event_variable member_index must be scalar");
            return false;
        }

        memberIndex = node["member_index"].as<size_t>();

        if (pParty->member(*memberIndex) == nullptr)
        {
            failures.push_back(stepLocation(step) + ": runtime_event_variable member_index is out of range");
            return false;
        }
    }

    const uint32_t id = node["id"].as<uint32_t>();
    const int32_t actual = EventRuntime::getVariableValue(
        *pRuntimeState, EventRuntime::decodeVariable(id), pParty, memberIndex);
    const std::string field = "runtime_event_variable[" + std::to_string(id) + "]"
        + (memberIndex ? ".member[" + std::to_string(*memberIndex) + "]" : "");
    bool success = true;
    if (node["value"])
    {
        success = recordScenarioAssertion(
            context, step, field, node["value"].as<std::string>(), std::to_string(actual), failures);
    }
    for (const char *pKey : {"min", "max"})
    {
        if (!node[pKey])
        {
            continue;
        }
        const int32_t bound = node[pKey].as<int32_t>();
        const bool isMinimum = std::string(pKey) == "min";
        const bool withinBound = isMinimum ? actual >= bound : actual <= bound;
        const std::string expected = std::string(isMinimum ? ">=" : "<=") + std::to_string(bound);
        scenarioLogAssert(context, step, field, expected, std::to_string(actual), withinBound);
        if (!withinBound)
        {
            failures.push_back(stepLocation(step) + ": expected " + field + expected
                + " but current value=" + std::to_string(actual));
        }
        success = withinBound && success;
    }
    return success;
}

bool runAssert(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    bool success = true;

    if (step.payload["map"])
    {
        if (!step.payload["map"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": field 'map' must be scalar");
            success = false;
        }
        else
        {
            const std::string expectedMap = step.payload["map"].as<std::string>();
            success = assertCurrentMap(context, step, expectedMap, failures) && success;
        }
    }

    success = assertInventoryAnyMember(context, step, failures) && success;
    success = assertInventoryList(context, step, failures) && success;
    success = assertEquippedItems(context, step, failures) && success;
    success = assertHeldItem(context, step, failures) && success;
    success = assertItemVisible(context, step, failures) && success;
    success = assertRuntimeWorldItemState(context, step, failures) && success;
    success = applyItemReceivedObservation(context, step, failures) && success;
    success = assertQuestItemLocationObservation(context, step, "world_item_spawned", failures) && success;
    success = assertQuestItemLocationObservation(context, step, "chest_contains_quest_item", failures) && success;
    success = assertQuestItemLocationObservation(context, step, "corpse_contains_quest_item", failures) && success;
    success = assertAwards(context, step, failures) && success;
    success = assertAwardCleared(context, step, failures) && success;
    success = assertMapObservation(context, step, failures) && success;
    success = assertVariableObservations(context, step, failures) && success;
    success = assertRuntimeEventVariable(context, step, failures) && success;
    success = assertActorVisible(context, step, failures) && success;
    success = assertMapTransitionTrace(
        context,
        step,
        "map_transition_requested",
        &EventRuntimeState::lastMapTransitionRequested,
        failures) && success;
    success = assertMapTransitionTrace(
        context,
        step,
        "map_transition_confirmed",
        &EventRuntimeState::lastMapTransitionConfirmed,
        failures) && success;
    success = assertMapTransitionTrace(
        context,
        step,
        "map_transition_canceled",
        &EventRuntimeState::lastMapTransitionCanceled,
        failures) && success;
    success = assertChestOpened(context, step, failures) && success;
    success = assertActorDialogStarted(context, step, failures) && success;
    success = assertPressurePlateTriggered(context, step, failures) && success;
    success = assertDialogueCanceled(context, step, failures) && success;
    success = assertDialogState(context, step, step.payload["dialog"], "assert", failures) && success;
    success = assertInputPromptState(context, step, step.payload["input_prompt"], "assert", failures) && success;
    success = applyRecordedNewGamePartyMember(context, step, failures) && success;
    success = applyRecordedNewGamePartySkill(context, step, failures) && success;
    success = assertQuestBits(context, step, "qbit_set", true, failures) && success;
    success = assertQuestBits(context, step, "qbit_clear", false, failures) && success;
    return success;
}

bool runSetupShortcut(
    ScenarioExecutionContext &context,
    const ScenarioStep &step,
    std::vector<std::string> &failures)
{
    if (!payloadIsMap(step, failures))
    {
        return false;
    }

    std::string kind;

    if (!readScalarString(step, "kind", kind, failures, true))
    {
        return false;
    }

    if (kind == "grant_item")
    {
        if (!step.payload["item_id"] || !step.payload["item_id"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": grant_item shortcut requires scalar field 'item_id'");
            return false;
        }

        return context.party.tryGrantItem(step.payload["item_id"].as<uint32_t>());
    }

    if (kind == "set_qbit")
    {
        if (!step.payload["id"] || !step.payload["id"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": set_qbit shortcut requires scalar field 'id'");
            return false;
        }

        const bool value = !step.payload["value"] || step.payload["value"].as<bool>();
        context.party.setQuestBit(step.payload["id"].as<uint32_t>(), value);
        return true;
    }

    if (kind == "map_transition_prompt")
    {
        if (!step.payload["source_id"] || !step.payload["source_id"].IsScalar())
        {
            failures.push_back(stepLocation(step) + ": map_transition_prompt shortcut requires scalar field 'source_id'");
            return false;
        }

        if (!ensureRuntimeDriver(context, failures))
        {
            return false;
        }

        seedRuntimeDriverFromContext(context);

        std::string failure;
        if (!context.runtimeDriver->openMapTransitionPrompt(step.payload["source_id"].as<uint32_t>(), failure))
        {
            failures.push_back(stepLocation(step) + ": " + failure);
            return false;
        }

        scenarioLog(
            context,
            ScenarioLogColor::Cyan,
            std::string("DIALOG after map_transition_prompt step=") + std::to_string(step.stepIndex)
                + " " + context.runtimeDriver->activeDialogSummary());
        syncContextFromRuntime(context);
        return true;
    }

    return true;
}
}

ScenarioRunResult ScenarioRunner::run(
    const std::filesystem::path &basePath,
    const Engine::ApplicationConfig &config,
    const ScenarioDocument &document,
    const ScenarioRunOptions &options)
{
    ScenarioRunResult result = {};

    if (options.dryRun)
    {
        std::cout << "Scenario dry run: " << document.id << " steps=" << document.steps.size() << '\n';

        for (const ScenarioStep &step : document.steps)
        {
            std::cout << "  " << step.stepIndex << ": " << step.kindName << '\n';
        }

        result.success = true;
        return result;
    }

    SDL_Environment *pEnvironment = SDL_GetEnvironment();

    if (pEnvironment == nullptr || !SDL_SetEnvironmentVariable(pEnvironment, "SDL_AUDIODRIVER", "dummy", true))
    {
        result.failures.push_back("could not force dummy audio driver for scenario runner");
        return result;
    }

    Engine::ApplicationConfig scenarioConfig = config;

    if (!document.world.empty())
    {
        scenarioConfig.activeWorldId = document.world;
    }

    ScenarioExecutionContext context = {};
    context.basePath = basePath;
    context.config = scenarioConfig;
    context.mode = document.mode;
    context.saveGameRootPath =
        std::filesystem::temp_directory_path() / "openyamm_scenario_saves" / sanitizeScenarioFileStem(document.id);
    context.gameDataLoader.setActiveWorldId(scenarioConfig.activeWorldId);
    context.traceEnabled = options.trace && !envFlagEnabled("OPENYAMM_SCENARIO_QUIET");
    context.colorsEnabled = options.colors && !envFlagEnabled("NO_COLOR")
        && !envFlagEnabled("OPENYAMM_SCENARIO_NO_COLOR");
    context.totalStepCount = document.steps.size();

    if (!context.assetFileSystem.initialize(
            basePath,
            scenarioConfig.assetRoot,
            scenarioConfig.assetScaleTier,
            scenarioConfig.assetScaleProfile,
            scenarioConfig.activeWorldId))
    {
        result.failures.push_back("could not initialize asset file system");
        return result;
    }

    scenarioLog(context, ScenarioLogColor::Blue, "Scenario run: " + document.id);
    scenarioLog(context, ScenarioLogColor::Blue, "Development assets: " + scenarioConfig.assetRoot);
    scenarioLog(context, ScenarioLogColor::Blue, "Active world: " + scenarioConfig.activeWorldId);
    scenarioLog(context, ScenarioLogColor::Blue, "Mounted search paths:");

    const std::vector<std::string> searchPaths = context.assetFileSystem.getSearchPaths();

    for (const std::string &searchPath : searchPaths)
    {
        scenarioLog(context, ScenarioLogColor::Blue, "  " + searchPath);
    }

    for (const ScenarioStep &step : document.steps)
    {
        scenarioLog(context, ScenarioLogColor::Yellow, "RUN " + stepSummary(context, step));

        bool stepSuccess = false;
        const size_t failureCountBeforeStep = result.failures.size();

        if (!syncRecordedGameTimeBeforeStep(context, step, result.failures))
        {
            stepSuccess = false;
        }
        else switch (step.kind)
        {
        case ScenarioStepKind::NewGameFlow:
            stepSuccess = runNewGameFlow(context, step, result.failures);
            break;
        case ScenarioStepKind::LoadMap:
            stepSuccess = runLoadMap(context, step, result.failures);
            break;
        case ScenarioStepKind::Travel:
            stepSuccess = runTravel(context, step, result.failures);
            break;
        case ScenarioStepKind::SetPose:
            stepSuccess = runSetPose(context, step, result.failures);
            break;
        case ScenarioStepKind::PressAction:
            stepSuccess = runPressAction(context, step, result.failures);
            break;
        case ScenarioStepKind::PressInteract:
            stepSuccess = runPressInteract(context, step, result.failures);
            break;
        case ScenarioStepKind::SpeakNpc:
            stepSuccess = runSpeakNpc(context, step, result.failures);
            break;
        case ScenarioStepKind::MovementSegment:
            stepSuccess = runMovementSegment(context, step, result.failures);
            break;
        case ScenarioStepKind::Note:
        case ScenarioStepKind::Todo:
        case ScenarioStepKind::TraceObservation:
            stepSuccess = runNoOpObservation(step, result.failures);
            break;
        case ScenarioStepKind::InteractActor:
            stepSuccess = runInteractActor(context, step, result.failures);
            break;
        case ScenarioStepKind::EnterDoorAt:
            stepSuccess = runEnterDoorAt(context, step, result.failures);
            break;
        case ScenarioStepKind::CombatLoot:
            stepSuccess = runCombatLoot(context, step, result.failures);
            break;
        case ScenarioStepKind::DamageActor:
            stepSuccess = runDamageActor(context, step, result.failures);
            break;
        case ScenarioStepKind::WaitUntil:
            stepSuccess = runWaitUntil(context, step, result.failures);
            break;
        case ScenarioStepKind::AssertMechanism:
            stepSuccess = runAssertMechanism(context, step, result.failures);
            break;
        case ScenarioStepKind::AdvanceRuntime:
            stepSuccess = runAdvanceRuntime(context, step, result.failures);
            break;
        case ScenarioStepKind::AdvanceGameTime:
            stepSuccess = runAdvanceGameTime(context, step, result.failures);
            break;
        case ScenarioStepKind::SelectTopic:
            stepSuccess = runSelectTopic(context, step, result.failures);
            break;
        case ScenarioStepKind::AnswerInput:
            stepSuccess = runAnswerInput(context, step, result.failures);
            break;
        case ScenarioStepKind::InteractTarget:
            stepSuccess = runInteractTarget(context, step, result.failures);
            break;
        case ScenarioStepKind::Assert:
            stepSuccess = runAssert(context, step, result.failures);
            break;
        case ScenarioStepKind::SetupShortcut:
            stepSuccess = runSetupShortcut(context, step, result.failures);
            break;
        case ScenarioStepKind::SaveCheckpoint:
            stepSuccess = runSaveCheckpoint(context, step, result.failures);
            break;
        case ScenarioStepKind::LoadCheckpoint:
            stepSuccess = runLoadCheckpoint(context, step, result.failures);
            break;
        case ScenarioStepKind::SaveGame:
            stepSuccess = runSaveGame(context, step, result.failures);
            break;
        case ScenarioStepKind::LoadGame:
            stepSuccess = runLoadGame(context, step, result.failures);
            break;
        }

        if (!stepSuccess)
        {
            if (result.failures.size() == failureCountBeforeStep)
            {
                result.failures.push_back(stepLocation(step) + ": step failed without a specific diagnostic");
            }

            scenarioLog(context, ScenarioLogColor::Red, "FAIL " + stepSummary(context, step));
            break;
        }

        scenarioLog(context, ScenarioLogColor::Green, "PASS step " + std::to_string(step.stepIndex));
    }

    result.success = result.failures.empty();
    return result;
}
}
