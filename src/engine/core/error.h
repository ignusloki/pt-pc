#pragma once

#include <string>
#include <utility>

namespace pt {

inline bool FailWithError(std::string* error, std::string text) {
    if (error) {
        *error = std::move(text);
    }
    return false;
}

}
