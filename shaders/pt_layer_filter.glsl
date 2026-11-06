// Shared verbatim by the Original and Enhanced shader families.
bool PreviousSecond() {
    return (pass.ids.w & 1u) != 0u;
}

void Temporal(ivec2 pixel, ivec2 size) {
    vec4 raw = imageLoad(pt_layer_raw, pixel);
    float kind = floor(raw.a * 0.5);
    bool second = !PreviousSecond();
    float depth = ImgFetch(IMG_DEPTH, pixel).x;
    if ((uint(pass.f1.w + 0.5) & 4u) != 0u && depth > 0.0) {
        vec4 value = vec4(raw.rgb, 1.0);
        if (second) imageStore(pt_layer_history1, pixel, value); else imageStore(pt_layer_history0, pixel, value);
        return;
    }
    bool coat = kind == 1.0 && raw.a > 2.0;
    vec2 ndc = coat ? PixelNdc(v, vec2(pixel) + 0.5) : (vec2(pixel) + 0.5) * v.viewport.zw * 2.0 - 1.0;
    float t_layer = 0.0;
    if (kind >= 2.0 && depth <= 0.0) {
        vec3 dir = normalize(mat3(v.inv_view) * normalize(vec3(ndc * v.projection_param.xy, 1.0)));
        RtSurfaceHit hit;
        t_layer = -1.0;
        if (!FindGlass(v.eye.xyz, dir, 200.0, t_layer, hit)) {
            t_layer = 0.0;
        }
    }
    if (kind < 1.0 || (depth <= 0.0 && t_layer <= 0.0)) {
        if (second) imageStore(pt_layer_history1, pixel, vec4(raw.rgb, 1.0)); else imageStore(pt_layer_history0, pixel, vec4(raw.rgb, 1.0));
        return;
    }
    float roughness = depth > 0.0 ? ImgFetch(IMG_MATERIAL, pixel).x : 0.0;
    float max_frames = kind >= 3.0 ? 6.0 : kind >= 2.0 ? 16.0 : mix(8.0, 24.0, Saturate(roughness / 0.5));
    vec3 lo = raw.rgb;
    vec3 hi = raw.rgb;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec4 c = imageLoad(pt_layer_raw, clamp(pixel + ivec2(x, y), ivec2(0), size - 1));
            if (floor(c.a * 0.5) == kind) {
                lo = min(lo, c.rgb);
                hi = max(hi, c.rgb);
            }
        }
    }
    vec3 spread = hi - lo;
    lo = max(vec3(0.0), lo - 0.5 * spread);
    hi = hi + 0.5 * spread;
    vec3 world;
    if (depth > 0.0) {
        float z = ViewZ(v, depth);
        world = (v.inv_view * vec4(ndc * v.projection_param.xy * z, z, 1.0)).xyz;
    } else {
        world = v.eye.xyz + normalize(mat3(v.inv_view) * normalize(vec3(ndc * v.projection_param.xy, 1.0))) * t_layer;
    }
    bool reflected = kind >= 2.0;
    if (reflected) {
        vec3 dir = normalize(mat3(v.inv_view) * normalize(vec3(ndc * v.projection_param.xy, 1.0)));
        float t_end = depth > 0.0 ? LayerEnd(length(world - v.eye.xyz)) : 200.0;
        float t_hit = -1.0;
        RtSurfaceHit hit;
        if (kind >= 3.0) {
            if (!FindMirror(v.eye.xyz, dir, t_end, t_hit, hit)) {
                t_hit = -1.0;
            }
        } else if (!FindGlass(v.eye.xyz, dir, t_end, t_hit, hit)) {
            t_hit = -1.0;
        }
        if (t_hit > 0.0) {
            vec3 p = v.eye.xyz + dir * t_hit;
            vec3 r = reflect(dir, hit.normal);
            float t_far = 100.0;
            rayQueryEXT rq;
            bool mirror_view = kind >= 3.0;
            rayQueryInitializeEXT(rq, rt_casters, mirror_view ? gl_RayFlagsNoOpaqueEXT : gl_RayFlagsOpaqueEXT,
                                  kPtSurfaceMask | 2u, p + hit.normal * 0.002, 0.0, r, t_far);
            while (rayQueryProceedEXT(rq)) {
                if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
                    uint record = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
                    uint flags = rt_records[record].flags;
                    if ((flags & 1024u) == 0u &&
                        ((flags & 9u) == 0u || PtCasterOpaque(record, rayQueryGetIntersectionPrimitiveIndexEXT(rq, false),
                                                            rayQueryGetIntersectionBarycentricsEXT(rq, false), 0.5))) {
                        rayQueryConfirmIntersectionEXT(rq);
                    }
                }
            }
            if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
                t_far = rayQueryGetIntersectionTEXT(rq, true);
            }
            world = v.eye.xyz + dir * (t_hit + t_far);
        } else {
            reflected = false;
        }
    }
    if (coat) {
        float t_far = imageLoad(pt_layer_geometry, pixel).w;
        vec3 dir = normalize(world - v.eye.xyz);
        world += dir * t_far;
        reflected = true;
    }
    vec3 history = vec3(0.0);
    float count = 0.0;
    vec4 previous = pass.m * vec4(world, 1.0);
    if ((pass.ids.w & 2u) == 0u && previous.w > 1.0e-4) {
        vec2 at = ((previous.xy / previous.w - (coat ? vec2(0.0) : pass.f2.zw)) * 0.5 + 0.5) * v.viewport.xy - 0.5;
        ivec2 base = ivec2(floor(at));
        vec2 f = at - vec2(base);
        float weights[4] = float[4]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
        ivec2 offsets[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
        float sum = 0.0;
        vec3 color_sum = vec3(0.0);
        float count_sum = 0.0;
        for (int i = 0; i < 4; ++i) {
            ivec2 p = base + offsets[i];
            if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) {
                continue;
            }
            float previous_z = PreviousSecond() ? imageLoad(rt_depth_history1, p).x : imageLoad(rt_depth_history0, p).x;
            if (reflected) {
            } else if (depth <= 0.0) {
                if (previous_z > 0.0) {
                    continue;
                }
            } else if (!(previous_z > 0.0) || abs(previous_z - previous.w) > 0.03 * previous.w) {
                continue;
            }
            vec4 h = PreviousSecond() ? imageLoad(pt_layer_history1, p) : imageLoad(pt_layer_history0, p);
            sum += weights[i];
            color_sum += weights[i] * h.rgb;
            count_sum += weights[i] * h.a;
        }
        if (sum > 0.05) {
            history = clamp(color_sum / sum, lo, hi);
            count = count_sum / sum;
        }
    }
    if (kind < 2.0 && raw.a <= 2.0) {
        vec4 kept = vec4(history, count);
        if (second) imageStore(pt_layer_history1, pixel, kept); else imageStore(pt_layer_history0, pixel, kept);
        return;
    }
    count = min(count + 1.0, max_frames);
    vec4 result = vec4(mix(history, raw.rgb, 1.0 / count), count);
    if (second) imageStore(pt_layer_history1, pixel, result); else imageStore(pt_layer_history0, pixel, result);
}

void Spatial(ivec2 pixel, ivec2 size) {
    vec4 raw = imageLoad(pt_layer_raw, pixel);
    float kind = floor(raw.a * 0.5);
    bool written_second = !PreviousSecond();
    vec4 center = written_second ? imageLoad(pt_layer_history1, pixel) : imageLoad(pt_layer_history0, pixel);
    float depth = ImgFetch(IMG_DEPTH, pixel).x;
    bool background = depth <= 0.0;
    if ((uint(pass.f1.w + 0.5) & 4u) != 0u && !background) return;
    if (kind < 1.0 || (background && kind < 2.0)) {
        imageStore(pt_layer_raw, pixel, vec4(center.rgb, raw.a));
        return;
    }
    bool pane = kind >= 2.0 && kind < 3.0;
    float z = background ? 1.0 : ViewZ(v, depth);
    vec3 n = background ? vec3(0.0, 0.0, -1.0) : DecodeNormal(ImgFetch(IMG_NORMAL, pixel).xyz);
    float roughness = background ? 0.0 : ImgFetch(IMG_MATERIAL, pixel).x;
    int radius = kind >= 3.0 ? 1 : kind >= 2.0 ? 2 : (roughness > 0.6 ? 2 : 1);
    float l_center = Luma709(center.rgb);
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            ivec2 p = clamp(pixel + ivec2(x, y), ivec2(0), size - 1);
            float code = imageLoad(pt_layer_raw, p).a;
            if (floor(code * 0.5) != kind) {
                continue;
            }
            if (kind < 2.0 && code <= 2.0 && (x != 0 || y != 0)) {
                continue;
            }
            float dk = ImgFetch(IMG_DEPTH, p).x;
            if (!pane && (dk <= 0.0) != background) {
                continue;
            }
            vec3 c = written_second ? imageLoad(pt_layer_history1, p).rgb : imageLoad(pt_layer_history0, p).rgb;
            float w = exp(-float(x * x + y * y) / 4.0);
            if (x != 0 || y != 0) {
                if (!background && !pane) {
                    w *= exp(-abs(ViewZ(v, dk) - z) / (0.01 * z + 0.002));
                    w *= pow(max(dot(n, DecodeNormal(ImgFetch(IMG_NORMAL, p).xyz)), 0.0), 32.0);
                }
                w *= exp(-abs(Luma709(c) - l_center) / (0.5 * l_center + 1.0e-4));
            }
            sum += w * c;
            weight_sum += w;
        }
    }
    imageStore(pt_layer_raw, pixel, vec4(weight_sum > 1.0e-6 ? sum / weight_sum : center.rgb, raw.a));
}

void main() {
    v = frame.views[pass.ids.x];
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(v.viewport.xy);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }
    if (pass.ids.z == 0u) {
        Trace(pixel, size);
    } else if (pass.ids.z == 1u) {
        Temporal(pixel, size);
    } else {
        Spatial(pixel, size);
    }
}
