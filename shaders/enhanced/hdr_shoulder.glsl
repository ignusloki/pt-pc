// Display-linear units: 1 is paper white. Unit slope at the knee, positive
// slope thereafter, asymptotic to the configured peak. No finite input clip.
float HdrShoulder(float m, float ratio) {
    float knee = min(1.0, 0.8 * ratio);
    if (m <= knee) return m;
    float headroom = ratio - knee;
    float excess = m - knee;
    return knee + headroom * (excess / (excess + headroom));
}
