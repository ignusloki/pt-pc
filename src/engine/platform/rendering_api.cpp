#include "engine/core/ascii.h"
#include "engine/platform/rendering_api.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <utility>

#include "engine/platform/os.h"

namespace pt {

std::string_view RenderingApiKey(RenderingApi api) {
    return api == RenderingApi::D3D12 ? "d3d12" : "vulkan";
}

std::string_view RenderingApiName(RenderingApi api) {
    return api == RenderingApi::D3D12 ? "DirectX 12" : "Vulkan";
}

bool ParseRenderingApi(std::string_view text, RenderingApi& out) {
    std::string v(text);
    v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c) != 0; }), v.end());
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return pt::AsciiLower(static_cast<char>(c)); });
    if (v == "vulkan") {
        out = RenderingApi::Vulkan;
        return true;
    }
    if (v == "d3d12" || v == "dx12" || v == "directx12") {
        out = RenderingApi::D3D12;
        return true;
    }
    return false;
}

bool RenderingApiSelectable() {
#ifdef _WIN32
#if defined(PT_D3D12_SELECTABLE)
    return true;
#else
    return os::GetEnv("PT_RENDERING_API_UI") == "1";
#endif
#else
    return false;
#endif
}

namespace {
bool g_d3d12_game = false;
}

bool D3D12RunsGame() {
#if defined(_WIN32)
    return g_d3d12_game;
#else
    return false;
#endif
}

void SetD3D12RunsGame(bool available) {
    g_d3d12_game = available;
}

namespace {
bool (*g_d3d12_probe)(std::string&) = nullptr;
int g_d3d12_usable = -1;
std::string g_d3d12_reason;
}

void SetD3D12Probe(bool (*probe)(std::string& reason)) {
    g_d3d12_probe = probe;
    g_d3d12_usable = -1;
}

void BlockD3D12(std::string reason) {
    g_d3d12_usable = 0;
    g_d3d12_reason = std::move(reason);
}

void SkipD3D12Probe() {
    if (g_d3d12_usable < 0) {
        g_d3d12_usable = 1;
        g_d3d12_reason.clear();
    }
}

bool D3D12Usable(std::string& reason) {
    if (!D3D12RunsGame()) {
        reason = "this build draws the game with Vulkan only";
        return false;
    }
    if (!g_d3d12_probe) {
        reason.clear();
        return true;
    }
    if (g_d3d12_usable < 0) {
        g_d3d12_usable = g_d3d12_probe(g_d3d12_reason) ? 1 : 0;
    }
    reason = g_d3d12_usable ? std::string() : g_d3d12_reason;
    return g_d3d12_usable == 1;
}

RenderingApi ChooseRenderingApi(std::string_view cli, std::string_view setting, std::string& note) {
    note.clear();
    const bool from_cli = !cli.empty();
    const std::string_view asked = from_cli ? cli : setting;
    const char* source = from_cli ? "--backend" : "pt.ini [display] rendering_api";
    RenderingApi api = RenderingApi::Vulkan;
    if (!asked.empty() && !ParseRenderingApi(asked, api)) {
        note = std::format("{} = '{}' is not vulkan or d3d12: using Vulkan", source, asked);
        return RenderingApi::Vulkan;
    }
    if (api == RenderingApi::D3D12) {
#ifndef _WIN32
        note = std::format("{} asks for DirectX 12, which exists on Windows only: using Vulkan", source);
        return RenderingApi::Vulkan;
#else
        if (!D3D12RunsGame()) {
            note = std::format("{} asks for DirectX 12, but this build draws the game with Vulkan only (the DirectX 12 backend is not "
                               "complete): using Vulkan",
                               source);
            return RenderingApi::Vulkan;
        }
        std::string reason;
        if (!D3D12Usable(reason)) {
            note = std::format("{} asks for DirectX 12, which cannot run on this PC ({}): using Vulkan", source, reason);
            return RenderingApi::Vulkan;
        }
#endif
    }
    return api;
}

AskAnswer AskRenderingApi(std::string_view title, std::string_view message, RenderingApi& out) {
    const std::string t(title);
    const std::string m(message);
    const SDL_MessageBoxButtonData buttons[] = {
        {0, 1, "DirectX 12"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 2, "Vulkan"},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    data.title = t.c_str();
    data.message = m.c_str();
    data.numbuttons = 2;
    data.buttons = buttons;
    int pressed = -1;
    if (!SDL_ShowMessageBox(&data, &pressed)) {
        return AskAnswer::NotShown;
    }
    if (pressed != 1 && pressed != 2) {
        return AskAnswer::Closed;
    }
    out = pressed == 1 ? RenderingApi::D3D12 : RenderingApi::Vulkan;
    return AskAnswer::Chosen;
}

}
