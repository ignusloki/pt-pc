#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif
#include "engine/platform/ios_diagnostics.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <mach/mach.h>
#include <mach/arm/thread_status.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <os/proc.h>
#include <pthread.h>
#include <sys/utsname.h>

namespace pt::ios_diag {
namespace {
using Clock = std::chrono::steady_clock;
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
FILE* file = nullptr;
FILE* operations_file = nullptr;
std::filesystem::path directory;
bool stopping = false;
std::string stage = "entering main";
Clock::time_point started, changed;
std::atomic<uint64_t> frames{0}, loop_polls{0};
std::atomic<int64_t> loop_seen_ns{0};
std::atomic<unsigned long> memory_pressure{0};
thread_t main_thread = MACH_PORT_NULL;
CFRunLoopObserverRef loop_observer = nullptr;
dispatch_source_t pressure_source = nil;
uint64_t next_operation = 0;
size_t operation_lines = 0;

struct Operation {
    uint64_t id = 0, thread = 0;
    bool main = false;
    std::string kind, name, step;
    Clock::time_point began;
};
// Bounded registry: nested main-thread loads and background work remain distinct.
std::array<Operation, 64> operations;

int64_t NowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }
double Seconds() { return std::chrono::duration<double>(Clock::now() - started).count(); }

double AvailableMemoryMiB() {
#if TARGET_OS_IPHONE
    return os_proc_available_memory() / 1048576.0;
#else
    return -1; // Host smoke tests cannot query an iOS process allowance.
#endif
}

struct Registers {
    uint64_t pc = 0, lr = 0;
    kern_return_t result = KERN_NOT_SUPPORTED;
};

Registers ReadMainRegisters() {
    Registers out;
    arm_thread_state64_t state{};
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    // Read only, without holding the diagnostic mutex: never suspend, signal or change the thread.
    out.result = main_thread ? thread_get_state(main_thread, ARM_THREAD_STATE64,
        reinterpret_cast<thread_state_t>(&state), &count) : KERN_INVALID_ARGUMENT;
    if (out.result == KERN_SUCCESS) {
        out.pc = arm_thread_state64_get_pc(state);
        out.lr = arm_thread_state64_get_lr(state);
    }
    return out;
}

void Trace(const char* event, const Operation& op) {
    if (!operations_file || operation_lines > 30000) return;
    if (operation_lines++ == 30000) {
        std::fprintf(operations_file, "Operation log capped at 30000 records; heartbeat monitoring continues.\n");
    } else {
        std::fprintf(operations_file, "[%.3f] %s #%llu thread=%llu main=%d kind=%s name=%s step=%s elapsed=%.3fs\n", Seconds(), event,
            static_cast<unsigned long long>(op.id), static_cast<unsigned long long>(op.thread), op.main,
            op.kind.c_str(), op.name.c_str(), op.step.c_str(), std::chrono::duration<double>(Clock::now() - op.began).count());
    }
    std::fflush(operations_file);
}

// Called with mutex held. No engine logger, Vulkan/allocator queries, or UIKit calls.
void Sample(const char* kind, std::string_view note, bool clean_exit = false, Registers registers = {}) {
    if (!file) return;
    task_vm_info_data_t vm{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const bool have_vm = task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS;
    task_thread_times_info_data_t cpu{};
    count = TASK_THREAD_TIMES_INFO_COUNT;
    task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, reinterpret_cast<task_info_t>(&cpu), &count);
    const double cpu_seconds = cpu.user_time.seconds + cpu.system_time.seconds + (cpu.user_time.microseconds + cpu.system_time.microseconds) / 1e6;
    const auto now = Clock::now();
    char line[2048];
    std::snprintf(line, sizeof(line), "[%.3f] %s %.*s | stage=%s age=%.1fs frames=%llu footprint=%.1fMiB resident=%.1fMiB compressed=%.1fMiB available-to-process=%.1fMiB cpu=%.2fs vm-query=%s ui-loop-age=%.2fs ui-loop-polls=%llu memory-pressure=%lu main-pc=0x%llx main-lr=0x%llx register-query=%d\n",
        std::chrono::duration<double>(now - started).count(), kind, static_cast<int>(note.size()), note.data(), stage.c_str(),
        std::chrono::duration<double>(now - changed).count(), static_cast<unsigned long long>(frames.load()),
        vm.phys_footprint / 1048576.0, vm.resident_size / 1048576.0, vm.compressed / 1048576.0, AvailableMemoryMiB(),
        cpu_seconds, have_vm ? "ok" : "failed", (NowNs() - loop_seen_ns.load()) / 1e9,
        static_cast<unsigned long long>(loop_polls.load()), memory_pressure.load(),
        static_cast<unsigned long long>(registers.pc), static_cast<unsigned long long>(registers.lr), registers.result);
    std::fputs(line, file);
    // A complete last snapshot survives abrupt termination; no crash handler is required.
    FILE* state = std::fopen((directory / "last-state.tmp").c_str(), "w");
    if (state) {
        std::fprintf(state, "P.T. iOS diagnostic 4\nmain-returned=%s\n", clean_exit ? "yes" : "no");
        std::fputs(line, state);
    }
    for (const Operation& op : operations) {
        if (!op.id) continue;
        auto active = [&](FILE* out) {
            if (out) std::fprintf(out, "  active #%llu thread=%llu main=%d %s %s | step=%s age=%.3fs\n",
                static_cast<unsigned long long>(op.id), static_cast<unsigned long long>(op.thread), op.main,
                op.kind.c_str(), op.name.c_str(), op.step.c_str(), std::chrono::duration<double>(now - op.began).count());
        };
        active(file);
        active(state);
    }
    std::fflush(file);
    if (state) {
        std::fclose(state);
        std::error_code ec;
        std::filesystem::rename(directory / "last-state.tmp", directory / "last-state.txt", ec);
    }
}

void Previous(const char* name) {
    const auto path = directory / name;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::copy_file(path, directory / (path.stem().string() + "-previous" + path.extension().string()),
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }
}
}

Session::Session(const std::filesystem::path& data, const char* build) {
    stopping = false;
    frames = loop_polls = memory_pressure = 0;
    next_operation = operation_lines = 0;
    for (Operation& op : operations) op.id = 0;
    stage = "entering main";
    started = changed = Clock::now();
    loop_seen_ns = NowNs();
    directory = data / "ios-diagnostics";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    for (const char* name : {"startup.log", "console.log", "operations.log", "pt-stderr.log", "last-state.txt", "images.log"}) Previous(name);
    file = std::fopen((directory / "startup.log").c_str(), "w");
    if (!file) return;
    operations_file = std::fopen((directory / "operations.log").c_str(), "w");
    if (std::freopen((directory / "console.log").c_str(), "w", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);
    main_thread = mach_thread_self();
    loop_observer = CFRunLoopObserverCreate(kCFAllocatorDefault, kCFRunLoopEntry | kCFRunLoopBeforeSources | kCFRunLoopAfterWaiting,
        true, 0, [](CFRunLoopObserverRef, CFRunLoopActivity, void*) { loop_seen_ns = NowNs(); ++loop_polls; }, nullptr);
    if (loop_observer) CFRunLoopAddObserver(CFRunLoopGetMain(), loop_observer, kCFRunLoopCommonModes);
    @autoreleasepool {
        struct utsname device{};
        uname(&device);
#if TARGET_OS_IPHONE
        NSString* version = UIDevice.currentDevice.systemVersion;
#else
        NSString* version = NSProcessInfo.processInfo.operatingSystemVersionString;
#endif
        std::fprintf(file, "P.T. iOS startup diagnostic 4 (memory profile and loading events) | build=%s | device=%s | iOS=%s | physical-memory=%.0fMiB | main-thread=%s\n",
            build, device.machine, version.UTF8String,
            NSProcessInfo.processInfo.physicalMemory / 1048576.0, NSThread.isMainThread ? "yes" : "no");
        std::fprintf(file, "No game content or microphone recordings are captured. Heartbeats every 0.5s for 180s. memory-pressure: 0=no notification yet, 1=normal, 2=warning, 4=critical. Main registers are queried on heartbeats only. Available-to-process is not total device free RAM.\n");
    }
    Sample("stage", stage);
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
        DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    pressure_source = source;
    if (source) {
        dispatch_source_set_event_handler(source, ^{
            memory_pressure = dispatch_source_get_data(source);
            Event("background memory-pressure notification");
        });
        dispatch_resume(source);
    }
    worker = std::thread([] {
        std::unique_lock lock(mutex);
        while (!wake.wait_for(lock, std::chrono::milliseconds(500), [] { return stopping; })) {
            lock.unlock();
            const Registers registers = ReadMainRegisters();
            lock.lock();
            if (stopping) break;
            Sample("heartbeat", "process alive", false, registers);
            if (Clock::now() - started >= std::chrono::seconds(180)) break;
        }
    });
}

Session::~Session() {
    if (loop_observer) {
        CFRunLoopRemoveObserver(CFRunLoopGetMain(), loop_observer, kCFRunLoopCommonModes);
        CFRelease(loop_observer);
        loop_observer = nullptr;
    }
    if (pressure_source) { dispatch_source_cancel(pressure_source); pressure_source = nil; }
    {
        std::lock_guard lock(mutex);
        stopping = true;
        Sample("end", "main returned", true);
    }
    wake.notify_all();
    if (worker.joinable()) worker.join();
    std::lock_guard lock(mutex);
    if (file) std::fclose(file);
    if (operations_file) std::fclose(operations_file);
    file = operations_file = nullptr;
    if (main_thread) mach_port_deallocate(mach_task_self(), main_thread);
    main_thread = MACH_PORT_NULL;
}

void Stage(std::string_view name) {
    std::lock_guard lock(mutex);
    if (stage == name) return;
    stage = name;
    changed = Clock::now();
    Sample("stage", name);
}

void Event(std::string_view name) {
    std::lock_guard lock(mutex);
    if (stopping) return;
    Sample("event", name);
}

void FramePresented() {
    if (++frames == 1) Stage("first frame presentation returned");
}

Scope::Scope(std::string_view kind, std::string_view name) {
    std::lock_guard lock(mutex);
    if (!file) return;
    for (Operation& op : operations) {
        if (op.id) continue;
        op.id = id_ = ++next_operation;
        pthread_threadid_np(nullptr, &op.thread);
        op.main = pthread_main_np();
        op.kind = kind;
        op.name = name;
        op.step = "begin";
        op.began = Clock::now();
        Trace("begin", op);
        break;
    }
}

Scope::~Scope() {
    std::lock_guard lock(mutex);
    if (!id_) return;
    for (Operation& op : operations) if (op.id == id_) { Trace("end", op); op.id = 0; break; }
}

void Scope::Step(std::string_view detail) {
    std::lock_guard lock(mutex);
    if (!id_) return;
    for (Operation& op : operations) if (op.id == id_) { op.step = detail; Trace("step", op); break; }
}

void RecordImages() {
    std::lock_guard lock(mutex);
    if (!file) return;
    FILE* images = std::fopen((directory / "images.log").c_str(), "w");
    if (!images) return;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const mach_header* header = _dyld_get_image_header(i);
        if (!header || header->magic != MH_MAGIC_64) continue;
        std::fprintf(images, "base=%p slide=0x%llx uuid=", header, static_cast<unsigned long long>(_dyld_get_image_vmaddr_slide(i)));
        const auto* command = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(header) + sizeof(mach_header_64));
        for (uint32_t c = 0; c < header->ncmds; ++c) {
            if (command->cmd == LC_UUID) {
                for (uint8_t byte : reinterpret_cast<const uuid_command*>(command)->uuid) std::fprintf(images, "%02x", byte);
                break;
            }
            command = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(command) + command->cmdsize);
        }
        std::fprintf(images, " %s\n", _dyld_get_image_name(i));
    }
    std::fclose(images);
}
}
