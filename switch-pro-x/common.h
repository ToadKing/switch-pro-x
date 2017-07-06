#pragma once

#include <atomic>
#include <string>
#include <iostream>
#include <algorithm>

#ifdef UNICODE
#include <cwctype>
#else
#include <cctype>
#endif

#include <cstdint>

#include <ViGEmUM.h>

namespace
{
#ifdef UNICODE
    using tstring = std::wstring;
    constexpr auto& ttolower = std::towlower;
    auto& tcout = std::wcout;
    auto& tcerr = std::wcerr;
#else
    using tstring = std::string;
    constexpr auto& ttolower = std::tolower;
    auto& tcout = std::cout;
    auto& tcerr = std::cerr;
#endif

    constexpr LPCTSTR WND_MODULE_NAME = TEXT("switch-pro-x.exe");
    constexpr std::uint16_t PRO_CONTROLLER_VID = 0x057E;
    constexpr std::uint16_t PRO_CONTROLLER_PID = 0x2009;

    inline bool tstring_icompare(const tstring& lhs, const tstring& rhs)
    {
        using std::equal;

        if (lhs.size() != rhs.size())
        {
            return false;
        }

        return equal(lhs.begin(), lhs.end(), rhs.begin(), [](const auto& lhs, const auto& rhs) { return ttolower(lhs) == ttolower(rhs); });
    }

    inline tstring::size_type tstring_ifind(const tstring& lhs, const tstring& rhs)
    {
        using std::search;

        auto it = search(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const auto& lhs, const auto& rhs) { return ttolower(lhs) == ttolower(rhs); });

        if (it == lhs.end())
        {
            return tstring::npos;
        }
        else
        {
            return it - lhs.begin();
        }
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

    class spinlock
    {
    public:
        void lock()
        {
            using std::memory_order_acquire;

            while (lck.test_and_set(memory_order_acquire));
        }

        void unlock()
        {
            using std::memory_order_release;

            lck.clear(memory_order_release);
        }

        bool try_lock()
        {
            using std::memory_order_acquire;

            return !lck.test_and_set(memory_order_acquire);
        }

    private:
        std::atomic_flag lck = ATOMIC_FLAG_INIT;
    };
}
