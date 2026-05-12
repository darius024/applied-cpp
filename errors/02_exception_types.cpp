#include <iostream>
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Custom exception types.
//
// Inherit from std::exception or one of its subtypes so callers can
// catch by base class. Override what() to return a descriptive message.
//
// Two common patterns:
//   1. Store the message in a std::string member (simple, allocates).
//   2. Inherit std::runtime_error and forward the message to its ctor
//      (preferred — less boilerplate, what() already implemented).
// ─────────────────────────────────────────────────────────────────────

// ── Pattern 1: manual what() ──────────────────────────────────────────
class DatabaseError : public std::exception
{
public:
    explicit DatabaseError(std::string msg) : msg_(std::move(msg)) {}
    const char* what() const noexcept override { return msg_.c_str(); }

private:
    std::string msg_;
};

// ── Pattern 2: delegate to std::runtime_error ─────────────────────────
// Adds structured data (error code) on top of the message.
class NetworkError : public std::runtime_error
{
public:
    NetworkError(int code, const std::string& msg)
        : std::runtime_error("NetworkError " + std::to_string(code) + ": " + msg)
        , code_(code)
    {}

    int code() const noexcept { return code_; }

private:
    int code_;
};

// ── Hierarchy: specialise further ────────────────────────────────────
class TimeoutError : public NetworkError
{
public:
    explicit TimeoutError(const std::string& host)
        : NetworkError(408, "timeout connecting to " + host) {}
};

void connect(const std::string& host)
{
    if (host == "slow.example.com") throw TimeoutError(host);
    if (host.empty())               throw NetworkError(400, "empty host");
}

int main()
{
    // ── Custom exception with extra data ──────────────────────────────
    try {
        throw DatabaseError("connection pool exhausted");
    }
    catch (const DatabaseError& e) {
        std::cout << "db: " << e.what() << "\n";
    }

    // ── Hierarchy: catch at different levels ─────────────────────────
    for (auto host : {"slow.example.com", "", "ok.example.com"}) {
        try {
            connect(host);
            std::cout << "connected to " << host << "\n";
        }
        catch (const TimeoutError& e) {           // most-derived first
            std::cout << "timeout: " << e.what() << "\n";
        }
        catch (const NetworkError& e) {
            std::cout << "network [" << e.code() << "]: " << e.what() << "\n";
        }
    }
}
