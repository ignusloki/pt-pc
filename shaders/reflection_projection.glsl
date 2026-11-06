bool Outside(vec2 uv) {
    return uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0;
}

vec2 Project(vec3 p) {
    return ReflectionProject(p, v.projection_param.xy, v.jitter.xy);
}
