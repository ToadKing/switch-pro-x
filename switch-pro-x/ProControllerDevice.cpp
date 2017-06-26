#include <Windows.h>
#include <hidsdi.h>

#include <ViGEmUM.h>

#include <chrono>
#include <iostream>
#include <memory>

#include <cstring>

#include "common.h"
#include "ProControllerDevice.h"
#include "switch-pro-x.h"

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

ProControllerDevice::ProControllerDevice(libusb_device *dev)
    : counter(0)
    , Device(dev)
    , handle(nullptr)
    , quitting(false)
    , last_rumble()
    , led_number(0xFF)
{
    if (libusb_open(Device, &handle) != 0)
    {
        std::cerr << "Error opening Device " << Device << std::endl;
        return;
    }

    if (libusb_kernel_driver_active(handle, 0) == 1 && libusb_detach_kernel_driver(handle, 0))
    {
        std::cerr << "Error detaching handle from " << Device << " from kernel" << std::endl;
        return;
    }

    if (libusb_claim_interface(handle, 0) != 0)
    {
        std::cerr << "Error claiming interface on " << Device << std::endl;
        return;
    }

    VIGEM_TARGET_INIT(&ViGEm_Target);

    // We don't want to match the libusb driver again, don't set vid/pid and use the default one from ViGEm
    //vigem_target_set_vid(&ViGEm_Target, PRO_CONTROLLER_VID);
    //vigem_target_set_pid(&ViGEm_Target, PRO_CONTROLLER_PID);

    auto ret = vigem_target_plugin(Xbox360Wired, &ViGEm_Target);

    if (!VIGEM_SUCCESS(ret))
    {
        std::cerr << "error creating controller: " << std::hex << std::showbase << ret << std::endl;

        return;
    }

    ret = vigem_register_xusb_notification(XUSBCallback, ViGEm_Target);

    if (!VIGEM_SUCCESS(ret))
    {
        std::cerr << "error creating notification callback: " << std::hex << std::showbase << ret << std::endl;

        return;
    }

    uint8_t data[] = { 0x80, 0x01 };
    WriteData(data, sizeof(data));

    read_thread = std::thread(&ProControllerDevice::ReadThread, this);

    connected = true;
}

void ProControllerDevice::ReadThread()
{
    UCHAR last_led = 0xFF;
    XUSB_REPORT last_report = { 0 };

    while (!quitting)
    {
        unsigned char data[64] = { 0 };
        int size = 0;
        int err = libusb_interrupt_transfer(handle, EP_IN, data, sizeof(data), &size, 100);

        ProControllerPacket *payload = (ProControllerPacket *)data;

        auto now = std::chrono::steady_clock::now();

        if (now > last_rumble + std::chrono::milliseconds(100))
        {
            if (led_number != last_led)
            {
                uint8_t buf[65] = { 0x01, static_cast<uint8_t>(counter++ & 0x0F), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x30, static_cast<uint8_t>(1 << led_number) };
                WriteData(buf, sizeof(buf));

                last_led = led_number;
            }
            else
            {
                uint8_t buf[65] = { 0x10, static_cast<uint8_t>(counter++ & 0x0F), 0x80, 0x00, 0x40, 0x40, 0x80, 0x00, 0x40, 0x40 };

                if (large_motor != 0)
                {
                    buf[2] = buf[6] = 0x08;
                    buf[3] = buf[7] = large_motor;
                }
                else if (small_motor != 0)
                {
                    buf[2] = buf[6] = 0x10;
                    buf[3] = buf[7] = small_motor;
                }

                WriteData(buf, sizeof(buf));
            }

            last_rumble = now;
        }

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

            if (report != last_report)
            {
                // work around weird xusb driver quirk: https://github.com/nefarius/ViGEm/issues/4
                for (auto i = 0; i < 3; i++)
                {
                    auto ret = vigem_xusb_submit_report(ViGEm_Target, report);

                    if (!VIGEM_SUCCESS(ret))
                    {
                        std::cerr << "error sending report: " << std::hex << std::showbase << ret << std::endl;

                        quitting = true;
                    }
                }

                last_report = report;
            }

            break;
        }
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        uint8_t buf[65] = { 0x10, static_cast<uint8_t>(counter++ & 0x0F), 0x80, 0x00, 0x40, 0x40, 0x80, 0x00, 0x40, 0x40 };
        WriteData(buf, sizeof(buf));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        uint8_t buf[65] = { 0x01, static_cast<uint8_t>(counter++ & 0x0F), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x30, 0x00 };
        WriteData(buf, sizeof(buf));
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
        vigem_target_unplug(&ViGEm_Target);
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

void ProControllerDevice::HandleXUSBCallback(UCHAR _large_motor, UCHAR _small_motor, UCHAR _led_number)
{
#ifdef PRO_CONTROLLER_DEBUG_OUTPUT
    std::cout << "XUSB CALLBACK (" << this << ") LARGE MOTOR: " << +_large_motor << ", SMALL MOTOR: " << +_small_motor << ", LED: " << +_led_number << std::endl;
#endif

    large_motor = _large_motor;
    small_motor = _small_motor;
    led_number = _led_number;
}
