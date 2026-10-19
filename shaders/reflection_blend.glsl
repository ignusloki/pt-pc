vec3 ReflectionBlendBase(vec3 base, bool hdr) {
    return SrgbEncode(hdr ? base : min(base, vec3(1.0)));
}
