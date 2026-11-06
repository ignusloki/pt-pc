#extension GL_EXT_nonuniform_qualifier : require

struct View {
    mat4 view_projection;
    mat4 view;
    mat4 inv_view;
    vec4 projection_param;
    vec4 exposure;
    vec4 viewport;
    vec4 eye;
    vec4 clip_plane;
    vec4 shadow;
    vec4 jitter;
    vec4 temporal;
    vec4 dominant_light;
};

struct Light {
    vec4 position;
    vec4 color;
    vec4 direction;
    vec4 range;
    vec4 cone;
    vec4 shadow_cone;
    vec4 scales;
    ivec4 info;
    vec4 shadow_rect;
    mat4 area;
    mat4 shadow;
    mat4 mask;
};

struct Probe {
    mat4 box;
    vec4 positive;
    vec4 negative;
    vec4 sh[9];
};

struct Material {
    uint albedo;
    uint normal;
    uint specular;
    uint flags;
    uint aux0;
    uint aux1;
    uint kind;
    uint aux2;
    vec4 albedo_factor;
    vec4 params;
    vec4 indices;
    vec4 extra;
};

#include "bindless.glsl"
layout(set = 0, binding = 0) uniform PT_TEXTURE2D_ARRAY textures[];
layout(set = 0, binding = 2) uniform PT_TEXTURECUBE_ARRAY cube_textures[];
layout(std430, set = 0, binding = 1) readonly buffer Materials {
    Material materials[];
};

layout(std430, set = 1, binding = 0) readonly buffer FrameData {
    View views[64];
    Light lights[256];
    Probe probes[512];
    vec4 fog[8];
    vec4 mirror;
} frame;

#include "common_functions.glsl"

#include "srgb.glsl"



float Saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 Saturate(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}
