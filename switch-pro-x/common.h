#pragma once

#include <string>
#include <algorithm>

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
}
