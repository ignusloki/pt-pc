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

// Half-resolution disk gather. Far input alpha is signed CoC. Near input is
// premultiplied linear colour / coverage, preventing bright background leakage.
// Far blur rejects foreground/in-focus neighbours. Near blur scatters coverage
// by gathering SOURCE radii, so silhouettes can blur outwards into the background.
void main() {
    vec4 center=Img(pass.ids.x,SMP_POINT_CLAMP,in_uv);
    bool near=pass.ids.z!=0u;
    float maxRadius=pass.f0.x;
    float radius=near ? maxRadius : maxRadius*Saturate(2.0*center.a-1.0);
    if(radius<0.25) {
        out_color=center;
        return;
    }
    vec2 texel=1.0/ImgSize(pass.ids.x);
    int count=radius>6.0 ? 97 : 49;
    vec3 sum=vec3(0.0);float weights=0.0;
    // Equal-area, deterministic Vogel disk; no temporal noise after the upscaler.
    for(int i=0;i<count;++i) {
        float distance=radius*sqrt((float(i)+0.5)/float(count));
        float angle=float(i)*2.39996323;
        vec2 offset=distance*vec2(cos(angle),sin(angle));
        vec4 c=Img(pass.ids.x,SMP_LINEAR_CLAMP,in_uv+offset*texel);
        float amount=near ? c.a : Saturate(2.0*c.a-1.0);
        float w;
        if(near) {
            float sourceRadius=max(0.75,maxRadius*amount);
            float coverage=1.0-smoothstep(max(0.0,sourceRadius-0.5),sourceRadius+0.5,distance);
            w=amount*coverage/(sourceRadius*sourceRadius);
        } else {
            float delta=(c.a-center.a)*2.0*maxRadius;
            w=smoothstep(0.0,0.25,amount*maxRadius)/(1.0+delta*delta);
        }
        sum+=c.rgb*(near ? w/max(amount,1e-6) : w);weights+=w;
    }
    vec3 color=weights>1e-6 ? sum/weights : (near ? vec3(0) : center.rgb);
    float alpha=near ? Saturate(weights*radius*radius/float(count)) : center.a;
    out_color=vec4(near ? color*alpha : color,alpha);
}
