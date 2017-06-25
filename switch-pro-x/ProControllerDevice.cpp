#include "ProControllerDevice.h"

#include <Windows.h>
#include <hidsdi.h>

#include <iostream>
#include <memory>

#include <cstring>

namespace
{
    constexpr uint8_t EP_IN = 0x81;
    constexpr uint8_t EP_OUT = 0x01;

    constexpr uint8_t PACKET_TYPE_STATUS = 0x81;
    constexpr uint8_t PACKET_TYPE_CONTROLLER_DATA = 0x30;

    constexpr uint8_t STATUS_TYPE_SERIAL = 0x01;
    constexpr uint8_t STATUS_TYPE_INIT = 0x02;

    typedef struct
    {
        uint8_t type;
        union
        {
            struct
            {
                uint8_t type;
                uint8_t serial[8];
            } status_response;
            struct
            {
                uint8_t timestamp;
            } controller_data;
            uint8_t padding[63];
        } data;
    } ProControllerPacket;
}

ProControllerDevice::ProControllerDevice(libusb_device *dev) : device(dev), handle(nullptr), quitting(false)
{
    if (libusb_open(device, &handle) != 0)
    {
        std::cerr << "Error opening device " << device << std::endl;
        return;
    }

    if (libusb_kernel_driver_active(handle, 0) == 1 && libusb_detach_kernel_driver(handle, 0))
    {
        std::cerr << "Error detaching handle from " << device << " from kernel" << std::endl;
        return;
    }

    if (libusb_claim_interface(handle, 0) != 0)
    {
        std::cerr << "Error claiming interface on " << device << std::endl;
        return;
    }

    uint8_t data[] = { 0x80, 0x01 };
    WriteData(data, sizeof(data));

    read_thread = std::thread(&ProControllerDevice::ReadThread, this);

    connected = true;
}

void ProControllerDevice::ReadThread()
{
    while (!quitting)
    {
        unsigned char data[64] = { 0 };
        int size = 0;
        int err = libusb_interrupt_transfer(handle, EP_IN, data, sizeof(data), &size, 100);

        ProControllerPacket *payload = (ProControllerPacket *)data;

        switch (payload->type)
        {
        case PACKET_TYPE_STATUS:
        {
            switch (payload->data.status_response.type)
            {
            case STATUS_TYPE_SERIAL:
            {
                uint8_t payload[] = { 0x80, 0x02 };
                WriteData(payload, sizeof(payload));
                break;
            }
            case STATUS_TYPE_INIT:
            {
                uint8_t payload[] = { 0x80, 0x04 };
                WriteData(payload, sizeof(payload));
                break;
            }
            }
            break;
        }
        }
    }
}

ProControllerDevice::~ProControllerDevice()
{
    quitting = true;

    if (read_thread.joinable())
    {
        read_thread.join();
    }
}

bool ProControllerDevice::Valid() {
    return connected;
}

void ProControllerDevice::WriteData(uint8_t *buf, size_t size)
{
    int tmp;
    int err = libusb_interrupt_transfer(handle, EP_OUT, buf, static_cast<int>(size), &tmp, 100);
}
