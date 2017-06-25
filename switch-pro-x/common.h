#pragma once

#include <string>
#include <algorithm>

#include <ViGEmUM.h>

namespace {
#ifdef UNICODE
    typedef std::wstring tstring;
#else
    typedef std::string tstring;
#endif

    constexpr uint16_t PRO_CONTROLLER_VID = 0x057E;
    constexpr uint16_t PRO_CONTROLLER_PID = 0x2009;

    inline void tstring_upper(tstring& str)
    {
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    }

    inline bool operator==(const XUSB_REPORT& lhs, const XUSB_REPORT& rhs)
    {
        return lhs.wButtons == rhs.wButtons &&
            lhs.bLeftTrigger == rhs.bLeftTrigger &&
            lhs.bRightTrigger == rhs.bRightTrigger &&
            lhs.sThumbLX == rhs.sThumbLX &&
            lhs.sThumbLY == rhs.sThumbLY &&
            lhs.sThumbRX == rhs.sThumbRX &&
            lhs.sThumbRY == rhs.sThumbRY;
    }

    inline bool operator!=(const XUSB_REPORT& lhs, const XUSB_REPORT& rhs)
    {
        return !(lhs == rhs);
    }
}
