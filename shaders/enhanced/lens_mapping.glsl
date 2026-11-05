// Authored P.T. lens geometry in normalized output coordinates.
// Green is also the motion/depth anchor used by frame generation.
vec2 LensSource(vec2 uv, vec2 size, float dispersion) {
    float aspect = size.x / size.y;
    vec2 p = (uv * 2.0 - 1.0) * vec2(aspect / (16.0 / 9.0), 1.0);
    float radius = Saturate(length(p) * 0.52240777);
    float dome = 1.0 - sin(1.5707964 * radius);
    vec2 offset = pow(radius, 1.75) * 84.0 * vec2(1.0 / (1080.0 * aspect), 1.0 / 1080.0)
                  * p * inversesqrt(dot(p,p) + dome*dome);
    return uv * 0.9 + 0.050000012 + offset * dispersion;
}
