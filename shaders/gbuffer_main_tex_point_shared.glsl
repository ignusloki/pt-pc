layout(location = 0) in vec2 in_uv0;
layout(location = 1) in vec2 in_uv1;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec3 in_tangent;
layout(location = 4) in vec3 in_bitangent;
layout(location = 5) in vec3 in_normal;
layout(location = 6) in vec3 in_view_position;
layout(location = 7) in vec3 in_world;
layout(location = 8) in vec3 in_view_dir;
layout(location = 9) in vec2 in_uv2;

layout(push_constant) uniform DrawPush {
    mat4 model;
    uvec4 ids;
    vec4 tint;
    vec4 aux0;
    vec4 aux1;
} draw;

layout(location = 0) out vec4 out_albedo;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;

float g_mip_bias = 0.0;

bool g_tile = false;
vec2 g_tile_dx = vec2(0.0);
vec2 g_tile_dy = vec2(0.0);
const float kTileEdge = 0.16;
const float kTileSpan = 0.68;
const float kTileBlend = 0.14;

vec4 TileTex(uint index, vec2 p) {
    vec2 f = fract(p);
    vec2 past = f + mix(mix(vec2(0.0), vec2(-1.0), greaterThan(f, vec2(1.0 - kTileBlend))), vec2(1.0), lessThan(f, vec2(kTileBlend)));
    vec2 w = 0.5 * (1.0 - smoothstep(vec2(0.0), vec2(kTileBlend), min(f, 1.0 - f)));
    vec2 a = kTileEdge + kTileSpan * f;
    vec2 b = kTileEdge + kTileSpan * past;
    vec4 mean = textureLod(PT_TEX2D(index), vec2(0.5), 12.0);
    vec4 aa = textureGrad(PT_TEX2D(index), a, g_tile_dx, g_tile_dy) - mean;
    vec4 ba = textureGrad(PT_TEX2D(index), vec2(b.x, a.y), g_tile_dx, g_tile_dy) - mean;
    vec4 ab = textureGrad(PT_TEX2D(index), vec2(a.x, b.y), g_tile_dx, g_tile_dy) - mean;
    vec4 bb = textureGrad(PT_TEX2D(index), b, g_tile_dx, g_tile_dy) - mean;
    float w00 = (1.0 - w.x) * (1.0 - w.y), w10 = w.x * (1.0 - w.y), w01 = (1.0 - w.x) * w.y, w11 = w.x * w.y;
    return mean + (aa * w00 + ba * w10 + ab * w01 + bb * w11) * inversesqrt(w00 * w00 + w10 * w10 + w01 * w01 + w11 * w11);
}

vec4 Tex(uint index, vec2 uv) {
    if (g_tile) {
        return TileTex(index, uv);
    }
    return texture(PT_TEX2D(index), uv, g_mip_bias);
}

vec4 TexPoint(uint index, vec2 uv) {
    ivec2 size = textureSize(PT_TEX2D(index), 0);
    ivec2 texel = ivec2(fract(uv) * vec2(size)) % size;
    return texelFetch(PT_TEX2D(index), texel, 0);
}

vec3 ViewReflection(vec3 albedo, vec3 n, vec3 view_dir, float specular, float reflection, float u) {
    vec4 color = Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(u, 0.25));
    vec4 param = Img(RES_MATERIAL, SMP_POINT_CLAMP, vec2(u, 0.75));
    vec3 r = mat3(frame.views[draw.ids.x].inv_view) * reflect(-view_dir, n);
    vec3 env = textureLod(PT_TEXCUBE(uint(draw.aux1.w)), r, 5.0 * (1.0 - reflection)).rgb;
    float k = param.z * (1.0 - reflection) + reflection;
    float amount = param.z * k;
    float keep = 1.0 - (param.z * (1.0 - specular) + specular) * amount;
    return albedo * keep + specular * amount * param.x * k * color.rgb * env;
}

float TearHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float TearNoise(vec2 p) {
    float sum = 0.0;
    float amp = 0.55;
    for (int i = 0; i < 3; ++i) {
        vec2 c = floor(p);
        vec2 f = fract(p);
        f = f * f * (3.0 - 2.0 * f);
        float a = TearHash(c), b = TearHash(c + vec2(1.0, 0.0)), d = TearHash(c + vec2(0.0, 1.0)), e = TearHash(c + vec2(1.0, 1.0));
        sum += amp * mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
        p *= 2.3;
        amp *= 0.45;
    }
    return sum / 0.95;
}

vec2 CreatureUv(vec2 t) {
    vec2 f = 1.0 - abs(1.0 - 2.0 * fract(0.5 * t));
    return vec2(0.04, 0.18) + vec2(0.46, 0.48) * f;
}
