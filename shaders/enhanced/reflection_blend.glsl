vec3 ReflectionBlendColor(vec3 base, bool hdr) {
    return SrgbEncode(SceneRadiance(base, 1.0, hdr));
}
