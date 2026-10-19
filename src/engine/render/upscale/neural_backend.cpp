#include "engine/render/upscale/neural_rendering.h"

#include <memory>

#if defined(PT_WITH_DLSS)

#include "engine/render/upscale/upscale.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_vk.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <string>

#include "engine/core/log.h"
#include "engine/core/resource_path.h"

namespace pt {
namespace {

constexpr const char* kProjectId = "3c9e5a1b-7d24-4f68-b0e3-5a2c8d91f47e";
constexpr const char* kEngineVersion = "1.0";
constexpr auto kFeatureNeural = static_cast<NVSDK_NGX_Feature>(18);

constexpr const char* kNrAvailable = "DLSSNR.Available";
constexpr const char* kNrNeedsDriver = "DLSSNR.NeedsUpdatedDriver";
constexpr const char* kNrMinMajor = "DLSSNR.MinDriverVersionMajor";
constexpr const char* kNrMinMinor = "DLSSNR.MinDriverVersionMinor";

constexpr const char* kColor = "DLSSNR.Color";
constexpr const char* kDepth = "DLSSNR.Depth";
constexpr const char* kMVec = "DLSSNR.MVec";
constexpr const char* kOutput = "DLSSNR.Output";
constexpr const char* kControlMask = "DLSSNR.ControlMask";
constexpr const char* kWidth = "DLSSNR.Width";
constexpr const char* kHeight = "DLSSNR.Height";
constexpr const char* kReset = "DLSSNR.Reset";
constexpr const char* kDepthInverted = "DLSSNR.DepthInverted";
constexpr const char* kMVecScaleX = "DLSSNR.MVecScaleX";
constexpr const char* kMVecScaleY = "DLSSNR.MVecScaleY";
constexpr const char* kScalingRatio = "DLSSNR.ScalingRatio";
constexpr const char* kRenderPreset = "DLSSNR.Hint.Render.Preset";
constexpr const char* kUseAutoMask = "DLSSNR.UseAutoMask";
constexpr const char* kIntensity = "DLSSNR.Intensity";
constexpr const char* kStyle = "DLSSNR.Style";

const std::wstring& DataPath() {
    static const std::wstring path = [] {
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "pt-port-ngx";
        std::filesystem::create_directories(dir, ec);
        return dir.wstring();
    }();
    return path;
}

NVSDK_NGX_Resource_VK Resource(const NeuralDispatch::Image& image, VkImageAspectFlags aspect, bool read_write) {
    return NVSDK_NGX_Create_ImageView_Resource_VK(reinterpret_cast<VkImageView>(image.view), reinterpret_cast<VkImage>(image.image),
                                                  {aspect, 0, 1, 0, 1}, static_cast<VkFormat>(image.format), image.width, image.height,
                                                  read_write);
}

class NeuralBackendVk final : public NeuralBackend {
public:
    explicit NeuralBackendVk(vk::Context& ctx) : ctx_(ctx) {}
    ~NeuralBackendVk() override { Shutdown(); }

    const NeuralRenderingSupport& Support(const std::string& ini_runtime) override {
        if (probed_ && ini_runtime == probed_ini_) {
            return support_;
        }
        probed_ = true;
        probed_ini_ = ini_runtime;
        support_ = NeuralRenderingSupport{};
        if (!NeuralRenderingVendorSupported(ctx_.properties.vendorID)) {
            support_.reason = "needs an NVIDIA GeForce RTX GPU";
            support_.note = "pc_note_nr_gpu";
            return support_;
        }
        std::optional<NeuralRuntimeInfo> runtime = FindNeuralRuntime(ini_runtime);
        if (!runtime) {
            support_.reason =
                "no DLSS 5 runtime (nvngx_dlssnr.dll) found; put one next to pt.exe, set upscaling.neural_runtime in pt.ini, or "
                "install DLSS 5 with a community tool (NVIDIA does not distribute it)";
            support_.note = "pc_note_nr_runtime";
            return support_;
        }
        support_.runtime = runtime;
        LogInfo("neural rendering: runtime {} ({} bytes, sha256 {}, from {})", runtime->path.string(), runtime->size,
                runtime->sha256.empty() ? "unread" : runtime->sha256, runtime->source);
        if (!EnsureInit(runtime->path)) {
            support_.reason = std::format("NGX did not initialise with this runtime (code {:#x})", init_code_);
            support_.note = "pc_note_nr_create";
            support_.code = init_code_;
            return support_;
        }
        int available = 0;
        NVSDK_NGX_Parameter_GetI(params_, kNrAvailable, &available);
        if (!available) {
            int needs_driver = 0;
            unsigned int major = 0;
            unsigned int minor = 0;
            NVSDK_NGX_Parameter_GetI(params_, kNrNeedsDriver, &needs_driver);
            NVSDK_NGX_Parameter_GetUI(params_, kNrMinMajor, &major);
            NVSDK_NGX_Parameter_GetUI(params_, kNrMinMinor, &minor);
            if (needs_driver && (major || minor)) {
                support_.reason = std::format("the NVIDIA driver is too old for DLSS 5 (needs {}.{} or newer)", major, minor);
                support_.note = "pc_note_nr_driver";
            } else {
                support_.reason =
                    "NVIDIA's NGX core will not run DLSS 5 here (this GPU or driver, or a runtime its signature and architecture "
                    "checks reject). The port does not bypass those checks";
                support_.note = "pc_note_nr_create";
            }
            LogInfo("neural rendering: DLSSNR.Available 0 (needs_driver {}, min {}.{})", needs_driver, major, minor);
            return support_;
        }
        support_.available = true;
        LogInfo("neural rendering: DLSS 5 available (runtime {})", runtime->path.string());
        return support_;
    }

    bool Create(void* cmd, uint32_t width, uint32_t height, NeuralStyle style, float intensity, float scaling_ratio) override {
        Release();
        if (!params_ || !support_.available) {
            return false;
        }
        NVSDK_NGX_Parameter_SetUI(params_, kWidth, width);
        NVSDK_NGX_Parameter_SetUI(params_, kHeight, height);
        NVSDK_NGX_Parameter_SetF(params_, kScalingRatio, scaling_ratio);
        NVSDK_NGX_Parameter_SetUI(params_, kRenderPreset, 1);
        NVSDK_NGX_Parameter_SetUI(params_, kStyle, static_cast<unsigned int>(style));
        NVSDK_NGX_Parameter_SetF(params_, kIntensity, intensity);
        NVSDK_NGX_Parameter_SetI(params_, kUseAutoMask, 0);
        NVSDK_NGX_Parameter_SetI(params_, kDepthInverted, 1);
        const NVSDK_NGX_Result result =
            NVSDK_NGX_VULKAN_CreateFeature(reinterpret_cast<VkCommandBuffer>(cmd), kFeatureNeural, params_, &feature_);
        if (NVSDK_NGX_FAILED(result) || !feature_) {
            LogError("neural rendering: feature 18 create failed ({:#x}) for {}x{}", static_cast<uint32_t>(result), width, height);
            feature_ = nullptr;
            support_.code = static_cast<uint32_t>(result);
            return false;
        }
        width_ = width;
        height_ = height;
        LogInfo("neural rendering: feature 18 created for {}x{}, style {}, intensity {:.2f}, scaling ratio {:.3f}", width, height,
                static_cast<int>(style), intensity, scaling_ratio);
        return true;
    }

    bool Created() const override { return feature_ != nullptr; }

    bool Dispatch(const NeuralDispatch& d) override {
        if (!feature_ || !params_) {
            return false;
        }
        NVSDK_NGX_Resource_VK color = Resource(d.color, VK_IMAGE_ASPECT_COLOR_BIT, false);
        NVSDK_NGX_Resource_VK output = Resource(d.output, VK_IMAGE_ASPECT_COLOR_BIT, true);
        NVSDK_NGX_Resource_VK depth = Resource(d.depth, VK_IMAGE_ASPECT_DEPTH_BIT, false);
        NVSDK_NGX_Resource_VK motion = Resource(d.motion, VK_IMAGE_ASPECT_COLOR_BIT, false);
        NVSDK_NGX_Parameter_SetVoidPointer(params_, kColor, &color);
        NVSDK_NGX_Parameter_SetVoidPointer(params_, kOutput, &output);
        NVSDK_NGX_Parameter_SetVoidPointer(params_, kDepth, &depth);
        NVSDK_NGX_Parameter_SetVoidPointer(params_, kMVec, &motion);
        NVSDK_NGX_Resource_VK control{};
        if (d.use_control_mask && d.control_mask.image) {
            control = Resource(d.control_mask, VK_IMAGE_ASPECT_COLOR_BIT, false);
            NVSDK_NGX_Parameter_SetVoidPointer(params_, kControlMask, &control);
        }
        NVSDK_NGX_Parameter_SetUI(params_, kWidth, d.color.width);
        NVSDK_NGX_Parameter_SetUI(params_, kHeight, d.color.height);
        NVSDK_NGX_Parameter_SetI(params_, kDepthInverted, d.depth_inverted ? 1 : 0);
        NVSDK_NGX_Parameter_SetF(params_, kMVecScaleX, d.motion_scale_x);
        NVSDK_NGX_Parameter_SetF(params_, kMVecScaleY, d.motion_scale_y);
        NVSDK_NGX_Parameter_SetF(params_, kScalingRatio, d.scaling_ratio);
        NVSDK_NGX_Parameter_SetI(params_, kReset, d.reset ? 1 : 0);
        NVSDK_NGX_Parameter_SetUI(params_, kStyle, static_cast<unsigned int>(d.style));
        NVSDK_NGX_Parameter_SetF(params_, kIntensity, d.intensity);
        const NVSDK_NGX_Result result =
            NVSDK_NGX_VULKAN_EvaluateFeature(reinterpret_cast<VkCommandBuffer>(d.cmd), feature_, params_, nullptr);
        if (NVSDK_NGX_FAILED(result)) {
            if (!error_logged_) {
                LogError("neural rendering: evaluate failed ({:#x})", static_cast<uint32_t>(result));
                error_logged_ = true;
            }
            return false;
        }
        return true;
    }

    void Release() override {
        if (feature_) {
            vkDeviceWaitIdle(ctx_.device);
            NVSDK_NGX_VULKAN_ReleaseFeature(feature_);
            feature_ = nullptr;
        }
        error_logged_ = false;
    }

    float LastCostMs() const override { return cost_ms_; }

private:
    bool EnsureInit(const std::filesystem::path& runtime) {
        if (initialized_) {
            return true;
        }
        feature_dir_ = runtime.parent_path().wstring();
        exe_dir_ = ExecutableDir().wstring();
        paths_[0] = feature_dir_.c_str();
        paths_[1] = exe_dir_.c_str();
        NVSDK_NGX_FeatureCommonInfo common{};
        common.PathListInfo.Path = paths_;
        common.PathListInfo.Length = feature_dir_ == exe_dir_ ? 1 : 2;
        common.LoggingInfo.MinimumLoggingLevel = std::getenv("PT_UPSCALE_CHECK") ? NVSDK_NGX_LOGGING_LEVEL_ON : NVSDK_NGX_LOGGING_LEVEL_OFF;
        const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, DataPath().c_str(), ctx_.instance, ctx_.physical, ctx_.device,
            vkGetInstanceProcAddr, vkGetDeviceProcAddr, &common, NVSDK_NGX_Version_API);
        if (NVSDK_NGX_FAILED(result)) {
            init_code_ = static_cast<uint32_t>(result);
            LogWarn("neural rendering: NVSDK_NGX_VULKAN_Init_with_ProjectID failed ({:#x})", init_code_);
            return false;
        }
        initialized_ = true;
        if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetCapabilityParameters(&params_)) || !params_) {
            init_code_ = 1;
            LogWarn("neural rendering: NGX capability query failed");
            return false;
        }
        return true;
    }

    void Shutdown() {
        Release();
        if (params_) {
            NVSDK_NGX_VULKAN_DestroyParameters(params_);
            params_ = nullptr;
        }
        if (initialized_) {
            vkDeviceWaitIdle(ctx_.device);
            NVSDK_NGX_VULKAN_Shutdown1(ctx_.device);
            initialized_ = false;
        }
    }

    vk::Context& ctx_;
    NVSDK_NGX_Parameter* params_ = nullptr;
    NVSDK_NGX_Handle* feature_ = nullptr;
    bool initialized_ = false;
    bool probed_ = false;
    bool error_logged_ = false;
    std::string probed_ini_;
    NeuralRenderingSupport support_;
    uint32_t init_code_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    float cost_ms_ = 0.0f;
    std::wstring feature_dir_;
    std::wstring exe_dir_;
    const wchar_t* paths_[2] = {nullptr, nullptr};
};

}

std::unique_ptr<NeuralBackend> CreateNeuralBackend(vk::Context& ctx) {
    return std::make_unique<NeuralBackendVk>(ctx);
}

}

#else

namespace pt {

std::unique_ptr<NeuralBackend> CreateNeuralBackend(vk::Context&) {
    return nullptr;
}

}

#endif
