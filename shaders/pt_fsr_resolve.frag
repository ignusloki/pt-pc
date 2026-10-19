#version 460
#include "common.glsl"
layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;
#define RT_FULL_IMAGES_READONLY 1
#include "rt_full_images.glsl"
layout(location = 0) out vec4 out_diffuse;
layout(location = 1) out vec4 out_specular;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 albedo = SrgbDecode(ImgFetch(IMG_ALBEDO, p).rgb);
    float exposure = frame.views[pass.ids.x].exposure.x;
    vec3 diffuse = imageLoad(rt_gi_ping, p).rgb + imageLoad(rt_gi_pong, p).rgb;
    vec3 specular = imageLoad(rt_glossy_ping, p).rgb + imageLoad(rt_glossy_pong, p).rgb;
    out_diffuse = vec4(diffuse / max(albedo, vec3(1.0e-4)) * exposure, 0.0);
    out_specular = vec4(specular * exposure, 0.0);
}
