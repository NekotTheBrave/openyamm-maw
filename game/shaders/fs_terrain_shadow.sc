#include "common.sh"

void main()
{
    // fs_model_shadow.sc's depth encoding; the zero alpha marks a terrain caster (sun_shadows.sh).
    vec3 encoded = fract(min(gl_FragCoord.z, 0.9999999) * vec3(1.0, 255.0, 65025.0));
    encoded.xy -= encoded.yz / 255.0;
    gl_FragColor = vec4(encoded, 0.0);
}
