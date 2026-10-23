#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pt::vk::address_log {

struct Binding {
    uint64_t base = 0;
    uint64_t size = 0;
    uint64_t handle = 0;
    int32_t type = 0;
    bool internal = false;
    bool bind = true;
};

void SetEnabled(bool on);
bool Enabled();
void Record(const Binding& binding);
void Describe(uint64_t handle, std::string text);

struct Counts {
    size_t live = 0;
    size_t freed = 0;
    uint64_t binds = 0;
    uint64_t unbinds = 0;
};
Counts Count();

std::vector<std::string> Explain(uint64_t address, uint64_t precision);

void Clear();

}
