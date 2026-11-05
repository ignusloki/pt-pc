#include "perceptual_grain.glsl"

// grain.y packs the selected algorithm and the original ending variant:
// 0/1 = Original, 2/3 = Improved Perceptual. Other parameters retain their ABI.
vec3 ApplyFilmGrain(vec3 color, vec2 uv, vec2 pixel, vec4 grain, vec4 offset,
                   sampler2D noise_texture, float eotf, float peak, float wide) {
    if (grain.x <= 0.0 || grain.z <= 0.0) return color;
    if (grain.y >= 2.0) {
        return EncodeSdrReference(PerceptualFilmGrain(DecodeSdrReference(color, eotf),
            pixel, offset.xy, grain.z, peak, wide), eotf);
    }
    // Original port algorithm, including texture, animated phase and ending
    // response. Preserve its behavior when Original is selected.
    float y = abs(dot(color, vec3(0.299, 0.587, 0.114)));
    float alpha;
    if (grain.y > 0.0) {
        alpha = clamp(pow(y, 0.08) * (1.0 - pow(y, 0.12)) * grain.z * 0.45, 0.0, 1.0);
    } else {
        float p = pow(y, 0.7);
        alpha = clamp(p * (p * -grain.z * 0.45 + grain.z * 0.45), 0.0, 1.0);
    }
    float across = offset.z > 0.0 ? offset.z : 1.0;
    vec2 grain_uv = vec2((uv.x - 0.5) * across + 0.5, uv.y);
    vec3 noise = clamp(textureLod(noise_texture, 3.0 * (offset.xy + grain_uv), log2(1536.0 / 1080.0)).rgb, 0.0, 1.0);
    return mix(color, noise, alpha);
}
