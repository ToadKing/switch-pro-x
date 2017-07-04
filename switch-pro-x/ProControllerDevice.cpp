#define NOMINMAX
#include <Windows.h>
#include <hidsdi.h>

#include <ViGEmUM.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

#include "common.h"
#include "ProControllerDevice.h"
#include "switch-pro-x.h"

//#define PRO_CONTROLLER_DEBUG_OUTPUT

namespace
{
    constexpr std::uint8_t PACKET_TYPE_STATUS = 0x81;
    constexpr std::uint8_t PACKET_TYPE_CONTROLLER_DATA = 0x30;

    constexpr std::uint8_t STATUS_TYPE_SERIAL = 0x01;
    constexpr std::uint8_t STATUS_TYPE_INIT = 0x02;

    constexpr DWORD TIMEOUT = 500;

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
        std::uint8_t type;
        union
        {
            struct
            {
                std::uint8_t type;
                std::uint8_t serial[8];
            } status_response;
            struct
            {
                std::uint8_t timestamp;
                std::uint32_t buttons;
                std::uint8_t analog[6];
            } controller_data;
            std::uint8_t padding[63];
        } data;
    } ProControllerPacket;
#pragma pack(pop)
}

ProControllerDevice::ProControllerDevice(const tstring& path)
    : counter(0)
    , Path(path)
    , handle(INVALID_HANDLE_VALUE)
    , quitting(false)
    , last_rumble()
    , led_number(0xFF)
    , rumble_mutex()
{
    using std::cerr;
    using std::endl;
    using std::thread;

    handle = CreateFile(
        path.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE)
    {
        auto err = GetLastError();

        if (err != ERROR_ACCESS_DENIED)
        {
            cerr << "error opening ";
            *tcerr << path;
            cerr << " (" << GetLastError() << ")" << endl;
        }

        return;
    }

    HIDD_ATTRIBUTES attributes;
    attributes.Size = sizeof(attributes);
    BOOLEAN ok = HidD_GetAttributes(handle, &attributes);

    if (!ok)
    {
        cerr << "Error calling HidD_GetAttributes (" << GetLastError() << ")" << endl;
        return;
    }

    if (attributes.ProductID != PRO_CONTROLLER_PID || attributes.VendorID != PRO_CONTROLLER_VID)
    {
        // not a pro controller, fail silently
        return;
    }

    PHIDP_PREPARSED_DATA preparsed_data;
    ok = HidD_GetPreparsedData(handle, &preparsed_data);

    if (!ok)
    {
        cerr << "Error calling HidD_GetPreparsedData (" << GetLastError() << ")" << endl;
        return;
    }

    HIDP_CAPS caps;
    NTSTATUS status = HidP_GetCaps(preparsed_data, &caps);
    HidD_FreePreparsedData(preparsed_data);

    if (status != HIDP_STATUS_SUCCESS)
    {
        cerr << "Error calling HidP_GetCaps (" << status << ")" << endl;
        return;
    }

    output_size = caps.OutputReportByteLength;
    input_size = caps.InputReportByteLength;

    VIGEM_TARGET_INIT(&ViGEm_Target);

    // use driver default vid/pid so we don't match recursively
    //vigem_target_set_vid(&ViGEm_Target, PRO_CONTROLLER_VID);
    //vigem_target_set_pid(&ViGEm_Target, PRO_CONTROLLER_PID);

    auto ret = vigem_target_plugin(Xbox360Wired, &ViGEm_Target);

    if (!VIGEM_SUCCESS(ret))
    {
        cerr << "error creating controller: " << ret << endl;

        return;
    }

    ret = vigem_register_xusb_notification(XUSBCallback, ViGEm_Target);

    if (!VIGEM_SUCCESS(ret))
    {
        cerr << "error creating notification callback: " << ret << endl;
        vigem_target_unplug(&ViGEm_Target);

        return;
    }

    bytes data = { 0x80, 0x01 };
    WriteData(data);

    read_thread = thread(&ProControllerDevice::ReadThread, this);

    connected = true;
}

ProControllerDevice::~ProControllerDevice()
{
    quitting = true;

    if (read_thread.joinable())
    {
        read_thread.join();
    }

    if (handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
    }

    if (connected)
    {
        //vigem_unregister_xusb_notification(XUSBCallback);
        vigem_target_unplug(&ViGEm_Target);
    }
}

void ProControllerDevice::ReadThread()
{
    using std::cout;
    using std::cerr;
    using std::endl;
    using std::chrono::steady_clock;
    using std::chrono::milliseconds;
    using std::this_thread::sleep_for;
    using std::uint8_t;
    using std::lock_guard;
    using std::mutex;

    UCHAR last_led = 0xFF;
    XUSB_REPORT last_report = { 0 };
    bool first_control = false;

    while (!quitting)
    {
        const auto data = ReadData();

        if (!data)
        {
            continue;
        }

        const auto hid_payload = reinterpret_cast<const ProControllerPacket *>(data->data());
        const auto now = steady_clock::now();

        if (first_control && now > last_rumble + milliseconds(100))
        {
            if (led_number != last_led)
            {
                bytes buf = { 0x01, static_cast<uint8_t>(counter++ & 0x0F), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x30, static_cast<uint8_t>(1 << led_number) };
                WriteData(buf);

                last_led = led_number;
            }
            else
            {
                bytes buf = { 0x10, static_cast<uint8_t>(counter++ & 0x0F), 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00 };

                {
                    lock_guard<mutex> lk(rumble_mutex);

                    // discovered through trial and error, seem to be good enough
                    // NOTE: xinput left/right motors are actually functionally different, not for directional rumble
                    if (large_motor != 0)
                    {
                        buf[2] = buf[6] = 0x80;
                        buf[3] = buf[7] = 0x20;
                        buf[4] = buf[8] = 0x62;
                        buf[5] = buf[9] = large_motor >> 2;
                    }
                    else if (small_motor != 0)
                    {
                        buf[2] = buf[6] = 0x98;
                        buf[3] = buf[7] = 0x20;
                        buf[4] = buf[8] = 0x62;
                        buf[5] = buf[9] = small_motor >> 2;
                    }

                    if (motor_large_will_empty)
                    {
                        large_motor = 0;
                        motor_large_will_empty = false;
                    }

                    if (motor_small_will_empty)
                    {
                        small_motor = 0;
                        motor_small_will_empty = false;
                    }

                    motor_large_waiting = false;
                    motor_small_waiting = false;
                }

                WriteData(buf);
            }

            last_rumble = now;
        }

        switch (hid_payload->type)
        {
        case PACKET_TYPE_STATUS:
        {
            switch (hid_payload->data.status_response.type)
            {
            case STATUS_TYPE_SERIAL:
            {
                bytes payload = { 0x80, 0x02 };
                WriteData(payload);
                break;
            }
            case STATUS_TYPE_INIT:
            {
                bytes payload = { 0x80, 0x04 };
                WriteData(payload);
                break;
            }
            }
            break;
        }
        case PACKET_TYPE_CONTROLLER_DATA:
        {
            const auto& analog = hid_payload->data.controller_data.analog;
            const auto& buttons = hid_payload->data.controller_data.buttons;

            if (!first_control)
            {
                last_rumble = steady_clock::now();
                first_control = true;
            }

            int8_t lx = static_cast<int8_t>(((analog[1] & 0x0F) << 4) | ((analog[0] & 0xF0) >> 4)) + 127;
            int8_t ly = analog[2] + 127;
            int8_t rx = static_cast<int8_t>(((analog[4] & 0x0F) << 4) | ((analog[3] & 0xF0) >> 4)) + 127;
            int8_t ry = analog[5] + 127;

#ifdef PRO_CONTROLLER_DEBUG_OUTPUT
            cout << "A: " << !!(buttons & SWITCH_BUTTON_MASK_A) << ", ";
            cout << "B: " << !!(buttons & SWITCH_BUTTON_MASK_B) << ", ";
            cout << "X: " << !!(buttons & SWITCH_BUTTON_MASK_X) << ", ";
            cout << "Y: " << !!(buttons & SWITCH_BUTTON_MASK_Y) << ", ";

            cout << "DU: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_UP) << ", ";
            cout << "DD: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_DOWN) << ", ";
            cout << "DL: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_LEFT) << ", ";
            cout << "DR: " << !!(buttons & SWITCH_BUTTON_MASK_DPAD_RIGHT) << ", ";

            cout << "P: " << !!(buttons & SWITCH_BUTTON_MASK_PLUS) << ", ";
            cout << "M: " << !!(buttons & SWITCH_BUTTON_MASK_MINUS) << ", ";
            cout << "H: " << !!(buttons & SWITCH_BUTTON_MASK_HOME) << ", ";
            cout << "S: " << !!(buttons & SWITCH_BUTTON_MASK_SHARE) << ", ";

            cout << "L: " << !!(buttons & SWITCH_BUTTON_MASK_L) << ", ";
            cout << "ZL: " << !!(buttons & SWITCH_BUTTON_MASK_ZL) << ", ";
            cout << "TL: " << !!(buttons & SWITCH_BUTTON_MASK_THUMB_L) << ", ";

            cout << "R: " << !!(buttons & SWITCH_BUTTON_MASK_R) << ", ";
            cout << "ZR: " << !!(buttons & SWITCH_BUTTON_MASK_ZR) << ", ";
            cout << "TR: " << !!(buttons & SWITCH_BUTTON_MASK_THUMB_R) << ", ";

            cout << "LX: " << +lx << ", ";
            cout << "LY: " << +ly << ", ";

            cout << "RX: " << +rx << ", ";
            cout << "RY: " << +ry;

            cout << endl;
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

            report.sThumbLX = lx;
            report.sThumbLY = ly;
            report.sThumbRX = rx;
            report.sThumbRY = ry;

            ScaleJoystick(report.sThumbLX, report.sThumbLY);
            ScaleJoystick(report.sThumbRX, report.sThumbRY);

            if (report != last_report)
            {
                // work around weird xusb driver quirk: https://github.com/nefarius/ViGEm/issues/4
                for (auto i = 0; i < 3; i++)
                {
                    auto ret = vigem_xusb_submit_report(ViGEm_Target, report);

                    if (!VIGEM_SUCCESS(ret))
                    {
                        cerr << "error sending report: " << ret << endl;

                        quitting = true;
                    }
                }

                last_report = report;
            }

            break;
        }
        }
    }

    sleep_for(milliseconds(100));

    {
        // stop haptic feedback
        bytes buf = { 0x10, static_cast<uint8_t>(counter++ & 0x0F), 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00 };
        WriteData(buf);
    }

    sleep_for(milliseconds(100));

    {
        // turn off LED
        bytes buf = { 0x01, static_cast<uint8_t>(counter++ & 0x0F), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x30, 0x00 };
        WriteData(buf);
    }
}

void ProControllerDevice::ScaleJoystick(std::int16_t& x, std::int16_t& y)
{
    using std::int16_t;
    using std::int_fast32_t;
    using std::clamp;
    using std::numeric_limits;

    typedef numeric_limits<int16_t> int16_limts;

    constexpr int_fast32_t DST_MIN = int16_limts::min();
    constexpr int_fast32_t DST_MAX = int16_limts::max();
    constexpr int_fast32_t DST_RNG = DST_MAX - DST_MIN;

    constexpr int_fast32_t SRC_X_MIN = -100;
    constexpr int_fast32_t SRC_X_MAX = 85;
    constexpr int_fast32_t SRC_X_RNG = SRC_X_MAX - SRC_X_MIN;

    constexpr int_fast32_t SRC_Y_MIN = -100;
    constexpr int_fast32_t SRC_Y_MAX = 90;
    constexpr int_fast32_t SRC_Y_RNG = SRC_Y_MAX - SRC_Y_MIN;

    auto new_x = (((x - SRC_X_MIN) * DST_RNG) / SRC_X_RNG) + DST_MIN;
    auto new_y = (((y - SRC_Y_MIN) * DST_RNG) / SRC_Y_RNG) + DST_MIN;

    x = static_cast<int16_t>(clamp(new_x, DST_MIN, DST_MAX));
    y = static_cast<int16_t>(clamp(new_y, DST_MIN, DST_MAX));
}

bool ProControllerDevice::Valid() {
    return connected;
}

std::optional<ProControllerDevice::bytes> ProControllerDevice::ReadData()
{
    using std::cerr;
    using std::endl;

    bytes buf(input_size);

    DWORD bytesRead = 0;
    OVERLAPPED ol = { 0 };
    ol.hEvent = CreateEvent(nullptr, FALSE, FALSE, TEXT(""));

    if (!ReadFile(handle, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, &ol))
    {
        auto read_err = GetLastError();

        if (read_err == ERROR_IO_PENDING)
        {
            auto waitObject = WaitForSingleObject(ol.hEvent, TIMEOUT);

            if (waitObject == WAIT_OBJECT_0)
            {
                if (!GetOverlappedResult(handle, &ol, &bytesRead, TRUE))
                {
                    auto err = GetLastError();

                    if (CheckIOError(err))
                    {
                        cerr << "Read failed (" << err << ")" << endl;
                    }

                    return {};
                }
            }
            else
            {
                cerr << "Read failed (" << waitObject << ")" << endl;

                // could have timed out, cancel the IO if possible
                if (CancelIo(handle))
                {
                    HANDLE handles[2];
                    handles[0] = handle;
                    handles[1] = ol.hEvent;
                    WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                }

                return {};
            }
        }
        else
        {
            if (CheckIOError(read_err))
            {
                cerr << "Read failed (" << read_err << ")" << endl;
            }

            return {};
        }
    }

    CloseHandle(ol.hEvent);

    buf.resize(bytesRead);

    return buf;
}

void ProControllerDevice::WriteData(const bytes& data)
{
    using std::cerr;
    using std::endl;
    using std::copy;

    bytes buf;

    if (data.size() < output_size)
    {
        buf.resize(output_size);
        copy(data.begin(), data.end(), buf.begin());
    }
    else
    {
        buf = data;
    }

    DWORD tmp;
    OVERLAPPED ol = { 0 };
    ol.hEvent = CreateEvent(nullptr, FALSE, FALSE, TEXT(""));

    if (!WriteFile(handle, buf.data(), static_cast<DWORD>(buf.size()), &tmp, &ol))
    {
        auto write_err = GetLastError();

        if (write_err == ERROR_IO_PENDING)
        {
            auto waitObject = WaitForSingleObject(ol.hEvent, TIMEOUT);

            if (waitObject == WAIT_OBJECT_0) {
                if (!GetOverlappedResult(handle, &ol, &tmp, TRUE)) {
                    auto err = GetLastError();

                    if (CheckIOError(err))
                    {
                        cerr << "Write failed (" << GetLastError() << ")" << endl;
                    }
                }
            }
            else
            {
                cerr << "Write failed (" << waitObject << ")" << endl;

                // could have timed out, cancel the IO if possible
                if (CancelIo(handle))
                {
                    HANDLE handles[2];
                    handles[0] = handle;
                    handles[1] = ol.hEvent;
                    WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                }
            }
        }
        else
        {
            if (CheckIOError(write_err))
            {
                cerr << "Write failed (" << write_err << ")" << endl;
            }
        }
    }

    CloseHandle(ol.hEvent);
}

void ProControllerDevice::HandleXUSBCallback(UCHAR _large_motor, UCHAR _small_motor, UCHAR _led_number)
{
    using std::cout;
    using std::endl;
    using std::lock_guard;
    using std::mutex;

#ifdef PRO_CONTROLLER_DEBUG_OUTPUT
    cout << "XUSB CALLBACK (";
    *tcout << Path;
    cout << ") LARGE MOTOR: " << +_large_motor << ", SMALL MOTOR: " << +_small_motor << ", LED: " << +_led_number << endl;
#endif

    {
        lock_guard<mutex> lk(rumble_mutex);

        // avoid rumble effects being lost because they toggle on and off too fast
        if (_large_motor == 0 && motor_large_waiting)
        {
            motor_large_will_empty = true;
        }
        else
        {
            large_motor = _large_motor;
            motor_large_will_empty = false;
        }
        motor_large_waiting = true;

        if (_small_motor == 0 && motor_small_waiting)
        {
            motor_small_will_empty = true;
        }
        else
        {
            small_motor = _small_motor;
            motor_small_will_empty = false;
        }
        motor_small_waiting = true;
    }

    led_number = _led_number;
}

bool ProControllerDevice::CheckIOError(DWORD err)
{
    bool ret = true;

    switch (err)
    {
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_OPERATION_ABORTED:
    {
        // not fatal
        ret = false;
        break;
    }
    }

    return ret;
}
