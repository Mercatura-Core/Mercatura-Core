// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_TEST_UTIL_SPARSE_H
#define BITCOIN_TEST_UTIL_SPARSE_H

#include <compat/compat.h>
#include <cstdio>

#ifdef WIN32
#include <io.h>
#include <windows.h>
#include <winioctl.h>
#endif

/** Mark disposable large-offset test files sparse before extending them.
 * Windows requires an explicit request; POSIX seek/truncate creates holes. */
inline bool MakeTestFileSparse(FILE* file)
{
#ifdef WIN32
    DWORD returned{0};
    return DeviceIoControl(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file))),
                           FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;
#else
    (void)file;
    return true;
#endif
}
#endif // BITCOIN_TEST_UTIL_SPARSE_H
