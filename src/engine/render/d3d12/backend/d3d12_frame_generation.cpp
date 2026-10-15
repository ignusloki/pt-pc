#ifdef _WIN32

#include <format>

#include "engine/core/log.h"
#include "engine/render/d3d12/backend/d3d12_internal.h"
#include "engine/render/d3d12/vk_d3d12_share.h"

namespace pt::gpu::d3d12 {

bool DeviceImpl::FrameGenAvailable(FrameGenOwner owner, std::string& reason) {
    reason.clear();
    switch (owner) {
    case FrameGenOwner::None: return true;
    case FrameGenOwner::Xess: {
        const uint32_t build = pt::d3d12::WindowsBuild();
        if (!XefgWindowsBuildSupported(build)) {
            reason = std::format("needs Windows 10 version 21H1 (build {}) or newer; this is build {}", kXefgMinWindowsBuild, build);
            return false;
        }
        if (!bridge_.ShaderModel(D3D_SHADER_MODEL_6_4)) {
            reason = std::format("needs a DirectX 12 GPU with Shader Model 6.4 ({} has less)", bridge_.AdapterName());
            return false;
        }
        return pt::d3d12::XefgPresenter::LibrariesPresent(reason);
    }
    case FrameGenOwner::Fsr: return FsrFrameGenAvailable(bridge_, reason);
    case FrameGenOwner::Dlss: return pt::d3d12::SlDlssgPresenter::Available(reason);
    }
    return false;
}

void DeviceImpl::SetGenerating(bool on) {
    generating_ = on;
    if (active_ != &presenter_) {
        active_->SetGenerating(on);
    }
}

bool DeviceImpl::CreateOwnerSwapchain(const pt::d3d12::SwapchainDesc& desc, std::string& error) {
    if (!FrameGenAvailable(wanted_owner_, error)) {
        return false;
    }
    pt::d3d12::SwapchainOwner* owner = nullptr;
    if (wanted_owner_ == FrameGenOwner::Xess) {
        if (!xefg_) {
            xefg_ = std::make_unique<pt::d3d12::XefgPresenter>(bridge_);
        }
        if (!xefg_->Load(error)) {
            return false;
        }
        owner = xefg_.get();
    } else if (wanted_owner_ == FrameGenOwner::Fsr) {
        if (!fsr_fg_) {
            fsr_fg_ = CreateFsrFgPresenter(bridge_, error);
            if (!fsr_fg_) {
                return false;
            }
        }
        owner = fsr_fg_.get();
    } else if (wanted_owner_ == FrameGenOwner::Dlss) {
        if (!dlss_fg_) {
            dlss_fg_ = std::make_unique<pt::d3d12::SlDlssgPresenter>(bridge_);
        }
        owner = dlss_fg_.get();
    }
    if (!owner) {
        error = "no such frame generation on DirectX 12";
        return false;
    }
    presenter_.Destroy();
    pt::d3d12::SwapchainDesc d = desc;
    d.ui_layer = true;
    if (!owner->Create(d, error)) {
        owner->Destroy();
        return false;
    }
    owner->SetGenerating(generating_);
    active_ = owner;
    active_owner_ = wanted_owner_;
    return true;
}

}

#endif
