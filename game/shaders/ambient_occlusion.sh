#ifndef OPENYAMM_AMBIENT_OCCLUSION_SH
#define OPENYAMM_AMBIENT_OCCLUSION_SH

uniform mat4 u_aoInverseProjection;
uniform vec4 u_aoDepthParams;
uniform vec4 u_aoParams;
uniform vec4 u_aoTexel;

// AO_MSAA_DEPTH: under multisampling the world depth is never resolved (CinematicGrading::resize); the passes read
// its first sample. ESSL 3.00 has no multisample samplers, so OpenGL ES keeps sampling the resolved depth.
#if AO_MSAA_DEPTH && !BGFX_SHADER_LANGUAGE_ESSL
SAMPLER2DMS(s_aoDepth, 0);

float aoDepth(vec2 uv)
{
    // A half-resolution pixel centre lies on the edge between two full-resolution pixels: the quarter-pixel shift
    // picks the lower one every time instead of whichever the rounding favours.
    return texelFetch(s_aoDepth, ivec2(uv / u_aoTexel.xy - 0.25), 0).r;
}
#else
SAMPLER2D(s_aoDepth, 0);

float aoDepth(vec2 uv)
{
    // Point-sampled with the same quarter-pixel shift as above.
    return texture2D(s_aoDepth, uv - 0.25 * u_aoTexel.xy).r;
}
#endif

vec3 aoViewPosition(vec2 uv, float depth)
{
    vec2 xy = uv * 2.0 - 1.0;
    xy.y *= u_aoDepthParams.z;
    vec4 position = mul(u_aoInverseProjection, vec4(xy,
        depth * u_aoDepthParams.x + u_aoDepthParams.y, 1.0));
    return position.xyz / position.w;
}

float aoDepthWeight(float center, float neighbor)
{
    float delta = abs(center - neighbor) / max(1.0, center * 0.002);
    return exp(-delta * delta);
}

#endif
