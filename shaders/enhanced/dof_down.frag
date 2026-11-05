#version 460
#include "common.glsl"
#include "post_filter.glsl"
layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;
layout(location=0) in vec2 in_uv;
layout(location=0) out vec4 out_color;

// Signed CoC is carried separately in alpha. Select the near/far layer before
// reducing resolution; never average foreground colour into the far layer.
void main() {
    vec2 size=ImgSize(pass.ids.x);
    ivec2 base=ivec2(floor(gl_FragCoord.xy*pass.f2.zw*size-0.5));
    vec4 c[4];
    float reference=pass.ids.z==0u ? 0.0 : 1.0;
    for(int i=0;i<4;++i) {
        c[i]=ImgFetch(pass.ids.x,clamp(base+ivec2(i&1,i>>1),ivec2(0),ivec2(size)-1));
        c[i].rgb=SrgbDecode(max(c[i].rgb,vec3(0.0)));
        reference=pass.ids.z==0u ? max(reference,c[i].a) : min(reference,c[i].a);
    }
    vec3 sum=vec3(0.0);float weights=0.0, coc=0.0;
    for(int i=0;i<4;++i) {
        // Bilateral in signed CoC pixels (AMD FidelityFX DoF prefilter principle).
        float delta=(c[i].a-reference)*2.0*pass.f0.x;
        float w=1.0/(1.0+delta*delta);
        float amount=pass.ids.z==0u ? c[i].a : Saturate(1.0-2.0*c[i].a);
        sum+=c[i].rgb*w*(pass.ids.z==0u ? 1.0 : amount);
        coc+=amount*w; weights+=w;
    }
    out_color=vec4(sum/weights,coc/weights);
}
