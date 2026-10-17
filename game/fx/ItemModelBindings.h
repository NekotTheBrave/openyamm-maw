#pragma once

#include <string>
#include <unordered_map>

namespace OpenYAMM::Engine
{
class AssetFileSystem;
}

namespace OpenYAMM::Game
{
// The 3D model an item lying on the ground draws in place of its sprite (GROUND_ITEMS_3D_PLAN.md).
struct ItemModelBinding
{
    // Engine model path. The model lies flat, its longest side along the model's horizontal axes, its underside at 0
    // and its origin at the centre of its footprint.
    std::string model;
    // World units of the model's longest horizontal side; the model is scaled uniformly to it.
    float length = 0.0f;
};

// Item icon (items.txt "Pic File", lower case) -> binding: items sharing an icon share a model.
using ItemModelBindings = std::unordered_map<std::string, ItemModelBinding>;

// Reads engine/models/items.yml (schema openyamm.itemModels.v1): an items list of {icon, model, length}. A missing file
// binds nothing; an unknown key, a repeated icon or a bad length fails.
bool loadItemModelBindings(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &path,
    ItemModelBindings &bindings,
    std::string &error);
// The same from YAML text; path names the source in errors.
bool parseItemModelBindings(const std::string &text, const std::string &path, ItemModelBindings &bindings,
    std::string &error);
}
