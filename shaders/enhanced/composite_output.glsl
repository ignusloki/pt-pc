#ifndef PT_COMPOSITE_OUTPUT_GLSL
#define PT_COMPOSITE_OUTPUT_GLSL
#include "srgb.glsl"
#include "color_gamut.glsl"

// SDR display reference: 0 = sRGB, 1 = gamma 2.2, 2 = gamma 2.4.
// For linear sRGB input L, the gamma option is the RenoDX-style correction
// pow(SrgbEncode(L), gamma). Here the post chain already contains those codes.
vec3 DecodeSdrReference(vec3 code, float eotf) {
    return sign(code) * (eotf > 0.5 ? pow(abs(code), vec3(eotf > 1.5 ? 2.4 : 2.2)) : SrgbDecode(abs(code)));
}

vec3 EncodeSdrReference(vec3 linear_color, float eotf) {
    return sign(linear_color) * (eotf > 0.5 ? pow(abs(linear_color), vec3(1.0 / (eotf > 1.5 ? 2.4 : 2.2))) : SrgbEncode(abs(linear_color)));
}

// Paired with the scene/display shoulder, not applied to already mapped UI.
vec3 HdrSceneBrightness(vec3 code, float brightness) {
    // Numerical overflow guard only: this extended code ceiling is far beyond
    // display range and is compressed by the shoulder before any FP16 write.
    return sign(code) * min(pow(abs(code), vec3(1.0 / max(brightness, 0.1))), vec3(65504.0));
}

float PqEncode(float nits) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float y = pow(clamp(nits / 10000.0, 0.0, 1.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

vec3 CompositeDisplayColor(vec3 color, float brightness, float output_mode, vec2 hdr, float sdr_eotf, float wide) {
    if (output_mode <= 0.5) {
        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / brightness));
    }
    if (output_mode > 0.5) {
        // Extended scene RGB has already been display-mapped before the UI.
        // Alpha is coverage; never use it to reconstruct highlight brightness.
        // Decode once to the selected SDR display reference. The gamma option
        // emulates the SDR monitor's EOTF; it is not a second texture decode.
        color = DecodeSdrReference(color, sdr_eotf);
        // Guard FP16 rounding / overlay overflow only, never the scene tonemapper.
        color = LimitOutputGamut(color, wide, hdr.y / hdr.x);
        if (output_mode < 1.5) {
            return color * (hdr.x / 80.0);
        }
        vec3 bt2020 = mat3(0.627404, 0.069097, 0.0163916,
                           0.329283, 0.919540, 0.0880132,
                           0.0433136, 0.0113612, 0.895595) * (color * hdr.x);
        return vec3(PqEncode(bt2020.r), PqEncode(bt2020.g), PqEncode(bt2020.b));
    }
    return color;
}

vec3 CompositeDisplayColor(vec3 color, float brightness, float output_mode, vec2 hdr, float sdr_eotf) {
    return CompositeDisplayColor(color, brightness, output_mode, hdr, sdr_eotf, 0.0);
}

vec4 CompositeUiLayer(vec3 shown, vec3 base, float tolerance, float signed_color) {
    vec3 change = shown - base;
    if (all(lessThanEqual(abs(change), vec3(tolerance)))) {
        return vec4(signed_color > 0.5 ? change : max(change, vec3(0.0)), 0.0);
    }
    float a = max(abs(change.r), max(abs(change.g), abs(change.b)));
    for (int c = 0; c < 3; ++c) {
        if (base[c] > 0.0) {
            a = max(a, 1.0 - shown[c] / base[c]);
        }
    }
    a = clamp(a, 0.0, 1.0);
    // scRGB wide colors can have negative channels, including premultiplied UI.
    vec3 layer = shown - (1.0 - a) * base;
    return vec4(signed_color > 0.5 ? layer : max(layer, vec3(0.0)), a);
}

vec4 CompositeUiLayer(vec3 shown, vec3 base, float tolerance) {
    return CompositeUiLayer(shown, base, tolerance, 0.0);
}

#endif
