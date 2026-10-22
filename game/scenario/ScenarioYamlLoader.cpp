#include "game/scenario/ScenarioYamlLoader.h"

#include <sstream>

namespace OpenYAMM::Game
{
std::string ScenarioYamlLoader::formatLocation(const std::filesystem::path &path, int line)
{
    std::ostringstream stream;
    stream << path.string();

    if (line > 0)
    {
        stream << ':' << line;
    }

    return stream.str();
}

bool ScenarioYamlLoader::parseMode(const std::string &value, ScenarioMode &mode)
{
    if (value == "faithful")
    {
        mode = ScenarioMode::Faithful;
        return true;
    }

    if (value == "hybrid")
    {
        mode = ScenarioMode::Hybrid;
        return true;
    }

    if (value == "unitized")
    {
        mode = ScenarioMode::Unitized;
        return true;
    }

    return false;
}

bool ScenarioYamlLoader::parseStepKind(const std::string &value, ScenarioStepKind &kind)
{
    if (value == "new_game_flow")
    {
        kind = ScenarioStepKind::NewGameFlow;
        return true;
    }

    if (value == "load_map")
    {
        kind = ScenarioStepKind::LoadMap;
        return true;
    }

    if (value == "travel")
    {
        kind = ScenarioStepKind::Travel;
        return true;
    }

    if (value == "set_pose")
    {
        kind = ScenarioStepKind::SetPose;
        return true;
    }

    if (value == "press_action")
    {
        kind = ScenarioStepKind::PressAction;
        return true;
    }

    if (value == "press_interact")
    {
        kind = ScenarioStepKind::PressInteract;
        return true;
    }

    if (value == "speak_npc")
    {
        kind = ScenarioStepKind::SpeakNpc;
        return true;
    }

    if (value == "select_topic")
    {
        kind = ScenarioStepKind::SelectTopic;
        return true;
    }

    if (value == "answer_input")
    {
        kind = ScenarioStepKind::AnswerInput;
        return true;
    }

    if (value == "interact_target")
    {
        kind = ScenarioStepKind::InteractTarget;
        return true;
    }

    if (value == "interact_actor")
    {
        kind = ScenarioStepKind::InteractActor;
        return true;
    }

    if (value == "movement_segment")
    {
        kind = ScenarioStepKind::MovementSegment;
        return true;
    }

    if (value == "enter_door_at")
    {
        kind = ScenarioStepKind::EnterDoorAt;
        return true;
    }

    if (value == "advance_runtime")
    {
        kind = ScenarioStepKind::AdvanceRuntime;
        return true;
    }

    if (value == "advance_game_time")
    {
        kind = ScenarioStepKind::AdvanceGameTime;
        return true;
    }

    if (value == "wait_until")
    {
        kind = ScenarioStepKind::WaitUntil;
        return true;
    }

    if (value == "assert")
    {
        kind = ScenarioStepKind::Assert;
        return true;
    }

    if (value == "assert_mechanism")
    {
        kind = ScenarioStepKind::AssertMechanism;
        return true;
    }

    if (value == "combat_loot")
    {
        kind = ScenarioStepKind::CombatLoot;
        return true;
    }

    if (value == "setup_shortcut")
    {
        kind = ScenarioStepKind::SetupShortcut;
        return true;
    }

    if (value == "save_checkpoint")
    {
        kind = ScenarioStepKind::SaveCheckpoint;
        return true;
    }

    if (value == "load_checkpoint")
    {
        kind = ScenarioStepKind::LoadCheckpoint;
        return true;
    }

    if (value == "save_game")
    {
        kind = ScenarioStepKind::SaveGame;
        return true;
    }

    if (value == "load_game")
    {
        kind = ScenarioStepKind::LoadGame;
        return true;
    }

    if (value == "damage_actor")
    {
        kind = ScenarioStepKind::DamageActor;
        return true;
    }

    if (value == "note")
    {
        kind = ScenarioStepKind::Note;
        return true;
    }

    if (value == "todo")
    {
        kind = ScenarioStepKind::Todo;
        return true;
    }

    if (value == "trace_observation")
    {
        kind = ScenarioStepKind::TraceObservation;
        return true;
    }

    return false;
}

ScenarioLoadResult ScenarioYamlLoader::load(const std::filesystem::path &path) const
{
    ScenarioLoadResult result = {};
    YAML::Node root;

    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (const YAML::Exception &exception)
    {
        result.errors.push_back("failed to load scenario YAML from " + path.string() + ": " + exception.what());
        return result;
    }

    if (!root.IsMap())
    {
        result.errors.push_back(formatLocation(path, 1) + ": scenario root must be a mapping");
        return result;
    }

    if (!root["id"] || !root["id"].IsScalar())
    {
        result.errors.push_back(formatLocation(path, 1) + ": missing scalar field 'id'");
    }
    else
    {
        result.document.id = root["id"].as<std::string>();
    }

    if (root["title"])
    {
        if (!root["title"].IsScalar())
        {
            result.errors.push_back(formatLocation(path, root["title"].Mark().line + 1)
                + ": field 'title' must be scalar");
        }
        else
        {
            result.document.title = root["title"].as<std::string>();
        }
    }

    if (!root["world"] || !root["world"].IsScalar())
    {
        result.errors.push_back(formatLocation(path, 1) + ": missing scalar field 'world'");
    }
    else
    {
        result.document.world = root["world"].as<std::string>();
    }

    if (root["mode"])
    {
        if (!root["mode"].IsScalar())
        {
            result.errors.push_back(formatLocation(path, root["mode"].Mark().line + 1)
                + ": field 'mode' must be scalar");
        }
        else if (!parseMode(root["mode"].as<std::string>(), result.document.mode))
        {
            result.errors.push_back(formatLocation(path, root["mode"].Mark().line + 1)
                + ": unknown scenario mode '" + root["mode"].as<std::string>() + "'");
        }
    }

    if (root["tags"])
    {
        if (!root["tags"].IsSequence())
        {
            result.errors.push_back(formatLocation(path, root["tags"].Mark().line + 1)
                + ": field 'tags' must be a sequence");
        }
        else
        {
            for (const YAML::Node &tagNode : root["tags"])
            {
                if (!tagNode.IsScalar())
                {
                    result.errors.push_back(formatLocation(path, tagNode.Mark().line + 1)
                        + ": tag entries must be scalar");
                    continue;
                }

                result.document.tags.push_back(tagNode.as<std::string>());
            }
        }
    }

    if (!root["steps"] || !root["steps"].IsSequence())
    {
        result.errors.push_back(formatLocation(path, 1) + ": missing sequence field 'steps'");
        return result;
    }

    const YAML::Node stepsNode = root["steps"];

    for (size_t stepIndex = 0; stepIndex < stepsNode.size(); ++stepIndex)
    {
        const YAML::Node stepNode = stepsNode[stepIndex];

        if (!stepNode.IsMap())
        {
            result.errors.push_back(formatLocation(path, stepNode.Mark().line + 1)
                + ": scenario step must be a mapping");
            continue;
        }

        std::string stepKindName;
        YAML::Node payload;
        int traceSourceLine = 0;

        for (YAML::const_iterator iterator = stepNode.begin(); iterator != stepNode.end(); ++iterator)
        {
            if (!iterator->first.IsScalar())
            {
                result.errors.push_back(formatLocation(path, iterator->first.Mark().line + 1)
                    + ": scenario step keys must be scalar");
                continue;
            }

            const std::string key = iterator->first.as<std::string>();

            if (key == "source_line")
            {
                if (iterator->second.IsScalar())
                {
                    traceSourceLine = iterator->second.as<int>();
                }
                continue;
            }

            if (key == "todo" && iterator->second.IsScalar())
            {
                continue;
            }

            if (!stepKindName.empty())
            {
                result.errors.push_back(formatLocation(path, iterator->first.Mark().line + 1)
                    + ": scenario step must contain one command key");
                continue;
            }

            stepKindName = key;
            payload = iterator->second;
        }

        if (stepKindName.empty())
        {
            result.errors.push_back(formatLocation(path, stepNode.Mark().line + 1)
                + ": scenario step is missing a command key");
            continue;
        }

        ScenarioStepKind kind = ScenarioStepKind::Assert;

        if (!parseStepKind(stepKindName, kind))
        {
            result.errors.push_back(formatLocation(path, stepNode.Mark().line + 1)
                + ": unknown scenario step '" + stepKindName + "'");
            continue;
        }

        ScenarioStep step = {};
        step.kind = kind;
        step.kindName = stepKindName;
        step.payload = payload;
        step.sourcePath = path;
        step.sourceLine = stepNode.Mark().line + 1;
        step.traceSourceLine = traceSourceLine;
        step.stepIndex = stepIndex + 1;
        result.document.steps.push_back(step);
    }

    return result;
}
}
