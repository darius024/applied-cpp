#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <iostream>
#include <string>
#include <cmath>

// compile: g++ -std=c++17 05_status.cpp -o 05_status \
//          -labsl_status -labsl_strings -labsl_base -labsl_cord \
//          -labsl_throw_delegate

// ─────────────────────────────────────────────────────────────────────
// absl::Status / absl::StatusOr<T>
//
// absl::Status:
//   Carries an absl::StatusCode enum (OK, NOT_FOUND, INVALID_ARGUMENT,
//   INTERNAL, UNAVAILABLE, etc.) plus a message string.
//   absl::OkStatus() is the zero-cost success sentinel.
//   Non-OK statuses allocate a small payload for the message.
//
// absl::StatusOr<T>:
//   Either a Status (error) or a T (success). The canonical return
//   type for operations that can fail.
//   - .ok()      — true if holds a value.
//   - .value()   — returns T&; throws/aborts if error.
//   - .status()  — returns the Status.
//   - *or_val    — dereference operator.
//
// Propagation macros (require the function to return Status/StatusOr):
//   ABSL_RETURN_IF_ERROR(expr)       — return the status if not OK.
//   ABSL_ASSIGN_OR_RETURN(var, expr) — assign or return the error.
//
// HPC relevance: exception-free error propagation with near-zero
// overhead on the success path. Used throughout gRPC and TensorFlow.
// ─────────────────────────────────────────────────────────────────────

// ── Functions returning StatusOr ─────────────────────────────────────
absl::StatusOr<double> safe_sqrt(double x)
{
    if (x < 0)
        return absl::InvalidArgumentError("sqrt of negative number");
    return std::sqrt(x);
}

absl::StatusOr<double> safe_divide(double a, double b)
{
    if (b == 0.0)
        return absl::InvalidArgumentError("division by zero");
    return a / b;
}

// ── Function chaining with ABSL_ASSIGN_OR_RETURN ─────────────────────
absl::StatusOr<double> compute(double a, double b)
{
    ABSL_ASSIGN_OR_RETURN(double q, safe_divide(a, b));
    ABSL_ASSIGN_OR_RETURN(double r, safe_sqrt(q));
    return r;
}

// ── Status-only return (no value) ─────────────────────────────────────
absl::Status validate_port(int port)
{
    if (port < 1 || port > 65535)
        return absl::OutOfRangeError(
            absl::StrCat("invalid port: ", port));
    return absl::OkStatus();
}

int main()
{
    // ── Successful path ───────────────────────────────────────────────
    auto r1 = safe_sqrt(16.0);
    if (r1.ok())
        std::cout << "sqrt(16) = " << *r1 << "\n"; // 4

    // ── Error path ────────────────────────────────────────────────────
    auto r2 = safe_sqrt(-1.0);
    if (!r2.ok())
        std::cout << "error: " << r2.status().message() << "\n";

    // ── Status code inspection ────────────────────────────────────────
    std::cout << "code: " << absl::StatusCodeToString(r2.status().code())
              << "\n"; // INVALID_ARGUMENT

    // ── Chained computation ───────────────────────────────────────────
    auto r3 = compute(100.0, 4.0); // sqrt(100/4) = sqrt(25) = 5
    std::cout << "compute(100,4) = " << r3.value() << "\n";

    auto r4 = compute(100.0, 0.0); // divide by zero
    std::cout << "compute(100,0): " << r4.status().message() << "\n";

    // ── Status-only ───────────────────────────────────────────────────
    absl::Status s = validate_port(8080);
    std::cout << "port 8080: " << (s.ok() ? "valid" : s.message()) << "\n";

    s = validate_port(99999);
    std::cout << "port 99999: " << s.message() << "\n";

    // ── value_or: default on error ─────────────────────────────────────
    double val = safe_sqrt(-4.0).value_or(-1.0);
    std::cout << "value_or: " << val << "\n"; // -1
}
