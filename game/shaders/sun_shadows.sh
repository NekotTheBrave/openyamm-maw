// Sun shadows from three cascades (ModelRenderer::renderSunShadows), side by side in one atlas. Each matrix maps
// to its cascade's own tile [0, 1]. u_sunShadowParams: [0] enabled, 1 / tile size, depth bias, normal offset;
// [1] camera position, shadow distance; [2] world size of a texel per cascade; [3] towards the sun, hard filter.
// Terrain casters write a zero alpha: receivers whose bake already holds the terrain's shadow ignore them.
SAMPLER2D(s_sunShadow, 6);
uniform mat4 u_sunShadowMatrices[3];
uniform vec4 u_sunShadowParams[4];

#define SUN_SHADOW_CASCADES 3.0

float sunShadowDepth(vec3 encoded)
{
    return dot(encoded, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}

bool sunShadowCovered(vec3 projected)
{
    float border = u_sunShadowParams[0].y * 2.0;
    return projected.x > border && projected.x < 1.0 - border
        && projected.y > border && projected.y < 1.0 - border && projected.z > 0.0 && projected.z < 1.0;
}

float sunShadowTap(vec2 uv, float depth, float ignoreTerrain)
{
    vec4 caster = texture2D(s_sunShadow, uv);
    return max(step(depth, sunShadowDepth(caster.rgb)), ignoreTerrain * (1.0 - caster.a));
}

float sunShadowFiltered(vec3 projected, float cascade, float ignoreTerrain)
{
    vec2 uv = vec2((projected.x + cascade) / SUN_SHADOW_CASCADES, projected.y);
    float depth = projected.z - u_sunShadowParams[0].z;
    if (u_sunShadowParams[3].w > 0.5)
    {
        return sunShadowTap(uv, depth, ignoreTerrain);
    }
    vec2 texel = vec2(u_sunShadowParams[0].y / SUN_SHADOW_CASCADES, u_sunShadowParams[0].y);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            visibility += sunShadowTap(uv + vec2(float(x), float(y)) * texel, depth, ignoreTerrain);
        }
    }
    return visibility / 9.0;
}

// Towards the next cascade over the outer part of a tile.
float sunShadowEdgeBlend(vec3 projected)
{
    float edge = max(abs(projected.x - 0.5), abs(projected.y - 0.5)) * 2.0;
    return smoothstep(0.80, 0.96, edge);
}

// The sun's visibility at a receiver, from the finest cascade covering it. ignoreTerrain: 1 for receivers lit by a
// bake that already holds the terrain's shadow.
float sunShadowVisibility(vec3 position, vec3 normal, float ignoreTerrain)
{
    if (u_sunShadowParams[0].x < 0.5)
    {
        return 1.0;
    }
    float distance = length(position - u_sunShadowParams[1].xyz);
    if (distance >= u_sunShadowParams[1].w)
    {
        return 1.0;
    }
    // The normal offset grows with a cascade's texel at grazing light.
    float grazing = 1.0 - abs(dot(normal, u_sunShadowParams[3].xyz));
    vec3 offset = normal * u_sunShadowParams[0].w;
    vec3 nearProjected = mul(u_sunShadowMatrices[0],
        vec4(position + offset + normal * (u_sunShadowParams[2].x * grazing), 1.0)).xyz;
    vec3 middleProjected = mul(u_sunShadowMatrices[1],
        vec4(position + offset + normal * (u_sunShadowParams[2].y * grazing), 1.0)).xyz;
    vec3 farProjected = mul(u_sunShadowMatrices[2],
        vec4(position + offset + normal * (u_sunShadowParams[2].z * grazing), 1.0)).xyz;
    float visibility = 1.0;
    if (sunShadowCovered(nearProjected))
    {
        float blend = sunShadowEdgeBlend(nearProjected);
        visibility = sunShadowFiltered(nearProjected, 0.0, ignoreTerrain);
        if (blend > 0.0)
        {
            float next = sunShadowCovered(middleProjected)
                ? sunShadowFiltered(middleProjected, 1.0, ignoreTerrain) : 1.0;
            visibility = mix(visibility, next, blend);
        }
    }
    else if (sunShadowCovered(middleProjected))
    {
        float blend = sunShadowEdgeBlend(middleProjected);
        visibility = sunShadowFiltered(middleProjected, 1.0, ignoreTerrain);
        if (blend > 0.0)
        {
            float next = sunShadowCovered(farProjected)
                ? sunShadowFiltered(farProjected, 2.0, ignoreTerrain) : 1.0;
            visibility = mix(visibility, next, blend);
        }
    }
    else if (sunShadowCovered(farProjected))
    {
        visibility = sunShadowFiltered(farProjected, 2.0, ignoreTerrain);
    }
    return mix(visibility, 1.0, smoothstep(u_sunShadowParams[1].w * 0.85, u_sunShadowParams[1].w, distance));
}
