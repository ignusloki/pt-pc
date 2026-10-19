#pragma once

#ifdef _WIN32
#include <windows.h>
#include <wrl/client.h>

namespace pt::gpu::d3d12 {

template <class T> Microsoft::WRL::ComPtr<T> ReShadeNative(T* object) {
    constexpr GUID unwrapped = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};
    Microsoft::WRL::ComPtr<T> native;
    if (object && SUCCEEDED(object->QueryInterface(unwrapped, reinterpret_cast<void**>(native.GetAddressOf()))) && native) {
        return native;
    }
    return object;
}

}
#endif
