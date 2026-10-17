$input v_texcoord0
#include "common.sh"

// Bloom: one direction of a separable Gaussian over the half-resolution glow (9 taps read as 5 bilinear fetches,
// spread 1.5x for a wider halo).
SAMPLER2D(s_bloomSource, 0);
// xy: the blur step (one half-resolution texel along x or y).
uniform vec4 u_bloomTexel;

void main()
{
    vec2 step = u_bloomTexel.xy * 1.5;
    vec3 sum = texture2D(s_bloomSource, v_texcoord0).rgb * 0.2270270270;
    sum += texture2D(s_bloomSource, v_texcoord0 + step * 1.3846153846).rgb * 0.3162162162;
    sum += texture2D(s_bloomSource, v_texcoord0 - step * 1.3846153846).rgb * 0.3162162162;
    sum += texture2D(s_bloomSource, v_texcoord0 + step * 3.2307692308).rgb * 0.0702702703;
    sum += texture2D(s_bloomSource, v_texcoord0 - step * 3.2307692308).rgb * 0.0702702703;
    gl_FragColor = vec4(sum, 1.0);
}
