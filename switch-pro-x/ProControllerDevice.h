#pragma once

#include <Windows.h>

#include <ViGEmUM.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <cstdint>

#include "common.h"

class ProControllerDevice
{
public:
    ProControllerDevice(const tstring& path);
    ~ProControllerDevice();

    bool Valid();
    void HandleXUSBCallback(UCHAR _large_motor, UCHAR _small_motor, UCHAR _led_number);

    // used for identification, so make them public
    VIGEM_TARGET ViGEm_Target;
    const tstring Path;

private:
    using bytes = std::vector<std::uint8_t>;

    void USBReadThread();
    void BluetoothReadThread();
    void HandleLEDAndVibration();
    void ClearLEDAndVibration();
    void HandleController(const XUSB_REPORT& report);
    std::optional<bytes> ReadData();
    void WriteData(const bytes& data);
    bool CheckIOError(DWORD err);
    std::int16_t ScaleJoystick(std::int_fast64_t src_min, std::int_fast64_t src_max, std::int16_t val);

    std::uint8_t counter;
    HANDLE handle;
    USHORT output_size;
    USHORT input_size;
    std::chrono::steady_clock::time_point last_rumble;

    std::atomic<UCHAR> led_number;
    UCHAR large_motor;
    UCHAR small_motor;
    bool motor_large_waiting;
    bool motor_small_waiting;
    bool motor_large_will_empty;
    bool motor_small_will_empty;
    spinlock rumble_lock;

    bool connected;
    std::atomic<bool> quitting;
    std::thread read_thread;

    bool is_bluetooth;
    UCHAR last_led = 0xFF;
    XUSB_REPORT last_report;
};
