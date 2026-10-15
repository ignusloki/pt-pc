#ifdef PT_IMAGES_READONLY
#define PT_IMAGE_ACCESS readonly
#else
#define PT_IMAGE_ACCESS
#endif
layout(set = 2, binding = 11, rgba32f) uniform PT_IMAGE_ACCESS image2D pt_rgba32[4];
layout(set = 2, binding = 12, rgba16f) uniform PT_IMAGE_ACCESS image2D pt_rgba16[11];

#define pt_reservoir_temporal pt_rgba32[0]
#define pt_reservoir_history0 pt_rgba32[1]
#define pt_reservoir_history1 pt_rgba32[2]
#define pt_reference_accum pt_rgba32[3]
#define pt_spec_unshadowed pt_rgba16[0]
#define pt_diffuse_moments0 pt_rgba16[1]
#define pt_diffuse_moments1 pt_rgba16[2]
#define pt_specular_moments0 pt_rgba16[3]
#define pt_specular_moments1 pt_rgba16[4]
#define pt_variance_ping pt_rgba16[5]
#define pt_variance_pong pt_rgba16[6]
#define pt_spare pt_rgba16[7]
#define pt_layer_raw pt_rgba16[8]
#define pt_layer_history0 pt_rgba16[9]
#define pt_layer_history1 pt_rgba16[10]

layout(std430, set = 2, binding = 13) readonly buffer PtLightRemap {
    uint pt_light_remap[];
};
