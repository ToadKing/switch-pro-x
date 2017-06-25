#pragma once

#include <Windows.h>

#include <libusb-1.0/libusb.h>

#include "common.h"

void AddController(libusb_device *device);
void RemoveController(libusb_device *device);
