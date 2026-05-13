#include <absl/time/time.h>
#include <absl/time/clock.h>
#include <absl/time/civil_time.h>
#include <iostream>
#include <thread>

// compile: g++ -std=c++17 07_time.cpp -o 07_time \
//          -labsl_time -labsl_base -labsl_strings -labsl_time_zone

// ─────────────────────────────────────────────────────────────────────
// absl::Time / absl::Duration
//
// Duration: a signed, nanosecond-precision time interval.
//   Created with: absl::Nanoseconds, Microseconds, Milliseconds,
//                 Seconds, Minutes, Hours.
//   Supports arithmetic (+, -, *, /), comparisons, and conversion
//   to/from doubles and integer counts.
//
// Time: an absolute point on the timeline (no timezone).
//   absl::Now()              — current wall-clock time.
//   absl::FromUnixNanos(n)   — construct from Unix timestamp.
//   absl::ToUnixNanos(t)     — convert to Unix nanoseconds.
//   t1 - t2 → Duration       — difference between two Times.
//
// TimeZone: maps absolute Time ↔ civil time.
//   absl::LoadTimeZone("America/New_York", &tz)
//   absl::UTCTimeZone()      — always UTC, no DST.
//   absl::LocalTimeZone()    — system local timezone.
//
// CivilTime: calendar representations (CivilSecond, CivilDay, etc.)
//   Used for human-readable formatting and calendar arithmetic.
//
// HPC relevance: precise timestamping, latency measurement, and
// deadline management without the messiness of timespec/timeval.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Duration arithmetic ───────────────────────────────────────────
    absl::Duration d1 = absl::Milliseconds(500);
    absl::Duration d2 = absl::Microseconds(250);
    absl::Duration total = d1 + d2;

    std::cout << "500ms + 250us = "
              << absl::ToInt64Microseconds(total) << " us\n"; // 500250

    std::cout << "2 * 500ms = "
              << absl::ToDoubleSeconds(2 * d1) << " s\n"; // 1.0

    // ── Time: wall clock and arithmetic ───────────────────────────────
    absl::Time t0 = absl::Now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    absl::Time t1 = absl::Now();

    absl::Duration elapsed = t1 - t0;
    std::cout << "sleep elapsed: "
              << absl::ToInt64Microseconds(elapsed) << " us\n";

    // ── Deadline / timeout pattern ────────────────────────────────────
    absl::Time deadline = absl::Now() + absl::Seconds(5);
    bool expired = absl::Now() > deadline;
    std::cout << "deadline expired: " << std::boolalpha << expired << "\n"; // false

    // ── Unix timestamp conversion ──────────────────────────────────────
    absl::Time epoch = absl::FromUnixSeconds(0);
    std::cout << "unix epoch ns: " << absl::ToUnixNanos(epoch) << "\n"; // 0

    absl::Time now = absl::Now();
    std::cout << "now unix s: " << absl::ToUnixSeconds(now) << "\n";

    // ── Formatting ────────────────────────────────────────────────────
    absl::TimeZone utc = absl::UTCTimeZone();
    std::string formatted = absl::FormatTime("%Y-%m-%d %H:%M:%S UTC", now, utc);
    std::cout << "now: " << formatted << "\n";

    // RFC 3339 format
    std::cout << "RFC3339: " << absl::FormatTime(absl::RFC3339_full, now, utc) << "\n";

    // ── Parsing ───────────────────────────────────────────────────────
    absl::Time parsed;
    std::string err;
    if (absl::ParseTime("%Y-%m-%d", "2025-01-15", utc, &parsed, &err))
        std::cout << "parsed unix s: " << absl::ToUnixSeconds(parsed) << "\n";

    // ── Civil time: calendar arithmetic ───────────────────────────────
    absl::CivilSecond cs = absl::ToCivilSecond(now, utc);
    std::cout << "year=" << cs.year()
              << " month=" << cs.month()
              << " day=" << cs.day()   << "\n";

    // Advance by 1 day using civil arithmetic (handles month/year rollover)
    absl::CivilDay tomorrow = absl::CivilDay(cs) + 1;
    std::cout << "tomorrow: " << tomorrow.year() << "-"
              << tomorrow.month() << "-" << tomorrow.day() << "\n";
}
