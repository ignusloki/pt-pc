#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(set = 2, binding = 0) uniform sampler2D sss_original;
layout(set = 2, binding = 1) uniform sampler2D sss_light;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

const float kWeightR[7] = float[7](1.0, 0.986717, 0.947919, 0.886615, 0.807391, 0.715847, 0.617933);
const float kWeightG[7] = float[7](1.0, 0.964554, 0.865578, 0.722668, 0.561334, 0.405661, 0.272743);
const float kWeightB[7] = float[7](1.0, 0.943217, 0.791492, 0.590886, 0.392446, 0.231894, 0.121903);

vec3 LightAt(vec2 uv) {
    return texture(sss_light, uv).rgb;
}

bool Skin(vec2 uv) {
    return int(255.0 * Img(IMG_MATERIAL, SMP_POINT_CLAMP, uv).z) == 10;
}

#include "subsurface_output_shared.glsl"
