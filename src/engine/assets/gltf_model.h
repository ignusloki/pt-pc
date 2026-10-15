#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/assets/fmdl.h"

namespace pt {

class Vfs;

bool LoadGltfModel(std::span<const uint8_t> data, const std::string& asset_path, const FmdlModel* original, FmdlModel& out,
                   std::string* error);

bool LoadModelFile(const Vfs& vfs, const std::string& path, FmdlModel& out);
bool LoadModelFile(const Vfs& vfs, const std::string& path, const std::vector<uint8_t>* game_bytes, FmdlModel& out);

}
