$input a_position, a_normal
$output v_worldPosition, v_worldNormal

#include "common.sh"

// Unit sphere scaled and placed by u_model: the vertex position doubles as the outward surface direction.
void main()
{
    vec4 worldPosition = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPosition);
    v_worldPosition = worldPosition.xyz;
    v_worldNormal = a_normal;
}
