#pragma once

#include <Windows.h>

#include <ViGEmUM.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <cstdint>

#include "common.h"

class ProControllerDevice {
public:
    ProControllerDevice(tstring Path);
    ~ProControllerDevice();

    bool Valid();
    void HandleXUSBCallback(UCHAR _large_motor, UCHAR _small_motor, UCHAR _led_number);

    // used for identification, so make them public
    VIGEM_TARGET ViGEm_Target;
    const tstring Path;

private:
    void ReadThread();
    std::vector<uint8_t> ReadData();
    void WriteData(const std::vector<uint8_t>& data);
    bool CheckIOError(DWORD err);

    uint8_t counter;
    HANDLE handle;
    USHORT output_size;
    USHORT input_size;
    std::chrono::steady_clock::time_point last_rumble;

    std::atomic<UCHAR> led_number;
    std::atomic<UCHAR> large_motor;
    std::atomic<UCHAR> small_motor;

    bool connected;
    std::atomic<bool> quitting;
    std::thread read_thread;
};

namespace std {
    template <> struct hash<ProControllerDevice> {
        size_t operator()(const ProControllerDevice& x) const {
            return reinterpret_cast<size_t>(&x);
        }
    };
}
