#ifndef PT_HDR_COLOR_GLSL
#define PT_HDR_COLOR_GLSL
#include "composite_output.glsl"
#include "hdr_shoulder.glsl"

// Oklab: Björn Ottosson, public-domain reference matrices (2021).
// RenoDX uses the same perceptual chroma operation in colorgrade.hlsl.
vec3 GradeHdrScene(vec3 rgb, vec4 grade, vec4 chroma) {
    if (chroma.w < 0.5 || (all(equal(grade, vec4(0, 1, 1, 1))) &&
                          all(equal(chroma.xy, vec2(1))))) return rgb;
    rgb *= exp2(grade.x);
    float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    if (y <= 1e-10) return rgb;
    float adjusted = 0.18 * pow(y / 0.18, grade.y);
    // Numerical headroom guard: even maximal finite FP16 scene codes at
    // +4 EV and 200% contrast/highlights must stay finite before the shoulder.
    // This ceiling is many orders of magnitude beyond any display luminance.
    adjusted = min(adjusted, 1e12);
    // RenoDX Highlights version 4: smooth log-domain transition above gray.
    if (grade.z != 1.0) {
        float t = clamp(log2(max(adjusted / 0.18, 1.0)) / log2(1.0 / 0.18), 0.0, 1.0);
        t = t*t*t*(t*(t*6.0-15.0)+10.0);
        float curved = 0.18 * pow(adjusted / 0.18, grade.z > 1.0 ? grade.z : 2.0-grade.z);
        adjusted = grade.z > 1.0 ? mix(adjusted, curved, t) :
            adjusted * (adjusted / max(mix(adjusted, curved, t), 1e-20));
    }
    // RenoDX Shadows version 4; exact black and middle gray remain anchored.
    if (grade.w != 1.0 && adjusted > 0.0) {
        float ratio = adjusted / 0.18;
        float t = clamp(log2(max(ratio, 1e-20)) / -4.0, 0.0, 1.0);
        t = t*t*t*(t*(t*6.0-15.0)+10.0);
        float delta = adjusted * 0.18 / max(pow(ratio, grade.w > 1.0 ? grade.w : 2.0-grade.w), 1e-20) - 0.18*0.18;
        adjusted *= max(0.0, 1.0 + (grade.w > 1.0 ? delta : -delta)*t);
    }
    rgb *= adjusted / y;
    if (chroma.x != 1.0 || chroma.y != 1.0) {
        vec3 lms = vec3(dot(rgb,vec3(.4122214708,.5363325363,.0514459929)),
                        dot(rgb,vec3(.2119034982,.6806995451,.1073969566)),
                        dot(rgb,vec3(.0883024619,.2817188376,.6299787005)));
        lms = sign(lms)*pow(abs(lms),vec3(1.0/3.0));
        vec3 lab = vec3(dot(lms,vec3(.2104542553,.7936177850,-.0040720468)),
                        dot(lms,vec3(1.9779984951,-2.4285922050,.4505937099)),
                        dot(lms,vec3(.0259040371,.7827717662,-.8086757660)));
        float highlight = smoothstep(0.0, 4.0, log2(max(adjusted, 1.0)));
        lab.yz *= chroma.x * mix(1.0, chroma.y, highlight);
        lms = vec3(lab.x+.3963377774*lab.y+.2158037573*lab.z,
                   lab.x-.1055613458*lab.y-.0638541728*lab.z,
                   lab.x-.0894841775*lab.y-1.2914855480*lab.z);
        lms = lms*lms*lms;
        rgb = vec3(dot(lms,vec3(4.0767416621,-3.3077115913,.2309699292)),
                   dot(lms,vec3(-1.2684380046,2.6097574011,-.3413193965)),
                   dot(lms,vec3(-.0041960863,-.7034186147,1.7076147010)));
    }
    return rgb;
}

vec3 MapHdrScene(vec3 code, float brightness, float eotf, float peak, vec4 grade, vec4 chroma, vec4 extra) {
    vec3 linear_color = DecodeSdrReference(HdrSceneBrightness(code, brightness), eotf);
    linear_color = GradeHdrScene(linear_color, grade, chroma);
    // The released identity/BT.709 path remains numerically identical.
    if (chroma.z > 0.5 || (chroma.w > 0.5 &&
        (!all(equal(grade,vec4(0,1,1,1))) || !all(equal(chroma.xy,vec2(1))))))
        linear_color = CompressGamut(linear_color, chroma.z);
    else linear_color = max(linear_color, vec3(0));
    float highest = GamutPeak(linear_color, chroma.z);
    if (highest > 0.0) linear_color *= HdrShoulder(highest, peak) / highest;
    // Witcher 3 Classic/Remastered output extension: preserve display
    // luminance while blending to grayscale. 0 is identity, 1 is grayscale.
    if (chroma.w > 0.5 && extra.x > 0.0) {
        float y = max(dot(linear_color, vec3(0.2126, 0.7152, 0.0722)), 0.0);
        linear_color = mix(linear_color, vec3(y), clamp(extra.x, 0.0, 1.0));
    }
    return EncodeSdrReference(linear_color, eotf);
}
#endif
