#include <cstdio>
#include <cmath>
#include <string>

#include <filesystem>
#include <fstream>

#include "engine/render/d3d12/vk_d3d12_share.h"
#include "engine/render/upscale/fg_unlock.h"
#include "engine/render/upscale/neural_rendering.h"
#include "engine/render/upscale/neural_unlock.h"
#include "engine/render/upscale/upscale.h"

namespace {

int failures = 0;

void Expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

}

int main() {
    Expect(pt::AmdGcnGpu(pt::kAmdVendor, 64, 64), "Polaris (64 to 64) is GCN");
    Expect(!pt::AmdGcnGpu(pt::kAmdVendor, 32, 64), "RDNA (32 to 64) is not GCN");
    Expect(!pt::AmdGcnGpu(pt::kAmdVendor, 32, 32), "wave 32 only is not GCN");
    Expect(!pt::AmdGcnGpu(0x10DE, 32, 32), "NVIDIA is not GCN");
    Expect(!pt::AmdGcnGpu(0x8086, 8, 32), "Intel is not GCN");
    Expect(!pt::AmdGcnGpu(0x10DE, 64, 64), "a non-AMD vendor at 64 to 64 is not GCN");
    pt::FrameGenKind kind = pt::FrameGenKind::Off;
    Expect(pt::ParseFrameGen("xess", kind) && kind == pt::FrameGenKind::Xess, "frame_generation = xess reads as XeSS-FG");
    Expect(std::string(pt::FrameGenKey(pt::FrameGenKind::Xess)) == "xess", "XeSS-FG writes xess");
    Expect(pt::ParseFrameGen("fsr3", kind) && kind == pt::FrameGenKind::Fsr, "fsr3 still reads as FSR 3");
    Expect(!pt::XefgWindowsBuildSupported(19042) && pt::XefgWindowsBuildSupported(19043) && pt::XefgWindowsBuildSupported(26200),
           "XeSS-FG Windows build gate at 19043");
    Expect(pt::XefgDxgiFormat(VK_FORMAT_B8G8R8A8_UNORM) == 87 && pt::XefgDxgiFormat(VK_FORMAT_R8G8B8A8_UNORM) == 28, "SDR formats");
    Expect(pt::XefgDxgiFormat(VK_FORMAT_R32_SFLOAT) == 41 && pt::XefgDxgiFormat(VK_FORMAT_R16G16_SFLOAT) == 34, "input formats");
    Expect(pt::XefgDxgiFormat(VK_FORMAT_R16G16B16A16_SFLOAT) == 0 && pt::XefgDxgiFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32) == 0,
           "HDR formats are not presented through XeSS-FG");
    Expect(std::string(pt::XefgNoteKey("needs a DirectX 12 GPU with Shader Model 6.4 (x has less)")) == "pc_note_xefg_gpu", "SM 6.4 note");
    Expect(std::string(pt::XefgNoteKey("needs Windows 10 version 21H1 (build 19043) or newer; this is build 19041")) == "pc_note_xefg_windows",
           "Windows note");
    Expect(std::string(pt::XefgNoteKey("libxess_fg.dll is missing next to pt.exe")) == "pc_note_frame_generation_missing", "missing DLL note");
    Expect(std::string(pt::XefgNoteKey("could not start and was turned off (x)")) == "pc_note_xefg_failed", "failed note");
    Expect(std::string(pt::XefgNoteKey("DLSS Frame Generation is loaded for this start; restart to switch to XeSS")) == "pc_note_xefg_restart",
           "restart note");
    Expect(std::string(pt::XefgNoteKey("needs a window")).empty(), "a reason without a note");
    pt::FrameGenPrepare p;
    p.position = glm::vec3(1.0f, 2.0f, 3.0f);
    p.forward = glm::vec3(0.0f, 0.0f, -1.0f);
    p.up = glm::vec3(0.0f, 1.0f, 0.0f);
    p.render = {1600, 900};
    p.fov_y = 1.0f;
    p.near_plane = 0.05f;
    const pt::XefgMatrices m = pt::XefgCameraMatrices(p);
    Expect(std::abs(m.view[12] + 1.0f) < 1e-5f && std::abs(m.view[13] + 2.0f) < 1e-5f && std::abs(m.view[14] + 3.0f) < 1e-5f,
           "view translation in elements 12 to 14");
    const float f = 1.0f / std::tan(0.5f);
    Expect(std::abs(m.projection[5] - f) < 1e-5f && std::abs(m.projection[0] - f * 900.0f / 1600.0f) < 1e-5f, "projection +y, aspect");
    Expect(m.projection[11] == -1.0f && std::abs(m.projection[14] - 0.05f) < 1e-7f && m.projection[10] == 0.0f, "reversed infinite depth");
    pt::NeuralRenderingKind nr = pt::NeuralRenderingKind::Dlss5;
    Expect(pt::ParseNeuralRendering("off", nr) && nr == pt::NeuralRenderingKind::Off, "neural_rendering = off");
    Expect(pt::ParseNeuralRendering("DLSS5", nr) && nr == pt::NeuralRenderingKind::Dlss5, "neural_rendering = dlss5, any case");
    Expect(!pt::ParseNeuralRendering("on", nr) && nr == pt::NeuralRenderingKind::Off, "an unknown value reads as off");
    Expect(std::string(pt::NeuralRenderingKey(pt::NeuralRenderingKind::Dlss5)) == "dlss5", "DLSS 5 writes dlss5");
    pt::NeuralStyle style = pt::NeuralStyle::Default;
    Expect(pt::ParseNeuralStyle("cinematic", style) && style == pt::NeuralStyle::Cinematic, "neural_style = cinematic");
    Expect(!pt::ParseNeuralStyle("vivid", style) && style == pt::NeuralStyle::Default, "an unknown style reads as default");
    Expect(pt::NeuralRenderingVendorSupported(0x10DE), "NVIDIA is the NGX vendor");
    Expect(!pt::NeuralRenderingVendorSupported(0x1002), "AMD has no NGX");
    Expect(!pt::NeuralRenderingVendorSupported(0x8086), "Intel has no NGX");
    Expect(pt::nr_unlock::SameFolder(std::filesystem::path("C:/Games/P.T. PC Port"), std::filesystem::path("C:/Games/P.T. PC Port/")),
           "a trailing separator names the same folder");
#ifdef _WIN32
    Expect(pt::nr_unlock::SameFolder(std::filesystem::path(L"C:\\Games\\P.T. PC Port"), std::filesystem::path(L"c:\\games\\p.t. pc port\\")),
           "backslashes and letter case name the same folder");
#endif
    Expect(!pt::nr_unlock::SameFolder(std::filesystem::path("C:/Games/P.T. PC Port"), std::filesystem::path("C:/Windows/System32")),
           "System32 is not the game folder");
    Expect(!pt::nr_unlock::SameFolder(std::filesystem::path("C:/Games/P.T. PC Port/reshade"), std::filesystem::path("C:/Games/P.T. PC Port/")),
           "the reshade subfolder is not the game folder");
    using pt::nr_unlock::GpuClass;
    Expect(pt::nr_unlock::ClassifyGpu(0x10DE, 0x2204, "NVIDIA GeForce RTX 3090") == GpuClass::NeedsUnlock, "RTX 3090 needs the unlock");
    Expect(pt::nr_unlock::ClassifyGpu(0x10DE, 0x1E87, "NVIDIA GeForce RTX 2080") == GpuClass::NeedsUnlock, "RTX 2080 needs the unlock");
    Expect(pt::nr_unlock::ClassifyGpu(0x10DE, 0x2684, "NVIDIA GeForce RTX 4090") == GpuClass::NeedsUnlock, "RTX 4090 needs the unlock");
    Expect(pt::nr_unlock::ClassifyGpu(0x10DE, 0x2B85, "NVIDIA GeForce RTX 5090") == GpuClass::Native, "RTX 5090 runs NVIDIA's runtime");
    Expect(pt::nr_unlock::ClassifyGpu(0x10DE, 0x2182, "NVIDIA GeForce GTX 1660 Ti") == GpuClass::Other, "GTX 16 has no tensor cores");
    Expect(pt::nr_unlock::ClassifyGpu(0x1002, 0x73BF, "AMD Radeon RX 6800 XT") == GpuClass::Other, "AMD gets neither");
    Expect(pt::nr_unlock::ClassifyGpu(0x10DE, 0, "NVIDIA GeForce RTX 5070 Laptop GPU") == GpuClass::Native, "RTX 50 by name");
    Expect(pt::nr_unlock::ClassifyGpu(0x10DE, 0, "NVIDIA GeForce RTX 3060") == GpuClass::NeedsUnlock, "RTX 30 by name");
    const auto install = pt::nr_unlock::ParseInstall(
        R"({"version":1,"tool":"2.0.8","complete":true,"path":"native","files":["dxgi.dll","renodx-dlss5.addon64","nvngx_dlssnr.dll","ReShade.ini"],)"
        R"("components":{"reshade":"6.8.0","renodx":"4.55","dlssnr":"310.8.SF-v2"}})");
    Expect(install && install->complete && install->route == "native" && install->addon == "4.55" && install->runtime == "310.8.SF-v2" &&
               install->files.size() == 4 && pt::nr_unlock::Usable(*install),
           "the installer's record reads, and is ours to drive");
    const auto other = pt::nr_unlock::ParseInstall(R"({"complete":true,"path":"optiscaler","files":["dxgi.dll"]})");
    Expect(other && !pt::nr_unlock::Usable(*other), "another route is not ours to drive");
    pt::nr_unlock::AddonSettings addon;
    addon.on = true;
    addon.style = 2;
    addon.intensity = 0.75f;
    const std::string ini = pt::nr_unlock::ApplyAddonSettings(
        "[ADDON]\nAddonPath=.\\\n\n[RenoDX.DLSS5]\nNeuralUplift=0\nNRLocalTone=0.8\n\n[STYLE]\nAlpha=1\n", addon);
    Expect(ini.find("NeuralUplift=1") != std::string::npos && ini.find("NRStyle=2") != std::string::npos &&
               ini.find("NRIntensity=0.75") != std::string::npos && ini.find("NRPaperWhiteScale=1.0") != std::string::npos &&
               ini.find("NRLocalTone=0.0") != std::string::npos && ini.find("NRLocalTone=0.8") == std::string::npos &&
               ini.find("EnableHooks=2") != std::string::npos && ini.find("[STYLE]\nAlpha=1") != std::string::npos &&
               ini.find("AddonPath=.\\") != std::string::npos,
           "the add-on's keys are set in place, a stale local tone replaced, the other sections kept");
    const std::string fresh = pt::nr_unlock::ApplyAddonSettings("", addon);
    Expect(fresh.find("[RenoDX.DLSS5]") == 0 && fresh.find("NeuralUplift=1") != std::string::npos, "the section is added when missing");
    addon.extra = pt::nr_unlock::ParseAddonKeys("NRPaperWhiteScale=4;NRNew=3");
    const std::string extra = pt::nr_unlock::ApplyAddonSettings("", addon);
    Expect(extra.find("NRPaperWhiteScale=4\n") != std::string::npos && extra.find("NRNew=3") != std::string::npos, "test keys override and add");
    pt::nr_unlock::AddonSettings parked;
    parked.parked = true;
    const std::string parked_ini = pt::nr_unlock::ApplyAddonSettings("[ADDON]\nAddonPath=.\\\n\n[RenoDX.DLSS5]\nNeuralUplift=1\n", parked);
    Expect(parked_ini.find("AddonPath=.\\pt-addons-parked\\\n") != std::string::npos && parked_ini.find("AddonPath=.\\\n") == std::string::npos &&
               parked_ini.find("NeuralUplift=0") != std::string::npos,
           "a start without the route parks the add-on");
    parked.parked = false;
    Expect(pt::nr_unlock::ApplyAddonSettings(parked_ini, parked).find("AddonPath=.\\\n") != std::string::npos, "a start with the route unparks it");
    using State = pt::nr_unlock::RouteReport::State;
    const auto ran = pt::nr_unlock::ParseReShadeLog(
        "10:27:44 | INFO | [DLSS 5 Neural Rendering] DLSS5 Generic: feature 18 created via the signed snippet\n"
        "10:27:44 | INFO | [DLSS 5 Neural Rendering] DLSS5 Generic: inline feature 18 evaluation succeeded (count=1)\n");
    Expect(ran.state == State::Running, "a feature 18 evaluation reads as running");
    const auto failed = pt::nr_unlock::ParseReShadeLog(
        "x | INFO | [DLSS 5 Neural Rendering] DLSS5 Generic: inline feature 18 evaluation succeeded (count=1)\n"
        "x | ERROR | [DLSS 5 Neural Rendering] DLSS5 Generic: feature 18 create failed with 0xBAD00001\n");
    Expect(failed.state == State::Failed && failed.code == "0xbad00001", "a create failure wins, with its code");
    const auto off = pt::nr_unlock::ParseReShadeLog("x | INFO | [DLSS 5 Neural Rendering] DLSS5 Generic: NR is currently OFF. To turn it on\n");
    Expect(off.state == State::Off, "the add-on switched off reads as off");
    const auto eval_fault = pt::nr_unlock::ParseReShadeLog(
        "x | ERROR | [DLSS 5 Neural Rendering] DLSS5 Generic: feature 18 evaluate failed with 0x4\n");
    Expect(eval_fault.state == State::Failed && eval_fault.code == "0x4", "an evaluate fault reads as failed, with its code");
    const auto newer = pt::nr_unlock::ParseInstall(
        R"({"complete":true,"path":"native","files":["renodx-dlss5.addon64"],"components":{"reshade":"7.1.2","renodx":"9.9.9","dlssnr":"310.9.ZZ"}})");
    Expect(newer && pt::nr_unlock::Usable(*newer) && newer->addon == "9.9.9" && newer->runtime == "310.9.ZZ",
           "an unknown component version is still read and driven");
    Expect(pt::fg_unlock::Applicable(0x10DE, "pc_note_dlssg_gpu"), "the FG unlock is offered when DLSS-G is greyed for the GPU");
    Expect(!pt::fg_unlock::Applicable(0x10DE, "pc_note_dlssg_hags"), "a HAGS or driver reason is not the FG unlock's case");
    Expect(!pt::fg_unlock::Applicable(0x1002, "pc_note_dlssg_gpu"), "a non-NVIDIA GPU is not offered the FG unlock");
    const auto fg = pt::fg_unlock::ParseInstall(
        R"({"complete":true,"tool_tag":"v0.4.0","runtime":"310.9.16","proxy":"version.dll",)"
        R"("files":[{"name":"version.dll","size":1,"sha256":"a"},{"name":"dlssg_sm86/runtime/nvngx_dlssg.dll","size":2,"sha256":"b"}]})");
    Expect(fg && fg->complete && fg->proxy == "version.dll" && fg->runtime == "310.9.16" && fg->files.size() == 2 && pt::fg_unlock::Usable(*fg),
           "the FG install record reads (files as objects), and is ours to drive");
    const auto fg_bad = pt::fg_unlock::ParseInstall(R"({"complete":true,"proxy":"version.dll","files":[{"name":"version.dll"}]})");
    Expect(fg_bad && !pt::fg_unlock::Usable(*fg_bad), "an install without the dlssg_sm86 folder is not usable");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "pt-fg-choose-test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        auto write = [&](const char* name, const std::string& data) { std::ofstream(dir / name, std::ios::binary) << data; };
        write("dxgi.dll", "ReShade unrelated");
        write("version.dll", "some other version.dll with no marker");
        const std::string chosen = pt::fg_unlock::ChooseProxy(dir, "");
        Expect(chosen == "winmm.dll", "with version.dll taken by other software and dxgi.dll the DLSS 5 route's, FG picks winmm.dll");
        write("version.dll", "ours dlssg_sm86 proxy");
        Expect(pt::fg_unlock::ChooseProxy(dir, "") == "version.dll", "our own version.dll is reused");
        Expect(pt::fg_unlock::ChooseProxy(dir, "") != std::string(pt::fg_unlock::kDlss5Proxy), "the DLSS 5 route's dxgi.dll is never chosen");
        Expect(pt::fg_unlock::ProxyPresent(dir), "a marked proxy is seen as present");
        fs::remove_all(dir, ec);
    }
    std::printf("upscaler gpu: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
