#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct Found {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK FindWindowOf(HWND hwnd, LPARAM lp) {
    Found* f = reinterpret_cast<Found*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == f->pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        f->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::printf("usage: pt_screen_capture <out.raw> <pid> <seconds> <trigger file>\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[2], nullptr, 10));
    const double seconds = std::atof(argv[3]);
    const std::filesystem::path trigger = argv[4];
    Found f;
    f.pid = pid;
    for (int i = 0; i < 600 && !f.hwnd; ++i) {
        EnumWindows(FindWindowOf, reinterpret_cast<LPARAM>(&f));
        if (!f.hwnd) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!f.hwnd) {
        std::printf("no window of pid %lu\n", pid);
        return 1;
    }
    while (!std::filesystem::exists(trigger)) {
        DWORD code = 0;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        const bool alive = h && GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        if (h) CloseHandle(h);
        if (!alive) {
            std::printf("the game ended before the trigger\n");
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    RECT client{};
    GetClientRect(f.hwnd, &client);
    POINT origin{0, 0};
    ClientToScreen(f.hwnd, &origin);
    const HMONITOR monitor = MonitorFromWindow(f.hwnd, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIFactory1> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC od{};
    for (UINT a = 0; !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> o;
        for (UINT i = 0; adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i) {
            o->GetDesc(&od);
            if (od.Monitor == monitor) {
                output = o;
                break;
            }
        }
        if (!output) adapter.Reset();
    }
    if (!output) {
        std::printf("no DXGI output for the window's monitor\n");
        return 1;
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
        std::printf("no D3D11 device\n");
        return 1;
    }
    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);
    ComPtr<IDXGIOutputDuplication> dup;
    if (FAILED(output1->DuplicateOutput(device.Get(), &dup))) {
        std::printf("DuplicateOutput failed\n");
        return 1;
    }
    DXGI_OUTDUPL_DESC dd{};
    dup->GetDesc(&dd);
    if (dd.ModeDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        std::printf("desktop format %d is not BGRA8 (an HDR desktop?)\n", static_cast<int>(dd.ModeDesc.Format));
        return 1;
    }
    const int x0 = origin.x - od.DesktopCoordinates.left;
    const int y0 = origin.y - od.DesktopCoordinates.top;
    const UINT w = static_cast<UINT>(client.right), h = static_cast<UINT>(client.bottom);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&td, nullptr, &staging);
    FILE* out = std::fopen(argv[1], "wb");
    if (!out) {
        std::printf("cannot write %s\n", argv[1]);
        return 1;
    }
    std::setvbuf(out, nullptr, _IOFBF, 64 << 20);
    const char magic[8] = {'P', 'T', 'C', 'A', 'P', '1', 0, 0};
    std::fwrite(magic, 1, 8, out);
    std::fwrite(&w, 4, 1, out);
    std::fwrite(&h, 4, 1, out);
    std::vector<uint8_t> luma(static_cast<size_t>(w) * h);
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    uint64_t frames = 0, accumulated = 0, timeouts = 0;
    while (std::chrono::steady_clock::now() < end) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> res;
        const HRESULT hr = dup->AcquireNextFrame(50, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            ++timeouts;
            continue;
        }
        if (FAILED(hr)) {
            std::printf("AcquireNextFrame failed %08lx\n", static_cast<unsigned long>(hr));
            break;
        }
        if (info.LastPresentTime.QuadPart != 0 && info.AccumulatedFrames > 0) {
            ComPtr<ID3D11Texture2D> tex;
            res.As(&tex);
            D3D11_BOX box{static_cast<UINT>(x0), static_cast<UINT>(y0), 0, static_cast<UINT>(x0) + w, static_cast<UINT>(y0) + h, 1};
            context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
                for (UINT y = 0; y < h; ++y) {
                    const uint8_t* row = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
                    uint8_t* dst = luma.data() + static_cast<size_t>(y) * w;
                    for (UINT x = 0; x < w; ++x) {
                        const uint8_t* p = row + x * 4;
                        dst[x] = static_cast<uint8_t>((p[0] * 29u + p[1] * 150u + p[2] * 77u) >> 8);
                    }
                }
                context->Unmap(staging.Get(), 0);
                const int64_t t = info.LastPresentTime.QuadPart;
                const uint32_t acc = info.AccumulatedFrames, zero = 0;
                std::fwrite(&t, 8, 1, out);
                std::fwrite(&acc, 4, 1, out);
                std::fwrite(&zero, 4, 1, out);
                std::fwrite(luma.data(), 1, luma.size(), out);
                ++frames;
                accumulated += acc;
            }
        }
        dup->ReleaseFrame();
    }
    std::fclose(out);
    LARGE_INTEGER qpf;
    QueryPerformanceFrequency(&qpf);
    std::printf("captured %llu desktop frames (%llu presents accumulated), %ux%u at (%d,%d), refresh %u/%u, qpf %lld\n",
                static_cast<unsigned long long>(frames), static_cast<unsigned long long>(accumulated), w, h, x0, y0,
                dd.ModeDesc.RefreshRate.Numerator, dd.ModeDesc.RefreshRate.Denominator, static_cast<long long>(qpf.QuadPart));
    return 0;
}
