#version 460

#include "xr_copy_decode_shared.glsl"

#include "film_grain.glsl"

void main() {
    vec2 uv = p.rect.xy + in_uv * p.rect.zw;
    vec4 c = texture(source, uv);
    if (p.params.x > 0.5) {
        float a = clamp(c.a, 0.0, 1.0);
        vec3 straight = a > 1.0e-4 ? c.rgb / a : vec3(0.0);
        out_color = vec4(Decode(straight) * a, a);
        return;
    }
    vec3 color = c.rgb;
    if (p.grain.x > 0.0) {
        color = FilmGrain(color, uv, gl_FragCoord.xy, p.grain, p.grain_offset, true);
    }
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / max(p.params.y, 0.1)));
    out_color = vec4(Decode(color), 1.0);
}
