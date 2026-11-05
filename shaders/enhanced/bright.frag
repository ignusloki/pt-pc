#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 block = ImgSize(IMG_HDR) / ImgSize(IMG_BLOOM_A);
    vec4 t = ImgFetch(IMG_HDR, ivec2(floor(floor(gl_FragCoord.xy) * block + 0.5 * block)));
    if (frame.hdr_output.x > 0.5) {
        vec2 lo = floor(gl_FragCoord.xy) * block, hi = lo + block;
        vec3 sum = vec3(0.0);
        for (int y = int(floor(lo.y)); y < int(ceil(hi.y)); ++y) {
            for (int x = int(floor(lo.x)); x < int(ceil(hi.x)); ++x) {
                vec2 coverage = max(min(hi, vec2(x + 1, y + 1)) - max(lo, vec2(x, y)), 0.0);
                sum += max(ImgFetch(IMG_HDR, clamp(ivec2(x, y), ivec2(0), ivec2(ImgSize(IMG_HDR)) - 1)).rgb, 0.0) * coverage.x * coverage.y;
            }
        }
        t.rgb = sum / (block.x * block.y);
    }
    vec3 x = max(t.rgb * pass.f1.x, vec3(0.0));
    bool hdr = frame.hdr_output.x > 0.5;
    float scale = hdr ? max(1.0, max(x.r, max(x.g, x.b))) : 1.0;
    x = hdr ? x / scale : min(x, vec3(1.0));
    float l1 = Luma709(x);
    float l2 = l1 * l1;
    float l3 = l2 * l1;
    float w = pass.f0.x * min(l1, 32.0) + pass.f0.y * min(l2, 32.0) + pass.f0.z * min(l3, 32.0) + pass.f0.w * min(l3 * l1, 32.0);
    out_color = vec4(hdr ? SceneRadiance(x * w * scale, 1.0, true) : clamp(x * w, 0.0, 1.0), 1.0);
}
