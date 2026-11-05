#version 460
#include "common.glsl"
#include "tonemap_curve.glsl"

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
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float across = pass.f0.w > 0.0 ? pass.f0.w : 1.0;
    vec2 uv = vec2((in_uv.x - 0.5) * across + 0.5, in_uv.y);
    float a = texture(PT_TEX2D(pass.ids.x & 0x3FFFFFFFu), pass.f0.xy + uv).a;
    float ink = 0.1 * clamp(pass.f0.z * a, 0.0, 1.0);

    vec3 hdr = ImgFetch(IMG_HDR, pixel).rgb * pass.f1.y;
    bool hdr_output = pass.f1.z > 0.0;
    vec3 flare = max(ImgFetch(IMG_FLARE, pixel).rgb, vec3(0.0));
    vec3 encoded = hdr_output ? SrgbEncode(SceneRadiance(hdr + flare, 1.0, true)) : SrgbEncode(SceneRadiance(max(hdr, vec3(0.0)), 1.0, false)) + flare;
    vec2 bloom_uv = hdr_output ? in_uv : in_uv + 0.5 / ImgSize(IMG_BLOOM_SUM);
    vec3 bloom = pass.f1.x > 0.0 ? max(Img(IMG_BLOOM_SUM, SMP_LINEAR_CLAMP, bloom_uv).rgb, vec3(0.0)) * pass.f1.x : vec3(0.0);
    if (!hdr_output) bloom = min(bloom, vec3(0.5));
    vec3 ink_contribution = hdr_output
        ? ScreenTonemapHdr(encoded + ink, bloom) - ScreenTonemapHdr(encoded, bloom)
        : ScreenTonemap(min(encoded + ink, vec3(1.0)), bloom) - ScreenTonemap(min(encoded, vec3(1.0)), bloom);
    vec3 c = ImgFetch(pass.ids.y, pixel).rgb + ink_contribution;
    c = pass.f1.z > 0.0 ? max(c, vec3(0.0)) : clamp(c, 0.0, 1.0);
    out_color = vec4(c, Luma601(c));
}
