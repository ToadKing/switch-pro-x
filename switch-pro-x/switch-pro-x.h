#pragma once

#include <Windows.h>

#include "common.h"

void AddController(const tstring &path);
void RemoveController(const tstring &path);
VOID CALLBACK XUSBCallback(VIGEM_TARGET target, UCHAR large_motor, UCHAR small_motor, UCHAR led_number);
