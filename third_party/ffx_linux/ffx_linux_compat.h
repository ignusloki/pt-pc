// Force-included into every FidelityFX SDK v1.1.4 source of the Linux FSR library (cmake/FsrLinux.cmake). AMD's code
// is written for MSVC: the bounds-checked copies of the C11 Annex K (wcscpy_s, strcpy_s) and _countof. These keep the
// same contract (copy, always terminate, truncate to the buffer) without changing a line of AMD's sources.
#pragma once

#ifdef __cplusplus
#include <cmath>
#include <codecvt>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <locale>
#include <new>

// ffx_message.cpp's non-Windows branch uses FFX_UNUSED without including ffx_util.h
#include <FidelityFX/host/ffx_util.h>

// The opaque effect contexts are sized for Windows, where wchar_t has 2 bytes; the FSR 3 upscaler's private context holds
// resource and pipeline names in wchar_t, which has 4 bytes here, and outgrows FFX_SDK_DEFAULT_CONTEXT_SIZE (the SDK's own
// static assertion in ffx_fsr3upscaler.cpp stops the build). The contexts live inside this library only: the FidelityFX API
// the game calls passes opaque pointers, so a larger size changes nothing outside it.
#undef FFX_SDK_DEFAULT_CONTEXT_SIZE
#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)

#ifndef _countof
template <typename T, std::size_t N>
constexpr std::size_t ffx_linux_countof(T (&)[N]) {
    return N;
}
#define _countof(array) ffx_linux_countof(array)
#endif

inline int wcscpy_s(wchar_t* dest, std::size_t size, const wchar_t* src) {
    if (!dest || size == 0) {
        return 22;
    }
    if (!src) {
        dest[0] = L'\0';
        return 22;
    }
    std::size_t n = std::wcslen(src);
    if (n >= size) {
        n = size - 1;
    }
    std::wmemcpy(dest, src, n);
    dest[n] = L'\0';
    return 0;
}

template <std::size_t N>
inline int wcscpy_s(wchar_t (&dest)[N], const wchar_t* src) {
    return wcscpy_s(dest, N, src);
}

inline int strcpy_s(char* dest, std::size_t size, const char* src) {
    if (!dest || size == 0) {
        return 22;
    }
    if (!src) {
        dest[0] = '\0';
        return 22;
    }
    std::size_t n = std::strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    std::memcpy(dest, src, n);
    dest[n] = '\0';
    return 0;
}

template <std::size_t N>
inline int strcpy_s(char (&dest)[N], const char* src) {
    return strcpy_s(dest, N, src);
}

// the breadcrumbs printing macros (ffx_breadcrumbs_list.h)
inline int sprintf_s(char* dest, std::size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = dest && size ? std::vsnprintf(dest, size, format, args) : -1;
    va_end(args);
    return written;
}

// resource names to the debug utils labels (ffx_vk.cpp); count is the destination size there, so this converts at most
// size - 1 bytes and always terminates
inline int wcstombs_s(std::size_t* converted, char* dest, std::size_t size, const wchar_t* src, std::size_t count) {
    if (converted) {
        *converted = 0;
    }
    if (!dest || size == 0) {
        return 22;
    }
    dest[0] = '\0';
    if (!src) {
        return 22;
    }
    const std::size_t limit = count < size ? count : size - 1;
    std::size_t n = std::wcstombs(dest, src, limit);
    if (n == static_cast<std::size_t>(-1)) {
        n = 0;
    }
    dest[n < size ? n : size - 1] = '\0';
    if (converted) {
        *converted = n + 1;
    }
    return 0;
}
#endif
