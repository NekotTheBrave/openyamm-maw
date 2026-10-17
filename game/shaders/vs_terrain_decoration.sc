$input a_position, a_texcoord0, a_color0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_texcoord1, v_worldPosition, v_flowInfo, v_depth, v_color0, v_sunlight

#include "common.sh"
#include "outdoor_sunlight.sh"
#include "outdoor_lighting.sh"
#if BAKED_SOURCES
SAMPLER2D(s_texLightmap, 2);
uniform vec4 u_bakedTerrainBounds;
#include "outdoor_baked_lighting.sh"
#endif

uniform vec4 u_cameraPosition;
// x = time, y = full density distance, z = far distance, w = 1 for grass cards, 2 for clumps, 0 for stones.
uniform vec4 u_terrainDecorationParams;
// x = largest footprint scale (TerrainDecorationLod::maxScale), y = bit mask of the layers that fade as the camera
// flies, z = that fade's progress from the camera's height above its own ground (0 on foot, 1 high), w = the camera
// distance where clumps are down to their first card (TerrainDecorationClump::distance).
uniform vec4 u_terrainDecorationLod;
// Clumps (TerrainDecorationClump): x = cards, y/z = sine and cosine of their outward lean, w = card scale.
uniform vec4 u_terrainDecorationClump;
// Card outline per tuft layer: 8 texture coordinates per layer, two per vec4 (TerrainDecorationRenderer).
uniform vec4 u_terrainDecorationCutout[64];

void main()
{
    // Level of detail (level_generation/terrain/GRASS_LOD.md, terrainDecorationKeep/Scale): beyond the full density
    // distance an area keeps (near / distance)^2 of its tufts, chosen by their fixed rank, and the survivors grow so
    // the coverage holds; the last 15 % of the range fades the rest. A tuft whose rank nears the share thins out from
    // its soft edges inward (the fragment shader raises its alpha cutoff), so nothing pops or sparkles.
    float distance = length(i_data0.xyz - u_cameraPosition.xyz);
    float nearDistance = u_terrainDecorationParams.y;
    float farDistance = u_terrainDecorationParams.z;
    // ratio > 1 inside the full density distance, where every tuft stays whole; the fade band only begins there.
    float ratio = nearDistance * nearDistance / max(distance * distance, 1.0)
        * (1.0 - smoothstep(farDistance * 0.85, farDistance, distance));
    float rank = i_data1.w;
    float fade = clamp((ratio - rank) / max(min(ratio, 1.0) * 0.2, 0.0001), 0.0, 1.0);
    float scale = clamp(distance / nearDistance, 1.0, u_terrainDecorationLod.x);
    float seed = fract(sin(dot(i_data0.xy, vec2(0.0129898, 0.078233))) * 43758.5453);
    float yaw = i_data0.w;
    vec3 position = a_position;
    vec2 texcoord = a_texcoord0;
    if (u_terrainDecorationParams.w > 0.5)
    {
        // Grass: a_position.x = outline slot (clumps: a_position.y = card). A single card turns about its vertical
        // axis to face the camera (within a fixed per-tuft offset of up to 30 degrees) and is mirrored for half the
        // tufts; crossed cards read as X shapes stamped on the ground when seen from above.
        float index = floor(i_data3.w + 0.5) * 8.0 + a_position.x;
        vec4 pair = u_terrainDecorationCutout[int(index * 0.5)];
        texcoord = fract(index * 0.5) > 0.25 ? pair.zw : pair.xy;
        // Short mats seen from above shrink to slivers that hatch the ground; the terrain under them has their colour.
        float fadesOverhead = mod(floor(u_terrainDecorationLod.y / exp2(floor(i_data3.w + 0.5))), 2.0);
        fade *= 1.0 - fadesOverhead * u_terrainDecorationLod.z;
        if (u_terrainDecorationParams.w > 1.5)
        {
            // Clump: card a_position.y of x in a star about the tuft's own yaw, each turned, sized and mirrored by
            // its own seed and leaning outward from a base a little off the centre. Seen from above the leaning
            // cards still cover the ground, where one upright card is a sliver.
            float card = a_position.y;
            float cardSeed = fract(sin((seed + card) * 78.233) * 43758.5453);
            // Level of detail: past u_terrainDecorationLod.w the cards after the first thin out (the shared fade, so
            // they skip their lighting) and the first grows to keep the clump's coverage.
            float extra = clamp((u_terrainDecorationLod.w - distance) / (u_terrainDecorationLod.w * 0.25), 0.0, 1.0);
            fade *= card < 0.5 ? 1.0 : extra;
            float angle = card * 6.2831853 / u_terrainDecorationClump.x + (cardSeed - 0.5) * 0.6;
            vec2 along = vec2(cos(angle), sin(angle));
            vec2 outward = vec2(-along.y, along.x);
            float rise = 1.0 - texcoord.y;
            // The lean in units of the width, which scales x and y below.
            float lean = rise * u_terrainDecorationClump.y * i_data1.y / max(i_data1.x, 1.0);
            float cardScale = u_terrainDecorationClump.w * (0.85 + 0.3 * cardSeed)
                * (card < 0.5 ? 1.0 + (1.0 - extra) * 0.5 : 1.0);
            vec2 spread = along * (texcoord.x - 0.5) * (cardSeed < 0.5 ? -1.0 : 1.0) + outward * (0.06 + lean);
            position = vec3(spread, rise * u_terrainDecorationClump.z) * cardScale;
        }
        else
        {
            float mirror = seed < 0.5 ? -1.0 : 1.0;
            position = vec3((texcoord.x - 0.5) * mirror, 0.0, 1.0 - texcoord.y);
            vec2 toCamera = u_cameraPosition.xy - i_data0.xy + vec2(0.001, 0.0);
            yaw = atan2(toCamera.y, toCamera.x) - 1.5707963 + (fract(i_data0.w * 0.1591549) - 0.5) * 1.0;
        }
    }
    float c = cos(yaw);
    float s = sin(yaw);
    // The card's screen area grows by scale^2: mostly wider, a little taller.
    vec3 local = position * vec3(i_data1.x * scale * sqrt(scale), i_data1.x * scale * sqrt(scale),
        i_data1.y * sqrt(scale));
    vec3 world = i_data0.xyz + vec3(c * local.x - s * local.y, s * local.x + c * local.y, local.z);
    world.z -= dot(world.xy - i_data0.xy, i_data3.xy) / max(i_data3.z, 0.5);
    float phase = u_terrainDecorationParams.x * 1.6 + dot(i_data0.xy, vec2(0.003, 0.004));
    world.xy += vec2(sin(phase), cos(phase * 0.7)) * i_data1.z * position.z * position.z;
    gl_Position = mul(u_viewProj, vec4(world, 1.0));
    if (fade <= 0.0)
    {
        // Thinned out: every vertex at one point, so the cards rasterize nothing.
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
    }
    v_worldPosition = world;
    v_texcoord0 = texcoord;
    v_texcoord1 = vec4(0.0, 1.0 - u_terrainDecorationParams.w, fade, seed);
    v_depth = gl_Position.w;
    v_color0 = a_color0 * i_data2;
    if (u_terrainDecorationParams.w > 0.5)
    {
        // Grass is shaded by height, darker at the roots and lighter towards the tips, and a share of the tufts have
        // drier tips: the shading, not the outline, sets a tuft off the grass ground it matches in colour
        // (TerrainDecorationShade in TerrainDecorationData.h, which the foliage map averages).
        float rise = 1.0 - texcoord.y;
        float dry = fract(seed * 13.73);
        v_color0.rgb *= (0.72 + 0.56 * rise) * mix(vec3_splat(1.0), vec3(1.15, 1.1, 0.85), dry * dry * rise);
    }
    v_sunlight = outdoorSunlight(i_data3.xyz);
    // A tuft is small, so its light is evaluated per vertex: v_flowInfo.yzw multiplies the display-space texel.
    // Baked: (texel^2.2 * light)^(1/2.2) = texel * light^(1/2.2). The sun-shadow fragment variants relight per pixel.
    // Thinned-out tufts skip it (the whole instance takes the same branch).
    vec3 light = vec3_splat(0.0);
    if (fade > 0.0)
    {
#if BAKED_SOURCES
        vec2 bakedUv = (world.xy - u_bakedTerrainBounds.xy) / u_bakedTerrainBounds.zw;
        light = bakedSourceLighting(texture2DLod(s_texLightmap, bakedUv, 0.0),
            texture2DLod(s_texBakedSky, bakedUv, 0.0)) + getFxLighting(world, 0.0);
        light = pow(max(light, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
#else
        light = getFxLighting(world, v_sunlight);
#endif
    }
    v_flowInfo = vec4(i_data3.w, light);
}
