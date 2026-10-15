#ifdef RT_FULL_IMAGES_READONLY
#define RT_FULL_IMAGE_ACCESS readonly
#else
#define RT_FULL_IMAGE_ACCESS
#endif
layout(set = 2, binding = 9, rgba16f) uniform RT_FULL_IMAGE_ACCESS image2D rt_full_rgba[15];
layout(set = 2, binding = 10, r32f) uniform RT_FULL_IMAGE_ACCESS image2D rt_full_depth[2];

#define rt_full_unshadowed rt_full_rgba[0]
#define rt_full_shadowed rt_full_rgba[1]
#define rt_ratio_history0 rt_full_rgba[2]
#define rt_ratio_history1 rt_full_rgba[3]
#define rt_ratio_spatial rt_full_rgba[4]
#define rt_depth_history0 rt_full_depth[0]
#define rt_depth_history1 rt_full_depth[1]
#define rt_gi_raw rt_full_rgba[5]
#define rt_gi_history0 rt_full_rgba[6]
#define rt_gi_history1 rt_full_rgba[7]
#define rt_gi_ping rt_full_rgba[8]
#define rt_gi_pong rt_full_rgba[9]
#define rt_glossy_raw rt_full_rgba[10]
#define rt_glossy_history0 rt_full_rgba[11]
#define rt_glossy_history1 rt_full_rgba[12]
#define rt_glossy_ping rt_full_rgba[13]
#define rt_glossy_pong rt_full_rgba[14]
