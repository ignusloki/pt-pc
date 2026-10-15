#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace pt {
namespace vk {
class Context;
}

enum class NeuralRenderingKind : int { Off = 0, Dlss5 = 1, Count = 2 };
enum class NeuralStyle : int { Default = 0, Natural = 1, Cinematic = 2, Count = 3 };

const char* NeuralRenderingKey(NeuralRenderingKind kind);
bool ParseNeuralRendering(const std::string& text, NeuralRenderingKind& out);
const char* NeuralStyleKey(NeuralStyle style);
bool ParseNeuralStyle(const std::string& text, NeuralStyle& out);

struct NeuralRuntimeInfo {
    std::filesystem::path path;
    uint64_t size = 0;
    std::string sha256;
    std::string source;
};

std::optional<NeuralRuntimeInfo> FindNeuralRuntime(const std::string& ini_path);

struct NeuralRenderingSupport {
    bool available = false;
    std::string reason;
    std::string note;
    std::optional<NeuralRuntimeInfo> runtime;
    uint32_t code = 0;
};

struct NeuralDispatch {
    void* cmd = nullptr;
    struct Image {
        uint64_t image = 0;
        uint64_t view = 0;
        uint32_t format = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    } color, output, depth, motion, control_mask;
    bool depth_inverted = true;
    float motion_scale_x = 0.0f;
    float motion_scale_y = 0.0f;
    float scaling_ratio = 1.0f;
    bool reset = false;
    NeuralStyle style = NeuralStyle::Default;
    float intensity = 1.0f;
    bool use_control_mask = false;
};

class NeuralBackend {
public:
    virtual ~NeuralBackend() = default;
    virtual const NeuralRenderingSupport& Support(const std::string& ini_runtime) = 0;
    virtual bool Create(void* cmd, uint32_t width, uint32_t height, NeuralStyle style, float intensity, float scaling_ratio) = 0;
    virtual bool Created() const = 0;
    virtual bool Dispatch(const NeuralDispatch& d) = 0;
    virtual void Release() = 0;
    virtual float LastCostMs() const = 0;
};

std::unique_ptr<NeuralBackend> CreateNeuralBackend(vk::Context& ctx);

bool NeuralRenderingVendorSupported(uint32_t vendor_id);

}
