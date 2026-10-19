#ifndef PT_FOLIAGE_SHADOW_GLSL
#define PT_FOLIAGE_SHADOW_GLSL

float PtFoliageShadowTransmission(float translucency, float strength, uint material_flags, uint record_flags, bool authored_shadow) {
    const uint required = MAT_ALPHA_TEST | MAT_TRANSLUCENT_TEX;
    if ((material_flags & required) != required || (material_flags & MAT_ALPHA_DITHER) != 0u || (record_flags & 4u) != 0u ||
        authored_shadow || !(strength > 0.0)) {
        return 0.0;
    }
    if (!(translucency > 0.0)) return 0.0;
    return clamp(translucency * clamp(strength, 0.0, 1.0), 0.0, 0.75);
}

#endif
