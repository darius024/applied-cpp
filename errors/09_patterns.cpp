#include <iostream>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// When to use which error mechanism.
//
// There is no single right answer — each mechanism fits different
// call sites, severities, and performance budgets.
//
// Guiding questions:
//   1. Is this a programming error or a runtime condition?
//   2. Is the failure expected in normal operation?
//   3. Does the caller need to inspect the error type or value?
//   4. Is this in a hot loop or a library with no-exception callers?
// ─────────────────────────────────────────────────────────────────────

// ── 1. Exceptions: truly exceptional, I/O, cross-layer ────────────────
// Use when failure is unexpected and recovery is at a high call level.
// Exceptions propagate automatically — no need for every layer to check.

struct Config { std::string host; int port; };

Config load_config(const std::string& path)
{
    if (path.empty()) throw std::invalid_argument("path is empty");
    // Simulate: file not found
    throw std::runtime_error("config file not found: " + path);
}

// ── 2. optional: "nothing found" is normal ────────────────────────────
// Use when absence of a value is a valid, expected outcome.
// Avoids magic sentinels (-1, nullptr, "").

std::optional<std::string> find_user(const std::vector<std::string>& users,
                                     const std::string& name)
{
    for (auto& u : users) if (u == name) return u;
    return std::nullopt;
}

// ── 3. error_code: library API, no-exception policy, C interop ────────
// Use when the caller is expected to handle every error inline.
// Zero allocation, works in -fno-exceptions environments.

int connect(const std::string& host, std::error_code& ec)
{
    ec.clear();
    if (host.empty()) { ec = std::make_error_code(std::errc::invalid_argument); return -1; }
    if (host == "down.example.com") { ec = std::make_error_code(std::errc::connection_refused); return -1; }
    return 42; // fake fd
}

// ── 4. expected: functions with typed errors, pipeline composition ─────
// Use in new C++23 code where you want both a result and a structured
// error without the overhead or semantics of exceptions.
// (See 08_expected.cpp for full coverage.)


// ── Decision table in comments ────────────────────────────────────────
//
//  Situation                              | Mechanism
//  ──────────────────────────────────────────────────────────────────
//  Bug / precondition violated            | assert / throw logic_error
//  I/O failure, OOM, unrecoverable        | exception
//  "Not found" (search, lookup)           | optional
//  Expected failure, library boundary     | error_code / expected
//  Multiple error types, typed inspection | variant / expected
//  C interop, no exceptions               | error_code + return value
//  Hot loop, latency-sensitive            | error_code / expected


int main()
{
    // ── Exception: catch at the top level ────────────────────────────
    try { load_config("app.toml"); }
    catch (const std::runtime_error& e) {
        std::cout << "config failed: " << e.what() << "\n";
    }

    // ── optional: no sentinel needed ─────────────────────────────────
    std::vector<std::string> users = {"alice", "bob"};
    auto user = find_user(users, "charlie");
    std::cout << "find charlie: " << user.value_or("(not found)") << "\n";

    // ── error_code: caller handles inline ─────────────────────────────
    std::error_code ec;
    int fd = connect("down.example.com", ec);
    if (ec) std::cout << "connect failed: " << ec.message() << "\n";

    fd = connect("ok.example.com", ec);
    if (!ec) std::cout << "connected, fd=" << fd << "\n";
}
