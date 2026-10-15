#include "game/fx/import/EffectResourceLibrary.h"

#include "engine/AssetFileSystem.h"
#include "engine/models/GltfModelLoader.h"

#include <yaml-cpp/yaml.h>

#include <functional>
#include <unordered_set>

namespace OpenYAMM::Game
{
namespace
{
bool readScalar(const YAML::Node &node, const char *pName, std::string &value, std::string &error)
{
    const YAML::Node valueNode = node[pName];
    if (!valueNode || !valueNode.IsScalar())
    {
        error = std::string("missing scalar field '") + pName + "'";
        return false;
    }
    value = valueNode.as<std::string>();
    return true;
}

bool loadSprite(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &resourceId,
    const std::unordered_map<std::string, std::string> &assetPaths,
    EffectSpriteResource &sprite,
    std::string &error)
{
    const auto descriptorIterator = assetPaths.find(resourceId);
    if (descriptorIterator == assetPaths.end())
    {
        error = "effect sprite has no bound descriptor: " + resourceId;
        return false;
    }
    const std::optional<std::string> text = assetFileSystem.readTextFile(descriptorIterator->second);
    if (!text)
    {
        error = "effect sprite descriptor was not found: " + descriptorIterator->second;
        return false;
    }
    const YAML::Node root = YAML::Load(*text);
    std::string schema;
    if (!readScalar(root, "schema", schema, error) || schema != "openyamm.effectSprite.v2" ||
        !readScalar(root, "id", sprite.id, error) || sprite.id != resourceId)
    {
        if (error.empty())
        {
            error = "invalid effect sprite descriptor for " + resourceId;
        }
        return false;
    }
    const YAML::Node framesPerSecondNode = root["framesPerSecond"];
    const YAML::Node logicalWidthNode = root["logicalWidth"];
    const YAML::Node logicalHeightNode = root["logicalHeight"];
    const YAML::Node framesNode = root["frames"];
    const YAML::Node atlasNode = root["atlas"];
    if (!framesPerSecondNode || !framesPerSecondNode.IsScalar() ||
        !logicalWidthNode || !logicalWidthNode.IsScalar() ||
        !logicalHeightNode || !logicalHeightNode.IsScalar() ||
        (atlasNode ? !atlasNode.IsScalar() || framesNode : !framesNode || !framesNode.IsSequence()))
    {
        error = "effect sprite has invalid frame metadata: " + resourceId;
        return false;
    }
    sprite.framesPerSecond = framesPerSecondNode.as<uint32_t>();
    sprite.logicalWidth = logicalWidthNode.as<uint32_t>();
    sprite.logicalHeight = logicalHeightNode.as<uint32_t>();
    if (sprite.framesPerSecond == 0 || sprite.logicalWidth == 0 || sprite.logicalHeight == 0)
    {
        error = "effect sprite has no playable frames: " + resourceId;
        return false;
    }
    if (atlasNode)
    {
        const auto readCount = [&root](const char *pName)
        {
            const YAML::Node node = root[pName];
            return node && node.IsScalar() ? node.as<uint32_t>() : 0u;
        };
        sprite.atlasColumns = readCount("atlasColumns");
        sprite.frameCount = readCount("frameCount");
        sprite.frameWidth = readCount("frameWidth");
        sprite.frameHeight = readCount("frameHeight");
        const auto atlasIterator = assetPaths.find(atlasNode.as<std::string>());
        if (sprite.atlasColumns == 0 || sprite.frameCount == 0 || sprite.frameWidth == 0 || sprite.frameHeight == 0)
        {
            error = "cooked effect sprite needs atlasColumns, frameCount, frameWidth and frameHeight: " + resourceId;
            return false;
        }
        if (atlasIterator == assetPaths.end() || !assetFileSystem.exists(atlasIterator->second))
        {
            error = "cooked effect sprite atlas was not found (run tools/cook_effect_textures.py): " + resourceId;
            return false;
        }
        sprite.atlasPath = atlasIterator->second;
        return true;
    }
    if (framesNode.size() == 0)
    {
        error = "effect sprite has no playable frames: " + resourceId;
        return false;
    }
    for (const YAML::Node &frameNode : framesNode)
    {
        const std::string frameId = frameNode.as<std::string>();
        const auto frameIterator = assetPaths.find(frameId);
        if (frameIterator == assetPaths.end())
        {
            error = "effect sprite frame has no binding: " + frameId;
            return false;
        }
        if (!assetFileSystem.exists(frameIterator->second))
        {
            error = "effect sprite frame asset was not found: " + frameIterator->second;
            return false;
        }
        sprite.framePaths.push_back(frameIterator->second);
    }
    return true;
}
}

bool EffectResourceLibrary::load(
    const Engine::AssetFileSystem &assetFileSystem,
    const std::string &bindingManifestPath,
    const std::vector<std::shared_ptr<const EffectDefinition>> &definitions,
    std::string &error,
    Engine::ModelAssetCache *pModelAssets)
{
    try
    {
        std::unordered_map<std::string, std::string> assetPaths;
        std::unordered_set<std::string> visited;
        const std::function<bool(const std::string &)> readBindings = [&](const std::string &path)
        {
            if (!visited.insert(path).second)
            {
                error = "repeated effect resource import: " + path;
                return false;
            }
            const std::optional<std::string> text = assetFileSystem.readTextFile(path);
            if (!text)
            {
                error = "effect resource binding manifest was not found: " + path;
                return false;
            }
            const YAML::Node root = YAML::Load(*text);
            std::string schema;
            if (!readScalar(root, "schema", schema, error) || schema != "openyamm.effectResourceBindings.v1")
            {
                if (error.empty())
                {
                    error = "unsupported effect resource binding schema: " + schema;
                }
                return false;
            }
            const YAML::Node bindingsNode = root["bindings"];
            if (!bindingsNode || !bindingsNode.IsSequence())
            {
                error = "effect resource bindings must be a sequence";
                return false;
            }

            for (const YAML::Node &bindingNode : bindingsNode)
            {
                std::string resource;
                std::string asset;
                std::string status;
                if (!readScalar(bindingNode, "resource", resource, error) ||
                    !readScalar(bindingNode, "asset", asset, error) ||
                    !readScalar(bindingNode, "status", status, error))
                {
                    return false;
                }
                if (status == "bound" && !assetPaths.emplace(resource, asset).second)
                {
                    error = "duplicate effect resource binding: " + resource;
                    return false;
                }
            }
            const YAML::Node imports = root["imports"];
            if (imports)
            {
                if (!imports.IsSequence())
                {
                    error = "effect resource imports must be a sequence: " + path;
                    return false;
                }
                for (const YAML::Node &entry : imports)
                {
                    if (!entry.IsScalar() || !readBindings(entry.as<std::string>()))
                    {
                        if (error.empty())
                        {
                            error = "effect resource import must be an asset path: " + path;
                        }
                        return false;
                    }
                }
            }
            return true;
        };
        if (!readBindings(bindingManifestPath))
        {
            return false;
        }

        std::unordered_map<std::string, EffectSpriteResource> sprites;
        std::unordered_set<std::string> requiredSprites;
        std::unordered_set<std::string> requiredSounds;
        std::unordered_set<std::string> requiredModels;
        for (const std::shared_ptr<const EffectDefinition> &definition : definitions)
        {
            for (const EffectSpriteEmitterDefinition &emitter : definition->spriteEmitters)
            {
                requiredSprites.insert(emitter.spriteResource);
            }
            for (const EffectSpriteDefinition &sprite : definition->sprites)
            {
                requiredSprites.insert(sprite.spriteResource);
            }
            for (const EffectRibbonDefinition &ribbon : definition->ribbons)
            {
                requiredSprites.insert(ribbon.spriteResource);
            }
            for (const EffectModelDefinition &model : definition->models)
            {
                requiredModels.insert(model.modelResource);
            }
            for (const EffectSoundDefinition &sound : definition->sounds)
            {
                requiredSounds.insert(sound.soundResource);
            }
        }
        for (const std::string &spriteId : requiredSprites)
        {
            EffectSpriteResource sprite;
            if (!loadSprite(assetFileSystem, spriteId, assetPaths, sprite, error))
            {
                return false;
            }
            sprites.emplace(spriteId, std::move(sprite));
        }
        // Ribbons tile their sprite's first source image; cooked atlases are for sprites and particles only.
        for (const std::shared_ptr<const EffectDefinition> &definition : definitions)
        {
            for (const EffectRibbonDefinition &ribbon : definition->ribbons)
            {
                if (sprites.at(ribbon.spriteResource).framePaths.empty())
                {
                    error = "effect ribbon needs an uncooked sprite: " + ribbon.spriteResource;
                    return false;
                }
            }
        }
        for (const std::string &soundId : requiredSounds)
        {
            const auto soundIterator = assetPaths.find(soundId);
            if (soundIterator == assetPaths.end() || !assetFileSystem.exists(soundIterator->second))
            {
                error = "effect sound asset was not found: " + soundId;
                return false;
            }
        }
        std::unordered_map<std::string, std::shared_ptr<const Engine::ModelAsset>> models;
        for (const std::string &modelId : requiredModels)
        {
            const auto modelIterator = assetPaths.find(modelId);
            if (modelIterator == assetPaths.end())
            {
                error = "effect model has no binding: " + modelId;
                return false;
            }
            if (pModelAssets == nullptr)
            {
                error = "effect model runtime is unavailable for " + modelId;
                return false;
            }
            const Engine::ModelLoadResult loaded = pModelAssets->load(assetFileSystem, modelIterator->second);
            if (!loaded)
            {
                error = "effect model load failed for " + modelId + ": " + loaded.error;
                return false;
            }
            models.emplace(modelId, loaded.asset);
        }
        for (const std::shared_ptr<const EffectDefinition> &definition : definitions)
        {
            std::unordered_map<uint32_t, const EffectModelDefinition *> modelsByComponent;
            for (const EffectModelDefinition &model : definition->models)
            {
                modelsByComponent.emplace(model.componentId, &model);
            }
            const auto validateLink = [&](const EffectComponentLink &link) -> bool
            {
                if (!link.componentId)
                {
                    return true;
                }
                const auto componentIterator = modelsByComponent.find(*link.componentId);
                if (componentIterator == modelsByComponent.end())
                {
                    error = "effect '" + definition->id + "' links to non-model component " +
                        std::to_string(*link.componentId);
                    return false;
                }
                const auto modelIterator = models.find(componentIterator->second->modelResource);
                if (modelIterator == models.end() || !modelIterator->second->findNode(link.nodeName))
                {
                    error = "effect '" + definition->id + "' model component " +
                        std::to_string(*link.componentId) + " has no node '" + link.nodeName + "'";
                    return false;
                }
                return true;
            };
            for (const EffectSpriteDefinition &sprite : definition->sprites)
            {
                if (!validateLink(sprite.link))
                {
                    return false;
                }
            }
            for (const EffectSpriteEmitterDefinition &emitter : definition->spriteEmitters)
            {
                if (!validateLink(emitter.link))
                {
                    return false;
                }
            }
            for (const EffectModelDefinition &model : definition->models)
            {
                if (!validateLink(model.link))
                {
                    return false;
                }
            }
        }
        m_assetPaths = std::move(assetPaths);
        m_sprites = std::move(sprites);
        m_models = std::move(models);
        return true;
    }
    catch (const YAML::Exception &exception)
    {
        error = std::string("invalid effect resource YAML: ") + exception.what();
        return false;
    }
}

void EffectResourceLibrary::clear()
{
    m_assetPaths.clear();
    m_sprites.clear();
    m_models.clear();
}

std::shared_ptr<const Engine::ModelAsset> EffectResourceLibrary::findModel(const std::string &id) const
{
    const auto iterator = m_models.find(id);
    return iterator != m_models.end() ? iterator->second : nullptr;
}

const EffectSpriteResource *EffectResourceLibrary::findSprite(const std::string &id) const
{
    const auto iterator = m_sprites.find(id);
    return iterator != m_sprites.end() ? &iterator->second : nullptr;
}

std::optional<std::string> EffectResourceLibrary::findAssetPath(const std::string &id) const
{
    const auto iterator = m_assetPaths.find(id);
    return iterator != m_assetPaths.end() ? std::optional<std::string>(iterator->second) : std::nullopt;
}

size_t EffectResourceLibrary::spriteCount() const
{
    return m_sprites.size();
}
}
