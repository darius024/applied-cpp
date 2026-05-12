#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// IILE — Immediately Invoked Lambda Expression.
//
//   const T x = [&]{ /* multi-step logic */ return value; }();
//                                                           ^^
//                                              Call the lambda right away.
//
// Enables initialising a `const` variable with logic that requires
// multiple statements, without a named helper function.
//
// Benefits:
//   - The variable is const — communicates "this value never changes".
//   - The logic is scoped to the initialisation — no temp variables leak.
//   - Cleaner than a named helper when the logic is used only once.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Basic: const variable from multi-step logic ───────────────────
    const int config_value = []{
        int base = 10;
        base *= 2;
        base += 5;
        return base;
    }();
    std::cout << "config_value=" << config_value << "\n";

    // ── Conditional initialisation ────────────────────────────────────
    // Without IILE: you'd need a non-const + if/else, or a ternary chain.
    const std::string env = "production"; // imagine this comes from the OS
    const int timeout_ms = [&]{
        if (env == "production") return 5000;
        if (env == "staging")    return 1000;
        return 200; // default / local
    }();
    std::cout << "timeout=" << timeout_ms << "ms\n";

    // ── Complex container initialisation ─────────────────────────────
    // Initialising a const map that needs computed values.
    const std::map<std::string, int> error_codes = []{
        std::map<std::string, int> m;
        m["ok"]           = 200;
        m["not_found"]    = 404;
        m["server_error"] = 500;
        // could do complex computation here before returning
        return m;
    }();
    std::cout << "error_codes[\"not_found\"]=" << error_codes.at("not_found") << "\n";

    // ── Exception handling inside an initialiser ──────────────────────
    // You can't use try/catch in a plain initialiser expression.
    // An IILE lets you wrap parsing or throwing logic cleanly.
    const int port = []{
        const char* env_port = std::getenv("APP_PORT");
        if (!env_port) return 8080; // default
        try {
            int p = std::stoi(env_port);
            if (p < 1 || p > 65535) throw std::range_error("port out of range");
            return p;
        } catch (...) {
            return 8080;
        }
    }();
    std::cout << "port=" << port << "\n";

    // ── Lookup table with computed entries ────────────────────────────
    const std::vector<int> squares = []{
        std::vector<int> v;
        v.reserve(10);
        for (int i = 0; i < 10; ++i) v.push_back(i * i);
        return v;
    }();
    std::cout << "squares[7]=" << squares[7] << "\n"; // 49
}
