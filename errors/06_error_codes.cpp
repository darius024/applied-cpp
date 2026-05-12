#include <iostream>
#include <system_error>
#include <fstream>
#include <cerrno>

// ─────────────────────────────────────────────────────────────────────
// std::error_code — low-overhead, value-based error reporting.
// No heap allocation, no exceptions, C-interop friendly.
//
// Three types:
//   error_code      — a specific OS/library error (int + category).
//   error_condition — a portable, abstract condition (e.g. "no such file").
//   error_category  — domain that owns the error numbers and their strings.
//
// std::errc: portable enum mapping common POSIX errors to error_condition.
// Comparison: error_code == errc::no_such_file_or_directory works across
// platforms via the generic_category mapping.
// ─────────────────────────────────────────────────────────────────────

// ── Returning error_code from a function ─────────────────────────────
// The out-parameter style: caller passes a reference and checks it.
// Return value carries the real result; error_code signals failure.
std::size_t file_size(const std::string& path, std::error_code& ec)
{
    ec.clear(); // start clean — caller may reuse the same variable
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return 0;
    }
    return static_cast<std::size_t>(f.tellg());
}

// ── Custom error category ─────────────────────────────────────────────
// Use this to give your library its own error namespace so codes don't
// collide with system or other library codes.

enum class AppError { ok = 0, bad_input = 1, quota_exceeded = 2 };

struct AppCategory : std::error_category
{
    const char* name() const noexcept override { return "app"; }

    std::string message(int ev) const override
    {
        switch (static_cast<AppError>(ev)) {
            case AppError::ok:             return "ok";
            case AppError::bad_input:      return "bad input";
            case AppError::quota_exceeded: return "quota exceeded";
            default:                       return "unknown";
        }
    }
};

const AppCategory& app_category()
{
    static AppCategory cat;
    return cat;
}

std::error_code make_error_code(AppError e)
{
    return {static_cast<int>(e), app_category()};
}

// Allow AppError to work directly with error_code (is_error_code_enum)
template<>
struct std::is_error_code_enum<AppError> : std::true_type {};

int main()
{
    // ── file_size: error_code out-parameter ───────────────────────────
    std::error_code ec;
    auto sz = file_size("nonexistent.txt", ec);
    if (ec) {
        std::cout << "error: " << ec.message() << "\n";
        // Portable comparison via errc:
        std::cout << "is no_such_file: "
                  << (ec == std::errc::no_such_file_or_directory) << "\n";
    }

    // ── Custom category ───────────────────────────────────────────────
    std::error_code app_ec = AppError::quota_exceeded;
    std::cout << "app error [" << app_ec.value() << "]: "
              << app_ec.message() << "\n";
    std::cout << "category: " << app_ec.category().name() << "\n";

    // ── system_error: exception wrapping an error_code ────────────────
    // Use when you DO want to throw but still carry a portable error_code.
    try {
        throw std::system_error(AppError::bad_input, "while parsing config");
    }
    catch (const std::system_error& e) {
        std::cout << "system_error: " << e.what() << "\n"; // includes message
        std::cout << "code: "         << e.code().message() << "\n";
    }
}
