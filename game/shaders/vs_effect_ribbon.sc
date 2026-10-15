$input a_position, a_texcoord1, a_color0
$output v_texcoord1, v_color0

#include "common.sh"

// World-space ribbon strip (EffectRenderer::renderRibbons): texcoord1 is (distance along the path in texture
// repeats, 0-1 across the strip, phase from head 0 to tail 1, unused).
void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord1 = a_texcoord1;
    v_color0 = a_color0;
}
