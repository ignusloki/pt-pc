#pragma once

#include <cstdint>
#include <string>

namespace pt {

struct PresenceActivity {
    std::string details;
    std::string state;
    int64_t start = 0;

    bool operator==(const PresenceActivity&) const = default;
};

void PresenceStart(const std::string& client_id);
void PresenceSet(const PresenceActivity& activity);
void PresenceStop();

}
