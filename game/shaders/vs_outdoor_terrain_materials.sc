$input a_position, a_normal, a_texcoord0
$output v_worldPosition, v_worldNormal, v_texcoord0

#include "common.sh"

void main()
{
    vec4 worldPosition = vec4(a_position, 1.0);
    gl_Position = mul(u_modelViewProj, worldPosition);
    v_worldPosition = a_position;
    v_worldNormal = a_normal;
    v_texcoord0 = a_texcoord0;
}
