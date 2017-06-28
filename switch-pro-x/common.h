#pragma once

#include <string>
#include <algorithm>

#include <ViGEmUM.h>

namespace {
#ifdef UNICODE
    typedef std::wstring tstring;
    const auto tcout = &std::wcout;
    const auto tcerr = &std::wcerr;
#else
    typedef std::string tstring;
    const auto tcout = &std::cout;
    const auto tcerr = &std::cerr;
#endif

    constexpr LPTSTR WND_MODULE_NAME = TEXT("switch-pro-x.exe");
    constexpr uint16_t PRO_CONTROLLER_VID = 0x057E;
    constexpr uint16_t PRO_CONTROLLER_PID = 0x2009;

    inline void tstring_upper(tstring& str)
    {
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    }

    inline bool tstring_icompare(const tstring& lhs, const tstring& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        return std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](auto lhs, auto rhs) { return ::tolower(lhs) == ::tolower(rhs); });
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
