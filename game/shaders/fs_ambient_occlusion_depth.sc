$input v_texcoord0
#include "common.sh"
#include "ambient_occlusion.sh"

void main()
{
    float depth = aoDepth(v_texcoord0);
    // Sky lies beyond every AO distance limit.
    float distance = depth >= 0.999999 ? 1000000.0 : -aoViewPosition(v_texcoord0, depth).z;
    gl_FragColor = vec4(distance, 0.0, 0.0, 1.0);
}
