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

void main() {
    vec4 sharp=OpticalLinearSample(pass.ids.x,in_uv);
    vec4 near=Img(pass.ids.y,SMP_LINEAR_CLAMP,in_uv);
    // CoC-aware bilinear reconstruction avoids dragging far blur over silhouettes.
    vec2 size=ImgSize(pass.ids.z),p=in_uv*size-0.5,f=fract(p);
    ivec2 base=ivec2(floor(p));
    vec3 far=vec3(0);float weight=0.0;
    for(int i=0;i<4;++i) {
        vec4 c=ImgFetch(pass.ids.z,clamp(base+ivec2(i&1,i>>1),ivec2(0),ivec2(size)-1));
        float delta=(c.a-sharp.a)*2.0*pass.f0.x;
        float w=((i&1)==0 ? 1.0-f.x : f.x)*((i&2)==0 ? 1.0-f.y : f.y)/(1.0+delta*delta);
        far+=c.rgb*w;weight+=w;
    }
    float coc=Saturate(2.0*sharp.a-1.0)*pass.f0.x;
    vec3 color=mix(sharp.rgb,far/max(weight,1e-6),smoothstep(0.0,1.5,coc));
    color=color*(1.0-pass.f0.y*Saturate(near.a))+pass.f0.y*near.rgb;
    out_color=vec4(SrgbEncode(max(color,vec3(0))),1);
}
