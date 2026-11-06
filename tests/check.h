#pragma once

#include <cstdio>
#include <string>

namespace pt::test {

inline void Check(int& failures, bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    failures += !ok;
}

inline void Check(int& failures, bool ok, const std::string& label) { Check(failures, ok, label.c_str()); }

inline void Expect(int& failures, bool ok, const char* label) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

inline void CheckNamed(int& failures, const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    failures += !ok;
}

inline void CheckNamed(int& failures, const std::string& label, bool ok) { CheckNamed(failures, label.c_str(), ok); }

}
