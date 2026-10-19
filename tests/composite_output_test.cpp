#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat3;
using glm::all;
using glm::lessThanEqual;
using glm::max;
using glm::abs;
vec3 operator*(vec3 v, double scale) { return v * float(scale); }
vec3 operator*(double scale, vec3 v) { return float(scale) * v; }
float max(float a, double b) { return std::max(a, float(b)); }
float clamp(float v, double lo, double hi) { return std::clamp(v, float(lo), float(hi)); }
vec3 clamp(vec3 v, double lo, double hi) { return glm::clamp(v, float(lo), float(hi)); }
float pow(float v, double power) { return std::pow(v, float(power)); }
vec3 pow(vec3 v, double power) { return glm::pow(v, vec3(float(power))); }
using glm::pow;
using glm::clamp;
#include "../shaders/composite_output.glsl"
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    const auto near = [](vec3 a, vec3 b, float tolerance = 1.0e-5f) {
        return glm::all(glm::lessThanEqual(glm::abs(a - b), vec3(tolerance)));
    };
    const vec2 hdr(200, 1000);
    check(near(CompositeDisplayColor(vec3(.2f, .5f, .9f), 8, 1, 0, hdr), vec3(.2f, .5f, .9f)),
          "SDR ignores HDR gain and retains encoded colour");
    check(near(CompositeDisplayColor(vec3(.25f, .0625f, 1), 1, 2, 0, hdr), vec3(.5f, .25f, 1)),
          "SDR brightness remains unchanged");
    check(near(CompositeDisplayColor(vec3(1), 1, 1, 1, hdr), vec3(2.5f)), "scRGB paper white remains 200 nits");
    check(near(CompositeDisplayColor(vec3(1), 1, 1, 2, hdr), vec3(.579133f), 3.0e-5f),
          "HDR10 paper white is PQ encoded at 200 nits");
    check(near(CompositeDisplayColor(vec3(1), 8, 1, 2, hdr), vec3(.751827f), 3.0e-5f),
          "HDR10 highlight gain clips at the 1000 nit peak");
    const vec3 red = CompositeDisplayColor(vec3(1, 0, 0), 1, 1, 2, hdr);
    check(near(red, vec3(PqEncode(125.4808f), PqEncode(13.8194f), PqEncode(3.27832f))),
          "HDR10 converts Rec709 primaries to BT2020 before PQ");
    const vec3 base = CompositeDisplayColor(vec3(.15f, .35f, .7f), 2, 1, 2, hdr);
    bool blends = true, bounded = true;
    for (int r = 0; r <= 10; ++r) {
        for (int g = 0; g <= 10; ++g) {
            for (int b = 0; b <= 10; ++b) {
                const vec3 shown = CompositeDisplayColor(vec3(r, g, b) / 10.0f, 1, 1.3f, 2, hdr);
                const vec4 layer = CompositeUiLayer(shown, base, 1.5f / 1023);
                blends &= near(vec3(layer) + (1 - layer.a) * base, shown, 1.5f / 1023 + 1.0e-6f);
                bounded &= layer.a >= 0 && layer.a <= 1 && glm::all(glm::greaterThanEqual(vec3(layer), vec3(0)));
            }
        }
    }
    check(blends, "HDR10 UI recomposes to the shown PQ frame across 1331 colours");
    check(bounded, "HDR10 UI alpha is valid and premultiplied colour stays nonnegative");
    check(CompositeUiLayer(base, base, 1.5f / 1023).a == 0, "unchanged HDR10 pixels have zero UI alpha");
    return failures ? 1 : 0;
}
