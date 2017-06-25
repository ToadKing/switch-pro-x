#pragma once

#include <Windows.h>

#include <libusb-1.0/libusb.h>
#include <ViGEmUM.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <cstdint>

#include "common.h"

class ProControllerDevice {
public:
    ProControllerDevice(libusb_device *dev);
    ~ProControllerDevice();

    bool Valid();
    void WriteData(uint8_t *bytes, size_t size);
    void HandleXUSBCallback(UCHAR _large_motor, UCHAR _small_motor, UCHAR _led_number);

    // used for identification, so make them public
    VIGEM_TARGET ViGEm_Target;
    libusb_device *Device;

private:
    void ReadThread();

    uint8_t counter;
    libusb_device_handle *handle;
    std::chrono::steady_clock::time_point last_rumble;

    std::atomic<UCHAR> led_number;
    std::atomic<UCHAR> large_motor;
    std::atomic<UCHAR> small_motor;

    bool connected;
    std::atomic<bool> quitting;
    std::thread read_thread;
};