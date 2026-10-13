#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/assets/fmdl.h"

namespace pt {

class Vfs;

// A mod's glTF 2.0 model (.glb, or .gltf with its .bin and images beside it or embedded) as the game's model container.
// `asset_path`: where the model sits in /Assets (relative buffer and image URIs resolve from its folder). `original`: the game
// model it replaces, when there is one: a skinned glTF takes its skeleton (joints matched by bone name, the bind pose of the
// original), and primitives exported by tools/fmdl.py keep their material, mesh flags and mesh group. Materials without
// tools/fmdl.py's extras become the game's standard opaque material with the glTF base colour and normal images.
bool LoadGltfModel(std::span<const uint8_t> data, const std::string& asset_path, const FmdlModel* original, FmdlModel& out,
                   std::string* error);

// The model at `path` for the renderer and the stages: a mod's .glb or .gltf at the same path (or at `path` itself when it
// ends in .glb or .gltf) before the game's .fmdl. Logs why a mod's model was refused, then uses the game's own.
bool LoadModelFile(const Vfs& vfs, const std::string& path, FmdlModel& out);
// the same with the game file's bytes already read (nullptr: none)
bool LoadModelFile(const Vfs& vfs, const std::string& path, const std::vector<uint8_t>* game_bytes, FmdlModel& out);

}
