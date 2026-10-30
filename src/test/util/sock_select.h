// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_TEST_UTIL_SOCK_SELECT_H
#define BITCOIN_TEST_UTIL_SOCK_SELECT_H

// Preincluded only by sock_select_tests; ordinary production targets retain
// their platform backend. compat.h's guard prevents it from re-enabling poll.
#include <compat/compat.h>
#undef USE_POLL

#endif // BITCOIN_TEST_UTIL_SOCK_SELECT_H
