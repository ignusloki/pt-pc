#pragma once

#include <glm/vec2.hpp>

namespace pt::game {

inline bool CellContains(glm::vec2 lo, glm::vec2 hi, glm::vec2 p) {
    return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
}

}
