#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>
#include "engine/core/log.h"
static std::wstring test_pipe;
static HANDLE TestCreateFileW(LPCWSTR, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attrs, DWORD disposition, DWORD flags, HANDLE template_file) {
    return ::CreateFileW(test_pipe.c_str(), access, share, attrs, disposition, flags, template_file);
}
#define CreateFileW TestCreateFileW
#include "engine/platform/discord_presence.cpp"
#undef CreateFileW
namespace pt { void LogWrite(LogLevel, std::string_view text) { std::printf("%.*s\n", static_cast<int>(text.size()), text.data()); } }
static bool ReceiveFrame(HANDLE pipe) {
    char header[8]; DWORD got=0;
    if (!ReadFile(pipe,header,8,&got,nullptr) || got!=8) return false;
    uint32_t length=0; std::memcpy(&length,header+4,4);
    std::string body(length,'\0');
    return length==0 || (ReadFile(pipe,body.data(),length,&got,nullptr) && got==length);
}
int main() {
    int failed=0;
    for (int ready=0;ready<4;++ready) {
        test_pipe=L"\\\\.\\pipe\\pt-presence-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(ready);
        HANDLE pipe=CreateNamedPipeW(test_pipe.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,nullptr);
        if (pipe==INVALID_HANDLE_VALUE) return 2;
        std::promise<bool> received; auto received_future=received.get_future();
        HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::thread server([&] {
            bool ok=ConnectNamedPipe(pipe,nullptr) || GetLastError()==ERROR_PIPE_CONNECTED;
            ok=ok && ReceiveFrame(pipe);
            if (ok && ready) {
                const std::string json="{\"evt\":\"READY\"}"; uint32_t frame[2]={1,static_cast<uint32_t>(json.size())}; DWORD wrote=0;
                ok=WriteFile(pipe,frame,3,&wrote,nullptr); Sleep(20); ok=ok && WriteFile(pipe,reinterpret_cast<char*>(frame)+3,5,&wrote,nullptr) && WriteFile(pipe,json.data(),3,&wrote,nullptr); Sleep(20); ok=ok && WriteFile(pipe,json.data()+3,static_cast<DWORD>(json.size()-3),&wrote,nullptr); if (ready!=2) ok=ok && ReceiveFrame(pipe); if (ready==3 && ok) { uint32_t answer[2]={1,2}; ok=WriteFile(pipe,answer,8,&wrote,nullptr) && WriteFile(pipe,"{}",2,&wrote,nullptr); }
            }
            received.set_value(ok);
            WaitForSingleObject(release,5000);
            DisconnectNamedPipe(pipe);
        });
        pt::PresenceStart("private-test-application"); pt::PresenceSet({ready==2?std::string(1048576,'x'):"Private test","",0});
        if (received_future.wait_for(std::chrono::seconds(5))!=std::future_status::ready || !received_future.get()) { std::printf("ERROR fixture didn't receive request\n"); return 3; }
        if (ready==2 || ready==3) Sleep(100); std::promise<void> stopped; auto stopped_future=stopped.get_future();
        std::thread stop([&] {pt::PresenceStop(); stopped.set_value();});
        bool bounded=stopped_future.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;
        std::printf("%s stop while peer withholds %s\n", bounded?"PASS":"FAIL",ready==0?"handshake":ready==1?"activity reply":ready==2?"activity write":"nothing (fragmented normal replies)");
        failed+=!bounded;
        SetEvent(release); server.join(); stop.join(); CloseHandle(release); CloseHandle(pipe);
    }
    return failed?1:0;
}
