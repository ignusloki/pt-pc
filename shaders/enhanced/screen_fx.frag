#version 460
#include "common.glsl"
#include "post_filter.glsl"
#include "lens_mapping.glsl"
#include "lens_filter.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec3 SampleScreen(uint source, vec2 uv) {
    vec3 color;
    if (pass.f0.x > 0.0) {
        vec2 size = ImgSize(source);
        color = vec3(LensSample(source,uv,size,1.42,0),
                     LensSample(source,uv,size,1.275,1),
                     LensSample(source,uv,size,1.12,2));
        color = SrgbEncode(max(color,vec3(0.0)));
    } else {
        color = ImgFetch(source, clamp(ivec2(uv*ImgSize(source)), ivec2(0), ivec2(ImgSize(source))-1)).rgb;
    }
    return color;
}

void main() {
    uint source=pass.ids.x;
    vec3 color=SampleScreen(source,in_uv);
    if (pass.f1.x > 0.0) {
        vec2 texel = 1.0 / ImgSize(source);
        vec3 a = SampleScreen(source, in_uv + vec2(-texel.x, 0));
        vec3 b = SampleScreen(source, in_uv + vec2(texel.x, 0));
        vec3 c = SampleScreen(source, in_uv + vec2(0, -texel.y));
        vec3 d = SampleScreen(source, in_uv + vec2(0, texel.y));
        vec3 lo = min(color, min(min(a,b),min(c,d))), hi = max(color,max(max(a,b),max(c,d)));
        vec3 upper = hi + (hi-lo)*0.1;
        if (frame.hdr_output.x <= 0.5) upper = min(upper, vec3(1.0));
        color = clamp(color + (color-(a+b+c+d)*0.25)*pass.f1.x, max(vec3(0),lo-(hi-lo)*0.1), upper);
    }
    if(pass.f1.y<=0.0) {
        out_color=vec4(color,1.0);
        return;
    }
    vec3 seed = fract(vec3(gl_FragCoord.xyx) * vec3(0.1031, 0.1030, 0.0973));
    seed += dot(seed, seed.yxz + 33.33);
    float noise = fract((seed.x + seed.y) * seed.z) - 0.5;
    color += noise / 255.0;
    out_color = vec4(pass.f1.z > 0.0 ? max(color, vec3(0.0)) : clamp(color, 0.0, 1.0), 1.0);
}
