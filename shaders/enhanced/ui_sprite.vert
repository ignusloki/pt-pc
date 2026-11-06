#version 460

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) flat out uint out_draw;

layout(push_constant) uniform UiPush {
    vec2 inverse_extent;
    uint draw;
    float eotf;
    vec4 grade;
    vec4 chroma;
    vec4 extra; // blowout, reserved
    vec4 ui;
} push;

#include "ui_sprite_vertex.glsl"
