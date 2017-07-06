#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include <cstdint>
#include <cstdlib>

#include <ViGEmUM.h>
#include <HidCerberus.Lib.h>

#include "common.h"
#include "connection_callback.h"
#include "switch-pro-x.h"
#include "ProControllerDevice.h"

namespace
{
    std::unordered_set<std::unique_ptr<ProControllerDevice>> proControllers;
    std::mutex controllerMapMutex;
}

void AddController(const tstring &path)
{
    using std::cout;
    using std::endl;
    using std::lock_guard;
    using std::mutex;
    using std::make_unique;
    using std::move;

    lock_guard<mutex> lk(controllerMapMutex);

    auto device = make_unique<ProControllerDevice>(path);

    if (device->Valid())
    {
        cout << "FOUND PRO CONTROLLER: ";
        tcout << device->Path;
        cout << endl;
        proControllers.insert(move(device));
    }
}

void RemoveController(const tstring &path)
{
    using std::cout;
    using std::endl;
    using std::lock_guard;
    using std::mutex;
    using std::find_if;

    lock_guard<mutex> lk(controllerMapMutex);

    auto it = find_if(proControllers.begin(), proControllers.end(), [path](const auto& c) { return tstring_icompare(c->Path, path); });

    if (it != proControllers.end())
    {
        cout << "REMOVED PRO CONTROLLER: ";
        tcout << (*it)->Path;
        cout << endl;
        proControllers.erase(it);
    }
}

VOID CALLBACK XUSBCallback(VIGEM_TARGET target, UCHAR large_motor, UCHAR small_motor, UCHAR led_number)
{
    using std::lock_guard;
    using std::mutex;
    using std::find_if;

    lock_guard<mutex> lk(controllerMapMutex);

    auto it = find_if(proControllers.begin(), proControllers.end(), [target](const auto& c) { return c->ViGEm_Target == target; });
    if (it != proControllers.end())
    {
        (*it)->HandleXUSBCallback(large_motor, small_motor, led_number);
    }
}

BOOL WINAPI ctrl_handler(DWORD _In_ event)
{
    using std::exit;

    if (event == CTRL_CLOSE_EVENT ||
        event == CTRL_C_EVENT ||
        event == CTRL_BREAK_EVENT)
    {
        exit(0);
    }

    return FALSE;
}

int main()
{
    using std::cerr;
    using std::endl;
    using std::atexit;
    using std::lock_guard;
    using std::mutex;
    using std::system;
    using std::this_thread::sleep_for;
    using std::chrono::hours;

    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    atexit([] {
        // trigger deconstructors for all controllers
        {
            lock_guard<mutex> lk(controllerMapMutex);

            proControllers.clear();
        }

        vigem_shutdown();

        HidGuardianClose();
    });

    auto ret = vigem_init();

    if (!VIGEM_SUCCESS(ret))
    {
        cerr << "error initializing ViGEm: " << ret << endl;

        system("pause");

        return 1;
    }

    HidGuardianOpen();

    SetupDeviceNotifications();

    // sleep forever
    for (;;)
    {
        sleep_for(hours::max());
    }
}
