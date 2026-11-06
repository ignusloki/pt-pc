// Shared verbatim by the Original and Enhanced shader families.
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D source;
layout(set = 0, binding = 1) uniform sampler2D grain_noise;

layout(push_constant) uniform XrCopyParams {
    vec4 rect;
    vec4 params;
    vec4 grain;
    vec4 grain_offset;
} p;

vec3 Decode(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
