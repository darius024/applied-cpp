#include <iostream>
#include <functional>
#include <vector>
#include <chrono>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Three ways to pass a callable:
//
//   1. Template parameter   — zero overhead, inlined, but caller type is baked in
//   2. Function pointer     — no captures allowed, minimal overhead, C-compatible
//   3. std::function        — type-erased, heap allocs for large captures,
//                             ~10-50x slower on hot paths; fine for I/O-bound code
//
// Rule: prefer template parameter for performance-sensitive code.
//       Use std::function when you need to STORE a callable whose type
//       is unknown at the point of storage (e.g. event queues, callbacks
//       registered at runtime).
// ─────────────────────────────────────────────────────────────────────


// ── 1. Template parameter ─────────────────────────────────────────────
// The lambda type is deduced; the call is resolved at compile time → inlined.
// Downside: every unique lambda type produces a new template instantiation.

template<typename F>
void apply_template(const std::vector<int>& v, F func)
{
    for (int x : v) func(x);
}

// ── 2. Function pointer ───────────────────────────────────────────────
// Only works for lambdas with an EMPTY capture list.
// A captureless lambda is implicitly convertible to a function pointer.

void apply_fp(const std::vector<int>& v, void (*func)(int))
{
    for (int x : v) func(x);
}

// ── 3. std::function ──────────────────────────────────────────────────
// Accepts ANY callable: lambda, function pointer, functor, bind expression.
// Heap-allocates the closure internals when the captured state is larger
// than the small-buffer optimisation (typically ~16–32 bytes).

void apply_stdfn(const std::vector<int>& v, std::function<void(int)> func)
{
    for (int x : v) func(x);
}

// ── Storing callables — when std::function is the right choice ────────
// You can't store a template parameter (its type is local to the function).
// std::function gives the callable a stable, erasable type for storage.

struct EventBus {
    using Handler = std::function<void(const std::string&)>;

    void subscribe(Handler h)      { handlers_.push_back(std::move(h)); }
    void publish(const std::string& event) {
        for (auto& h : handlers_) h(event);
    }

private:
    std::vector<Handler> handlers_; // couldn't store templated lambdas here
};

// ── Lightweight alternative: template + type erasure only at boundaries ──
// Keep hot inner loops as templates; erase to std::function only at the
// API boundary where the caller type is truly unknown.

template<typename F>
void register_hot_handler(EventBus& bus, F&& f)
{
    // f is inlined here; only erased once when handed to the bus
    bus.subscribe(std::forward<F>(f));
}


int main()
{
    std::vector<int> v = {1, 2, 3, 4, 5};

    // ── Template param: captures work, fully inlined ──────────────────
    int sum = 0;
    apply_template(v, [&sum](int x){ sum += x; });
    std::cout << "template sum=" << sum << "\n";

    // ── Function pointer: captureless only ───────────────────────────
    apply_fp(v, [](int x){ std::cout << x << " "; });
    std::cout << "\n";

    // ── std::function: captures work, type erased ─────────────────────
    std::string log;
    apply_stdfn(v, [&log](int x){ log += std::to_string(x) + " "; });
    std::cout << "stdfn log=" << log << "\n";

    // ── EventBus: std::function for stored callbacks ───────────────────
    EventBus bus;
    int count = 0;
    register_hot_handler(bus, [&count](const std::string& e){
        ++count;
        std::cout << "handler1: " << e << "\n";
    });
    bus.subscribe([](const std::string& e){
        std::cout << "handler2: " << e << "\n";
    });

    bus.publish("click");
    bus.publish("hover");
    std::cout << "handler1 fired " << count << " times\n";
}
