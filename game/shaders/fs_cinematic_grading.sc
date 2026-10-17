$input v_texcoord0
#include "common.sh"

SAMPLER2D(s_gradeScene, 0);
SAMPLER3D(s_gradeLut, 1);
// The blurred half-resolution glow (bloom, lighting plan phase 7).
SAMPLER2D(s_gradeBloom, 2);
// x: grade strength, y and z: LUT scale and offset, w: sharpening 0..1.
uniform vec4 u_gradeParams;
// x: bloom strength (0: none).
uniform vec4 u_gradeBloom;

// Contrast-adaptive sharpening (after AMD FidelityFX CAS): subtracts the four direct neighbours, weighted down
// where the neighbourhood already spans most of the range, so hard edges do not ring or brighten aliasing.
// Full strength sits between CAS's lowest and highest sharpness: its highest turns the sky's dithering into grain.
// Lower strengths fade towards none.
vec3 sharpen(vec3 centre, vec2 uv, float strength)
{
    vec2 texel = u_viewTexel.xy;
    vec3 up = texture2D(s_gradeScene, uv - vec2(0.0, texel.y)).rgb;
    vec3 down = texture2D(s_gradeScene, uv + vec2(0.0, texel.y)).rgb;
    vec3 left = texture2D(s_gradeScene, uv - vec2(texel.x, 0.0)).rgb;
    vec3 right = texture2D(s_gradeScene, uv + vec2(texel.x, 0.0)).rgb;
    vec3 low = min(centre, min(min(up, down), min(left, right)));
    vec3 high = max(centre, max(max(up, down), max(left, right)));
    vec3 headroom = sqrt(clamp(min(low, 1.0 - high) / max(high, vec3_splat(0.0001)), 0.0, 1.0));
    vec3 weight = headroom * (-0.15 * strength);
    return clamp((centre + (up + down + left + right) * weight) / (1.0 + 4.0 * weight), 0.0, 1.0);
}

void main()
{
    vec4 original = texture2D(s_gradeScene, v_texcoord0);
    if (u_gradeParams.x <= 0.0 && u_gradeParams.w <= 0.0 && u_gradeBloom.x <= 0.0)
    {
        gl_FragColor = original;
        return;
    }
    vec3 color = clamp(original.rgb, 0.0, 1.0);
    if (u_gradeParams.w > 0.0)
    {
        color = sharpen(color, v_texcoord0, u_gradeParams.w);
    }
    vec3 uvw = color * u_gradeParams.y + u_gradeParams.z;
    vec3 graded = texture3D(s_gradeLut, uvw).rgb;
    color = mix(color, graded, u_gradeParams.x);
    if (u_gradeBloom.x > 0.0)
    {
        // Screen blend in display space: the glow lifts what is around an emitter without clipping it.
        vec3 bloom = clamp(texture2D(s_gradeBloom, v_texcoord0).rgb * u_gradeBloom.x, 0.0, 1.0);
        color = 1.0 - (1.0 - color) * (1.0 - bloom);
    }
    gl_FragColor = vec4(color, original.a);
}
