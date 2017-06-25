#pragma once

#include <Windows.h>

#include <libusb-1.0/libusb.h>

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

private:
    libusb_device *device;
    libusb_device_handle *handle;

    void ReadThread();

    bool connected;
    std::atomic<bool> quitting;
    std::thread read_thread;
};