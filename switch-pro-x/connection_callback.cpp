#include <Windows.h>
#include <Dbt.h>
#include <hidsdi.h>
#include <SetupAPI.h>

#include <atomic>
#include <iostream>
#include <thread>

#include "common.h"
#include "switch-pro-x.h"

namespace {
    constexpr LPTSTR WND_CLASS_NAME = TEXT("DeviceCallback");

    HWND hWnd;
    HDEVNOTIFY hDeviceNotification;
    GUID HID_GUID;

    void GetInitialiPluggedDevices()
    {
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
                    std::unique_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> interface_detail(reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(operator new(requiredBufferSize)));
                    interface_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                    if (SetupDiGetDeviceInterfaceDetail(devices, &data, interface_detail.get(), requiredBufferSize, nullptr, nullptr)) {
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
            PDEV_BROADCAST_DEVICEINTERFACE b = (PDEV_BROADCAST_DEVICEINTERFACE)lParam;

            switch (wParam) {
            case DBT_DEVICEARRIVAL:
            {
                tstring devicePath(b->dbcc_name);
                if (IsEqualGUID(b->dbcc_classguid, HID_GUID))
                {
                    AddController(devicePath);
                }
                break;
            }
            case DBT_DEVICEREMOVECOMPLETE:
            {
                tstring devicePath(b->dbcc_name);
                if (IsEqualGUID(b->dbcc_classguid, HID_GUID))
                {
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
    HidD_GetHidGuid(&HID_GUID);

    GetInitialiPluggedDevices();

    std::thread([] {
        WNDCLASS wndClass = { 0 };
        wndClass.lpfnWndProc = WinProcCallback;
        wndClass.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandle(0));
        wndClass.lpszClassName = WND_CLASS_NAME;

        ATOM atom = RegisterClass(&wndClass);
        if (atom != 0)
        {
            hWnd = CreateWindow(
                WND_CLASS_NAME,
                WND_MODULE_NAME,
                WS_DISABLED,
                CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT,
                nullptr,
                nullptr,
                nullptr,
                nullptr);

            if (hWnd != nullptr) {
                DEV_BROADCAST_DEVICEINTERFACE notificationFilter = { 0 };
                notificationFilter.dbcc_size = sizeof(notificationFilter);
                notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
                notificationFilter.dbcc_classguid = HID_GUID;
                hDeviceNotification = RegisterDeviceNotification(hWnd, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

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
                    // Cleanup if registering for device notifications fails
                    DestroyWindow(hWnd);
                    UnregisterClass(WND_CLASS_NAME, NULL);
                }
            }
            else
            {
                // Cleanup if window creation fails
                UnregisterClass(WND_CLASS_NAME, NULL);
            }
        }
    }).detach();
}
