#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct SDL_Joystick;

namespace pt {

struct VirtualRumble {
    uint16_t low = 0;
    uint16_t high = 0;
    uint16_t peak_low = 0;
    uint16_t peak_high = 0;
    uint32_t calls = 0;
};

class VirtualPads {
public:
    static constexpr int kSlots = 4;

    VirtualPads() = default;
    ~VirtualPads();
    VirtualPads(const VirtualPads&) = delete;
    VirtualPads& operator=(const VirtualPads&) = delete;

    static void UseOnlyVirtualDevices();

    bool Attach(int slot, std::string_view kind);
    bool Detach(int slot);
    void DetachAll();
    bool SetButton(int slot, std::string_view name, bool down);
    bool SetAxis(int slot, std::string_view name, float value);
    bool SetTouch(int slot, bool down, float x, float y);
    bool SetGyro(int slot, float x, float y, float z);
    void PumpSensors(float dt);
    bool Attached(int slot) const;
    uint32_t Id(int slot) const;
    VirtualRumble TakeRumble(int slot);

private:
    struct Slot {
        uint32_t id = 0;
        SDL_Joystick* joystick = nullptr;
        std::string kind;
        std::unique_ptr<VirtualRumble> rumble;
        bool gyro_streaming = false;
        float gyro[3] = {0.0f, 0.0f, 0.0f};
        uint64_t sensor_time = 0;
    };

    Slot* Get(int slot);

    std::array<Slot, kSlots> slots_;
};

}
