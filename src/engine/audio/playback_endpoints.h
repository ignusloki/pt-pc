#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/audio/output_device.h"

namespace pt::audio {

std::vector<PlaybackEndpoint> ListPlaybackEndpoints(std::vector<uint32_t>* sdl_ids = nullptr);

std::string SystemDefaultPlaybackName();

std::vector<PlaybackEndpoint> NativePlaybackEndpoints();

}
