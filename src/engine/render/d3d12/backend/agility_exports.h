#pragma once

#if defined(_WIN32) && defined(PT_D3D12_AGILITY_VERSION)

#define PT_D3D12_AGILITY_EXPORTS()                                                                       \
    extern "C" {                                                                                         \
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = PT_D3D12_AGILITY_VERSION;         \
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";                              \
    }

#else

#define PT_D3D12_AGILITY_EXPORTS()

#endif
