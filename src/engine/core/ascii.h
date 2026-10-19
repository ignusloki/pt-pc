#pragma once

#include <string>
#include <string_view>

namespace pt {

constexpr char AsciiLower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
constexpr char AsciiUpper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

inline std::string AsciiLowerCopy(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = AsciiLower(c);
    return out;
}

constexpr bool EqualsIgnoreAsciiCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (AsciiLower(a[i]) != AsciiLower(b[i])) return false;
    return true;
}

}
