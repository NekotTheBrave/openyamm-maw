$input v_texcoord0
#include "common.sh"

// Bloom (lighting plan, phase 7): the glow target (what emitters draw into WorldGlowView) reduced to half resolution.
// Four bilinear taps cover the 4 x 4 source texels around the destination texel.
SAMPLER2D(s_bloomSource, 0);
// xy: one source texel; zw: unused.
uniform vec4 u_bloomTexel;

void main()
{
    vec2 offset = u_bloomTexel.xy;
    vec3 sum = texture2D(s_bloomSource, v_texcoord0 + vec2(-offset.x, -offset.y)).rgb
        + texture2D(s_bloomSource, v_texcoord0 + vec2(offset.x, -offset.y)).rgb
        + texture2D(s_bloomSource, v_texcoord0 + vec2(-offset.x, offset.y)).rgb
        + texture2D(s_bloomSource, v_texcoord0 + vec2(offset.x, offset.y)).rgb;
    gl_FragColor = vec4(sum * 0.25, 1.0);
}
