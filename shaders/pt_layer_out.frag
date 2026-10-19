#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) out vec4 out_color;

#define RT_FULL_IMAGES_READONLY 1
#include "rt_full_images.glsl"
#define PT_IMAGES_READONLY 1
#include "pt_images.glsl"

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (ImgFetch(IMG_DEPTH, pixel).x > 0.0) {
        discard;
    }
    vec4 layer = imageLoad(pt_layer_raw, pixel);
    float kind = floor(layer.a * 0.5);
    if (kind < 2.0) {
        discard;
    }
    float amount = Saturate(layer.a - 2.0 * kind);
    View v = frame.views[pass.ids.x];
    vec3 light = max(layer.rgb * amount * v.exposure.x, vec3(0.0));
    if (any(isnan(light)) || any(isinf(light))) {
        light = vec3(0.0);
    }
    out_color = vec4(light, amount);
}
