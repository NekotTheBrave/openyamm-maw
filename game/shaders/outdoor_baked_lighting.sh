// RGBM4 stores linear incident diffuse lighting, independently of receiver albedo.
SAMPLER2D(s_texBakedSky, 3);
uniform vec4 u_bakedLighting[2];
#if SUN_SHADOWS
#include "sun_shadows.sh"
SAMPLER2D(s_texBakedSunDirect, 5);
#endif

vec3 bakedSourceLighting(vec4 sun, vec4 sky)
{
    return sun.rgb * (sun.a * 4.0) * u_bakedLighting[0].rgb
        + sky.rgb * (sky.a * 4.0) * u_bakedLighting[1].rgb;
}

vec3 bakedShadowedSourceLighting(vec4 sun, vec4 sky, vec2 uv, vec3 position)
{
#if SUN_SHADOWS
    if (u_sunShadowParams[0].x < 0.5)
    {
        return bakedSourceLighting(sun, sky);
    }
    vec3 normal = cross(dFdx(position), dFdy(position));
    normal *= inversesqrt(max(dot(normal, normal), 0.000001));
    if (dot(normal, u_sunShadowParams[3].xyz) < 0.0)
    {
        normal = -normal;
    }
    float visibility = sunShadowVisibility(position, normal, 1.0);
    vec3 totalSun = sun.rgb * (sun.a * 4.0);
    vec4 directSample = texture2D(s_texBakedSunDirect, uv);
    // Both pages already contain static occlusion. Remove only moving-actor occlusion of direct sunlight.
    // Independent RGBM quantization can exceed total sun slightly; clamp the subtraction to that total.
    vec3 directSun = min(directSample.rgb * (directSample.a * 4.0), totalSun);
    return (totalSun - directSun * (1.0 - visibility)) * u_bakedLighting[0].rgb
        + sky.rgb * (sky.a * 4.0) * u_bakedLighting[1].rgb;
#else
    return bakedSourceLighting(sun, sky);
#endif
}

vec3 bakedSurfaceColor(vec3 color, vec3 lighting)
{
    // Native bitmap textures and the existing output are display encoded.
    return pow(
        max(pow(max(color, vec3_splat(0.0)), vec3_splat(2.2)) * lighting, vec3_splat(0.0)),
        vec3_splat(1.0 / 2.2));
}
