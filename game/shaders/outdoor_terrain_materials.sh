// The outdoor terrain: blended ground materials on the smoothed terrain mesh (OutdoorTerrainMaterials).
// Weights cover the 128x128 cell map; the two strongest layers blend by height, each sampled twice with a rotated
// second projection to hide its repeat. The liquid renderer draws the liquid cells; liquid edge cells keep the
// ground only where their native coverage mask leaves some.
#include "common.sh"
#include "world_clip.sh"

// The seabed's caustics: layer 0 the main Voronoi web, layer 1 a finer faint one (tools/terrain_materials.py
// caustics).
SAMPLER2DARRAY(s_terrainCaustics, 0);
// The liquid edge coverage masks (WaterRenderer), one layer per raw tile id: 1 where the liquid shows.
SAMPLER2DARRAY(s_texWaterCoverage, 4);
SAMPLER2DARRAY(s_terrainAlbedoHeight, 8);
SAMPLER2DARRAY(s_terrainNormalAoRough, 9);
SAMPLER2D(s_terrainWeights0, 10);
SAMPLER2D(s_terrainWeights1, 11);
SAMPLER2D(s_terrainCells, 12);
// Native triangle normals at the terrain lightmap texels, filtered like the bake.
SAMPLER2D(s_terrainBakedNormals, 13);
// Tileable cloudy noise over 16384 world units: R ~4000-unit, G ~1000-unit, B ~2500-unit (tint), A ~250-unit.
SAMPLER2D(s_terrainMacro, 14);
// Signed distance to the waterline: 128 + distance / range * 127, positive on land.
SAMPLER2D(s_terrainShore, 15);
// The grass decorations' far colour (buildTerrainFoliageMap): rgb = average tuft colour premultiplied by a,
// a = optical depth of the grass cards seen edge on.
SAMPLER2D(s_terrainFoliage, 1);

// Per layer: x = 1 / repeat in world units, y = height blend contrast, z = normal strength, w = cavity: how much
// the material height darkens its albedo (photoscans are delit; the native tiles carry their shading).
uniform vec4 u_terrainLayerParams[8];
// rgb = tint, w = strength of the meadow tint (u_terrainMeadow) on this layer.
uniform vec4 u_terrainLayerTint[8];
// Per ground layer: the slope layer's tint where that layer dominates (cliffs take the colour of their ground).
uniform vec4 u_terrainLayerCliff[8];
uniform vec4 u_terrainLayerMacro[8];
// Per layer: x = layer of its variant material (mixed in patches for mid-scale variety), y = variant coverage,
// z = scale of the edge fringe noise where it meets another layer, w = 1 when its smoothest texels are standing
// water (swamp pools).
uniform vec4 u_terrainLayerVariant[8];
// x = weighted layer count, y = slope layer (negative: none), z/w = slope start/end as 1 - normal.z.
uniform vec4 u_terrainMaterialControl;
// xyz = direction toward the baked sun, w = 1 when the bake's direct-only sun page is bound at stage 5.
uniform vec4 u_terrainMaterialLight;
// x = fringe strength, y = 1 / world size of the fringe noise repeat, z = 1 for second projections (anti-tiling,
// blended cliff sides).
uniform vec4 u_terrainMaterialMisc;
// x = waterline distance range, y = 1 when shores have a seabed under depth-faded water, z = waterline distance
// under the water past which the seabed is plain (one sample of layer w, no detail normals or relighting).
uniform vec4 u_terrainShore;
// x = foliage tint strength (0: no grass decorations), y = grass full density distance, z = grass far distance,
// w = grass largest LOD scale (TerrainDecorationRenderer::foliageParams).
uniform vec4 u_terrainFoliage;
// Colour multipliers of the driest ([0].rgb) and lushest ([1].rgb) ground (OutdoorMeadowField, v_texcoord0.y).
uniform vec4 u_terrainMeadow[2];

#if BAKED_SOURCES
SAMPLER2D(s_texLightmap, 2);
uniform vec4 u_bakedTerrainBounds;
#include "outdoor_baked_lighting.sh"
#if !SUN_SHADOWS
SAMPLER2D(s_texBakedSunDirect, 5);
#endif
#elif SUN_SHADOWS
#include "sun_shadows.sh"
#endif

#include "outdoor_sunlight.sh"

uniform vec4 u_cameraPosition;
uniform vec4 u_secretPulseParams;

#include "outdoor_lighting.sh"
#include "sky_common.sh"
#include "weather_wetness.sh"

// Macro variation (UE-style): per layer, rgb = tint of the cloudy patches, a = broad brightness variation. Then the
// meadow: dry crests and slopes, lush hollows and shores (meadow -1 lush .. +1 dry), scaled by the layer's strength.
vec3 macroVaried(vec3 albedo, float layer, vec4 macroSample, float meadow)
{
    vec4 macro = u_terrainLayerMacro[int(layer + 0.5)];
    float broad = macroSample.r * 0.6 + macroSample.g * 0.4;
    albedo *= 1.0 + (broad - 0.5) * 2.0 * macro.a;
    albedo *= mix(vec3_splat(1.0), macro.rgb, smoothstep(0.35, 0.75, macroSample.b));
    vec3 meadowTint = meadow > 0.0 ? u_terrainMeadow[0].rgb : u_terrainMeadow[1].rgb;
    return albedo * mix(vec3_splat(1.0), meadowTint, min(abs(meadow) * u_terrainLayerTint[int(layer + 0.5)].w, 1.0));
}

// Whiteout blends of a tangent-space detail normal on the vertical planes facing X (right = +Y, up = +Z) and
// facing Y (right = +X, up = +Z).
vec3 facingXNormal(vec3 tangent, vec3 geometric)
{
    vec3 blended = vec3(tangent.xy + geometric.yz, abs(geometric.x) * tangent.z);
    return vec3(sign(geometric.x) * blended.z, blended.x, blended.y);
}

vec3 facingYNormal(vec3 tangent, vec3 geometric)
{
    vec3 blended = vec3(tangent.xy + geometric.xz, abs(geometric.y) * tangent.z);
    return vec3(blended.x, sign(geometric.y) * blended.z, blended.y);
}

vec3 detailTangent(vec4 detail, float strength)
{
    vec2 slopes = detail.xy * 2.0 - 1.0;
    return vec3(slopes * strength, sqrt(max(1.0 - dot(slopes, slopes), 0.0)));
}

// Wet sand along the waterline and caustics in the shallows; shore is the signed waterline distance.
vec3 shoreAlbedo(vec3 albedo, vec3 position, float shore)
{
    // Damp sand along the waterline, darker and cooler under the water.
    albedo *= mix(vec3_splat(1.0), vec3(0.72, 0.74, 0.78), 1.0 - smoothstep(0.0, 60.0, shore));
    // Caustics: a web of light whose lines bend and drift with the waves, over a faint finer web drifting the
    // other way. Only in the shallows: everywhere else (all the land) skips the two samples; the branch edges sit
    // where the caustics have faded to nothing, so their mip selection cannot show.
    float shallows = clamp(-shore / 30.0, 0.0, 1.0) * (1.0 - clamp((-shore - 60.0) / 260.0, 0.0, 1.0));
    if (shallows <= 0.0)
    {
        return albedo;
    }
    float time = u_secretPulseParams.y;
    vec2 world = position.xy;
    vec2 wobble = vec2(sin(world.y / 23.0 + time * 1.3) + 0.6 * sin(world.y / 41.0 - time * 0.8),
        cos(world.x / 27.0 + time * 1.1) + 0.6 * cos(world.x / 37.0 - time * 0.9)) * 2.2;
    vec3 web = texture2DArray(s_terrainCaustics,
        vec3((world + wobble) / 380.0 + vec2(time * 0.006, time * 0.004), 0.0)).rgb;
    vec3 fine = texture2DArray(s_terrainCaustics,
        vec3((world - wobble * 0.6) / 290.0 - vec2(time * 0.008, time * 0.005), 1.0)).rgb;
    return albedo * (1.0 + max(web, fine * 0.45) * 1.5 * shallows);
}

// One layer: albedo with height, world-space shading normal, and ambient occlusion with roughness. Ground layers
// project from above; the slope layer projects onto the vertical planes so cliffs are not stretched. With
// u_terrainMaterialMisc.z, a second projection (rotated from above, or the other vertical plane) hides the repeat
// and blends cliff sides; detail scales its share (zero on the plain seabed). That branch is uniform, so
// anisotropic filtering keeps its implicit derivatives.
void sampleTerrainLayer(float layer, vec3 tint, vec3 world, vec3 geometric, float variation, float detail,
    out vec4 albedoHeight, out vec3 normal, out vec2 occlusionRoughness)
{
    vec4 params = u_terrainLayerParams[int(layer + 0.5)];
    vec4 variant = u_terrainLayerVariant[int(layer + 0.5)];
    bool slope = abs(layer - u_terrainMaterialControl.y) < 0.5;
    bool detailed = u_terrainMaterialMisc.z > 0.5;
    bool facingX = detailed || abs(geometric.x) >= abs(geometric.y);
    // U follows world X and V world -Y, so an OpenGL normal map's red and green are world X and Y.
    vec2 planar = vec2(world.x, -world.y) * params.x;
    vec2 side = facingX ? vec2(world.y, -world.z) : vec2(world.x, -world.z);
    vec2 uvA = slope ? side * params.x : planar;
    vec4 colorA = texture2DArray(s_terrainAlbedoHeight, vec3(uvA, layer));
    vec4 detailA = texture2DArray(s_terrainNormalAoRough, vec3(uvA, layer));
    vec3 tangentA = detailTangent(detailA, params.z);
    vec3 normalA = slope
        ? (facingX ? facingXNormal(tangentA, geometric) : facingYNormal(tangentA, geometric))
        : vec3(tangentA.xy + geometric.xy, tangentA.z * geometric.z);
    colorA.rgb *= tint;
    albedoHeight = colorA;
    normal = normalize(normalA);
    occlusionRoughness = detailA.zw;
    if (detailed)
    {
        // Ground layers take the second sample from their variant material (or themselves), rotated.
        float layerB = slope ? layer : variant.x;
        vec4 paramsB = u_terrainLayerParams[int(layerB + 0.5)];
        vec2 planarB = vec2(world.x, -world.y) * paramsB.x;
        vec2 rotated = vec2(0.8 * planarB.x - 0.6 * planarB.y, 0.6 * planarB.x + 0.8 * planarB.y) * 0.71
            + vec2(0.37, 0.61);
        vec2 uvB = slope ? vec2(world.x, -world.z) * params.x : rotated;
        vec4 colorB = texture2DArray(s_terrainAlbedoHeight, vec3(uvB, layerB));
        vec4 detailB = texture2DArray(s_terrainNormalAoRough, vec3(uvB, layerB));
        colorB.rgb *= slope ? tint : u_terrainLayerTint[int(layerB + 0.5)].rgb;
        vec3 tangentB = detailTangent(detailB, paramsB.z);
        vec3 normalB;
        float share;
        if (slope)
        {
            normalB = facingYNormal(tangentB, geometric);
            share = smoothstep(-0.15, 0.15, abs(geometric.y) - abs(geometric.x) + (colorB.a - colorA.a) * 0.25);
        }
        else
        {
            // Back from the rotated texture frame (V points down the image) to world X/Y.
            vec2 tangent = vec2(tangentB.x, -tangentB.y);
            vec2 unrotated = vec2(0.8 * tangent.x + 0.6 * tangent.y, -0.6 * tangent.x + 0.8 * tangent.y);
            normalB = vec3(vec2(unrotated.x, -unrotated.y) + geometric.xy, tangentB.z * geometric.z);
            // The variant covers about variant.y of the ground in soft macro-noise patches; within the wide
            // transition the taller sample wins, so edges break up finely instead of averaging the two.
            share = smoothstep(-0.12, 0.12, (variation - 1.0 + variant.y) * 0.9 + (colorB.a - colorA.a) * 0.45);
        }
        share *= detail;
        albedoHeight = mix(colorA, colorB, share);
        normal = normalize(mix(normal, normalize(normalB), share));
        occlusionRoughness = mix(detailA.zw, detailB.zw, share);
    }
}

void main()
{
    clipWorldPosition(v_worldPosition);
    vec3 position = v_worldPosition;
    vec2 mapUv = vec2(position.x + 32768.0, 32768.0 - position.y) / 65536.0;
    vec2 cellCoord = mapUv * 128.0;
    vec2 cell = clamp(floor(cellCoord), vec2_splat(0.0), vec2_splat(126.0));
    vec2 local = clamp(cellCoord - cell, vec2_splat(0.0), vec2_splat(1.0));
    vec4 info = texture2DLod(s_terrainCells, (cell + 0.5) / 128.0, 0.0);
    if (info.a < 0.5)
    {
        discard;
    }
    float tileLayer = floor(info.r * 255.0 + 0.5);
    float waterKind = floor(info.g * 255.0 + 0.5);
    // info.b: 1 water, 0.5 an opaque liquid (tar, lava), 0 land; liquid cells are drawn by the liquid renderer.
    bool renderedWater = info.b > 0.25;
    // With a seabed, water cells draw the ground under the water renderer's see-through shallows.
    bool seabed = renderedWater && info.b > 0.75 && u_terrainShore.y > 0.5;
    float shore = u_terrainShore.y > 0.5
        ? (texture2D(s_terrainShore, mapUv).r * 255.0 - 128.0) / 127.0 * u_terrainShore.x : 0.0;
    // Deeper seabed, mostly hidden by the water, is plain: the full path fades into it, then a single sample.
    float plain = seabed && u_terrainShore.w >= 0.0
        ? smoothstep(u_terrainShore.z * 0.5, u_terrainShore.z, -shore) : 0.0;
    vec3 geometricNormal = normalize(v_worldNormal);

    vec3 albedo;
    vec3 shadingNormal = geometricNormal;
    float occlusion = 1.0;
    float pool = 0.0;
    // Open liquid without a seabed: the liquid renderer draws the whole cell.
    if (waterKind > 0.5 && waterKind < 1.5 && renderedWater && !seabed)
    {
        discard;
    }
    if (plain > 0.999)
    {
        float layer = u_terrainShore.w;
        vec4 params = u_terrainLayerParams[int(layer + 0.5)];
        vec4 color = texture2DArray(s_terrainAlbedoHeight, vec3(vec2(position.x, -position.y) * params.x, layer));
        vec3 plainAlbedo = color.rgb * u_terrainLayerTint[int(layer + 0.5)].rgb
            * mix(1.0, 0.45 + 1.1 * color.a, params.w);
        albedo = shoreAlbedo(macroVaried(plainAlbedo, layer, texture2D(s_terrainMacro, position.xy / 16384.0), 0.0),
            position, shore);
    }
    else
    {
        // Liquid edge tiles without a seabed: the liquid alone where the native overlay was fully transparent.
        // The coverage masks have no mip levels.
        if (waterKind > 1.5 && !seabed && texture2DArray(s_texWaterCoverage, vec3(local, tileLayer)).r >= 0.999)
        {
            discard;
        }
        vec4 weights0 = texture2D(s_terrainWeights0, mapUv);
        vec4 weights1 = texture2D(s_terrainWeights1, mapUv);
        float weights[8];
        weights[0] = weights0.x;
        weights[1] = weights0.y;
        weights[2] = weights0.z;
        weights[3] = weights0.w;
        weights[4] = weights1.x;
        weights[5] = weights1.y;
        weights[6] = weights1.z;
        weights[7] = weights1.w;
        float dominantWeight = -1.0;
        float dominantLayer = 0.0;
        for (int index = 0; index < 8; ++index)
        {
            if (weights[index] > dominantWeight)
            {
                dominantWeight = weights[index];
                dominantLayer = float(index);
            }
        }
        vec4 macroSample = texture2D(s_terrainMacro, position.xy / 16384.0);
        float variation = macroSample.g * 0.65 + macroSample.a * 0.35;
        // The meadow field is smooth; mid-scale noise frays the borders of its dry and lush patches.
        float meadow = clamp(v_texcoord0.y + (macroSample.g - 0.5) * 0.5 + (macroSample.a - 0.5) * 0.3, -1.0, 1.0);
        if (u_terrainMaterialControl.y >= 0.0)
        {
            // v_texcoord0.x: steepness of the broadly smoothed terrain; the macro noise roughens the edge. The
            // single-material test (OutdoorTerrainMaterials::createMesh) relies on the noise moving it by 0.1 at most.
            float steep = safeSmoothstep(u_terrainMaterialControl.z, u_terrainMaterialControl.w,
                v_texcoord0.x + (macroSample.a - 0.5) * 0.12 + (macroSample.g - 0.5) * 0.08);
            for (int index = 0; index < 8; ++index)
            {
                weights[index] = weights[index] * (1.0 - steep)
                    + (abs(float(index) - u_terrainMaterialControl.y) < 0.5 ? steep : 0.0);
            }
        }
        float firstWeight = -1.0;
        float secondWeight = -1.0;
        float firstLayer = 0.0;
        float secondLayer = 0.0;
        for (int index = 0; index < 8; ++index)
        {
            float weight = weights[index];
            if (weight > firstWeight)
            {
                secondWeight = firstWeight;
                secondLayer = firstLayer;
                firstWeight = weight;
                firstLayer = float(index);
            }
            else if (weight > secondWeight)
            {
                secondWeight = weight;
                secondLayer = float(index);
            }
        }
        // The two strongest layers take their slots in layer order, not by strength: where they are equally strong,
        // neighbouring pixels then still sample each layer in the same slot. A swap inside a 2x2 pixel quad tore the
        // texture coordinate derivatives (each layer has its own repeat), sampled the smallest mips and drew dotted
        // lines through the middle of every blend.
        if (secondLayer < firstLayer)
        {
            float layer = firstLayer;
            firstLayer = secondLayer;
            secondLayer = layer;
            float weight = firstWeight;
            firstWeight = secondWeight;
            secondWeight = weight;
        }
        vec4 firstColor;
        vec3 firstNormal;
        vec2 firstSurface;
        vec4 secondColor;
        vec3 secondNormal;
        vec2 secondSurface;
        vec3 cliffTint = u_terrainLayerCliff[int(dominantLayer + 0.5)].rgb;
        vec3 firstTint = abs(firstLayer - u_terrainMaterialControl.y) < 0.5
            ? cliffTint : u_terrainLayerTint[int(firstLayer + 0.5)].rgb;
        vec3 secondTint = abs(secondLayer - u_terrainMaterialControl.y) < 0.5
            ? cliffTint : u_terrainLayerTint[int(secondLayer + 0.5)].rgb;
#if SINGLE_MATERIAL
        // This draw holds the triangles whose pixels read one material only (OutdoorTerrainMaterials::createMesh):
        // the stronger slot alone is sampled, and the empty slot copies it with no weight.
        if (firstWeight < secondWeight)
        {
            firstLayer = secondLayer;
            firstWeight = secondWeight;
            firstTint = secondTint;
        }
        secondLayer = firstLayer;
        secondWeight = 0.0;
        sampleTerrainLayer(firstLayer, firstTint, position, geometricNormal, variation, 1.0 - plain, firstColor,
            firstNormal, firstSurface);
        secondColor = firstColor;
        secondNormal = firstNormal;
        secondSurface = firstSurface;
#else
        sampleTerrainLayer(firstLayer, firstTint, position, geometricNormal, variation, 1.0 - plain, firstColor,
            firstNormal, firstSurface);
        sampleTerrainLayer(secondLayer, secondTint, position, geometricNormal, variation, 1.0 - plain, secondColor,
            secondNormal, secondSurface);
#endif
        // Height blend: the taller material shows through first where both are present.
        float total = max(firstWeight + max(secondWeight, 0.0), 0.0001);
        float firstShare = firstWeight / total;
        // Fringe: across the soft band between two layers, fine noise (A ~1/64 and G ~1/16 of its repeat) tips the
        // balance, scattering clumps and islands of the lower-numbered layer into the other and holes back into it.
        vec4 fringeSample = texture2D(s_terrainMacro, position.xy * u_terrainMaterialMisc.y);
        float fringeScale = min(u_terrainLayerVariant[int(firstLayer + 0.5)].z,
            u_terrainLayerVariant[int(secondLayer + 0.5)].z);
        float fringe = (fringeSample.a * 0.55 + fringeSample.g * 0.45 - 0.5) * u_terrainMaterialMisc.x * fringeScale
            * 4.0 * firstShare * (1.0 - firstShare);
        firstShare = clamp(firstShare + fringe, 0.0, 1.0);
        float secondShare = 1.0 - firstShare;
        vec4 firstParams = u_terrainLayerParams[int(firstLayer + 0.5)];
        vec4 secondParams = u_terrainLayerParams[int(secondLayer + 0.5)];
        float depth = mix(firstParams.y, secondParams.y, 0.5);
        float top = max(firstColor.a + firstShare, secondColor.a + secondShare) - depth;
        // A layer fades out with its weight: one without weight never shows through the low spots of the other, so
        // the single-material draw, which skips it, matches the triangles around it.
        float firstAmount = max(firstColor.a + firstShare - top, 0.0) * clamp(firstWeight * 10.0, 0.0, 1.0);
        float secondAmount = max(secondColor.a + secondShare - top, 0.0) * clamp(secondWeight * 10.0, 0.0, 1.0);
        // The plain seabed is the shore layer alone, whichever slot it holds.
        float secondMix = mix(secondAmount / max(firstAmount + secondAmount, 0.0001),
            abs(secondLayer - u_terrainShore.w) < 0.5 ? 1.0 : 0.0, plain);
        vec3 firstAlbedo = firstColor.rgb * mix(1.0, 0.45 + 1.1 * firstColor.a, firstParams.w);
        vec3 secondAlbedo = secondColor.rgb * mix(1.0, 0.45 + 1.1 * secondColor.a, secondParams.w);
        albedo = mix(macroVaried(firstAlbedo, firstLayer, macroSample, meadow),
            macroVaried(secondAlbedo, secondLayer, macroSample, meadow), secondMix);
        shadingNormal = normalize(mix(firstNormal, secondNormal, secondMix));
        if (u_terrainShore.y > 0.5)
        {
            albedo = shoreAlbedo(albedo, position, shore);
        }
        occlusion = mix(mix(1.0, mix(firstSurface.x, secondSurface.x, secondMix), 0.85), 1.0, plain);
        pool = mix(u_terrainLayerVariant[int(firstLayer + 0.5)].w * safeSmoothstep(0.2, 0.12, firstSurface.y),
            u_terrainLayerVariant[int(secondLayer + 0.5)].w * safeSmoothstep(0.2, 0.12, secondSurface.y), secondMix)
            * (1.0 - plain) * safeSmoothstep(0.94, 0.985, geometricNormal.z);
    }

    if (u_terrainFoliage.x > 0.0)
    {
        // Where the grass decorations have thinned out (GRASS_LOD.md), the ground takes their colour: by the share of
        // their coverage the remaining tufts no longer supply, and by how much of the grass a ray at this elevation
        // crosses (grazing views see mostly blades, views from above the ground between them).
        vec4 foliage = texture2D(s_terrainFoliage, mapUv);
        if (foliage.a > 0.0001)
        {
            vec3 toCamera = u_cameraPosition.xyz - position;
            float distance = length(toCamera);
            float thinned = min(1.0, u_terrainFoliage.y * u_terrainFoliage.y / max(distance * distance, 1.0));
            float scale = clamp(distance / u_terrainFoliage.y, 1.0, u_terrainFoliage.w);
            float drawn = min(thinned * scale * scale, 1.0)
                * (1.0 - smoothstep(u_terrainFoliage.z * 0.85, u_terrainFoliage.z, distance));
            float elevation = clamp(dot(toCamera / max(distance, 1.0), geometricNormal), 0.02, 1.0);
            float grazing = sqrt(1.0 - elevation * elevation) / elevation;
            float coverage = 1.0 - exp(-foliage.a * grazing);
            albedo = mix(albedo, foliage.rgb / foliage.a, coverage * (1.0 - drawn) * u_terrainFoliage.x);
        }
    }
    albedo = mix(albedo, u_fogColor.rgb, u_fogDensities.z);
#if BAKED_SOURCES
    vec2 bakedUv = (position.xy - u_bakedTerrainBounds.xy) / u_bakedTerrainBounds.zw;
    vec4 sunSample = texture2DLod(s_texLightmap, bakedUv, 0.0);
    vec4 skySample = texture2DLod(s_texBakedSky, bakedUv, 0.0);
    vec3 totalSun = sunSample.rgb * (sunSample.a * 4.0);
    vec3 directSun = totalSun;
    if (u_terrainMaterialLight.w > 0.5 && plain < 0.999)
    {
        vec4 directSample = texture2DLod(s_texBakedSunDirect, bakedUv, 0.0);
        directSun = min(directSample.rgb * (directSample.a * 4.0), totalSun);
    }
    // The bake lit the native flat triangles; relight its direct sun for the smooth, detailed normal. The native
    // normal comes filtered exactly like the bake, so the ratio is smooth across native triangle edges. The plain
    // seabed keeps the bake as it is.
    float relight = 1.0;
    float skyFacing = 1.0;
    float visibility = 1.0;
    if (plain < 0.999)
    {
        vec3 facetNormal = texture2DLod(s_terrainBakedNormals, bakedUv, 0.0).xyz * 2.0 - 1.0;
        vec3 toSun = u_terrainMaterialLight.xyz;
        float facetFacing = dot(facetNormal, toSun);
        relight = mix(clamp(max(dot(shadingNormal, toSun), 0.0) / max(facetFacing, 0.15), 0.0, 2.0), 1.0, plain);
        skyFacing = mix((0.65 + 0.35 * shadingNormal.z) / (0.65 + 0.35 * facetNormal.z), 1.0, plain);
#if SUN_SHADOWS
        if (u_sunShadowParams[0].x > 0.5)
        {
            vec3 shadowNormal = dot(geometricNormal, u_sunShadowParams[3].xyz) < 0.0
                ? -geometricNormal : geometricNormal;
            visibility = mix(sunShadowVisibility(position, shadowNormal), 1.0, plain);
        }
#endif
    }
    vec3 sunLight = (totalSun - directSun) * occlusion + directSun * relight * visibility;
    vec3 skyLight = skySample.rgb * (skySample.a * 4.0) * occlusion * skyFacing;
    vec3 lighting = sunLight * u_bakedLighting[0].rgb + skyLight * u_bakedLighting[1].rgb;
    vec3 litColor = bakedSurfaceColor(albedo, lighting + getFxLighting(position, 0.0));
#else
    vec3 litColor = albedo * getFxLighting(position, outdoorSunlight(normalize(mix(shadingNormal, geometricNormal,
        plain)))) * occlusion;
#endif
    if (plain < 0.999 && (waterKind < 0.5 || waterKind > 1.5))
    {
        litColor = mix(applyWetness(litColor, position), litColor, plain);
    }
    if (pool > 0.0)
    {
        // Standing water (level ground only): the dark, murky lit pool under a rippled surface that mirrors the sky
        // at grazing angles and glints.
        float time = u_secretPulseParams.y;
        vec2 ripplePosition = position.xy / 70.0;
        vec2 ripple = vec2(sin(ripplePosition.x * 1.7 + time * 1.9 + sin(ripplePosition.y * 1.3)),
            cos(ripplePosition.y * 1.9 - time * 1.6 + sin(ripplePosition.x * 1.1))) * 0.03;
        vec3 waterNormal = normalize(vec3(ripple, 1.0));
        vec3 toCamera = normalize(u_cameraPosition.xyz - position);
        float facing = clamp(dot(waterNormal, toCamera), 0.0, 1.0);
        float fresnel = 0.03 + 0.97 * pow(1.0 - facing, 5.0);
        vec3 toSun = normalize(u_terrainMaterialLight.xyz);
        float glint = pow(max(dot(reflect(-toCamera, waterNormal), toSun), 0.0), 160.0) * 0.5;
        vec3 water = mix(litColor * 0.65, u_fogColor.rgb * 0.8, min(fresnel, 0.7)) + vec3(1.0, 0.95, 0.85) * glint;
        litColor = mix(litColor, water, pool);
    }

    float fogDistance = length(position - u_cameraPosition.xyz);
    float fogRatio = skyFogRatio(getFogRatio(fogDistance), position, fogDistance, u_fogDistances.z);
    vec4 fogColor = vec4(skyFogDisplayColor(fogRatio, u_fogColor.rgb, position), getFogAlpha(fogDistance));
    gl_FragColor = mix(vec4(litColor, 1.0), fogColor, fogRatio);
}
