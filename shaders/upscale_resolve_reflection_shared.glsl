// Shared verbatim by the Original and Enhanced shader families.
#include "common.glsl"
#include "reflection_blend.glsl"

#define IMG_UPSCALED 43
#define IMG_MOTION 45
#define IMG_HANDY_FACTOR 55

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_depth;
layout(location = 2) out vec4 out_velocity;

vec4 Reflection(vec2 render_uv) {
    ivec2 size = ivec2(pass.f1.xy);
    vec2 texel = render_uv * pass.f1.xy - 0.5;
    ivec2 p0 = ivec2(floor(texel));
    vec2 f = texel - vec2(p0);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        vec4 s = ImgFetch(pass.ids.z, clamp(p0 + o, ivec2(0), size - 1));
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
        sum += s.a > 0.0 ? s * w : vec4(0.0);
    }
    return sum;
}
