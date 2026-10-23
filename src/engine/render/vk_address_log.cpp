#include "engine/render/vk_address_log.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "engine/core/log.h"

namespace pt::vk::address_log {
namespace {

struct Entry {
    Binding binding;
    std::string text;
    double bound_at = 0.0;
    double freed_at = -1.0;
};

constexpr size_t kFreedKept = 32768;
constexpr size_t kLiveShown = 6;
constexpr size_t kFreedShown = 4;

struct State {
    std::mutex mutex;
    bool enabled = false;
    std::map<std::pair<uint64_t, uint64_t>, Entry> live;
    std::deque<Entry> freed;
    std::unordered_map<uint64_t, std::string> text;
    uint64_t binds = 0;
    uint64_t unbinds = 0;
};

State& S() {
    static State* state = new State;
    return *state;
}

const char* TypeName(int32_t type) {
    switch (type) {
    case 0: return "object";
    case 6: return "command buffer";
    case 7: return "fence";
    case 8: return "device memory";
    case 9: return "buffer";
    case 10: return "image";
    case 11: return "event";
    case 12: return "query pool";
    case 13: return "buffer view";
    case 14: return "image view";
    case 15: return "shader module";
    case 16: return "pipeline cache";
    case 17: return "pipeline layout";
    case 19: return "pipeline";
    case 22: return "descriptor pool";
    case 23: return "descriptor set";
    case 25: return "command pool";
    case 1000150000: return "acceleration structure";
    case 1000029000: return "CUDA module (NVX)";
    case 1000029001: return "CUDA function (NVX)";
    default: return "object";
    }
}

bool Overlaps(const Binding& b, uint64_t lo, uint64_t hi) {
    return b.size > 0 && b.base < hi && lo < b.base + b.size;
}

std::string Line(const Entry& e) {
    const Binding& b = e.binding;
    std::string line = std::format("{} {:#x}{}: {:#x} + {:#x}", TypeName(b.type), b.handle, b.internal ? " (driver internal)" : "", b.base, b.size);
    if (!e.text.empty()) {
        line += ", " + e.text;
    } else if (b.type == 9 || b.type == 10) {
        line += ", not made by the port's allocator (NGX, Streamline, a layer or the swapchain)";
    }
    line += std::format(", bound at {:.3f} s", e.bound_at);
    if (e.freed_at >= 0.0) {
        line += std::format(", unbound at {:.3f} s", e.freed_at);
    }
    return line;
}

}

void SetEnabled(bool on) {
    std::lock_guard lock(S().mutex);
    S().enabled = on;
}

bool Enabled() {
    std::lock_guard lock(S().mutex);
    return S().enabled;
}

void Record(const Binding& binding) {
    const double now = LogClock();
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (!s.enabled) {
        return;
    }
    static const long trace = [] {
        const char* e = std::getenv("PT_VK_ADDRESS_TRACE");
        return e ? std::atol(e) : 0L;
    }();
    if (s.binds + s.unbinds < static_cast<uint64_t>(std::max(trace, 0L))) {
        LogInfo("vulkan address: {} {} {:#x}{} {:#x} + {:#x}", binding.bind ? "bind" : "unbind", TypeName(binding.type), binding.handle,
                binding.internal ? " internal" : "", binding.base, binding.size);
    }
    const auto key = std::make_pair(binding.handle, binding.base);
    if (binding.bind) {
        ++s.binds;
        Entry& e = s.live[key];
        e.binding = binding;
        e.bound_at = now;
        e.freed_at = -1.0;
        const auto t = s.text.find(binding.handle);
        e.text = t != s.text.end() ? t->second : std::string();
        return;
    }
    ++s.unbinds;
    Entry e;
    if (const auto it = s.live.find(key); it != s.live.end()) {
        e = std::move(it->second);
        s.live.erase(it);
    } else {
        e.binding = binding;
        e.bound_at = -1.0;
    }
    if (const auto t = s.text.find(binding.handle); t != s.text.end()) {
        if (e.text.empty()) e.text = t->second;
        const auto next = s.live.lower_bound(std::make_pair(binding.handle, uint64_t{0}));
        if (next == s.live.end() || next->first.first != binding.handle) s.text.erase(t);
    }
    e.binding.bind = false;
    e.freed_at = now;
    s.freed.push_back(std::move(e));
    if (s.freed.size() > kFreedKept) {
        s.freed.pop_front();
    }
}

void Describe(uint64_t handle, std::string text) {
    State& s = S();
    std::lock_guard lock(s.mutex);
    if (!s.enabled || handle == 0) {
        return;
    }
    for (auto it = s.live.lower_bound(std::make_pair(handle, uint64_t{0})); it != s.live.end() && it->first.first == handle; ++it) {
        it->second.text = text;
    }
    s.text[handle] = std::move(text);
}

Counts Count() {
    State& s = S();
    std::lock_guard lock(s.mutex);
    return Counts{s.live.size(), s.freed.size(), s.binds, s.unbinds};
}

std::vector<std::string> Explain(uint64_t address, uint64_t precision) {
    const uint64_t page = precision > 1 ? precision : 1;
    const uint64_t lo = precision > 1 ? address & ~(precision - 1) : address;
    const uint64_t hi = lo + page;
    State& s = S();
    std::lock_guard lock(s.mutex);
    std::vector<std::string> lines;
    if (!s.enabled) {
        return lines;
    }
    std::vector<const Entry*> live;
    for (const auto& [key, e] : s.live) {
        if (Overlaps(e.binding, lo, hi)) live.push_back(&e);
    }
    std::sort(live.begin(), live.end(), [](const Entry* a, const Entry* b) { return a->binding.size < b->binding.size; });
    for (size_t i = 0; i < live.size() && i < kLiveShown; ++i) {
        lines.push_back("in " + Line(*live[i]));
    }
    if (live.size() > kLiveShown) {
        lines.push_back(std::format("and {} more live ranges over it", live.size() - kLiveShown));
    }
    size_t shown = 0;
    for (auto it = s.freed.rbegin(); it != s.freed.rend() && shown < kFreedShown; ++it) {
        if (Overlaps(it->binding, lo, hi)) {
            lines.push_back("was " + Line(*it));
            ++shown;
        }
    }
    if (lines.empty()) {
        lines.push_back(std::format("no range over {:#x} was reported in this run ({} live, {} unbound kept)", lo, s.live.size(), s.freed.size()));
    } else if (live.empty()) {
        lines.push_back("no live object holds it: the access went to memory freed before the fault");
    }
    return lines;
}

void Clear() {
    State& s = S();
    std::lock_guard lock(s.mutex);
    s.live.clear();
    s.freed.clear();
    s.text.clear();
    s.binds = s.unbinds = 0;
}

}
