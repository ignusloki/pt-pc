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

float Inverse(float x) {
    return x != 0.0 ? 1.0 / x : 1.0e30;
}

// The original's switch-on length of the blur (its squared length 1.1286062e-06, about two pixels at 1080p)
const float kSwitchOn = 1.0623588e-03;

void main() {
    uint image = pass.ids.x;
    uint vd = pass.ids.y;
    vec4 lp = pass.f0;
    vec2 uv = in_uv;
    vec4 center_vd = ImgLod(vd, SMP_POINT_CLAMP, uv, 0.0);
    vec2 dir = vec2(0.06666667 * lp.x * lp.w, 0.11851852 * lp.y * lp.w) * (2.0 * center_vd.zw - 1.0) * lp.z;
    bool street = pass.ids.z != 0u;
    // Street exploration (mouse look): the original blurs nothing below its switch-on length and the full length above it, at half resolution
    // laid over the frame. The port blurs at the frame's resolution and eases the switch on: no blur below half the length,
    // the full blur above one and a half times it, a blur growing from nothing between. The gate had made a contour between
    // a sharp and a half resolution soft floor that rose and fell with the walk, and a turn that crossed it at every change of
    // direction flickered the wet street's grain (1.0.3 tests; standing, the camera tilt's sway stays below it). Taking the
    // whole length off every blur instead had left almost none
    float full = length(dir) / max(lp.z, 1.0e-6);
    float kept = street ? full * smoothstep(0.5 * kSwitchOn, 1.5 * kSwitchOn, full) : full;
    dir *= full > 0.0 ? kept / full : 0.0;
    if ((street && kept <= 0.0) || (!street && dot(dir, dir) < 1.1286062e-06)) {
        vec4 c = ImgLod(image, SMP_POINT_CLAMP, uv, 0.0);
        out_color = vec4(c.rgb, lp.z >= 0.99 ? 0.0 : c.a);
        return;
    }
    vec4 center = ImgLod(image, SMP_POINT_CLAMP, uv, 0.0);
    // Indoors keeps MotionBlurMcGuire's seven point samples and centre weight 4 (PS4 shader 4a40d5c52ed6402a).
    // The street uses fifteen filtered taps shifted by a fixed fraction of a step: seven long-blur taps repeated bright detail.
    int kTaps = street ? 15 : 7;
    int kCentre = kTaps / 2;
    float jitter = street ? fract(52.9829189 * fract(0.06711056 * gl_FragCoord.x + 0.00583715 * gl_FragCoord.y)) - 0.5 : 0.0;
    vec2 step = street ? dir / float(kTaps - 1) : 0.16666667 * dir;
    vec2 start = step * (jitter - float(kCentre)) + uv;
    float d0 = 0.015625 * length((start - uv) * vec2(1920.0, 1080.0));
    float dstep = -0.015625 * length(step * vec2(1920.0, 1080.0));
    float zc = center_vd.y * center_vd.y;
    float inv_c = Inverse(center_vd.x);
    float kCentreWeight = 4.0 * float(kTaps - 1) / 6.0;
    vec3 sum = kCentreWeight * center.rgb;
    float weight = kCentreWeight;
    for (int k = 0; k < kTaps; ++k) {
        if (k == kCentre) {
            continue;
        }
        float fk = float(k);
        vec2 suv = fk * step + start;
        vec4 s_vd = ImgLod(vd, SMP_POINT_CLAMP, suv, 0.0);
        vec3 s = ImgLod(image, street ? SMP_LINEAR_CLAMP : SMP_POINT_CLAMP, suv, 0.0).rgb;
        float d = abs(fk * dstep + d0);
        float zs = s_vd.y * s_vd.y;
        float cone_s = 1.0 - d * Inverse(s_vd.x);
        float cone_c = 1.0 - d * inv_c;
        float front = Saturate((zc - zs) * 128.0 + 1.0);
        float back = Saturate((zs - zc) * 128.0 + 1.0);
        float w = 2.0 * Saturate(cone_s + 0.95) * Saturate(cone_c + 0.95) + front * Saturate(cone_s) + back * Saturate(cone_c);
        sum += s * w;
        weight += w;
    }
    vec3 c = sum * Inverse(weight);
    float alpha = street ? Saturate(0.25 * weight * 6.0 / float(kTaps - 1) - 1.2) : Saturate(0.25 * weight - 1.2);
    out_color = vec4(c, lp.z >= 0.99 ? alpha : center.a);
}
