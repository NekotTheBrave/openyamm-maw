$input v_texcoord1, v_color0

#include "common.sh"

SAMPLER2D(s_texColor, 0);
// x: tail erosion 0-1, y: white-hot core brightness, z: 1 for additive blending, w: solid fill under the noise.
uniform vec4 u_ribbonParams;
// x, y: scroll offsets of the two noise layers, in texture repeats.
uniform vec4 u_ribbonScroll;

void main()
{
    float u = v_texcoord1.x;
    float across = v_texcoord1.y;
    float phase = v_texcoord1.z;

    // Two layers of the same noise, the second slower, finer and mirrored across the strip.
    float noiseA = texture2D(s_texColor, vec2(u + u_ribbonScroll.x, across)).r;
    float noiseB = texture2D(s_texColor, vec2(u * 1.7 + u_ribbonScroll.y, 1.0 - across)).r;
    float noise = noiseA * 0.6 + noiseB * 0.4;

    // Soft edges across the strip, then noise erosion that eats more of the strip towards the tail.
    float centre = 1.0 - abs(across * 2.0 - 1.0);
    float body = smoothstep(0.0, 0.9, centre);
    // Noise also pushes the edges in and out, so the strip has no straight outline.
    float density = body * (u_ribbonParams.w + noise) - (1.0 - centre) * (1.0 - noise) * 0.35;
    float threshold = u_ribbonParams.x * phase;
    float mask = smoothstep(threshold, threshold + 0.35, density) * body;

    float headFade = 1.0 - phase;
    float core = u_ribbonParams.y * pow(centre, 6.0) * headFade * headFade * mask;
    vec3 color = v_color0.rgb * mask + vec3_splat(core);
    float alpha = clamp(v_color0.a * mask, 0.0, 1.0);

    if (u_ribbonParams.z > 0.5)
    {
        gl_FragColor = vec4(color * v_color0.a, alpha);
    }
    else
    {
        gl_FragColor = vec4(color, alpha);
    }
}
