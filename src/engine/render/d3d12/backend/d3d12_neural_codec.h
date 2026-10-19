#pragma once

inline constexpr char kNativeNrCodec[] = R"hlsl(
Texture2D<float4> Input0 : register(t0);
Texture2D<float4> Input1 : register(t1);
RWTexture2D<float4> Destination : register(u0);
// the scene's motion (the previous position minus this one, in render UV), for a frame that reuses the last network answer
Texture2D<float2> Motion : register(t2);
// the network's change kept over time (its gain over the frame, TAA-like): this frame's and the last one's, swapped each frame
RWTexture2D<float4> HistoryOut : register(u1);
RWTexture2D<float4> HistoryIn : register(u2);
SamplerState Linear : register(s0);
// extent: the pass's own size (Encode: the network's, Resolve: the frame's); other: the other one (Encode reads the frame, Resolve
// the network's answer). The network runs smaller than the frame on a reduced input (upscaling.neural_quality)
// reuse: this frame did not run the network; the resolve takes the last answer from where this pixel was then
// history: the last frame's gain is there to blend with (0 on the first frame the network runs, or after a reset)
// delta: the network's change carried as a change (a smaller network, delta_transfer below), not as the calibrated transfer
cbuffer Settings : register(b0) { uint2 extent; uint2 other; float white_scale; float reuse; float history; float delta; float boost; float3 reserved; };
float Luma(float3 c) { return dot(c, float3(0.212639, 0.715169, 0.072192)); }
float3 DisplayEncode(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
float3 DisplayDecode(float3 c) {
    c = saturate(c);
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float3 ToLab(float3 c) {
    const float3x3 a = {
        0.4122214708, 0.5363325363, 0.0514459929,
        0.2119034982, 0.6806995451, 0.1073969566,
        0.0883024619, 0.2817188376, 0.6299787005 };
    const float3x3 b = {
        0.2104542553, 0.7936177850, -0.0040720468,
        1.9779984951, -2.4285922050, 0.4505937099,
        0.0259040371, 0.7827717662, -0.8086757660 };
    const float3 x = mul(a, c);
    return mul(b, sign(x) * pow(abs(x), 1.0 / 3.0));
}
float3 FromLab(float3 c) {
    const float3x3 a = {
        1.0, 0.3963377774, 0.2158037573,
        1.0, -0.1055613458, -0.0638541728,
        1.0, -0.0894841775, -1.2914855480 };
    const float3x3 b = {
        4.0767416621, -3.3077115913, 0.2309699292,
        -1.2684380046, 2.6097574011, -0.3413193965,
        -0.0041960863, -0.7034186147, 1.7076147010 };
    const float3 x = mul(a, c);
    return mul(b, x * x * x);
}
float3 PositiveAp1(float3 c) {
    const float3x3 a = {
        0.613097, 0.339523, 0.047379,
        0.070194, 0.916354, 0.013452,
        0.020616, 0.109570, 0.869815 };
    const float3x3 b = {
        1.705051, -0.621792, -0.083259,
        -0.130256, 1.140805, -0.010548,
        -0.024003, -0.128969, 1.152972 };
    return mul(b, max(0.0, mul(a, c)));
}
float3 Transfer(float3 original, float3 proxy, float3 neural) {
    const float original_y = Luma(original);
    const float proxy_y = Luma(proxy);
    const float neural_y = Luma(neural);
    float3 result = original;
    if (neural_y > 1e-5) {
        float ratio = original_y / max(proxy_y, 1e-6);
        if (original_y >= proxy_y) ratio = (neural_y + max(0.0, original_y - proxy_y)) / neural_y;
        float3 changed = ToLab(neural * ratio);
        const float3 reference = ToLab(neural);
        const float chroma = length(reference.yz);
        changed.yz = reference.yz * (chroma == 0.0 ? 1.0 : length(changed.yz) / chroma);
        result = PositiveAp1(FromLab(changed));
    }
    return result;
}
// the proxy's tone: linear up to 0.75, then a soft shoulder to 1
float3 Shoulder(float3 c) {
    const float3 shoulder = 0.75 + 0.25 * (1.0 - exp(-5.770780 * (c - 0.75)));
    return c <= 0.75 ? c : shoulder;
}
// A smaller network's change, carried onto the full frame as a change (Oklab): the frame's lightness times the network's gain
// over its proxy, and the frame's own colour plus the network's colour shift. The calibrated transfer takes the network's colour
// whole and the frame's luminance over a proxy that is blurred at a smaller size, which shimmers on edges and fine texture from
// frame to frame; this keeps every edge the frame's own.
float3 DeltaTransfer(float3 original, float3 proxy, float3 neural) {
    const float3 o = ToLab(original), p = ToLab(proxy), n = ToLab(neural);
    const float scale = o.x / max(p.x, 1e-3);
    const float lightness = o.x * clamp(n.x / max(p.x, 1e-3), 0.0, 4.0);
    const float2 colour = o.yz + (n.yz - p.yz) * min(scale, 4.0);
    return PositiveAp1(FromLab(float3(lightness, colour)));
}
[numthreads(16,16,1)] void Encode(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= extent)) return;
    // the frame's linear light averaged over the network pixel's footprint: four bilinear taps a quarter of the footprint less a
    // pixel from its centre (an exact 2x2 box at half size, close to a 4x4 one at a quarter); at the full size all four are the
    // pixel itself
    const float2 uv = (float2(id.xy) + 0.5) / float2(extent);
    const float2 reach = 0.25 * (float2(other) / float2(extent) - 1.0) / float2(other);
    float3 c = 0.0;
    if (all(extent == other)) c = max(Input0.Load(int3(id.xy, 0)).rgb, 0.0) * 4.0;
    else [unroll] for (int n = 0; n < 4; ++n) {
        const float2 offset = float2((n & 1) ? 1.0 : -1.0, (n & 2) ? 1.0 : -1.0) * reach;
        c += max(Input0.SampleLevel(Linear, uv + offset, 0).rgb, 0.0);
    }
    c = c * 0.25 / white_scale;
    Destination[id.xy] = float4(DisplayEncode(Shoulder(c)), 1.0);
}
[numthreads(16,16,1)] void Resolve(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= extent)) return;
    const float4 original = Destination[id.xy];
    // the network's proxy and answer at this pixel, bilinear from the network's size: the transfer keeps the frame's own detail
    // (its luminance over the proxy's) and takes the network's change, so a smaller network costs only sharpness in that change
    const float2 here = (float2(id.xy) + 0.5) / float2(extent);
#ifdef NR_CURRENT_FRAME
    const float2 motion = 0.0;
#else
    const float2 motion = Motion.SampleLevel(Linear, here, 0);
#endif
    float2 uv = here;
    if (reuse > 0.5) uv += motion;
    const float3 proxy = DisplayDecode(Input0.SampleLevel(Linear, uv, 0).rgb);
    const float3 neural = DisplayDecode(Input1.SampleLevel(Linear, uv, 0).rgb);
    const float3 frame = max(original.rgb, 0.0) / white_scale;
    float3 result = delta > 0.5 ? DeltaTransfer(frame, proxy, neural) : Transfer(frame, proxy, neural);
    if (boost > 1.0) {
        // Amplify neural lighting/detail while retaining its colour. Per-channel HDR subtraction clips individual channels
        // below zero at high strength and creates coloured fringes around bright skin and lamps.
        const float before_y = Luma(frame), after_y = Luma(result);
        const float boosted_y = max(before_y + (after_y - before_y) * boost, 0.0);
        result *= boosted_y / max(after_y, 1e-6);
    }
    if (reuse > 0.5) {
        // the last answer moved here is trusted only where its proxy still matches this frame (TAA's rejection): a pixel just
        // uncovered, off screen then, or with motion that does not hold keeps the frame as it is for this one frame
        const float distance = length(ToLab(Shoulder(frame)) - ToLab(proxy));
        float weight = 1.0 - saturate((distance - 0.03) / 0.09);
        if (any(uv < 0.0) || any(uv > 1.0)) weight = 0.0;
        result = lerp(frame, result, weight);
    }
    // the change kept over time: the network's answer flickers from frame to frame where it is unsure (most at a reduced size,
    // on lamps and bright edges); its gain over the frame is blended with the last frame's, moved along the motion, a quarter new
    // each frame. A gain that changes a lot (a pixel just uncovered, a light switched, off screen then) takes the new one at once.
    const float3 gain = min(result / max(frame, 1e-3), 8.0);
    float3 kept = gain;
#ifndef NR_CURRENT_FRAME
    if (history > 0.5) {
        const float2 from = here + motion;
        if (all(from >= 0.0) && all(from <= 1.0)) {
            const float2 at = from * float2(extent) - 0.5;
            const int2 base = int2(floor(at));
            const float2 f = at - float2(base);
            const int2 last = int2(extent) - 1;
            const float3 h00 = HistoryIn[clamp(base, 0, last)].rgb, h10 = HistoryIn[clamp(base + int2(1, 0), 0, last)].rgb;
            const float3 h01 = HistoryIn[clamp(base + int2(0, 1), 0, last)].rgb, h11 = HistoryIn[clamp(base + int2(1, 1), 0, last)].rgb;
            const float3 before = lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
            const float now_y = Luma(gain), before_y = Luma(before);
            const float jump = abs(now_y - before_y) / max(max(now_y, before_y), 0.05);
            const float fresh = lerp(0.25, 1.0, saturate((jump - 0.2) / 0.25));
            kept = lerp(before, gain, fresh);
        }
    }
    HistoryOut[id.xy] = float4(kept, 1.0);
#endif
    Destination[id.xy] = float4(frame * kept * white_scale, original.a);
}
)hlsl";
