#include "game/fx/ItemModelBindings.h"

#include "engine/AssetFileSystem.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace OpenYAMM::Game
{
bool loadItemModelBindings(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &path,
    ItemModelBindings &bindings,
    std::string &error)
{
    if (!assetFileSystem.exists(path))
    {
        return true;
    }
    const std::optional<std::string> text = assetFileSystem.readTextFile(path);
    if (!text)
    {
        error = path + ": unreadable";
        return false;
    }
    return parseItemModelBindings(*text, path, bindings, error);
}

bool parseItemModelBindings(const std::string &text, const std::string &path, ItemModelBindings &bindings,
    std::string &error)
{
    try
    {
        const YAML::Node root = YAML::Load(text);
        if (!root["schema"] || root["schema"].as<std::string>() != "openyamm.itemModels.v1" || !root["items"]
            || !root["items"].IsSequence())
        {
            error = path + ": expected schema openyamm.itemModels.v1 with an items list";
            return false;
        }
        for (const YAML::Node &entry : root["items"])
        {
            if (!entry.IsMap() || !entry["icon"] || !entry["model"] || !entry["length"])
            {
                error = path + ": each item needs icon, model and length";
                return false;
            }
            std::string icon = entry["icon"].as<std::string>();
            std::transform(icon.begin(), icon.end(), icon.begin(),
                [](unsigned char character)
                {
                    return char(std::tolower(character));
                });
            const std::string context = path + ": icon " + icon;
            for (const auto &field : entry)
            {
                const std::string key = field.first.as<std::string>();
                if (key != "icon" && key != "model" && key != "length")
                {
                    error = context + ": unknown key " + key;
                    return false;
                }
            }
            ItemModelBinding binding;
            binding.model = entry["model"].as<std::string>();
            binding.length = entry["length"].as<float>();
            if (icon.empty() || !std::isfinite(binding.length) || binding.length <= 0.0f)
            {
                error = context + ": needs an icon and a positive length";
                return false;
            }
            if (!bindings.emplace(icon, std::move(binding)).second)
            {
                error = context + ": bound twice";
                return false;
            }
        }
    }
    catch (const YAML::Exception &exception)
    {
        error = path + ": " + exception.what();
        return false;
    }
    return true;
}
}
