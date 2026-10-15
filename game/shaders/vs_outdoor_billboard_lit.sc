$input a_position, a_texcoord0, a_color0, a_texcoord2
$output v_texcoord0, v_color0, v_depth, v_worldPosition, v_texcoord1

#include "common.sh"

void main()
{
    vec4 worldPosition = vec4(a_position, 1.0);
    vec4 viewPosition = mul(u_modelView, worldPosition);
    gl_Position = mul(u_modelViewProj, worldPosition);
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    v_depth = abs(viewPosition.z);
    v_worldPosition = mul(u_model[0], worldPosition).xyz;
    // x: actor hit flash.
    v_texcoord1 = vec4(a_texcoord2, 0.0, 0.0, 0.0);
}
