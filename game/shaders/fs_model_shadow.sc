$input v_texcoord0

#include "common.sh"

SAMPLER2D(s_modelTexture, 0);
SAMPLER2D(s_modelBurn, 10);
uniform vec4 u_modelMaterial[2];
// fs_model.sc: a burning creature casts no shadow from the surface it has lost.
uniform vec4 u_modelBurn[3];

void main()
{
    if (u_modelMaterial[1].y > 0.5
        && texture2D(s_modelTexture, v_texcoord0).a * u_modelMaterial[0].a < u_modelMaterial[1].x)
    {
        discard;
    }
    if (u_modelBurn[2].x > 0.5 && u_modelBurn[0].x - texture2D(s_modelBurn, v_texcoord0).r > u_modelBurn[0].z)
    {
        discard;
    }
    // Hardware depth selects the nearest caster; ordinary colour channels retain 24-bit sampling precision.
    vec3 encoded = fract(min(gl_FragCoord.z, 0.9999999) * vec3(1.0, 255.0, 65025.0));
    encoded.xy -= encoded.yz / 255.0;
    gl_FragColor = vec4(encoded, 1.0);
}
