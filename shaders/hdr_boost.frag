#version 460
#include "common.glsl"
#include "hdr_shoulder.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec2 LensUv(vec2 uv) {
    if (pass.f0.x <= 0.0) {
        return uv;
    }
    vec2 size = ImgSize(IMG_HDR);
    float across = (size.x / size.y) / (16.0 / 9.0);
    vec2 texel = vec2(1.0 / (1080.0 * size.x / size.y), 1.0 / 1080.0);
    vec2 p = (uv * 2.0 - 1.0) * vec2(across, 1.0);
    float r3 = clamp(length(p) * 0.52240777, 0.0, 1.0);
    float r5 = 1.0 - sin(1.5707964 * r3);
    float r6 = inversesqrt(dot(p, p) + r5 * r5);
    float r7 = pow(r3, 1.75);
    vec2 offset = r7 * 84.0 * texel * p * r6;
    return clamp(uv * 0.9 + 0.050000012 + offset * 1.275, 0.0, 1.0);
}

void main() {
    vec2 uv = LensUv(in_uv);
    vec3 hdr;
    vec3 flare;
    if (pass.f0.x > 0.0) {
        hdr = Img(IMG_HDR, SMP_LINEAR_CLAMP, uv).rgb;
        flare = Img(IMG_FLARE, SMP_LINEAR_CLAMP, uv).rgb;
    } else {
        ivec2 pixel = ivec2(gl_FragCoord.xy);
        hdr = ImgFetch(IMG_HDR, pixel).rgb;
        flare = ImgFetch(IMG_FLARE, pixel).rgb;
    }
    hdr = mix(hdr, vec3(0.0), isnan(hdr));
    hdr = clamp(hdr * pass.f0.y, vec3(0.0), vec3(1.0e4));
    vec3 x = SrgbEncode(hdr) + max(flare, vec3(0.0));
    vec3 x2 = x * x;
    vec3 bloom = pass.f0.z > 0.0 ? min(vec3(0.5), Img(IMG_BLOOM_SUM, SMP_LINEAR_CLAMP, uv + 0.5 / ImgSize(IMG_BLOOM_SUM)).rgb) : vec3(0.0);
    vec3 r = x2 + bloom * max(1.0 - x2, 0.0);
    vec3 encoded = sqrt(r);
    float light = pow(max(encoded.r, max(encoded.g, encoded.b)), 2.2);
    out_color = vec4(HdrGain(light, pass.f0.w) - 1.0, 0.0, 0.0, 1.0);
}
