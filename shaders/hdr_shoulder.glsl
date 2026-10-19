const float kHdrSourceWhite = 3.0;
float HdrShoulder(float m, float ratio) {
    if (m <= 1.0 || ratio <= 1.001) {
        return min(m, 1.0);
    }
    float width = min(kHdrSourceWhite - 1.0, 2.97 * (ratio - 1.0));
    if (m >= 1.0 + width) {
        return ratio;
    }
    float t = (m - 1.0) / width;
    float t2 = t * t;
    float t3 = t2 * t;
    return (1.0 - 3.0 * t2 + 2.0 * t3) + (t - 2.0 * t2 + t3) * width + (3.0 * t2 - 2.0 * t3) * ratio;
}

float HdrGain(float m, float ratio) {
    return max(HdrShoulder(m, ratio), 1.0);
}
