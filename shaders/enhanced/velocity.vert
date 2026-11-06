#version 460
#include "velocity_skinned_shared.glsl"

void main() {
    View v = frame.views[draw.ids.x];
    vec3 position = in_position;
    vec3 previous = in_position;
    float weight_sum = dot(in_weights, vec4(1.0));
    uint previous_skin = floatBitsToUint(draw.aux[0].y);
    if (draw.ids.w != 0xFFFFFFFFu && weight_sum > 0.0) {
        position = Skinned(draw.ids.w, weight_sum);
        previous = previous_skin != 0xFFFFFFFFu ? Skinned(previous_skin, weight_sum) : position;
    }
    vec4 world = draw.model * vec4(position, 1.0);
    world.xyz += WaveOffset(draw.ids.y, in_uv0);
    gl_Position = v.view_projection * world;
    out_uv0 = in_uv0;
    // Rasterize with jitter, but measure physical motion without it.
    out_current = gl_Position;
    out_current.xy -= v.jitter.xy * out_current.w;
    vec4 previous_world = skin[floatBitsToUint(draw.aux[0].x)] * vec4(previous, 1.0) + vec4(WaveOffset(draw.ids.y, in_uv0), 0.0);
    if (draw.aux[0].z > 0.5) {
        out_previous = frame.views[draw.ids.z].view_projection * previous_world;
    } else {
        // Hold the camera fixed for both poses: retain rigid and skeletal motion
        // without camera translation, rotation or FOV changes. Do this BEFORE
        // perspective division/clamping, not by subtracting clamped velocities.
        out_previous = v.view_projection * previous_world;
        out_previous.xy -= v.jitter.xy * out_previous.w;
    }
}
