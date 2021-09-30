/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/audio_device/win/audio_device_utility_win.h"

#include "webrtc/system_wrappers/interface/critical_section_wrapper.h"
#include "webrtc/system_wrappers/interface/trace.h"

#include <windows.h>
#include <tchar.h>
#include <strsafe.h>

#define STRING_MAX_SIZE 256

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
typedef BOOL (WINAPI *PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);

namespace webrtc
{

// ============================================================================
//                            Construction & Destruction
// ============================================================================

// ----------------------------------------------------------------------------
//  AudioDeviceUtilityWindows() - ctor
// ----------------------------------------------------------------------------

AudioDeviceUtilityWindows::AudioDeviceUtilityWindows(const int32_t id) :
    _critSect(*CriticalSectionWrapper::CreateCriticalSection()),
    _id(id),
    _lastError(AudioDeviceModule::kAdmErrNone)
{
    WEBRTC_TRACE(kTraceMemory, kTraceAudioDevice, id, "%s created", __FUNCTION__);
}

// ----------------------------------------------------------------------------
//  AudioDeviceUtilityWindows() - dtor
// ----------------------------------------------------------------------------

AudioDeviceUtilityWindows::~AudioDeviceUtilityWindows()
{
    WEBRTC_TRACE(kTraceMemory, kTraceAudioDevice, _id, "%s destroyed", __FUNCTION__);
    {
        CriticalSectionScoped lock(&_critSect);

        // free stuff here...
    }

    delete &_critSect;
}

// ============================================================================
//                                     API
// ============================================================================

// ----------------------------------------------------------------------------
//  Init()
// ----------------------------------------------------------------------------

int32_t AudioDeviceUtilityWindows::Init()
{

    TCHAR szOS[STRING_MAX_SIZE];

    if (GetOSDisplayString(szOS))
    {
#ifdef _UNICODE
        char os[STRING_MAX_SIZE];
        if (WideCharToMultiByte(CP_UTF8, 0, szOS, -1, os, STRING_MAX_SIZE, NULL, NULL) == 0)
        {
            strncpy(os, "Could not get OS info", STRING_MAX_SIZE);
        }
        WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, _id, "  OS info: %s", os);
#else
        WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, _id, "  OS info: %s", szOS);
#endif
    }

    return 0;
}

// ============================================================================
//                                 Private Methods
// ============================================================================

BOOL AudioDeviceUtilityWindows::GetOSDisplayString(LPTSTR pszOS)
{
    OSVERSIONINFOEX osvi;
    SYSTEM_INFO si;
    PGNSI pGNSI;
    BOOL bOsVersionInfoEx;

    ZeroMemory(&si, sizeof(SYSTEM_INFO));
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));

    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

    // Retrieve information about the current operating system
    //
    bOsVersionInfoEx = GetVersionEx((OSVERSIONINFO *) &osvi);
    if (!bOsVersionInfoEx)
        return FALSE;

    // Parse our OS version string
    //
    if (VER_PLATFORM_WIN32_NT == osvi.dwPlatformId && osvi.dwMajorVersion > 4)
    {
        StringCchCopy(pszOS, STRING_MAX_SIZE, TEXT("Microsoft "));

        if (osvi.dwMajorVersion == 6)
        {
            if (osvi.dwMinorVersion == 1)
            {
                // Windows 7 or Server 2008 R2
                if (osvi.wProductType == VER_NT_WORKSTATION)
                    StringCchCat(pszOS, STRING_MAX_SIZE, TEXT("Windows 7 "));
                else
                    StringCchCat(pszOS, STRING_MAX_SIZE, TEXT("Windows Server 2008 R2 " ));
            }
        }

        // Include service pack (if any)
        //
        if (_tcslen(osvi.szCSDVersion) > 0)
        {
            StringCchCat(pszOS, STRING_MAX_SIZE, TEXT(" "));
            StringCchCat(pszOS, STRING_MAX_SIZE, osvi.szCSDVersion);
        }

        TCHAR buf[80];

        // Include build number
        //
        StringCchPrintf( buf, 80, TEXT(" (build %d)"), osvi.dwBuildNumber);
        StringCchCat(pszOS, STRING_MAX_SIZE, buf);

        // Call GetNativeSystemInfo if supported or GetSystemInfo otherwise
        //
        pGNSI = (PGNSI) GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetNativeSystemInfo");
        if (NULL != pGNSI)
            pGNSI(&si);
        else
            GetSystemInfo(&si);

        // Add 64-bit or 32-bit
        //
        if ((si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ||
            (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64))
            StringCchCat(pszOS, STRING_MAX_SIZE, TEXT( ", 64-bit" ));
        else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL )
            StringCchCat(pszOS, STRING_MAX_SIZE, TEXT(", 32-bit"));

        return TRUE;
    }
    else
    {
        return FALSE;
   }
}

}  // namespace webrtc
