// Shared shader prelude. Every shader includes "common.sh", which resolves here before bgfx's
// examples/common/common.sh; it pulls in the same bgfx headers.
#include <bgfx_shader.sh>
#include "shaderlib.sh"

#if BGFX_SHADER_LANGUAGE_ESSL
// OpenGL ES samplers default to lowp, and glsl-optimizer gives every value computed from a fetch that precision:
// world-space joint matrices, light-grid positions and radii, and the specular terms of normal-mapped surfaces then
// overflow or underflow fp16 on phones (distorted creatures, fully lit light-grid tiles, white and black blotches).
// Desktop GLSL and SPIR-V have no precision qualifiers.
#   undef SAMPLER2D
#   undef SAMPLER2DARRAY
#   undef SAMPLER2DSHADOW
#   undef SAMPLER3D
#   undef SAMPLERCUBE
#   define SAMPLER2D(_name, _reg) uniform highp sampler2D _name
#   define SAMPLER2DARRAY(_name, _reg) uniform highp sampler2DArray _name
#   define SAMPLER2DSHADOW(_name, _reg) uniform highp sampler2DShadow _name
#   define SAMPLER3D(_name, _reg) uniform highp sampler3D _name
#   define SAMPLERCUBE(_name, _reg) uniform highp samplerCube _name
#endif
