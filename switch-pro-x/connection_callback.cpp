#include <Windows.h>
#include <Dbt.h>
#include <hidsdi.h>
#include <SetupAPI.h>

#include <memory>
#include <thread>

#include "common.h"
#include "switch-pro-x.h"

namespace
{
    constexpr LPCTSTR WND_CLASS_NAME = TEXT("DeviceCallback");

    GUID HID_GUID;

    void GetInitialPluggedDevices()
    {
        using std::unique_ptr;

        HDEVINFO devices = SetupDiGetClassDevs(&HID_GUID, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (devices != INVALID_HANDLE_VALUE)
        {
            DWORD i = 0;
            SP_DEVICE_INTERFACE_DATA data;
            data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

            while (SetupDiEnumDeviceInterfaces(devices, nullptr, &HID_GUID, i++, &data))
            {
                DWORD requiredBufferSize;
                BOOL result = SetupDiGetDeviceInterfaceDetail(devices, &data, nullptr, 0, &requiredBufferSize, nullptr);

                if (result == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
                {
                    unique_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> interface_detail(static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(operator new(requiredBufferSize)));
                    interface_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                    if (SetupDiGetDeviceInterfaceDetail(devices, &data, interface_detail.get(), requiredBufferSize, nullptr, nullptr))
                    {
                        tstring path(interface_detail->DevicePath);
                        AddController(path);
                    }
                }
            }
        }

        SetupDiDestroyDeviceInfoList(devices);
    }

    LRESULT CALLBACK WinProcCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LRESULT result = 1;

        switch (msg) {
        case WM_DEVICECHANGE:
        {
            auto b = (PDEV_BROADCAST_DEVICEINTERFACE)lParam;

            switch (wParam) {
            case DBT_DEVICEARRIVAL:
            {
                if (IsEqualGUID(b->dbcc_classguid, HID_GUID))
                {
                    tstring devicePath(b->dbcc_name);
                    AddController(devicePath);
                }
                break;
            }
            case DBT_DEVICEREMOVECOMPLETE:
            {
                if (IsEqualGUID(b->dbcc_classguid, HID_GUID))
                {
                    tstring devicePath(b->dbcc_name);
                    RemoveController(devicePath);
                }
                break;
            }
            }
        }
        default:
        {
            result = DefWindowProc(hWnd, msg, wParam, lParam);
            break;
        }
        }

        return result;
    }
}

void SetupDeviceNotifications()
{
    using std::thread;

    HidD_GetHidGuid(&HID_GUID);

    GetInitialPluggedDevices();

    thread([] {
        WNDCLASS wndClass = { 0 };
        wndClass.lpfnWndProc = WinProcCallback;
        wndClass.hInstance = GetModuleHandle(0);
        wndClass.lpszClassName = WND_CLASS_NAME;

        auto atom = RegisterClass(&wndClass);

        if (atom != 0)
        {
            auto hWnd = CreateWindow(
                WND_CLASS_NAME,
                WND_MODULE_NAME,
                WS_DISABLED,
                CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT,
                nullptr,
                nullptr,
                nullptr,
                nullptr);

            if (hWnd != nullptr)
            {
                DEV_BROADCAST_DEVICEINTERFACE notificationFilter = { 0 };
                notificationFilter.dbcc_size = sizeof(notificationFilter);
                notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
                notificationFilter.dbcc_classguid = HID_GUID;
                auto hDeviceNotification = RegisterDeviceNotification(hWnd, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

                if (hDeviceNotification != nullptr)
                {
                    MSG msg;

                    while (GetMessage(&msg, hWnd, WM_DEVICECHANGE, WM_DEVICECHANGE))
                    {
                        DispatchMessage(&msg);
                    }
                }
                else
                {
                    DestroyWindow(hWnd);
                    UnregisterClass(WND_CLASS_NAME, nullptr);
                }
            }
            else
            {
                UnregisterClass(WND_CLASS_NAME, nullptr);
            }
        }
    }).detach();
}
