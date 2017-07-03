#pragma once

#include <string>
#include <iostream>
#include <algorithm>

#include <cctype>

#include <ViGEmUM.h>

namespace
{
#ifdef UNICODE
    typedef std::wstring tstring;
    const auto tcout = &std::wcout;
    const auto tcerr = &std::wcerr;
#else
    typedef std::string tstring;
    const auto tcout = &std::cout;
    const auto tcerr = &std::cerr;
#endif

    constexpr LPCTSTR WND_MODULE_NAME = TEXT("switch-pro-x.exe");
    constexpr uint16_t PRO_CONTROLLER_VID = 0x057E;
    constexpr uint16_t PRO_CONTROLLER_PID = 0x2009;

    inline bool tstring_icompare(const tstring& lhs, const tstring& rhs)
    {
        using std::equal;
        using std::tolower;

        if (lhs.size() != rhs.size())
        {
            return false;
        }

        return equal(lhs.begin(), lhs.end(), rhs.begin(), [](const auto& lhs, const auto& rhs) { return tolower(lhs) == tolower(rhs); });
    }

    inline bool operator==(const XUSB_REPORT& lhs, const XUSB_REPORT& rhs)
    {
        return
            lhs.wButtons == rhs.wButtons &&
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

    inline bool operator==(const VIGEM_TARGET& lhs, const VIGEM_TARGET& rhs)
    {
        return
            lhs.ProductId == rhs.ProductId &&
            lhs.VendorId == rhs.VendorId &&
            lhs.SerialNo == rhs.SerialNo;
    }

    inline bool operator!=(const VIGEM_TARGET& lhs, const VIGEM_TARGET& rhs)
    {
        return !(lhs == rhs);
    }
}
