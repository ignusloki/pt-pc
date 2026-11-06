#pragma once

#include <string_view>

namespace pt::localized {

template <typename Entries>
std::string_view FindMenuText(const Entries& entries, std::string_view key) {
    for (const auto& e : entries) if (e.key == key) return e.text;
    return {};
}

}
