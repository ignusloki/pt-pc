// Shared verbatim by the Original and Enhanced shader families.
layout(set = 0, binding = 0) uniform PT_TEXTURE2D_ARRAY textures[];
layout(set = 0, binding = 2) uniform PT_TEXTURECUBE_ARRAY cube_textures[];
layout(set = 1, binding = 1) uniform sampler2D depth_texture;
layout(set = 1, binding = 2) uniform sampler2D scene_copy;
layout(set = 1, binding = 3) uniform VfxFog {
    vec4 fog[8];
    uvec4 mode;
} fog_block;

layout(push_constant) uniform VfxPush {
    mat4 view_projection;
    vec4 eye;
    vec4 frame;
    uvec4 ids;
} push;

layout(location = 0) in vec4 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_params;
layout(location = 3) in vec4 in_luminance;
layout(location = 4) flat in uvec4 in_info;
layout(location = 5) in float in_view_depth;
layout(location = 6) flat in vec4 in_extra;
layout(location = 7) in vec3 in_world;
layout(location = 8) flat in vec3 in_tangent;
layout(location = 9) flat in vec3 in_bitangent;
layout(location = 10) flat in vec3 in_normal;
layout(location = 11) flat in vec4 in_rain_rotation;
layout(location = 12) flat in vec4 in_light_position0;
layout(location = 13) flat in vec4 in_light_position1;
layout(location = 14) flat in vec4 in_light_position2;
layout(location = 15) flat in vec4 in_light_color0;
layout(location = 16) flat in vec4 in_light_color1;
layout(location = 17) flat in vec4 in_light_color2;
layout(location = 18) flat in vec4 in_light_factors;

layout(location = 0) out vec4 out_color_near;
layout(location = 1) out vec4 out_color_far;

const uint kSoft = 1u;
const uint kExposure = 2u;
const uint kLuminance = 4u;
const uint kLiquid = 8u;
const uint kScreen = 16u;
const uint kAnimeBlend = 32u;
const uint kFlare = 64u;
const uint kLiquidHnm = 128u;
const uint kClip = 256u;
const uint kRain = 1024u;
const uint kLit = 2048u;
const uint kNoTexture = 0xFFFFFFFFu;

const uint kBlendAlpha = 0u;
const uint kBlendAdd = 1u;
const uint kBlendSub = 2u;
const uint kBlendMul = 3u;
const uint kBlendMin = 4u;
const uint kBlendOpaque = 5u;

vec3 SrgbToLinear(vec3 c) {
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045)));
}

vec3 LinearToSrgb(vec3 c) {
    return mix(1.055 * pow(max(c, vec3(1e-5)), vec3(1.0 / 2.4)) - 0.055, c * 12.92, lessThanEqual(c, vec3(0.0031308)));
}

vec3 LitTextureToLinear(vec3 c) {
    vec3 linear = c / 12.92;
    vec3 nonlinear = pow(max(vec3(1.0e-5), 0.94786733 * (vec3(0.055) + c)), vec3(2.4));
    return mix(nonlinear, linear, lessThanEqual(c, vec3(0.03928)));
}

vec3 PointLight(vec4 position, vec4 color) {
    if (dot(color.rgb, color.rgb) < 1.0e-12) {
        return vec3(0.0);
    }
    vec3 delta = position.xyz - in_world;
    float distance_to_light = max(length(delta), 0.01);
    float distance_squared = distance_to_light * distance_to_light;
    float attenuation = max(0.0, 1.0 / distance_squared - distance_squared * position.w);
    return color.rgb * attenuation;
}

vec3 LiquidPointLight(vec4 position, vec4 color, vec3 n, float t) {
    if (dot(color.rgb, color.rgb) < 1.0e-12) {
        return vec3(0.0);
    }
    vec3 delta = position.xyz - in_world;
    float distance_to_light = max(length(delta), 0.010002136);
    float distance_squared = distance_to_light * distance_to_light;
    float attenuation = clamp(1.0 / distance_squared - distance_squared * position.w, 0.0, 1.0);
    float diffuse = (1.0 - t) * clamp((1.0 - t) * dot(n, delta / distance_to_light) + t, 0.0, 1.0);
    return color.rgb * attenuation * diffuse;
}

float LinearDepth(float d) {
    return push.frame.y / max(d, 1e-7);
}

vec3 ReflectionSample(uint slot, vec3 direction) {
    return texture(PT_TEXCUBE(slot), direction).rgb;
}

vec4 LitSample(uint slot, vec2 uv, float computed_lod) {
    if (any(lessThan(uv, vec2(-1.0e-4))) || any(greaterThan(uv, vec2(1.0001)))) {
        return textureLod(PT_TEX2D(slot), uv, computed_lod);
    }
    int level_count = max(textureQueryLevels(PT_TEX2D(slot)), 1);
    float lod = clamp(computed_lod, 0.0, float(level_count - 1));
    int low_level = int(floor(lod));
    int high_level = min(low_level + 1, level_count - 1);
    vec2 low_size = vec2(textureSize(PT_TEX2D(slot), low_level));
    vec2 high_size = vec2(textureSize(PT_TEX2D(slot), high_level));
    vec2 low_uv = clamp(uv, 0.5 / low_size, 1.0 - 0.5 / low_size);
    vec2 high_uv = clamp(uv, 0.5 / high_size, 1.0 - 0.5 / high_size);
    vec4 low_sample = textureLod(PT_TEX2D(slot), low_uv, float(low_level));
    vec4 high_sample = textureLod(PT_TEX2D(slot), high_uv, float(high_level));
    return mix(low_sample, high_sample, fract(lod));
}

float FoldScreen(float u) {
    if (u >= 0.995) {
        u = 0.995 - fract(u / 0.995) * 0.995;
    }
    return abs(u);
}

float SceneDepth(vec2 pixel) {
    return LinearDepth(texture(depth_texture, pixel * push.frame.zw).r);
}
