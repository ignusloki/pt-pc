#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "spirv_hlsl.hpp"

using namespace spirv_cross;

static std::vector<uint32_t> ReadWords(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), words.size() * 4);
    return words;
}

struct Binding {
    uint32_t set = 0, binding = 0, space = 0, reg = 0, count = 1;
    char kind = 'b';
    std::string what;
};

std::string SvPositionFirst(std::string text, bool fragment) {
    for (const char* name : {"struct SPIRV_Cross_Output", "struct SPIRV_Cross_Input"}) {
        const size_t at = text.find(name);
        if (at == std::string::npos) continue;
        const size_t open = text.find('{', at);
        const size_t close = text.find("};", open);
        if (open == std::string::npos || close == std::string::npos) continue;
        const size_t first = text.find('\n', open) + 1;
        const size_t sv = text.find(": SV_Position;", open);
        if (sv == std::string::npos || sv > close) {
            if (fragment && std::string(name) == "struct SPIRV_Cross_Input") {
                text.insert(first, "    float4 pt_unread_position : SV_Position;\n");
            }
            continue;
        }
        const size_t line_start = text.rfind('\n', sv) + 1;
        const size_t line_end = text.find('\n', sv) + 1;
        const std::string line = text.substr(line_start, line_end - line_start);
        text.erase(line_start, line_end - line_start);
        text.insert(first, line);
    }
    return text;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: spirv_to_hlsl in.spv out.hlsl [--sm 66] [--flip-y] [--json out.json] [--push-space N] [--unbounded-space-base N]\n");
        return 2;
    }
    unsigned sm = 66, push_space = 64, array_base = 100;
    bool flip = false, verbose = false;
    const char* json_path = nullptr;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--sm") && i + 1 < argc) sm = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--flip-y")) flip = true;
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) json_path = argv[++i];
        else if (!std::strcmp(argv[i], "--push-space") && i + 1 < argc) push_space = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--unbounded-space-base") && i + 1 < argc) array_base = static_cast<unsigned>(std::atoi(argv[++i]));
    }
    try {
        CompilerHLSL hlsl(ReadWords(argv[1]));
        CompilerGLSL::Options common = hlsl.get_common_options();
        common.vertex.flip_vert_y = flip;
        hlsl.set_common_options(common);
        CompilerHLSL::Options opts = hlsl.get_hlsl_options();
        opts.shader_model = sm;
        hlsl.set_hlsl_options(opts);

        const auto model = hlsl.get_execution_model();
        auto active = hlsl.get_active_interface_variables();
        for (const auto& ep : hlsl.get_entry_points_and_stages()) {
            for (VariableID var : hlsl.get_entry_point(ep.name, ep.execution_model).interface_variables) {
                const spv::StorageClass storage = hlsl.get_storage_class(var);
                if (storage == spv::StorageClassInput || storage == spv::StorageClassOutput) {
                    active.insert(var);
                }
            }
        }
        hlsl.set_enabled_interface_variables(active);
        const ShaderResources res = hlsl.get_shader_resources(active);
        std::vector<Binding> found;
        auto add = [&](const Resource& r, char kind, const char* what) {
            const SPIRType& t = hlsl.get_type(r.type_id);
            Binding e;
            e.set = hlsl.get_decoration(r.id, spv::DecorationDescriptorSet);
            e.binding = hlsl.get_decoration(r.id, spv::DecorationBinding);
            const bool array = !t.array.empty();
            if (array && !t.array_size_literal[0]) throw std::runtime_error("an array sized by a specialization constant is not supported");
            const bool unbounded = array && t.array[0] == 0;
            if (e.binding >= 16) throw std::runtime_error("binding >= 16 does not fit the register space scheme");
            e.count = array ? (unbounded ? 0 : t.array[0]) : 1;
            e.space = array ? array_base + e.set * 16 + e.binding : e.set;
            e.reg = array ? 0 : e.binding;
            e.kind = kind;
            e.what = what;
            found.push_back(e);
        };
        for (const Resource& r : res.uniform_buffers) add(r, 'b', "constant buffer");
        for (const Resource& r : res.storage_buffers)
            add(r, hlsl.get_buffer_block_flags(r.id).get(spv::DecorationNonWritable) ? 't' : 'u', "raw buffer");
        for (const Resource& r : res.separate_images) add(r, 't', "texture");
        for (const Resource& r : res.separate_samplers) add(r, 's', "sampler");
        for (const Resource& r : res.storage_images) add(r, 'u', "storage image");
        for (const Resource& r : res.acceleration_structures) add(r, 't', "acceleration structure");
        for (const Resource& r : res.sampled_images) {
            if (!hlsl.get_type(r.type_id).array.empty())
                throw std::runtime_error("an array of combined image samplers has no D3D12 form (use PT_TEX2D, shaders/bindless.glsl)");
            add(r, 't', "texture of a combined sampler");
            add(r, 's', "sampler of a combined sampler");
        }
        std::vector<std::pair<uint32_t, uint32_t>> bound;
        for (const Binding& e : found) {
            if (std::find(bound.begin(), bound.end(), std::make_pair(e.set, e.binding)) != bound.end()) continue;
            bound.emplace_back(e.set, e.binding);
            HLSLResourceBinding b;
            b.stage = model;
            b.desc_set = e.set;
            b.binding = e.binding;
            b.cbv.register_space = b.uav.register_space = b.srv.register_space = b.sampler.register_space = e.space;
            b.cbv.register_binding = b.uav.register_binding = b.srv.register_binding = b.sampler.register_binding = e.reg;
            hlsl.add_hlsl_resource_binding(b);
            if (verbose) std::printf("// set %u binding %u -> space %u register %u%s\n", e.set, e.binding, e.space, e.reg, e.count == 0 ? " (unbounded)" : "");
        }
        uint32_t push_bytes = 0;
        if (!res.push_constant_buffers.empty()) {
            HLSLResourceBinding b;
            b.stage = model;
            b.desc_set = ResourceBindingPushConstantDescriptorSet;
            b.binding = ResourceBindingPushConstantBinding;
            b.cbv.register_space = push_space;
            b.cbv.register_binding = 0;
            hlsl.add_hlsl_resource_binding(b);
            push_bytes = static_cast<uint32_t>(hlsl.get_declared_struct_size(hlsl.get_type(res.push_constant_buffers[0].base_type_id)));
        }
        const uint32_t workgroups = hlsl.remap_num_workgroups_builtin();
        if (workgroups) {
            hlsl.set_decoration(workgroups, spv::DecorationDescriptorSet, 65);
            hlsl.set_decoration(workgroups, spv::DecorationBinding, 0);
            HLSLResourceBinding b;
            b.stage = model;
            b.desc_set = 65;
            b.binding = 0;
            b.cbv.register_space = 65;
            b.cbv.register_binding = 0;
            hlsl.add_hlsl_resource_binding(b);
        }
        const std::string text = SvPositionFirst(hlsl.compile(), model == spv::ExecutionModelFragment);
        std::ofstream(argv[2], std::ios::binary) << text;

        for (const Binding& e : found) {
            std::ostringstream want;
            want << "register(" << e.kind << e.reg << ", space" << e.space << ")";
            if (text.find(want.str()) == std::string::npos)
                throw std::runtime_error("set " + std::to_string(e.set) + " binding " + std::to_string(e.binding) + ": " + want.str() + " is not in the HLSL");
            if (e.kind == 's' && e.count == 0) throw std::runtime_error("an unbounded array of samplers has no D3D12 form (the sampler heap holds 2048)");
        }
        if (json_path) {
            std::ofstream j(json_path, std::ios::binary);
            const char* stage = model == spv::ExecutionModelVertex ? "vs" : model == spv::ExecutionModelFragment ? "ps" : "cs";
            j << "{\n \"stage\": \"" << stage << "\",\n \"shader_model\": \"" << sm / 10 << "_" << sm % 10 << "\",\n";
            if (model == spv::ExecutionModelGLCompute)
                j << " \"workgroup_size\": [" << hlsl.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0) << ", "
                  << hlsl.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1) << ", "
                  << hlsl.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2) << "],\n";
            j << " \"push_constants\": " << (push_bytes ? "{\"space\": " + std::to_string(push_space) + ", \"register\": 0, \"bytes\": " + std::to_string(push_bytes) + "}" : "null") << ",\n";
            j << " \"num_workgroups_builtin\": " << (workgroups ? "{\"space\": 65, \"register\": 0}" : "null") << ",\n";
            j << " \"resources\": [";
            std::sort(found.begin(), found.end(), [](const Binding& a, const Binding& b) {
                return a.set != b.set ? a.set < b.set : a.binding != b.binding ? a.binding < b.binding : a.kind < b.kind;
            });
            for (size_t i = 0; i < found.size(); ++i) {
                const Binding& e = found[i];
                j << (i ? ",\n  " : "\n  ") << "{\"set\": " << e.set << ", \"binding\": " << e.binding << ", \"class\": \"" << e.kind
                  << "\", \"space\": " << e.space << ", \"register\": " << e.reg << ", \"count\": " << e.count << ", \"what\": \"" << e.what << "\"}";
            }
            j << "\n ]\n}\n";
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SPIRV-Cross threw an exception: %s\n", e.what());
        return 1;
    }
    return 0;
}
