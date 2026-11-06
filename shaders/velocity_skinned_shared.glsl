// Shared verbatim by the Original and Enhanced shader families.
#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv0;
layout(location = 4) in vec2 in_uv1;
layout(location = 5) in vec4 in_color;
layout(location = 6) in uvec4 in_joints;
layout(location = 7) in vec4 in_weights;

layout(push_constant) uniform DrawPush {
    mat4 model;
    uvec4 ids;
    vec4 tint;
    vec4 aux[2];
} draw;

layout(location = 0) out vec2 out_uv0;
layout(location = 1) out vec4 out_current;
layout(location = 2) out vec4 out_previous;

out gl_PerVertex {
    invariant vec4 gl_Position;
};

#include "skinning.glsl"
