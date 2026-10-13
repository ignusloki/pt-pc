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

// Catmull-Rom over a 4x4 texel footprint from nine bilinear fetches. The lens pass resamples the frame at 0.9 of its size,
// so its samples land at every phase between texels in a 10 pixel cycle; bilinear filtering is sharp at a texel and soft
// between two, which laid a fixed pattern of sharp and soft bands over the screen. The wet street's fine grain sliding under
// it showed as waves rising and falling on the floor with the walk, and with the fringe offsets as rainbow ellipses at the
// bottom of the screen (1.0.3 tests). Catmull-Rom's response hardly changes with the phase
vec3 SampleCatmullRom(uint source, vec2 uv) {
    vec2 size = ImgSize(source);
    vec2 position = uv * size;
    vec2 t1 = floor(position - 0.5) + 0.5;
    vec2 f = position - t1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (t1 - 1.0) / size;
    vec2 t3 = (t1 + 2.0) / size;
    vec2 t12 = (t1 + w2 / w12) / size;
    vec3 c = vec3(0.0);
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t0.x, t0.y)).rgb * w0.x * w0.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t12.x, t0.y)).rgb * w12.x * w0.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t3.x, t0.y)).rgb * w3.x * w0.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t0.x, t12.y)).rgb * w0.x * w12.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t12.x, t12.y)).rgb * w12.x * w12.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t3.x, t12.y)).rgb * w3.x * w12.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t0.x, t3.y)).rgb * w0.x * w3.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t12.x, t3.y)).rgb * w12.x * w3.y;
    c += Img(source, SMP_LINEAR_CLAMP, vec2(t3.x, t3.y)).rgb * w3.x * w3.y;
    return max(c, vec3(0.0));
}

// The frame softened over a square about three times `spread` across (nine bilinear fetches)
vec3 SampleSoft(uint source, vec2 uv, vec2 spread) {
    vec3 c = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            c += Img(source, SMP_LINEAR_CLAMP, uv + vec2(x, y) * spread).rgb;
        }
    }
    return c / 9.0;
}

vec3 SampleScreen(uint source, vec2 uv) {
    vec3 color;
    if (pass.f0.x > 0.0) {
        // the lens of the original's 1920x1080 frame: the radius is measured across its 16:9 frame (a wider window reaches past
        // it at the sides, where the curve has saturated), and the fringe offsets are in its pixels at any output size
        vec2 size = ImgSize(source);
        float across = (size.x / size.y) / (16.0 / 9.0);
        vec2 texel = vec2(1.0 / (1080.0 * size.x / size.y), 1.0 / 1080.0);
        vec2 p = (uv * 2.0 - 1.0) * vec2(across, 1.0);
        float r3 = Saturate(length(p) * 0.52240777);
        float r5 = 1.0 - sin(1.5707964 * r3);
        float r6 = inversesqrt(dot(p, p) + r5 * r5);
        float r7 = pow(r3, 1.75);
        vec2 offset = r7 * 84.0 * texel * p * r6;
        vec2 base = uv * 0.9 + 0.050000012;
        // The original takes each channel at its own offset (1.42, 1.275, 1.12), up to about twenty pixels apart at the corners.
        // With the port's sharp floor (anisotropic filtering, the upscaler, the fine texture detail) the channels landed on
        // different grains of the wet street, and the grain broke into rainbow noise in ellipses around the centre that moved
        // with the walk (1.0.3 tests). The picture is taken whole at the green offset, and the red and blue fringes are added
        // as the differences of a softened frame between their offsets and green's: edges, lamps and frames keep the
        // original's fringe, grain finer than the softening gets none
        vec2 uv_r = clamp(base + offset * 1.42, 0.0, 1.0);
        vec2 uv_g = clamp(base + offset * 1.275, 0.0, 1.0);
        vec2 uv_b = clamp(base + offset * 1.12, 0.0, 1.0);
        // the softening grows with the channels' distance (0.145 of the offset between neighbours), at least 1.5 of the
        // original's pixels
        vec2 spread = max(1.5 * texel, vec2(length(offset * 0.145 / texel)) * texel);
        color = SampleCatmullRom(source, uv_g);
        vec3 soft_g = SampleSoft(source, uv_g, spread);
        color.r = max(color.r + SampleSoft(source, uv_r, spread).r - soft_g.r, 0.0);
        color.b = max(color.b + SampleSoft(source, uv_b, spread).b - soft_g.b, 0.0);
    } else {
        color = ImgFetch(source, clamp(ivec2(uv*ImgSize(source)), ivec2(0), ivec2(ImgSize(source))-1)).rgb;
    }
    return color;
}

void main() {
    uint source=pass.ids.x;
    vec3 color=SampleScreen(source,in_uv);
    // Opt-in local-contrast sharpening; the native/original path remains unchanged at zero.
    if(pass.f1.x>0.0) {
        vec2 texel=1.0/ImgSize(source);
        vec3 a=SampleScreen(source,in_uv+vec2(-texel.x,0));
        vec3 b=SampleScreen(source,in_uv+vec2(texel.x,0));
        vec3 c=SampleScreen(source,in_uv+vec2(0,-texel.y));
        vec3 d=SampleScreen(source,in_uv+vec2(0,texel.y));
        vec3 lo=min(color,min(min(a,b),min(c,d))), mx=max(color,max(max(a,b),max(c,d)));
        vec3 sharpened=color+(color-(a+b+c+d)*.25)*pass.f1.x;
        color=clamp(sharpened,max(vec3(0),lo-(mx-lo)*.1),min(vec3(1),mx+(mx-lo)*.1));
    }
    // Stable sub-LSB dither at the final 8-bit scene conversion prevents coherent halo contours.
    vec3 seed = fract(vec3(gl_FragCoord.xyx) * vec3(0.1031, 0.1030, 0.0973));
    seed += dot(seed, seed.yxz + 33.33);
    float noise = fract((seed.x + seed.y) * seed.z) - 0.5;
    out_color = vec4(clamp(color + noise / 255.0, 0.0, 1.0), 1.0);
}
