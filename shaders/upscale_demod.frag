#version 460
#include "common.glsl"
#include "lighting.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_factor;

const float kBeamProbeDistance = 2.0;

float BeamFactor(ivec2 pixel) {
    if (pass.ids.y == 0xFFFFFFFFu) return 1.0;
    View v = frame.views[pass.ids.x];
    Light l = frame.lights[pass.ids.y];
    Surface s;
    vec2 frag = vec2(pixel) + 0.5;
    vec3 ray = vec3(PixelNdc(v, frag) * v.projection_param.xy, 1.0);
    s.P = normalize(ray) * kBeamProbeDistance;
    s.world = (v.inv_view * vec4(s.P, 1.0)).xyz;
    s.roughness = 1.0;
    s.specular = 0.0;
    s.material_u = 0.0;
    vec3 to_light = (v.view * vec4(l.position.xyz, 1.0)).xyz - s.P;
    s.N = to_light / max(length(to_light), 1.0e-6);
    s.translucency = 0.0;
    vec3 beam;
    vec3 specular;
    if (!EvaluateLight(l, v, s, false, frag, beam, specular)) return 1.0;
    return min(65504.0, 1.0 + Luma709(max(beam, vec3(0.0))) / pass.f0.x);
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 hdr = ImgFetch(IMG_HDR, pixel);
    float factor = BeamFactor(pixel);
    out_color = vec4(hdr.rgb / factor, hdr.a);
    out_factor = vec4(factor, 0.0, 0.0, 0.0);
}
