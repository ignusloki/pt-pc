#include "engine/render/upscale/fsr_provider_gate.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void Expect(bool ok, const char* label) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

pt::FsrGateInput Input(int generation, pt::FsrApi api, uint32_t vendor, std::vector<pt::FsrProvider> providers, bool sdk2 = false) {
    pt::FsrGateInput in;
    in.generation = generation;
    in.api = api;
    in.vendor_id = vendor;
    in.query_ok = true;
    in.vulkan_sdk2 = sdk2;
    in.providers = std::move(providers);
    return in;
}

}

int main() {
    using pt::FsrApi;
    constexpr uint32_t kAmd = 0x1002, kNvidia = 0x10DE, kIntel = 0x8086;
    const std::vector<pt::FsrProvider> with41 = {{11, "4.1.1"}, {12, "3.1.5"}, {13, "2.3.3"}};
    const std::vector<pt::FsrProvider> without4 = {{12, "3.1.5"}, {13, "2.3.3"}};

    {
        const auto r = pt::DecideFsrProvider(Input(4, FsrApi::D3D12, kAmd, with41));
        Expect(r.available && r.id == 11 && r.name == "4.1.1", "RX 6000 with 4.1 offers FSR 4 with the 4.1.1 provider");
        Expect(r.listed == "4.1.1, 3.1.5, 2.3.3", "every provider is listed for pt.log");
        const auto fsr3 = pt::DecideFsrProvider(Input(3, FsrApi::D3D12, kAmd, with41));
        Expect(fsr3.available && fsr3.id == 12 && fsr3.name == "3.1.5", "FSR 3 keeps the newest 3.x provider beside a 4.x one");
    }
    {
        const auto r = pt::DecideFsrProvider(Input(4, FsrApi::D3D12, kAmd, without4));
        Expect(!r.available && r.id == 0 && r.reason == pt::kFsr4ReasonDriver, "AMD without 4.x: refused with the driver reason");
        Expect(r.reason.find("26.10.1") != std::string::npos, "the driver reason names Adrenalin 26.10.1");
        Expect(std::string(pt::FsrReasonNote(r.reason)) == "pc_note_fsr4_driver", "the driver reason maps to its localized note");
        Expect(pt::DecideFsrProvider(Input(3, FsrApi::D3D12, kAmd, without4)).available, "FSR 3 unaffected without 4.x");
    }
    {
        pt::FsrGateInput failed = Input(4, FsrApi::D3D12, kAmd, {});
        failed.query_ok = false;
        const auto r = pt::DecideFsrProvider(failed);
        Expect(!r.available && r.reason == pt::kFsr4ReasonDriver && r.listed == "query failed", "a failed query refuses and says so");
        Expect(pt::DecideFsrProvider(Input(4, FsrApi::D3D12, kAmd, {})).listed == "none", "an empty list is logged as none");
    }
    for (const uint32_t vendor : {kNvidia, kIntel}) {
        for (const auto& list : {with41, without4}) {
            const auto r = pt::DecideFsrProvider(Input(4, FsrApi::D3D12, vendor, list));
            Expect(!r.available && r.id == 0 && r.reason == pt::kFsr4ReasonNotAmd, "non-AMD device refused with the AMD reason");
            Expect(std::string(pt::FsrReasonNote(r.reason)) == "pc_note_fsr4_amd", "the AMD reason maps to its note");
        }
        Expect(pt::DecideFsrProvider(Input(3, FsrApi::D3D12, vendor, without4)).available, "FSR 3 stays available on non-AMD");
    }
    {
        const std::vector<pt::FsrProvider> v114 = {{21, "3.1.4"}, {22, "2.3.3"}};
        for (const uint32_t vendor : {kAmd, kNvidia}) {
            const auto r = pt::DecideFsrProvider(Input(4, FsrApi::Vulkan, vendor, v114));
            Expect(!r.available && r.reason == pt::kFsr4ReasonVulkan, "Vulkan v1.1.4: refused with the Vulkan reason");
            Expect(std::string(pt::FsrReasonNote(r.reason)) == "pc_note_fsr4_vulkan", "the Vulkan reason maps to pc_note_fsr4_vulkan");
        }
        const auto fsr3 = pt::DecideFsrProvider(Input(3, FsrApi::Vulkan, kNvidia, v114));
        Expect(fsr3.available && fsr3.id == 21 && fsr3.name == "3.1.4", "Vulkan FSR 3 keeps 3.1.4");
    }
    {
        Expect(pt::DecideFsrProvider(Input(4, FsrApi::Vulkan, kAmd, with41, true)).available, "SDK 2.4 Vulkan with 4.x on AMD offers FSR 4");
        Expect(pt::DecideFsrProvider(Input(4, FsrApi::Vulkan, kAmd, without4, true)).reason == pt::kFsr4ReasonDriver,
               "SDK 2.4 Vulkan without 4.x on AMD gives the driver reason");
        Expect(pt::DecideFsrProvider(Input(4, FsrApi::Vulkan, kNvidia, with41, true)).reason == pt::kFsr4ReasonNotAmd,
               "SDK 2.4 Vulkan on NVIDIA gives the AMD reason");
    }
    {
        const auto r = pt::DecideFsrProvider(Input(4, FsrApi::D3D12, kAmd, {{1, "4.0.2"}, {2, "4.10.0"}, {3, "4.1.1"}}));
        Expect(r.available && r.id == 2, "4.10.0 is newer than 4.1.1 and 4.0.2");
    }
    Expect(pt::FsrCreatedIsGeneration("4.1.1", 4), "a 4.1.1 context is FSR 4");
    Expect(!pt::FsrCreatedIsGeneration("3.1.5", 4), "a 3.1.5 fallback context is not FSR 4");
    Expect(!pt::FsrCreatedIsGeneration("", 4), "an unnamed context is not FSR 4");
    Expect(pt::FsrReasonNote("something else") == nullptr, "other reasons have no note");
    Expect(pt::ParseFsrVersion("3.1.4") == std::vector<int>({3, 1, 4}), "version parse");

    std::printf("fsr provider gate: %d failures\n", failures);
    return failures ? 1 : 0;
}
