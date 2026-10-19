#ifndef RT_SKIN_SHADOW_RECEIVER_GLSL
#define RT_SKIN_SHADOW_RECEIVER_GLSL

const uint kRtSkinShadingModel = 10u;

bool RtSkinUsesNativeReceiver(uint shading_model, bool spot, bool authored_shadow, bool mirror_light, vec2 biases) {
    return shading_model == kRtSkinShadingModel && spot && authored_shadow && !mirror_light && (biases.x != 0.0 || biases.y != 0.0);
}

vec3 RtSkinNativeReceiver(vec3 world, vec3 view_forward, vec3 light_axis, vec2 biases) {
    return world + view_forward * biases.x + normalize(light_axis) * biases.y;
}

#endif
