#include <atomic>
#include <iostream>
#include <thread>

#include <libusb-1.0/libusb.h>

#include "common.h"
#include "switch-pro-x.h"

namespace {
    int HotplugCallback(struct libusb_context *ctx, struct libusb_device *dev, libusb_hotplug_event event, void *user_data)
    {
        switch (event)
        {
        case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED:
        {
            AddController(dev);
            break;
        }
        case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT:
        {
            RemoveController(dev);
            break;
        }
        }

        return 0;
    }
}

void SetupDeviceNotifications()
{
    libusb_init(nullptr);

    struct libusb_device **devices;

    libusb_hotplug_callback_handle callback;
    int hotplug_ret = libusb_hotplug_register_callback(
        nullptr,
        static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_MATCH_ANY),
        LIBUSB_HOTPLUG_ENUMERATE,
        PRO_CONTROLLER_VID, PRO_CONTROLLER_PID,
        LIBUSB_HOTPLUG_MATCH_ANY,
        reinterpret_cast<libusb_hotplug_callback_fn>(HotplugCallback),
        nullptr,
        &callback);

    if (hotplug_ret != 0)
    {
        std::cerr << "cannot register hotplug callback (" << hotplug_ret << "), hotplugging not enabled" << std::endl;
    }

    // pump events until shutdown
    for (;;)
    {
        libusb_handle_events_completed(nullptr, nullptr);
    }
}
