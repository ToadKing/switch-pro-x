#include <Windows.h>

#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <string>

#include <cstdint>
#include <cstdlib>

#include <ViGEmUM.h>

#include "common.h"
#include "connection_callback.h"
#include "switch-pro-x.h"
#include "ProControllerDevice.h"

namespace {
    std::unordered_set<std::unique_ptr<ProControllerDevice>> proControllers;
    std::mutex controllerMapMutex;
}

void AddController(libusb_device *dev)
{
    std::lock_guard<std::mutex> lk(controllerMapMutex);
    auto device = std::make_unique<ProControllerDevice>(dev);
    if (device->Valid()) {
        std::cout << "FOUND PRO CONTROLLER: " << device.get() << std::endl;
        proControllers.insert(std::move(device));
    }
}

void RemoveController(libusb_device *dev)
{
    std::lock_guard<std::mutex> lk(controllerMapMutex);
    auto it = std::find_if(proControllers.begin(), proControllers.end(), [dev](const std::unique_ptr<ProControllerDevice>& c) { return c->Device == dev; });
    if (it != proControllers.end())
    {
        std::cout << "REMOVED PRO CONTROLLER: " << it->get() << std::endl;
        proControllers.erase(it);
    }
}

VOID CALLBACK XUSBCallback(VIGEM_TARGET target, UCHAR large_motor, UCHAR small_motor, UCHAR led_number)
{
    std::lock_guard<std::mutex> lk(controllerMapMutex);
    auto it = std::find_if(proControllers.begin(), proControllers.end(), [target](const std::unique_ptr<ProControllerDevice>& c) { return c->ViGEm_Target == target; });
    if (it != proControllers.end())
    {
        (*it)->HandleXUSBCallback(large_motor, small_motor, led_number);
    }
}

BOOL WINAPI ctrl_handler(DWORD _In_ event)
{
    if (event == CTRL_CLOSE_EVENT ||
        event == CTRL_C_EVENT ||
        event == CTRL_BREAK_EVENT)
    {
        std::exit(0);

        return TRUE;
    }

    return FALSE;
}

int main()
{
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    std::atexit([] {
        // trigger deconstructors for all controllers
        proControllers.clear();

        vigem_shutdown();
    });

    auto ret = vigem_init();

    if (!VIGEM_SUCCESS(ret))
    {
        std::cerr << "error initializing ViGEm: " << std::hex << std::showbase << ret << std::endl;

        system("pause");

        return 1;
    }

    SetupDeviceNotifications();

    return 0;
}
