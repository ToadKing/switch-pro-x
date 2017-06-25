#include <Windows.h>
#include <hidsdi.h>

#include <ViGEmUM.h>

#include <iostream>
#include <memory>

#include <cstring>

#include "ProControllerDevice.h"

//#define PRO_CONTROLLER_DEBUG_OUTPUT

namespace
{
    constexpr uint8_t EP_IN = 0x81;
    constexpr uint8_t EP_OUT = 0x01;

    constexpr uint8_t PACKET_TYPE_STATUS = 0x81;
    constexpr uint8_t PACKET_TYPE_CONTROLLER_DATA = 0x30;

    constexpr uint8_t STATUS_TYPE_SERIAL = 0x01;
    constexpr uint8_t STATUS_TYPE_INIT = 0x02;

    enum {
        SWITCH_BUTTON_MASK_A = 0x00000800,
        SWITCH_BUTTON_MASK_B = 0x00000400,
        SWITCH_BUTTON_MASK_X = 0x00000200,
        SWITCH_BUTTON_MASK_Y = 0x00000100,

        SWITCH_BUTTON_MASK_DPAD_UP = 0x02000000,
        SWITCH_BUTTON_MASK_DPAD_DOWN = 0x01000000,
        SWITCH_BUTTON_MASK_DPAD_LEFT = 0x08000000,
        SWITCH_BUTTON_MASK_DPAD_RIGHT = 0x04000000,

        SWITCH_BUTTON_MASK_PLUS = 0x00020000,
        SWITCH_BUTTON_MASK_MINUS = 0x00010000,
        SWITCH_BUTTON_MASK_HOME = 0x00100000,
        SWITCH_BUTTON_MASK_SHARE = 0x00200000,

        SWITCH_BUTTON_MASK_L = 0x40000000,
        SWITCH_BUTTON_MASK_ZL = 0x80000000,
        SWITCH_BUTTON_MASK_THUMB_L = 0x00080000,

        SWITCH_BUTTON_MASK_R = 0x00004000,
        SWITCH_BUTTON_MASK_ZR = 0x00008000,
        SWITCH_BUTTON_MASK_THUMB_R = 0x00040000,
    };

#pragma pack(push, 1)
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
                uint32_t buttons;
                uint8_t analog[6];
            } controller_data;
            uint8_t padding[63];
        } data;
    } ProControllerPacket;
#pragma pack(pop)
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

    VIGEM_TARGET_INIT(&vigem_target);
    vigem_target_set_vid(&vigem_target, PRO_CONTROLLER_VID);
    vigem_target_set_vid(&vigem_target, PRO_CONTROLLER_PID);

    auto ret = vigem_target_plugin(Xbox360Wired, &vigem_target);

    if (!VIGEM_SUCCESS(ret))
    {
        std::cerr << "error creating controller: " << std::hex << std::showbase << ret << std::endl;

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
        case PACKET_TYPE_CONTROLLER_DATA:
        {
            const auto analog = payload->data.controller_data.analog;
            const auto buttons = payload->data.controller_data.buttons;

            int8_t lx = static_cast<int8_t>(((analog[1] & 0x0F) << 4) | ((analog[0] & 0xF0) >> 4)) + 127;
            int8_t ly = analog[2] + 127;
            int8_t rx = static_cast<int8_t>(((analog[4] & 0x0F) << 4) | ((analog[3] & 0xF0) >> 4)) + 127;
            int8_t ry = analog[5] + 127;

#ifdef PRO_CONTROLLER_DEBUG_OUTPUT
            std::cout << "A: " << !!(buttons & SWITCH_BUTTON_MASK_A) << ", ";
            std::cout << "B: " << !!(buttons & SWITCH_BUTTON_MASK_B) << ", ";
            std::cout << "X: " << !!(buttons & SWITCH_BUTTON_MASK_X) << ", ";
            std::cout << "Y: " << !!(buttons & SWITCH_BUTTON_MASK_Y) << ", ";

            std::cout << "DU: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_UP) << ", ";
            std::cout << "DD: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_DOWN) << ", ";
            std::cout << "DL: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_LEFT) << ", ";
            std::cout << "DR: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_RIGHT) << ", ";

            std::cout << "P: " << !!(buttons & SWITCH_BUTTON_MASK_PLUS) << ", ";
            std::cout << "M: " << !!(buttons & SWITCH_BUTTON_MASK_MINUS) << ", ";
            std::cout << "H: " << !!(buttons & SWITCH_BUTTON_MASK_HOME) << ", ";
            std::cout << "S: " << !!(buttons & SWITCH_BUTTON_MASK_SHARE) << ", ";

            std::cout << "L: " << !!(buttons & SWITCH_BUTTON_MASK_L) << ", ";
            std::cout << "ZL: " << !!(buttons & SWITCH_BUTTON_MASK_ZL) << ", ";
            std::cout << "TL: " << !!(buttons & SWITCH_BUTTON_MASK_THUMB_L) << ", ";

            std::cout << "R: " << !!(buttons & SWITCH_BUTTON_MASK_R) << ", ";
            std::cout << "ZR: " << !!(buttons & SWITCH_BUTTON_MASK_ZR) << ", ";
            std::cout << "TR: " << !!(buttons & SWITCH_BUTTON_MASK_THUMB_R) << ", ";

            std::cout << "LX: " << +lx << ", ";
            std::cout << "LY: " << +ly << ", ";

            std::cout << "RX: " << +rx << ", ";
            std::cout << "RY: " << +ry;

            std::cout << std::endl;
#endif

            XUSB_REPORT report = { 0 };

            // assign a/b/x/y so they match the positions on the xbox layout
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_A) ? XUSB_GAMEPAD_B : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_B) ? XUSB_GAMEPAD_A : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_X) ? XUSB_GAMEPAD_Y : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_Y) ? XUSB_GAMEPAD_X : 0;

            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_DPAD_UP) ? XUSB_GAMEPAD_DPAD_UP : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_DPAD_DOWN) ? XUSB_GAMEPAD_DPAD_DOWN : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_DPAD_LEFT) ? XUSB_GAMEPAD_DPAD_LEFT : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_DPAD_RIGHT) ? XUSB_GAMEPAD_DPAD_RIGHT : 0;

            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_PLUS) ? XUSB_GAMEPAD_START : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_MINUS) ? XUSB_GAMEPAD_BACK : 0;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_HOME) ? XUSB_GAMEPAD_GUIDE : 0;

            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_L) ? XUSB_GAMEPAD_LEFT_SHOULDER : 0;
            report.bLeftTrigger = !!(buttons & SWITCH_BUTTON_MASK_ZL) * 0xFF;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_THUMB_L) ? XUSB_GAMEPAD_LEFT_THUMB : 0;

            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_R) ? XUSB_GAMEPAD_RIGHT_SHOULDER : 0;
            report.bRightTrigger = !!(buttons & SWITCH_BUTTON_MASK_ZR) * 0xFF;
            report.wButtons |= !!(buttons & SWITCH_BUTTON_MASK_THUMB_R) ? XUSB_GAMEPAD_RIGHT_THUMB : 0;

            // 257 is not a typo, that's so we get the full range from 0x0000 to 0xffff
            report.sThumbLX = lx * 257;
            report.sThumbLY = ly * 257;
            report.sThumbRX= rx * 257;
            report.sThumbRY = ry * 257;

            auto ret = vigem_xusb_submit_report(vigem_target, report);

            if (!VIGEM_SUCCESS(ret))
            {
                std::cerr << "error sending report: " << std::hex << std::showbase << ret << std::endl;

                quitting = true;
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

    if (connected)
    {
        vigem_target_unplug(&vigem_target);
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
