// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

// Exercise the actual select() implementation on Linux as well as on native
// select platforms. Only this test translation unit changes backend selection.
#include <compat/compat.h>
#undef USE_POLL
#include <util/sock.cpp>
