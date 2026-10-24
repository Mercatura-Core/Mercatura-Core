// Copyright (c) 2026 The Mercatura Core developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_QT_NOTIFICATIONTIMING_H
#define BITCOIN_QT_NOTIFICATIONTIMING_H

#include <QDebug>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <string_view>

namespace GUIUtil {
/** Opt-in callback diagnostics, aggregated per thread at most once a second. */
class NotificationTiming
{
    using Clock = std::chrono::steady_clock;
    const char* m_name;
    const bool m_enabled{Enabled()};
    Clock::time_point m_start{m_enabled ? Clock::now() : Clock::time_point{}};

public:
    static bool Enabled()
    {
        static const bool enabled{qEnvironmentVariableIsSet("MERCATURA_QT_TIMING")};
        return enabled;
    }
    explicit NotificationTiming(const char* name) : m_name{name} {}
    ~NotificationTiming()
    {
        if (m_enabled) Record(m_name, Clock::now() - m_start);
    }
    static void Record(const char* name, Clock::duration duration)
    {
        if (!Enabled()) return;
        struct Metrics {
            uint64_t calls{0};
            int64_t total_us{0}, max_us{0};
            Clock::time_point last_log{Clock::now()};
        };
        thread_local std::map<std::string_view, Metrics> metrics;
        auto& value{metrics[name]};
        const auto us{std::chrono::duration_cast<std::chrono::microseconds>(duration).count()};
        ++value.calls;
        value.total_us += us;
        value.max_us = std::max<int64_t>(value.max_us, us);
        const auto now{Clock::now()};
        if (now - value.last_log >= std::chrono::seconds{1} || (value.calls == 1 && us >= 50'000)) {
            qInfo().nospace() << "QtNotifyTiming name=" << name << " calls=" << qulonglong{value.calls}
                             << " total_us=" << value.total_us << " max_us=" << value.max_us;
            value.last_log = now;
        }
    }
};
} // namespace GUIUtil

#endif // BITCOIN_QT_NOTIFICATIONTIMING_H
