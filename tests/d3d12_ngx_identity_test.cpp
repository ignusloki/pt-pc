#include <windows.h>
#include <wrl/client.h>

#include <cstdio>

#include "engine/render/d3d12/backend/reshade_native.h"

namespace {
constexpr GUID kUnwrapped = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};
struct Object final : IUnknown {
    ULONG refs = 1;
    IUnknown* native = nullptr;
    bool null_success = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid == kUnwrapped && native) {
            native->AddRef();
            *out = native;
            return S_OK;
        }
        return null_success ? S_OK : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};
int failures = 0;
void Check(bool ok, const char* label) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
}

int main() {
    using pt::gpu::d3d12::ReShadeNative;
    Object native_device, native_list, device_proxy, list_proxy;
    device_proxy.native = &native_device;
    list_proxy.native = &native_list;
    {
        auto device = ReShadeNative<IUnknown>(&device_proxy);
        auto create_list = ReShadeNative<IUnknown>(&list_proxy);
        auto eval_list = ReShadeNative<IUnknown>(&list_proxy);
        Check(device.Get() == &native_device, "NGX initializes and shuts down on the native device");
        Check(create_list.Get() == &native_list && eval_list.Get() == &native_list,
              "feature creation and evaluation use the matching native list");
        Check(native_device.refs == 2 && native_list.refs == 3, "native interfaces stay owned during NGX calls");
        Check(device_proxy.refs == 1 && list_proxy.refs == 1, "unwrapped calls do not retain proxy references");
    }
    Check(native_device.refs == 1 && native_list.refs == 1, "each QueryInterface reference is released once");
    Object plain;
    {
        auto fallback = ReShadeNative<IUnknown>(&plain);
        Check(fallback.Get() == &plain && plain.refs == 2, "plain native interfaces are retained");
    }
    Check(plain.refs == 1, "plain fallback reference is released");
    plain.null_success = true;
    {
        auto fallback = ReShadeNative<IUnknown>(&plain);
        Check(fallback.Get() == &plain && plain.refs == 2, "a null successful query preserves the caller interface");
    }
    Check(!ReShadeNative<IUnknown>(nullptr), "null remains null");
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
