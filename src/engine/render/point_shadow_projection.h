#pragma once

#include <glm/gtc/matrix_transform.hpp>

namespace pt {

inline float PointShadowRange(float radius) {
    return radius * (1024.0f / 1022.0f);
}

inline bool PointShadowCullFront(bool positive_hemisphere) {
    return positive_hemisphere;
}

inline glm::mat4 PointShadowView(glm::vec3 position) {
    return glm::translate(glm::mat4(1.0f), -position);
}

}
