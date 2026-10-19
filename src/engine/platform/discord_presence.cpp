#include "engine/platform/discord_presence.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#include "engine/core/log.h"

namespace pt {
namespace {

std::atomic<bool> g_running{false};

std::string JsonString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out + "\"";
}

uint32_t ProcessId() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

class Connection {
public:
    ~Connection() { Close(); }

    bool Open() {
        Close();
#ifdef _WIN32
        for (int i = 0; i < 10; ++i) {
            if (!g_running) return false;
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                pipe_ = pipe;
                return true;
            }
        }
        return false;
#else
        std::vector<std::string> dirs;
        for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
            if (const char* v = std::getenv(var); v && *v) dirs.emplace_back(v);
        }
        dirs.emplace_back("/tmp");
        std::vector<std::string> paths;
        for (const std::string& dir : dirs) {
            for (const char* sub : {"", "/app/com.discordapp.Discord", "/snap.discord", "/.flatpak/dev.vencord.Vesktop/xdg-run"}) {
                for (int i = 0; i < 10; ++i) paths.push_back(std::format("{}{}/discord-ipc-{}", dir, sub, i));
            }
        }
        for (const std::string& path : paths) {
            if (!g_running) return false;
            const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) return false;
#ifdef SO_NOSIGPIPE
            const int no_sigpipe = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0) {
                close(fd);
                continue;
            }
#endif
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                close(fd);
                continue;
            }
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            if (path.size() >= sizeof(address.sun_path)) {
                close(fd);
                continue;
            }
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
            int result = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            if (result != 0 && errno == EINPROGRESS) {
                pollfd pending{fd, POLLOUT, 0};
                int error = 0;
                socklen_t length = sizeof(error);
                result = poll(&pending, 1, 500) > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0 ? 0 : -1;
            }
            const timeval timeout{0, 500000};
            if (result == 0 && fcntl(fd, F_SETFL, flags) == 0 &&
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0) {
                fd_ = fd;
                return true;
            }
            close(fd);
        }
        return false;
#endif
    }

    void Close() {
#ifdef _WIN32
        if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
#endif
    }

    bool IsOpen() const {
#ifdef _WIN32
        return pipe_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }

    bool Send(uint32_t opcode, const std::string& payload, bool stopping = false) {
        std::vector<char> frame(8 + payload.size());
        const uint32_t length = static_cast<uint32_t>(payload.size());
        std::memcpy(frame.data(), &opcode, 4);
        std::memcpy(frame.data() + 4, &length, 4);
        std::memcpy(frame.data() + 8, payload.data(), payload.size());
        return Write(frame.data(), frame.size(), stopping);
    }

    std::optional<std::string> Receive() {
        char header[8];
        if (!Read(header, 8)) return std::nullopt;
        uint32_t length = 0;
        std::memcpy(&length, header + 4, 4);
        if (length > (1u << 20)) return std::nullopt;
        std::string payload(length, '\0');
        if (length && !Read(payload.data(), length)) return std::nullopt;
        return payload;
    }

private:
#ifdef _WIN32
    bool Transfer(char* data, size_t size, bool writing, bool stopping = false, size_t* read_count = nullptr) {
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent) return false;
        DWORD transferred = 0;
        bool ok = writing ? WriteFile(pipe_, data, static_cast<DWORD>(size), &transferred, &operation)
                          : ReadFile(pipe_, data, static_cast<DWORD>(size), &transferred, &operation);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(stopping ? 200 : 2000);
            while (WaitForSingleObject(operation.hEvent, 50) == WAIT_TIMEOUT) {
                if ((!g_running && !stopping) || std::chrono::steady_clock::now() >= deadline) {
                    CancelIoEx(pipe_, &operation);
                    break;
                }
            }
            ok = GetOverlappedResult(pipe_, &operation, &transferred, TRUE) != FALSE;
        }
        CloseHandle(operation.hEvent);
        if (read_count) *read_count = transferred;
        return ok && (writing ? transferred == size : transferred > 0);
    }
#endif

    bool Write(const char* data, size_t size, bool stopping) {
#ifdef _WIN32
        return Transfer(const_cast<char*>(data), size, true, stopping);
#else
        size_t done = 0;
        while (done < size) {
            if (!g_running && !stopping) return false;
            const ssize_t n = send(fd_, data + done, size - done, MSG_NOSIGNAL);
            if (n <= 0) return false;
            done += static_cast<size_t>(n);
        }
        return true;
#endif
    }

    bool Read(char* data, size_t size) {
        size_t done = 0;
        while (done < size) {
            if (!g_running) return false;
#ifdef _WIN32
            size_t got = 0;
            if (!Transfer(data + done, size - done, false, false, &got)) return false;
#else
            const ssize_t got = recv(fd_, data + done, size - done, 0);
            if (got <= 0) return false;
#endif
            done += static_cast<size_t>(got);
        }
        return true;
    }

#ifdef _WIN32
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::optional<PresenceActivity> g_wanted;
std::thread g_thread;

void Run(std::string client_id) {
    using clock = std::chrono::steady_clock;
    Connection connection;
    std::optional<PresenceActivity> sent;
    clock::time_point last_send{};
    clock::time_point next_try{};
    uint64_t nonce = 0;
    bool logged_absent = false;
    while (g_running) {
        std::optional<PresenceActivity> wanted;
        {
            std::unique_lock lock(g_mutex);
            g_wake.wait_for(lock, std::chrono::seconds(2));
            wanted = g_wanted;
        }
        if (!g_running) break;
        if (!wanted) continue;
        if (!connection.IsOpen()) {
            if (clock::now() < next_try) continue;
            next_try = clock::now() + std::chrono::seconds(20);
            if (!connection.Open()) {
                if (!logged_absent) LogInfo("discord: no Discord client running, rich presence waits for one");
                logged_absent = true;
                continue;
            }
            std::optional<std::string> answer;
            if (connection.Send(0, std::format("{{\"v\":1,\"client_id\":{}}}", JsonString(client_id)))) answer = connection.Receive();
            if (!answer || answer->find("\"READY\"") == std::string::npos) {
                LogWarn("discord: the handshake failed{}", answer ? ": " + answer->substr(0, 200) : std::string());
                if (answer) next_try = clock::now() + std::chrono::hours(24);
                connection.Close();
                continue;
            }
            LogInfo("discord: rich presence connected");
            logged_absent = false;
            sent.reset();
        }
        if (sent == wanted || clock::now() - last_send < std::chrono::seconds(15)) continue;
        std::string activity = "{\"details\":" + JsonString(wanted->details);
        if (!wanted->state.empty()) activity += ",\"state\":" + JsonString(wanted->state);
        if (wanted->start > 0) activity += std::format(",\"timestamps\":{{\"start\":{}}}", wanted->start);
        activity += ",\"assets\":{\"large_image\":\"pt\",\"large_text\":\"P.T. PC Port\"}}";
        const std::string command = std::format("{{\"cmd\":\"SET_ACTIVITY\",\"args\":{{\"pid\":{},\"activity\":{}}},\"nonce\":\"{}\"}}",
                                                ProcessId(), activity, ++nonce);
        if (!connection.Send(1, command) || !connection.Receive()) {
            LogInfo("discord: the client went away");
            connection.Close();
            continue;
        }
        sent = wanted;
        last_send = clock::now();
    }
    if (connection.IsOpen()) {
        connection.Send(1, std::format("{{\"cmd\":\"SET_ACTIVITY\",\"args\":{{\"pid\":{}}},\"nonce\":\"{}\"}}", ProcessId(), ++nonce), true);
    }
}

}

void PresenceStart(const std::string& client_id) {
    if (client_id.empty() || g_running) return;
    g_running = true;
    g_thread = std::thread(Run, client_id);
}

void PresenceSet(const PresenceActivity& activity) {
    if (!g_running) return;
    std::lock_guard lock(g_mutex);
    if (g_wanted && *g_wanted == activity) return;
    g_wanted = activity;
    g_wake.notify_one();
}

void PresenceStop() {
    if (!g_running) return;
    g_running = false;
    g_wake.notify_one();
    if (g_thread.joinable()) g_thread.join();
}

}
