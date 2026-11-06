#version 460
#extension GL_EXT_nonuniform_qualifier : require

#include "bindless.glsl"
#include "radiance.glsl"
#include "vfx_particle_lighting.glsl"

void TppFog(vec3 eye, vec3 point, out vec3 inscatter, out float transmittance) {
    vec4 g0 = fog_block.fog[0];
    vec4 g1 = fog_block.fog[1];
    vec4 mie = fog_block.fog[2];
    vec4 ray = fog_block.fog[3];
    vec3 d = point - eye;
    vec3 dh = 0.01 * d;
    float dist = length(dh);
    float cos_theta = dot(fog_block.fog[7].xyz, dh / max(0.0001, dist));
    float height = max(1.0e-6, 100.0 * g0.y * dh.y);
    float amount = g0.x * pow(max(0.0, dist - g0.z * 0.01), g1.w);
    float depth = (amount * 100.0 - 100.0 * amount * exp(-height)) / height;
    float rayleigh_phase = 0.059683103 * cos_theta * cos_theta + 0.059683103;
    float m = 1.0 - cos_theta * mie.w;
    float mie_phase = 0.07957747 * (1.0 - mie.w * mie.w) / (m * m);
    vec3 sigma = mie.rgb + ray.rgb;
    vec3 directional = ray.w * (mie.rgb * mie_phase + ray.rgb * rayleigh_phase) / max(vec3(1.0e-6), sigma);
    float opacity = 1.0 - exp(-depth);
    vec3 color = g1.rgb + directional * (1.0 - exp(-depth * sigma));
    inscatter = SceneRadiance(max(color * opacity, vec3(0.0)), 1.0, fog_block.mode.y != 0u);
    transmittance = 1.0 - opacity;
}

float FlareAlpha(float tex_alpha) {
    float a = in_color.a;
    if (in_luminance.z > 0.0) {
        float len = in_luminance.y * push.frame.x;
        if (len * len < 0.001 && in_luminance.z >= 1.0) {
            a = 0.0;
        }
        a *= mix(1.0, in_luminance.x * push.frame.x / max(len, 1.0), min(in_luminance.z, 1.0));
    }
    a = in_extra.x > 0.0 ? clamp(a, 0.0, 1.0) : floor(clamp(a, 0.0, 1.0) * 255.0) / 255.0;
    vec2 pixel = in_params.xy / push.frame.zw;
    float tolerance = in_params.w;
    float v = clamp((SceneDepth(pixel + vec2(0.69, -3.94)) + tolerance - in_params.z) / tolerance, 0.0, 1.0);
    v += clamp((SceneDepth(pixel + vec2(-3.76, 1.37)) + tolerance - in_params.z) / tolerance, 0.0, 1.0);
    v += clamp((SceneDepth(pixel + vec2(3.06, 2.57)) + tolerance - in_params.z) / tolerance, 0.0, 1.0);
    float r = 1.0 - v * 0.33325195;
    return (1.0 - r * r) * tex_alpha * a;
}

void main() {
    const uint flags = in_info.y;
    vec2 base_uv = in_uv.xy;
    if ((flags & kRain) != 0u) {
        vec2 centered = base_uv - in_extra.xy;
        base_uv = in_extra.xy + vec2(in_extra.w * centered.x - in_extra.z * centered.y,
                                    in_extra.z * centered.x + in_extra.w * centered.y);
    }
    float lit_lod = textureQueryLod(PT_TEX2D(in_info.x), base_uv).y;
    float lit_next_lod = textureQueryLod(PT_TEX2D(in_info.x), in_uv.zw).y;
    vec4 tex = (flags & kLit) != 0u ? LitSample(in_info.x, base_uv, lit_lod) : texture(PT_TEX2D(in_info.x), base_uv);
    if ((flags & kAnimeBlend) != 0u) {
        vec4 next_tex = (flags & kLit) != 0u ? LitSample(in_info.x, in_uv.zw, lit_next_lod) : texture(PT_TEX2D(in_info.x), in_uv.zw);
        tex = mix(tex, next_tex, in_params.w);
    }
    vec3 rgb;
    float alpha;
    float alpha_near;
    float alpha_far;
    float transmit = 0.0;
    if ((flags & kFlare) != 0u) {
        if (in_extra.x > 0.0) {
            // Positive 3x3 tent filter in flare texture space. Premultiply before
            // filtering to avoid color fringes at transparent texel boundaries.
            vec4 filtered=vec4(0.0);
            for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
                vec2 uv=base_uv+vec2(x,y)*in_extra.x;
                vec4 t=texture(PT_TEX2D(in_info.x),clamp(uv,0.0,1.0));
                if (any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) t=vec4(0);
                filtered+=vec4(t.rgb*t.a,t.a)*float((x==0?2:1)*(y==0?2:1))/16.0;
            }
            tex=vec4(filtered.a>0.0 ? filtered.rgb/filtered.a : vec3(0),filtered.a);
            // Convolution cannot draw outside the sprite quad. Fade its last
            // filter radius to zero so truncation cannot introduce a new rim.
            float edge=min(min(base_uv.x,base_uv.y),min(1.0-base_uv.x,1.0-base_uv.y));
            tex.a*=smoothstep(0.0,in_extra.x,edge);
        }
        rgb = tex.rgb * in_color.rgb;
        alpha = FlareAlpha(tex.a);
        if (in_extra.y > 0.5 && in_extra.x > 0.0) {
            vec2 edge = min(in_extra.zw, 1.0 - in_extra.zw);
            alpha *= smoothstep(0.0, 0.12, min(edge.x, edge.y));
        }
    } else if ((flags & kLiquid) != 0u) {
        vec3 nt;
        if ((flags & kLiquidHnm) != 0u) {
            nt.xy = vec2(tex.a, tex.g) * 2.0 - 1.0;
            nt.z = sqrt(clamp(1.0 - dot(nt.xy, nt.xy), 0.0, 1.0)) + 1e-4;
            alpha = in_color.a;
        } else {
            nt = tex.rgb * 2.0 - 1.0 + vec3(0.0, 0.0, 1e-4);
            alpha = tex.a * in_color.a;
        }
        vec3 n = normalize(nt.x * in_tangent + nt.y * in_bitangent + nt.z * in_normal);
        vec3 v = normalize(in_world - push.eye.xyz);
        float t = in_luminance.x;
        float ndv = dot(n, v);
        float through = (1.0 - t) * clamp((1.0 - t) * ndv + t, 0.0, 1.0);
        rgb = in_color.rgb * push.frame.x;
        if (in_light_factors.w > 0.0) {
            vec3 lights = LiquidPointLight(in_light_position0, in_light_color0, n, t) + LiquidPointLight(in_light_position1, in_light_color1, n, t);
            rgb += vec3(unpackHalf2x16(in_info.w), in_extra.w) * lights * (in_light_factors.w * push.frame.x);
        }
        if (push.eye.w > 0.0) {
            vec2 pixel = gl_FragCoord.xy - 0.00390625 - n.xy * (in_extra.y * push.eye.w);
            vec2 uv = pixel * push.frame.zw;
            vec3 scene = texture(scene_copy, vec2(FoldScreen(uv.x), FoldScreen(uv.y))).rgb;
            vec3 unlit = vec3(unpackHalf2x16(in_info.w), in_extra.w);
            rgb += (unlit * through + t * t) * scene;
        } else {
            transmit = t * t + through * in_luminance.y;
        }
        if (in_info.z != kNoTexture && in_extra.x > 0.0) {
            float fresnel = in_luminance.z + (1.0 - in_luminance.z) * pow(max(1.0 - max(-ndv, 0.0), 1e-5), in_luminance.w);
            vec3 c = ReflectionSample(in_info.z, reflect(v, n));
            rgb += in_extra.x * fresnel * c * c;
        }
        if ((flags & kClip) != 0u) {
            if (alpha < 0.49609375) {
                discard;
            }
            alpha = 1.0;
        }
    } else if ((flags & kRain) != 0u) {
        vec2 pixel = gl_FragCoord.xy - 0.5;
        vec2 scale = in_luminance.xy * push.frame.zw;
        vec2 uv0 = scale * vec2(pixel.x * in_rain_rotation.y - pixel.y * in_rain_rotation.x,
                               pixel.y * in_rain_rotation.y + pixel.x * in_rain_rotation.x) + in_luminance.z;
        vec2 uv1 = scale * vec2(pixel.x * in_rain_rotation.w - pixel.y * in_rain_rotation.z,
                               pixel.y * in_rain_rotation.w + pixel.x * in_rain_rotation.z) + in_luminance.w;
        float rain = in_info.z == kNoTexture ? 1.0 : texture(PT_TEX2D(in_info.z), uv0).a +
                                                    texture(PT_TEX2D(in_info.z), uv1).a;
        vec3 c = tex.rgb * in_color.rgb * rain * 0.5;
        rgb = mix(pow(max((c + 0.055) / 1.055, vec3(1e-5)), vec3(2.4)), c / 12.92,
                  lessThanEqual(c, vec3(0.03928)));
        alpha = tex.a * in_color.a;
    } else if ((flags & kScreen) != 0u) {
        rgb = tex.rgb * in_color.rgb;
        alpha = tex.a * in_color.a;
    } else {
        rgb = SrgbToLinear(tex.rgb) * in_color.rgb;
        alpha = tex.a * in_color.a;
    }
    if ((flags & kLit) != 0u) {
        vec3 point_lights = PointLight(in_light_position0, in_light_color0) + PointLight(in_light_position1, in_light_color1) +
                            PointLight(in_light_position2, in_light_color2);
        vec3 illumination = in_light_factors.xyz + point_lights * in_light_factors.w;
        rgb = LitTextureToLinear(tex.rgb) * in_color.rgb * illumination;
    }
    if ((flags & kExposure) != 0u) {
        rgb *= push.frame.x;
    }
    if ((flags & kLuminance) != 0u) {
        float ev = -log2(max(push.frame.x, 1e-8));
        float t = clamp((ev - in_luminance.x) / max(in_luminance.y - in_luminance.x, 1e-4), 0.0, 1.0);
        rgb *= mix(max(in_luminance.z, 0.0), max(in_luminance.w, 0.0), t);
    }
    const uint blend_mode = push.ids.z;
    const bool world = push.ids.y != 1u;
    if (world && (fog_block.mode.x & 1u) != 0u && (flags & (kLiquid | kFlare | kScreen)) == 0u) {
        vec3 inscatter;
        float transmittance;
        const float fog_depth = clamp(in_view_depth, 1.0, fog_block.fog[0].w);
        TppFog(push.eye.xyz, push.eye.xyz + (in_world - push.eye.xyz) * (fog_depth / max(in_view_depth, 1.0e-4)), inscatter, transmittance);
        if (blend_mode == kBlendAlpha) {
            rgb = rgb * transmittance + inscatter;
        } else {
            alpha *= transmittance;
        }
    }
    if (world) {
        float near_fade = in_params.y;
        float far_fade = in_params.z;
        if (far_fade > 0.0 || near_fade > 0.0) {
            float d = in_view_depth;
            alpha *= d > far_fade ? 1.0 : (d < near_fade ? 0.0 : clamp((d - near_fade) / max(far_fade - near_fade, 1e-3), 0.0, 1.0));
        }
        if ((flags & kSoft) != 0u) {
            if (push.ids.y != 2u) {
                float scene = SceneDepth(gl_FragCoord.xy);
                alpha *= clamp((scene - in_view_depth) * in_params.x, 0.0, 1.0);
            }
        }
    }
    alpha = clamp(alpha, 0.0, 1.0);
    alpha_near = alpha;
    alpha_far = alpha;
    if (world && push.ids.y == 2u && (flags & kSoft) != 0u) {
        vec2 range = texture(depth_texture, gl_FragCoord.xy / vec2(textureSize(depth_texture, 0))).rg;
        alpha_near *= clamp((LinearDepth(range.x) - in_view_depth) * in_params.x, 0.0, 1.0);
        alpha_far *= clamp((LinearDepth(range.y) - in_view_depth) * in_params.x, 0.0, 1.0);
    } else if (world && push.ids.y == 2u) {
        vec2 range = texture(depth_texture, gl_FragCoord.xy / vec2(textureSize(depth_texture, 0))).rg;
        alpha_near *= LinearDepth(range.x) >= in_view_depth ? 1.0 : 0.0;
        alpha_far *= LinearDepth(range.y) >= in_view_depth ? 1.0 : 0.0;
    }
    if (push.ids.w != 0u) {
        out_color_near = vec4(push.ids.y == 0u ? vec3(1.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), 1.0) * 0.5;
        out_color_far = out_color_near;
        return;
    }
    const uint blend = push.ids.z;
    if (blend == kBlendMul) {
        out_color_far = vec4(mix(vec3(1.0), rgb, alpha_far), alpha_far);
        out_color_near = vec4(mix(vec3(1.0), rgb, alpha_near), alpha_near);
    } else if (blend == kBlendMin) {
        out_color_far = vec4(mix(vec3(65504.0), rgb, alpha_far), alpha_far);
        out_color_near = vec4(mix(vec3(65504.0), rgb, alpha_near), alpha_near);
    } else if (blend == kBlendOpaque) {
        out_color_far = vec4(rgb, 1.0);
        out_color_near = out_color_far;
    } else if (push.ids.y == 2u) {
        vec3 encoded = LinearToSrgb(max(rgb, vec3(0.0)));
        out_color_far = vec4(encoded * alpha_far, alpha_far);
        out_color_near = vec4(encoded * alpha_near, alpha_near);
    } else {
        out_color_far = vec4(rgb * alpha, alpha * (1.0 - transmit));
        out_color_near = out_color_far;
    }
}
