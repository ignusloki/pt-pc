#include "engine/render/d3d12/backend/d3d12_neural.h"
#include "engine/render/upscale/neural_native.h"
#if defined(_WIN32) && defined(PT_WITH_DLSS)
#include <d3dcompiler.h>
#include <nvsdk_ngx.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "engine/core/log.h"
#include "engine/platform/os.h"
#include "engine/render/d3d12/backend/d3d12_native.h"
#include "engine/render/d3d12/backend/d3d12_neural_codec.h"
#include "engine/render/d3d12/backend/reshade_native.h"

namespace pt::gpu::d3d12 {
namespace {
using Microsoft::WRL::ComPtr;
using CreateFn = decltype(&NVSDK_NGX_D3D12_CreateFeature);
using EvaluateFn = decltype(&NVSDK_NGX_D3D12_EvaluateFeature);
using ReleaseFn = decltype(&NVSDK_NGX_D3D12_ReleaseFeature);
using InitFn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);

NVSDK_NGX_Result CreateSafely(CreateFn fn, ID3D12GraphicsCommandList* list, NVSDK_NGX_Parameter* params, NVSDK_NGX_Handle** feature) {
    __try { return fn(list, NVSDK_NGX_Feature_Reserved18, params, feature); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}
NVSDK_NGX_Result EvaluateSafely(EvaluateFn fn, ID3D12GraphicsCommandList* list, NVSDK_NGX_Handle* feature, NVSDK_NGX_Parameter* params) {
    __try { return fn(list, feature, params, nullptr); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}
unsigned long ReleaseSafely(ReleaseFn fn, NVSDK_NGX_Handle* feature) {
    __try { fn(feature); return 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}
unsigned long ShutdownSafely(decltype(&NVSDK_NGX_D3D12_Shutdown1) fn, ID3D12Device* device) {
    __try { fn(device); return 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}

class NativeNr final : public PostUpscaleNeural {
public:
    explicit NativeNr(gpu::Device& device) : owner_(device), device_(ReShadeNative(NativeDevice(device))) {
        status_.runtime = NativeNeuralRuntime();
        status_.available = true;
        status_.reason = "native NR has not evaluated yet";
    }
    const NeuralRenderingSupport& Status() const override { return status_; }
    ~NativeNr() override {
        owner_.WaitIdle();
        if (feature_ && release_) {
            if (const unsigned long fault = ReleaseSafely(release_, feature_)) LogError("native nr d3d12: feature release faulted ({:#x}) at teardown", fault);
        }
        if (params_) NVSDK_NGX_D3D12_DestroyParameters(params_);
        if (direct_shutdown_) {
            if (const unsigned long fault = ShutdownSafely(direct_shutdown_, device_.Get())) LogError("native nr d3d12: companion shutdown faulted ({:#x}) at teardown", fault);
            else LogInfo("native nr d3d12: companion runtime shut down");
        }
    }
    bool Record(const UpscaleDispatch& d) override {
        if (!d.neural_enabled) {
            if (was_enabled_) LogInfo("native nr d3d12: off, feature retained, evaluate skipped");
            was_enabled_ = false;
            return true;
        }
        if (failed_ || !d.list || !d.output.handle) return false;
        const auto native_list = ReShadeNative(Native(*d.list));
        ID3D12Resource* color = Native(d.output.handle);
        const auto desc = color->GetDesc();
        if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || !(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
            return Fail("output must be an RGBA16F UAV", 0);
        }
        static const char* scale_override = std::getenv("PT_NR_SCALE");
        const float scale = std::clamp(scale_override ? static_cast<float>(std::atof(scale_override)) : d.neural_scale, 0.25f, 1.0f);
        if (color != color_ || width_ != desc.Width || height_ != desc.Height || output_epoch_ != d.neural_output_epoch || small_model_ != d.neural_small_model ||
            scale_ != scale) {
            small_model_ = d.neural_small_model;
            scale_ = scale;
            if (!Prepare(native_list.Get(), color, static_cast<UINT>(desc.Width), desc.Height)) return false;
            output_epoch_ = d.neural_output_epoch;
        }
        auto* list = native_list.Get();
        static const char* every_override = std::getenv("PT_NR_EVERY");
        const unsigned every = every_override ? std::max(1u, static_cast<unsigned>(std::atoi(every_override))) : std::max(1u, d.neural_every);
        const bool reuse = every > 1 && was_enabled_ && evaluated_ && (frame_++ % every) != 0;
        const UINT motion_slot = kMotionSlot + (frame_ & 1);
        {
            ID3D12Resource* motion = d.motion.handle ? Native(d.motion.handle) : nullptr;
            D3D12_SHADER_RESOURCE_VIEW_DESC mv{}; mv.Format = motion ? motion->GetDesc().Format : DXGI_FORMAT_R16G16_FLOAT;
            mv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; mv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; mv.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(motion, &mv, Cpu(motion_slot));
        }
        static const char* history_override = std::getenv("PT_NR_HISTORY");
        const bool before_dlss = d.output.handle == d.color.handle;
        const bool keep = history_override ? *history_override == '1' : !before_dlss;
        boost_ = std::clamp(d.neural_intensity, 1.0f, 3.0f);
        const bool current_only = every == 1 && !keep && std::getenv("PT_NR_CODEC_REFERENCE") == nullptr;
        const bool history = keep && history_valid_ && was_enabled_ && !d.reset;
        ++history_flip_;
        auto history_written = [&] {
            D3D12_RESOURCE_BARRIER b[2]{};
            for (int i = 0; i < 2; ++i) { b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b[i].UAV.pResource = history_[i].Get(); }
            list->ResourceBarrier(2, b);
            history_valid_ = true;
        };
        if (reuse) {
            if (d.neural_queries) d.list->ResetQueries(d.neural_queries, 0, 4);
            if (d.neural_queries) { d.list->WriteTimestamp(d.neural_queries, 0); d.list->WriteTimestamp(d.neural_queries, 1); d.list->WriteTimestamp(d.neural_queries, 2); }
            Barrier(list, proxy_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(list, output_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Bind(list, resolve_.Get(), 3, 5, width_, height_, net_width_, net_height_, true, motion_slot, history);
            list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
            D3D12_RESOURCE_BARRIER written{}; written.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; written.UAV.pResource = color;
            list->ResourceBarrier(1, &written);
            history_written();
            Barrier(list, proxy_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(list, output_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (d.neural_queries) d.list->WriteTimestamp(d.neural_queries, 3);
            return true;
        }
        auto stamp = [&](unsigned index) {
            if (d.neural_queries) d.list->WriteTimestamp(d.neural_queries, index);
        };
        if (d.neural_queries) d.list->ResetQueries(d.neural_queries, 0, 4);
        stamp(0);
        Barrier(list, color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Bind(list, encode_.Get(), 0, 2, net_width_, net_height_, width_, height_);
        list->Dispatch((net_width_ + 15) / 16, (net_height_ + 15) / 16, 1);
        Barrier(list, proxy_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        params_->Reset();
        params_->Set("DLSSNR.Color", proxy_.Get()); params_->Set("DLSSNR.Output", output_.Get());
        params_->Set("DLSSNR.MVec", d.motion.handle ? Native(d.motion.handle) : nullptr);
        params_->Set("DLSSNR.Depth", d.depth.handle ? Native(d.depth.handle) : nullptr);
        Subrect("Color", net_width_, net_height_); Subrect("Output", net_width_, net_height_);
        Subrect("MVec", d.motion.extent.width, d.motion.extent.height); Subrect("Depth", d.depth.extent.width, d.depth.extent.height);
        params_->Set("DLSSNR.DepthInverted", 1u);
        const float render_w = static_cast<float>(std::max(1u, d.render.width)), render_h = static_cast<float>(std::max(1u, d.render.height));
        params_->Set("DLSSNR.MVecScaleX", d.motion_scale.x * net_width_ / render_w); params_->Set("DLSSNR.MVecScaleY", d.motion_scale.y * net_height_ / render_h);
        params_->Set("DLSSNR.ScalingRatio", 1.0f); params_->Set("DLSSNR.Scale", 1.0f);
        params_->Set("DLSSNR.Enabled", 1u); params_->Set("DLSSNR.Reset", d.reset || !was_enabled_ ? 1u : 0u);
        params_->Set("DLSSNR.Style", static_cast<unsigned>(d.neural_style));
        params_->Set("DLSSNR.Intensity", std::clamp(d.neural_intensity, 0.0f, 1.0f));
        params_->Set("DLSSNR.LocalToneStrength", 0.0f); params_->Set("DLSSNR.LocalStructureStrength", 1.0f);
        params_->Set("DLSSNR.SkinStructureStrength", -1.0f); params_->Set("DLSSNR.UseAutoMask", 0u); params_->Set("DLSSNR.UICorrection", 0u);
        params_->Set("DLSS.Pre.Exposure", 1.0f); params_->Set("DLSS.Exposure.Scale", 1.0f);
        stamp(1);
        const auto result = EvaluateSafely(evaluate_, list, feature_, params_);
        stamp(2);
        Barrier(list, color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (NVSDK_NGX_FAILED(result)) {
            Barrier(list, proxy_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            return Fail("evaluate", result);
        }
        Barrier(list, output_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Bind(list, current_only ? resolve_current_.Get() : resolve_.Get(), 3, 5, width_, height_, net_width_, net_height_, false, motion_slot, history);
        list->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);
        D3D12_RESOURCE_BARRIER written{}; written.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; written.UAV.pResource = color;
        list->ResourceBarrier(1, &written);
        if (current_only) history_valid_ = false;
        else history_written();
        Barrier(list, proxy_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(list, output_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        stamp(3);
        evaluated_ = true;
        if (!was_enabled_) LogInfo("native nr d3d12: feature 18 evaluate succeeded, post-upscale/native {}x{} (network {}x{}), style {}, intensity {:.2f}, no ReShade control", width_, height_,
                                   net_width_, net_height_, static_cast<int>(d.neural_style), d.neural_intensity);
        was_enabled_ = true;
        status_.available = true;
        status_.reason = "native feature 18 evaluate succeeded";
        return true;
    }
private:
    bool Fail(const char* action, unsigned code) {
        LogError("native nr d3d12: {} failed ({:#x}); native context disabled for this session", action, code);
        failed_ = true;
        status_.available = false;
        status_.code = code;
        status_.reason = std::string("native NR ") + action + " failed";
        status_.note = "pc_note_nr_native_failed";
        return false;
    }
    static void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &b);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT index) const {
        auto h = heap_->GetCPUDescriptorHandleForHeapStart(); h.ptr += static_cast<SIZE_T>(index) * stride_; return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT index) const {
        auto h = heap_->GetGPUDescriptorHandleForHeapStart(); h.ptr += static_cast<UINT64>(index) * stride_; return h;
    }
    void Bind(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pipeline, UINT srv, UINT uav, UINT width, UINT height, UINT other_width, UINT other_height,
              bool reuse = false, UINT motion = kMotionSlot, bool history = false) {
        ID3D12DescriptorHeap* heaps[] = {heap_.Get()}; list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root_.Get()); list->SetPipelineState(pipeline);
        list->SetComputeRootDescriptorTable(0, Gpu(srv)); list->SetComputeRootDescriptorTable(1, Gpu(uav));
        list->SetComputeRootDescriptorTable(3, Gpu(motion));
        list->SetComputeRootDescriptorTable(4, Gpu(kHistorySlot + 2 * (history_flip_ & 1u)));
        static const char* delta_override = std::getenv("PT_NR_DELTA");
        const bool delta = delta_override ? *delta_override == '1' : (net_width_ != width_ || net_height_ != height_);
        const std::array<unsigned, 12> constants = {width, height, other_width, other_height, 0x3f800000, reuse ? 0x3f800000u : 0u,
                                                   history ? 0x3f800000u : 0u, delta ? 0x3f800000u : 0u,
                                                   std::bit_cast<unsigned>(boost_), 0, 0, 0};
        list->SetComputeRoot32BitConstants(2, 12, constants.data(), 0);
    }
    void Subrect(const char* kind, UINT width, UINT height) {
        const std::string prefix = std::string("DLSSNR.") + kind + "Subrect";
        params_->Set((prefix + "BaseX").c_str(), 0u); params_->Set((prefix + "BaseY").c_str(), 0u);
        params_->Set((prefix + "Width").c_str(), width); params_->Set((prefix + "Height").c_str(), height);
    }
    bool Codec() {
        if (root_) return true;
        const HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto compile_shader = compiler ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
        const auto serialize = reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeRootSignature"));
        if (!compile_shader || !serialize) return Fail("codec compiler exports", GetLastError());
        D3D12_DESCRIPTOR_RANGE ranges[4]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
        ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0};
        ranges[3] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 0, 0};
        D3D12_ROOT_PARAMETER parameters[5]{};
        for (UINT n = 0; n < 2; ++n) {
            parameters[n].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[n].DescriptorTable = {1, &ranges[n]};
        }
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants = {0, 0, 12};
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[3].DescriptorTable = {1, &ranges[2]};
        parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[4].DescriptorTable = {1, &ranges[3]};
        D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC description{}; description.NumParameters = 5; description.pParameters = parameters;
        description.NumStaticSamplers = 1; description.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> blob, errors;
        if (FAILED(serialize(&description, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
            FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)))) return Fail("codec root signature", 0);
        for (const auto& stage : {std::pair{"Encode", std::addressof(encode_)}, std::pair{"Resolve", std::addressof(resolve_)}}) {
            blob.Reset(); errors.Reset();
            const auto compile = compile_shader(kNativeNrCodec, std::strlen(kNativeNrCodec), "native-nr-codec", nullptr, nullptr, stage.first, "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
            if (FAILED(compile)) {
                if (errors) LogError("native nr d3d12: codec compiler {}", static_cast<const char*>(errors->GetBufferPointer()));
                return Fail("codec compile", compile);
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC pso{}; pso.pRootSignature = root_.Get();
            pso.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
            if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(stage.second->ReleaseAndGetAddressOf())))) return Fail("codec pipeline", 0);
        }
        const D3D_SHADER_MACRO defines[] = {{"NR_CURRENT_FRAME", "1"}, {nullptr, nullptr}};
        blob.Reset(); errors.Reset();
        const auto compile = compile_shader(kNativeNrCodec, std::strlen(kNativeNrCodec), "native-nr-current", defines, nullptr, "Resolve", "cs_5_1",
                                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
        if (FAILED(compile)) {
            if (errors) LogError("native nr d3d12: codec compiler {}", static_cast<const char*>(errors->GetBufferPointer()));
            return Fail("current-frame codec compile", compile);
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC current{}; current.pRootSignature = root_.Get();
        current.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
        if (FAILED(device_->CreateComputePipelineState(&current, IID_PPV_ARGS(&resolve_current_)))) return Fail("current-frame codec pipeline", 0);
        return true;
    }
    bool Prepare(ID3D12GraphicsCommandList* list, ID3D12Resource* color, UINT width, UINT height) {
        owner_.WaitIdle();
        if (feature_) { release_(feature_); feature_ = nullptr; }
        if (!params_ && NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_AllocateParameters(&params_))) return Fail("parameter allocation", 0);
        if (!Codec()) return false;
        const UINT net_width = std::max(1u, static_cast<UINT>(width * scale_ + 0.5f)), net_height = std::max(1u, static_cast<UINT>(height * scale_ + 0.5f));
        params_->Reset(); params_->Set("CreationNodeMask", 1u); params_->Set("VisibilityNodeMask", 1u);
        for (const char* key : {"DLSSNR.Width", "DLSSNR.InputWidth", "DLSSNR.OutputWidth", "DLSSNR.Output.Width"}) params_->Set(key, net_width);
        for (const char* key : {"DLSSNR.Height", "DLSSNR.InputHeight", "DLSSNR.OutputHeight", "DLSSNR.Output.Height"}) params_->Set(key, net_height);
        params_->Set("DLSSNR.Upscaling", 0u); params_->Set("DLSSNR.Scale", 1.0f); params_->Set("DLSSNR.ScalingRatio", 1.0f);
        params_->Set("DLSSNR.Hint.Render.Preset", small_model_ ? 0x80000000u : 1u); params_->Set("DLSS.Feature.Create.Flags", 0u);
        if (!compat_armed_) {
            compat_armed_ = true;
            const std::filesystem::path code = nr_native::CodePath();
            const char* compat = std::getenv("PT_NR_COMPAT");
            const std::filesystem::path path = nr_native::CompanionPath();
            if ((!code.empty() || (compat && *compat == '1')) && !path.empty()) {
                if (const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
                    const auto set_code = reinterpret_cast<void (*)(const wchar_t*)>(GetProcAddress(module, "NativeNrSetCodeDir"));
                    if (set_code && !code.empty()) set_code(code.c_str());
                    if (!set_code && !code.empty())
                        LogWarn("native nr d3d12: companion {} has no NativeNrSetCodeDir export; installed GPU code cannot be passed to this companion", os::PathToUtf8(path));
                    if (const auto enable = reinterpret_cast<int (*)()>(GetProcAddress(module, "NativeNrEnableCompatibility")))
                        LogInfo("native nr d3d12: compatibility layer {} (GPU code {})", enable(), code.empty() ? std::string("from PT_NR_CUBIN_DIR") : os::PathToUtf8(code));
                } else {
                    LogWarn("native nr d3d12: compatibility companion {} not loaded ({})", os::PathToUtf8(path), GetLastError());
                }
            }
        }
        auto result = CreateSafely(create_, list, params_, &feature_);
        if ((NVSDK_NGX_FAILED(result) || !feature_) && !companion_) {
            const std::filesystem::path path = nr_native::CompanionPath();
            const auto& runtime = status_.runtime;
            if (!path.empty() && runtime) {
                companion_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (!companion_) return Fail("explicit companion load", GetLastError());
                const auto load = reinterpret_cast<int(*)(const wchar_t*)>(GetProcAddress(companion_, "NativeNrLoad"));
                const auto init = reinterpret_cast<InitFn>(GetProcAddress(companion_, "NativeNrInit"));
                const auto create = reinterpret_cast<CreateFn>(GetProcAddress(companion_, "NativeNrCreate"));
                const auto evaluate = reinterpret_cast<EvaluateFn>(GetProcAddress(companion_, "NativeNrEvaluate"));
                const auto release = reinterpret_cast<ReleaseFn>(GetProcAddress(companion_, "NativeNrRelease"));
                const auto shutdown = reinterpret_cast<decltype(direct_shutdown_)>(GetProcAddress(companion_, "NativeNrShutdown"));
                if (!load || !init || !create || !evaluate || !release || !shutdown) return Fail("companion exports", 0);
                if (const int loaded = load(runtime->path.c_str()); loaded != 0) return Fail("companion runtime load", static_cast<unsigned>(loaded));
                const auto data = std::filesystem::temp_directory_path().wstring();
                result = init(0x1000000, data.c_str(), device_.Get(), NVSDK_NGX_Version_API, params_);
                if (NVSDK_NGX_FAILED(result)) return Fail("companion initialize", result);
                create_ = create;
                evaluate_ = evaluate;
                release_ = release;
                direct_shutdown_ = shutdown;
                LogInfo("native nr d3d12: companion {}, runtime {} sha256 {}", os::PathToUtf8(path), os::PathToUtf8(runtime->path), runtime->sha256);
                unsigned preset = small_model_ ? 0x80000000u : 0x80000001u;
                if (const char* chosen = std::getenv("PT_NR_PRESET")) preset = static_cast<unsigned>(std::strtoul(chosen, nullptr, 0));
                params_->Set("DLSSNR.Hint.Render.Preset", preset);
                LogInfo("native nr d3d12: render preset 0x{:x}", preset);
                result = CreateSafely(create_, list, params_, &feature_);
            }
        }
        if ((NVSDK_NGX_FAILED(result) || !feature_) && create_) {
            double used = 0.0, budget = 0.0;
            owner_.DeviceLocalUsage(used, budget);
            const bool reshade = GetModuleHandleW(L"ReShade64.dll") != nullptr || GetModuleHandleW(L"dxgi.dll") != nullptr &&
                                 std::filesystem::exists(std::filesystem::path(nr_native::CompanionPath()).parent_path() / "dxgi.dll");
            LogWarn("native nr d3d12: feature 18 create failed ({:#x}) with the {} network at {}x{}: video memory {:.0f} of {:.0f} MB in use{}",
                    static_cast<unsigned>(result), small_model_ ? "small" : "large", net_width, net_height, used / 1048576.0, budget / 1048576.0,
                    reshade ? "; a ReShade dxgi.dll next to pt.exe wraps the device (the retired DLSS 5 route's, remove it)" : "");
        }
        if (companion_) {
            if (const auto diagnostics = reinterpret_cast<void (*)(char*, unsigned)>(GetProcAddress(companion_, "NativeNrGetDiagnostics"))) {
                char text[768]{};
                diagnostics(text, sizeof(text));
                LogInfo("native nr d3d12: create diagnostics: {}", text);
            } else if (NVSDK_NGX_FAILED(result) || !feature_) {
                LogWarn("native nr d3d12: companion has no NativeNrGetDiagnostics export; its GPU code failure details are unavailable");
            }
        }
        if (NVSDK_NGX_FAILED(result) || !feature_) return Fail("feature 18 create", result);
        D3D12_HEAP_PROPERTIES properties{}; properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{}; texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; texture.Width = net_width; texture.Height = net_height;
        texture.DepthOrArraySize = 1; texture.MipLevels = 1; texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; texture.SampleDesc.Count = 1;
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        proxy_.Reset(); output_.Reset(); heap_.Reset();
        for (auto* target : {std::addressof(proxy_), std::addressof(output_)}) {
            if (FAILED(device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(target->ReleaseAndGetAddressOf())))) return Fail("workset texture", 0);
        }
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = kHistorySlot + 4; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&heap_)))) return Fail("workset descriptors", 0);
        stride_ = device_->GetDescriptorHandleIncrementSize(heap.Type);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = texture.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(color, &srv, Cpu(0)); device_->CreateShaderResourceView(nullptr, &srv, Cpu(1));
        device_->CreateShaderResourceView(proxy_.Get(), &srv, Cpu(3)); device_->CreateShaderResourceView(output_.Get(), &srv, Cpu(4));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = texture.Format; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(proxy_.Get(), nullptr, &uav, Cpu(2)); device_->CreateUnorderedAccessView(color, nullptr, &uav, Cpu(5));
        D3D12_SHADER_RESOURCE_VIEW_DESC none{}; none.Format = DXGI_FORMAT_R16G16_FLOAT; none.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        none.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; none.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(nullptr, &none, Cpu(kMotionSlot)); device_->CreateShaderResourceView(nullptr, &none, Cpu(kMotionSlot + 1));
        evaluated_ = false;
        D3D12_RESOURCE_DESC kept = texture; kept.Width = width; kept.Height = height;
        for (auto& h : history_) {
            if (FAILED(device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &kept, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(h.ReleaseAndGetAddressOf())))) return Fail("history texture", 0);
        }
        device_->CreateUnorderedAccessView(history_[0].Get(), nullptr, &uav, Cpu(kHistorySlot)); device_->CreateUnorderedAccessView(history_[1].Get(), nullptr, &uav, Cpu(kHistorySlot + 1));
        device_->CreateUnorderedAccessView(history_[1].Get(), nullptr, &uav, Cpu(kHistorySlot + 2)); device_->CreateUnorderedAccessView(history_[0].Get(), nullptr, &uav, Cpu(kHistorySlot + 3));
        history_valid_ = false;
        color_ = color; width_ = width; height_ = height; net_width_ = net_width; net_height_ = net_height; was_enabled_ = false;
        LogInfo("native nr d3d12: feature 18 created {}x{} for a {}x{} frame ({} network), persistent two-texture workset, no per-frame copies or queue waits",
                net_width, net_height, width, height, small_model_ ? "small" : "large");
        return true;
    }
    gpu::Device& owner_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> encode_, resolve_, resolve_current_;
    float boost_ = 1.0f;
    ComPtr<ID3D12Resource> proxy_, output_;
    ComPtr<ID3D12DescriptorHeap> heap_;
    ID3D12Resource* color_ = nullptr;
    UINT width_ = 0, height_ = 0, net_width_ = 0, net_height_ = 0, stride_ = 0;
    float scale_ = 1.0f;
    static constexpr UINT kMotionSlot = 6;
    static constexpr UINT kHistorySlot = 8;
    ComPtr<ID3D12Resource> history_[2];
    unsigned history_flip_ = 0;
    bool history_valid_ = false;
    uint64_t frame_ = 0;
    bool evaluated_ = false;
    uint64_t output_epoch_ = 0;
    bool small_model_ = false;
    NVSDK_NGX_Parameter* params_ = nullptr;
    NVSDK_NGX_Handle* feature_ = nullptr;
    CreateFn create_ = &NVSDK_NGX_D3D12_CreateFeature;
    EvaluateFn evaluate_ = &NVSDK_NGX_D3D12_EvaluateFeature;
    ReleaseFn release_ = &NVSDK_NGX_D3D12_ReleaseFeature;
    decltype(&NVSDK_NGX_D3D12_Shutdown1) direct_shutdown_ = nullptr;
    HMODULE companion_ = nullptr;
    bool failed_ = false, was_enabled_ = false, compat_armed_ = false;
    NeuralRenderingSupport status_;
};
}
std::unique_ptr<PostUpscaleNeural> CreatePostUpscaleNeural(gpu::Device& device) { return std::make_unique<NativeNr>(device); }
}
#elif defined(_WIN32)
namespace pt::gpu::d3d12 {
std::unique_ptr<PostUpscaleNeural> CreatePostUpscaleNeural(gpu::Device&) { return nullptr; }
}
#endif
