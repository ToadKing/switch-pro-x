#pragma once

#include <Windows.h>

#include <libusb-1.0/libusb.h>
#include <ViGEmUM.h>

#include <atomic>
#include <thread>

#include <cstdint>

#include "common.h"

class ProControllerDevice {
public:
    ProControllerDevice(libusb_device *dev);
    ~ProControllerDevice();

    bool Valid();
    void WriteData(uint8_t *bytes, size_t size);
    void HandleXUSBCallback(UCHAR large_motor, UCHAR small_motor, UCHAR led_number);
    VIGEM_TARGET ViGEm_Target;
    libusb_device *Device;

private:
    uint8_t counter;
    libusb_device_handle *handle;
    XUSB_REPORT last_report;

    void ReadThread();

    bool connected;
    std::atomic<bool> quitting;
    std::thread read_thread;
};