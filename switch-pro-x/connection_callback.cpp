#include <atomic>
#include <iostream>
#include <thread>

#include <libusb-1.0/libusb.h>

#include "common.h"
#include "switch-pro-x.h"

namespace {
    volatile int quitting;

    int HotplugCallback(struct libusb_context *ctx, struct libusb_device *dev, libusb_hotplug_event event, void *user_data)
    {
        (void)ctx;
        (void)user_data;
        if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)
        {
            AddController(dev);
        }
        else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)
        {
            RemoveController(dev);
        }

        return 0;
    }
}

void SetupDeviceNotifications()
{
    quitting = 0;

    libusb_init(nullptr);

    struct libusb_device **devices;

    ssize_t count = libusb_get_device_list(nullptr, &devices);

    for (int i = 0; i < count; i++)
    {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(devices[i], &desc);
        if (desc.idVendor == PRO_CONTROLLER_VID && desc.idProduct == PRO_CONTROLLER_PID)
        {
            AddController(devices[i]);
        }
    }

    if (count > 0)
    {
        libusb_free_device_list(devices, 1);
    }

    libusb_hotplug_callback_handle callback;
    int hotplug_ret = libusb_hotplug_register_callback(
        nullptr,
        static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_MATCH_ANY),
        static_cast<libusb_hotplug_flag>(0),
        0x057e, 0x0337,
        LIBUSB_HOTPLUG_MATCH_ANY,
        reinterpret_cast<libusb_hotplug_callback_fn>(HotplugCallback),
        nullptr,
        &callback);

    if (hotplug_ret != 0)
    {
        std::cerr << "cannot register hotplug callback, hotplugging not enabled" << std::endl;
    }

    // pump events until shutdown & all helper threads finish cleaning up
    while (!quitting)
    {
        libusb_handle_events_completed(nullptr, const_cast<int *>(&quitting));
    }

    libusb_exit(nullptr);
}
