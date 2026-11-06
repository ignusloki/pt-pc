// Shared verbatim by the Original and Enhanced shader families.
void main() {
    View v = frame.views[pass.ids.x];
    vec2 size = vec2(textureSize(sss_light, 0));
    vec2 uv = gl_FragCoord.xy / size;
    vec3 light = LightAt(uv);
    if (!Skin(uv)) {
        out_color = vec4(light, 1.0);
        return;
    }
    bool horizontal = pass.ids.y == 1u;
    vec3 n = normalize(DecodeNormal(Img(IMG_NORMAL, SMP_POINT_CLAMP, uv).xyz));
    float depth = Img(IMG_DEPTH, SMP_POINT_CLAMP, uv).x;
    vec3 p = ViewPosition(v, PixelNdc(v, gl_FragCoord.xy), depth);
    float z = p.z;
    vec3 eye = -normalize(p);
    float base = sqrt(max(0.0, 0.000121 / (abs(eye.z) * eye.z * eye.z * (dot(p, p) + 1.0))));
    float cosine;
    float focal;
    float tan_y = v.projection_param.y;
    if (horizontal) {
        cosine = dot(normalize(n.xz), normalize(eye.xz));
        focal = 1.0 / tan(size.x / size.y * atan(tan_y));
    } else {
        cosine = dot(normalize(n.yz), normalize(eye.yz));
        focal = 1.0 / tan_y;
    }
    float half_width = abs(focal * abs(cosine) * base) * 0.5;
    vec3 sum = vec3(0.0);
    vec3 weight = vec3(0.0);
    for (int k = -6; k <= 6; ++k) {
        float offset = float(k) / 6.0 * half_width;
        vec2 tap = uv + (horizontal ? vec2(offset, 0.0) : vec2(0.0, offset));
        if (!Skin(tap)) {
            continue;
        }
        float dz = ViewZ(v, Img(IMG_DEPTH, SMP_POINT_CLAMP, tap).x) - z;
        float g = -dz * dz;
        vec3 w = vec3(kWeightR[abs(k)] * exp(217391.3 * g), kWeightG[abs(k)] * exp(357142.84 * g), kWeightB[abs(k)] * exp(454545.47 * g));
        sum += w * LightAt(tap);
        weight += w;
    }
    vec3 blurred = vec3(weight.x > 0.0 ? sum.x / weight.x : 0.0, weight.y > 0.0 ? sum.y / weight.y : 0.0,
                        weight.z > 0.0 ? sum.z / weight.z : 0.0);
    if (horizontal) {
        out_color = vec4(blurred, 1.0);
        return;
    }
    vec3 original = texture(sss_original, uv).rgb;
    float t = 2.0 + 1.0 / dot(n, eye);
    vec3 mixw = clamp(1.0 - t * vec3(0.15, 0.18, 0.2), 0.0, 1.0);
    out_color = vec4(original + mixw * (blurred - original), 1.0);
}
