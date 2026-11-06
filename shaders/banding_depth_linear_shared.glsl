// Shared verbatim by the Original and Enhanced shader families.
layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

float DepthLinear(vec2 uv, ivec2 size) {
    vec2 p = clamp(uv, 0.0, 1.0) * vec2(size) - 0.5;
    ivec2 a = ivec2(floor(p));
    vec2 f = p - vec2(a);
    ivec2 hi = size - 1;
    float d00 = ImgFetch(IMG_DEPTH, clamp(a, ivec2(0), hi)).x;
    float d10 = ImgFetch(IMG_DEPTH, clamp(a + ivec2(1, 0), ivec2(0), hi)).x;
    float d01 = ImgFetch(IMG_DEPTH, clamp(a + ivec2(0, 1), ivec2(0), hi)).x;
    float d11 = ImgFetch(IMG_DEPTH, clamp(a + ivec2(1, 1), ivec2(0), hi)).x;
    return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
}
