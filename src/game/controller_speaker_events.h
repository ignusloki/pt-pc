#pragma once

#include <array>
#include <cstdint>

namespace pt::game {

inline constexpr std::array<uint32_t, 9> kControllerSpeakerEvents{0xAD52F3C2u, 0x0CB2A9B7u, 0x9983E7F9u, 0xC910062Eu, 0x958C5411u,
                                                                  0x7B136101u, 0xB8D5B537u, 0xF9E2D015u, 0x52B9C804u};
inline constexpr std::array<uint32_t, 7> kControllerHapticOnlyEvents{0x28E58AD8u, 0xF688B813u, 0xF688B80Du, 0x07F2CC27u,
                                                                     0xC180CD50u, 0x07F2CC39u, 0x6A89341Eu};

}
