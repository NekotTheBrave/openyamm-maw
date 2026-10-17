#pragma once

#include "engine/models/ModelAsset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace OpenYAMM::Engine
{
struct ModelPose
{
    std::vector<ModelTransform> localTransforms;
    std::vector<ModelMatrix> localMatrices;
    std::vector<ModelMatrix> globalMatrices;
    std::vector<std::vector<float>> morphWeights;
    // Node/primitive vertices: world space for skins, mesh-local space for unskinned morphs.
    std::vector<std::vector<std::vector<ModelVertex>>> deformedVertices;
    uint64_t deformationRevision = 0;
    uint64_t matrixRevision = 0;
};

// Mesh-local bounds per influencing joint (one entry for an unskinned mesh).
// Keeping morph deltas separate permits arbitrary animated weights, including negative weights.
struct ModelJointBounds
{
    ModelBounds base;
    std::vector<ModelBounds> morphs;
};
struct ModelDeformationBounds
{
    std::vector<std::vector<ModelJointBounds>> nodes;
    float motionRadius = -1;
};

ModelDeformationBounds buildModelDeformationBounds(const ModelAsset &asset);
ModelBounds modelPoseBounds(const ModelAsset &asset, const ModelPose &pose, const ModelDeformationBounds &bounds);
// coarsestLod: bound the lowest LOD mesh instead (hover/picking: same silhouette within a few cm, far fewer
// vertices to skin).
ModelBounds modelExactPoseBounds(const ModelAsset &asset, const ModelPose &pose, bool coarsestLod = false);

// The posed triangles behind modelExactPoseBounds: positions, a triangle list indexing them, and their bounds.
struct ModelPoseGeometry
{
    std::vector<std::array<float, 3>> positions;
    std::vector<uint32_t> indices;
    ModelBounds bounds;
};

void modelExactPoseGeometry(const ModelAsset &asset, const ModelPose &pose, bool coarsestLod,
    ModelPoseGeometry &geometry);
// Distance along direction to the nearest triangle the ray meets from either side, or none.
std::optional<float> intersectModelPoseGeometry(const ModelPoseGeometry &geometry,
    const std::array<float, 3> &origin, const std::array<float, 3> &direction);

void resetModelPose(const ModelAsset &asset, ModelPose &pose);
void evaluateModelClip(const ModelAsset &asset, uint32_t clipIndex, float timeSeconds, ModelPose &pose);
// evaluateModelClip without the local matrices: only the local transforms and morph weights, for poses that are blended
// (blendModelPose without compose) before composeModelPose.
void sampleModelClip(const ModelAsset &asset, uint32_t clipIndex, float timeSeconds, ModelPose &pose);
// The local matrix of every node animated by translation, rotation and scale, from its local transform.
void composeModelPose(const ModelAsset &asset, ModelPose &pose);
// Blend local TRS and morphs before evaluating the hierarchy. An empty mask covers every node. compose: also update
// the blended nodes' local matrices.
void blendModelPose(const ModelAsset &asset, ModelPose &pose, const ModelPose &other, float amount,
    const std::vector<float> &mask = {}, bool compose = true);
void evaluateModelHierarchy(const ModelAsset &asset, const ModelMatrix &rootMatrix, ModelPose &pose);
void deformModelPose(const ModelAsset &asset, ModelPose &pose, bool deformSkins = true);
// One vertex of a skinned node's primitive in the pose's space (world space for an instance's pose), for effects that
// follow a deforming surface. Morph targets are ignored.
std::array<float, 3> skinnedModelVertex(const ModelAsset &asset, const ModelPose &pose, size_t nodeIndex,
    const ModelPrimitive &primitive, size_t vertexIndex);
// A static copy of `asset` frozen in `pose` (hierarchy evaluated with an identity root): every mesh node's mesh and its
// colour and shadow LOD chains are skinned (or moved by the node's matrix) and morphed into asset-root space, on
// unparented identity nodes without skins, clips or morph targets. Materials, images and variants are copied, so
// renderers share the source's textures. Used for corpses (ModelInstanceSystem::setStaticStandIn).
std::shared_ptr<ModelAsset> bakeStaticModelPose(const ModelAsset &asset, const ModelPose &pose);
}
