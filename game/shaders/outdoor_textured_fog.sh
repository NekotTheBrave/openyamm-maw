
#include "common.sh"
#include "world_clip.sh"

#if TERRAIN_DECORATION
SAMPLER2DARRAY(s_texColor, 0);
#elif BMODEL_TEXTURE_ARRAY
// A bmodel face whose texture lives in a size array (u_bmodelTextureLayer.x is its layer).
SAMPLER2DARRAY(s_texColor, 0);
uniform vec4 u_bmodelTextureLayer;
#else
SAMPLER2D(s_texColor, 0);
#endif

#if BAKED_SOURCES
SAMPLER2D(s_texLightmap, 2);
uniform vec4 u_bakedTerrainBounds;
#include "outdoor_baked_lighting.sh"
#else
#if SUN_SHADOWS
#include "sun_shadows.sh"
#endif
uniform vec4 u_outdoorSunlight;
#endif

uniform vec4 u_cameraPosition;
uniform vec4 u_secretPulseParams;
uniform vec4 u_waterSurfaceControl;

#include "outdoor_lighting.sh"
#include "sky_common.sh"
#include "weather_wetness.sh"

void main()
{
    clipWorldPosition(v_worldPosition);
    vec2 texcoord = v_texcoord0;
    bool terrainWater = v_texcoord1.x < -0.5;

#if !TERRAIN_DECORATION
    if (!terrainWater)
    {
        texcoord.xy += v_flowInfo.xy * u_secretPulseParams.y;

        if (v_flowInfo.z > 0.5)
        {
            float lavaPhase = sin(mod(u_secretPulseParams.y, 8.0) * 6.2831853 / 8.0);
            texcoord.y += lavaPhase;
        }
    }

#endif
#if TERRAIN_DECORATION
    vec4 textureColor = texture2DArray(s_texColor, vec3(texcoord, v_flowInfo.x));
#elif BMODEL_TEXTURE_ARRAY
    vec4 textureColor = texture2DArray(s_texColor, vec3(texcoord, u_bmodelTextureLayer.x));
#else
    vec4 textureColor = texture2D(s_texColor, texcoord);
#endif
#if TERRAIN_DECORATION
    if (v_texcoord1.y > 0.5)
    {
        textureColor = vec4(1.0, 1.0, 1.0, 1.0);
    }
    textureColor *= v_color0;
    if (v_texcoord1.y > 0.5)
    {
        // Stones: fixed object-space noise, independent of frame/time and screen position. Coverage fades
        // without shrinking the mesh or introducing a screen-space pattern that crawls over it.
        vec2 coverageCell = floor(v_texcoord0 * vec2(128.0, 96.0));
        float coverageNoise = fract(sin(dot(coverageCell, vec2(12.9898, 78.233))
                                       + v_texcoord1.w * 91.7) * 43758.5453);
        if (textureColor.a < 0.4 || v_texcoord1.z <= coverageNoise)
        {
            discard;
        }
    }
    else if (v_texcoord1.z <= 0.0 || textureColor.a < mix(1.0, 0.4, v_texcoord1.z))
    {
        // Grass: a fading tuft's blades thin from their soft edges inward (vs_terrain_decoration).
        discard;
    }
#if !SUN_SHADOWS
    // Lit per vertex (vs_terrain_decoration); decorations are never secret faces.
    textureColor.rgb = mix(textureColor.rgb, u_fogColor.rgb, u_fogDensities.z) * v_flowInfo.yzw;
    float decorationFogDistance = length(v_worldPosition - u_cameraPosition.xyz);
    float decorationFogRatio =
        skyFogRatio(getFogRatio(decorationFogDistance), v_worldPosition, decorationFogDistance, u_fogDistances.z);
    gl_FragColor = mix(textureColor,
        vec4(skyFogDisplayColor(decorationFogRatio, u_fogColor.rgb, v_worldPosition),
            getFogAlpha(decorationFogDistance)),
        decorationFogRatio);
    return;
#endif
#endif
    if (textureColor.a <= 0.1)
    {
        discard;
    }

    textureColor.rgb = mix(textureColor.rgb, u_fogColor.rgb, u_fogDensities.z);
#if BAKED_SOURCES
    vec2 bakedUv = (v_worldPosition.xy - u_bakedTerrainBounds.xy) / u_bakedTerrainBounds.zw;
    vec3 baked = bakedShadowedSourceLighting(texture2D(s_texLightmap, bakedUv),
        texture2D(s_texBakedSky, bakedUv), bakedUv, v_worldPosition);
    vec4 litTextureColor = vec4(bakedSurfaceColor(textureColor.rgb,
        baked + getFxLighting(v_worldPosition, 0.0)), textureColor.a);
#else
    vec4 litTextureColor = vec4(textureColor.rgb * getFxLighting(v_worldPosition, v_sunlight), textureColor.a);
#endif
#if !TERRAIN_DECORATION
    if (!terrainWater)
    {
        litTextureColor.rgb = applyWetness(litTextureColor.rgb, v_worldPosition);
    }
#endif

    bool classicSecret = v_texcoord1.x > 0.5 && v_texcoord1.x < 1.5;
    bool authoredPerception = v_texcoord1.x >= 1.5;
    bool perceptionDetected = u_secretPulseParams.z >= v_texcoord1.x - 2.0;
    bool secretDetected =
        (classicSecret && u_secretPulseParams.x > 0.5)
        || (authoredPerception && perceptionDetected);

    if (authoredPerception && !perceptionDetected)
    {
        discard;
    }

    if (secretDetected)
    {
        float pulse = 0.5 + 0.5 * sin(u_secretPulseParams.y * 4.0);
        litTextureColor.rgb *= vec3(1.0, pulse, pulse);
    }

    float fogDistance = length(v_worldPosition - u_cameraPosition.xyz);
    float fogRatio = skyFogRatio(getFogRatio(fogDistance), v_worldPosition, fogDistance, u_fogDistances.z);
    float fogAlpha = getFogAlpha(fogDistance);
    vec4 fogColor = vec4(skyFogDisplayColor(fogRatio, u_fogColor.rgb, v_worldPosition), fogAlpha);
    gl_FragColor = mix(litTextureColor, fogColor, fogRatio);
}
