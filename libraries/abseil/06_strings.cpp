#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_split.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_replace.h>
#include <absl/strings/numbers.h>
#include <absl/strings/ascii.h>
#include <iostream>
#include <vector>
#include <map>

// compile: g++ -std=c++17 06_strings.cpp -o 06_strings \
//          -labsl_strings -labsl_base -labsl_str_format_internal \
//          -labsl_throw_delegate

// ─────────────────────────────────────────────────────────────────────
// Abseil string utilities — fast, allocation-minimising helpers.
//
// StrCat    — concatenate any mix of strings, numbers, bools in one
//             allocation. Uses AlphaNum to convert arguments without
//             intermediate temporaries.
//
// StrAppend — like StrCat but appends to an existing string in-place.
//
// StrJoin   — join a container with a separator. Works with any range.
//             Custom formatters available for key-value pairs.
//
// StrSplit  — split a string into a container of string_view (no copy).
//             Supports string, char, or ByAnyChar delimiters.
//             SkipEmpty() / SkipWhitespace() filters.
//
// StrFormat — type-safe printf. Format string checked at compile time
//             (wrong types are a compile error). Writes to string or
//             FILE* or any sink.
//
// SimpleAtoi / SimpleAtof — fast string → number conversion.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── StrCat: single allocation, any types ──────────────────────────
    std::string s = absl::StrCat("host=", "localhost", " port=", 8080,
                                  " ratio=", 0.75);
    std::cout << s << "\n";

    // ── StrAppend: in-place, no reallocation if capacity allows ───────
    std::string log = "[INFO] ";
    absl::StrAppend(&log, "connected to ", "10.0.0.1", ":", 9000);
    std::cout << log << "\n";

    // ── StrJoin: any range, any separator ─────────────────────────────
    std::vector<int> nums{1, 2, 3, 4, 5};
    std::cout << absl::StrJoin(nums, ", ") << "\n"; // 1, 2, 3, 4, 5

    // Join map as key=value pairs
    std::map<std::string, int> m{{"a", 1}, {"b", 2}, {"c", 3}};
    std::cout << absl::StrJoin(m, " | ", absl::PairFormatter("=")) << "\n";
    // a=1 | b=2 | c=3

    // ── StrSplit: returns vector<string_view> — zero copy ─────────────
    std::string csv = "alpha,beta,,gamma,delta";
    std::vector<absl::string_view> parts =
        absl::StrSplit(csv, ',', absl::SkipEmpty());
    std::cout << "split: ";
    for (auto p : parts) std::cout << "[" << p << "] ";
    std::cout << "\n"; // [alpha] [beta] [gamma] [delta]

    // Split by any of multiple delimiters
    std::string path = "/usr/local/lib";
    std::vector<absl::string_view> segments =
        absl::StrSplit(path, absl::ByChar('/'), absl::SkipEmpty());
    std::cout << "path segments: ";
    for (auto seg : segments) std::cout << seg << " ";
    std::cout << "\n"; // usr local lib

    // ── StrFormat: type-safe printf ───────────────────────────────────
    // Checked at compile time — passing a string where %d expects int
    // is a compile error.
    std::string formatted = absl::StrFormat(
        "worker=%02d throughput=%.2f MB/s latency=%d us",
        7, 1234.56, 42);
    std::cout << formatted << "\n";

    // Write directly to stdout (no intermediate string)
    absl::PrintF("pi ≈ %.5f\n", 3.14159265358979);

    // ── StrReplaceAll ─────────────────────────────────────────────────
    std::string tmpl = "Hello, {name}! You have {count} messages.";
    std::string out = absl::StrReplaceAll(tmpl, {{"{name}", "Alice"},
                                                  {"{count}", "3"}});
    std::cout << out << "\n";

    // ── SimpleAtoi / SimpleAtof ───────────────────────────────────────
    int port;
    if (absl::SimpleAtoi("8080", &port))
        std::cout << "parsed port: " << port << "\n";

    double ratio;
    if (absl::SimpleAtod("0.75", &ratio))
        std::cout << "parsed ratio: " << ratio << "\n";

    // ── ASCII utilities ───────────────────────────────────────────────
    std::string mixed = "  Hello World  ";
    absl::StripAsciiWhitespace(&mixed);
    std::cout << "[" << mixed << "]\n"; // [Hello World]

    std::cout << absl::AsciiStrToUpper("hello") << "\n"; // HELLO
    std::cout << absl::AsciiStrToLower("WORLD") << "\n"; // world
}
