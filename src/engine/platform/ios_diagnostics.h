#pragma once

#include <filesystem>
#include <cstdint>
#include <string_view>

namespace pt::ios_diag {

#if PT_IOS && defined(PT_IOS_DIAGNOSTICS)
// A separate file and mutex keep the heartbeat usable if the ordinary logger stalls.
class Session {
public:
    Session(const std::filesystem::path& data, const char* build);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};
void Stage(std::string_view name);
void Event(std::string_view name);
void FramePresented();
void RecordImages();
class Scope {
public:
    Scope(std::string_view kind, std::string_view name);
    ~Scope();
    void Step(std::string_view detail);
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    uint64_t id_ = 0;
};
#else
inline void Stage(std::string_view) {}
inline void Event(std::string_view) {}
inline void FramePresented() {}
inline void RecordImages() {}
class Scope {
public:
    Scope(std::string_view, std::string_view) {}
    void Step(std::string_view) {}
};
#endif

}
