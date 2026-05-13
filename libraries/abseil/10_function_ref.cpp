#include <absl/functional/function_ref.h>
#include <absl/functional/any_invocable.h>
#include <iostream>
#include <vector>
#include <memory>
#include <numeric>

// compile: g++ -std=c++17 10_function_ref.cpp -o 10_function_ref \
//          -labsl_base -labsl_strings -labsl_throw_delegate

// ─────────────────────────────────────────────────────────────────────
// absl::FunctionRef<Sig>
//
// A non-owning reference to any callable with signature Sig.
// Stores only a void* (to the callable) and a function pointer (thunk).
// Zero heap allocation. Like absl::Span, it does not own the callable.
//
// Use for function PARAMETERS when:
//   - The callable will not outlive the function call.
//   - You want zero overhead (no std::function type erasure or heap).
//   - You need to accept lambdas, function pointers, and functors
//     without templates.
//
// NOT safe to store beyond the call site (non-owning).
//
// ─────────────────────────────────────────────────────────────────────
// absl::AnyInvocable<Sig>
//
// An owning, move-only callable wrapper. Like std::function but:
//   - Move-only: supports lambdas that capture unique_ptr, etc.
//   - No small-buffer-overflow copies (std::function may copy on assign).
//   - Lower overhead: no virtual dispatch, better inlining potential.
//
// Use when you need to STORE or TRANSFER a callable (e.g. task queue).
// ─────────────────────────────────────────────────────────────────────

// ── Functions taking FunctionRef — no templates required ──────────────
int apply_to_range(std::vector<int>& v, absl::FunctionRef<int(int)> f)
{
    int sum = 0;
    for (int& x : v) { x = f(x); sum += x; }
    return sum;
}

void for_each(absl::Span<const int> data, absl::FunctionRef<void(int)> fn)
{
    for (int x : data) fn(x);
}

// ── Task queue using AnyInvocable ─────────────────────────────────────
class TaskQueue {
public:
    void push(absl::AnyInvocable<void()> task) {
        tasks_.push_back(std::move(task));
    }
    void run_all() {
        for (auto& t : tasks_) t();
        tasks_.clear();
    }
private:
    std::vector<absl::AnyInvocable<void()>> tasks_;
};

int main()
{
    // ── FunctionRef with lambda ───────────────────────────────────────
    std::vector<int> v{1, 2, 3, 4, 5};
    int sum = apply_to_range(v, [](int x) { return x * x; });
    std::cout << "sum of squares: " << sum << "\n"; // 55

    // ── FunctionRef with free function ────────────────────────────────
    auto print = [](int x) { std::cout << x << " "; };
    for_each(v, print);
    std::cout << "\n"; // 1 4 9 16 25

    // ── FunctionRef with stateful lambda (no copy of lambda) ──────────
    int multiplier = 3;
    apply_to_range(v, [&multiplier](int x) { return x * multiplier; });
    // FunctionRef just holds a pointer to the lambda — no allocation

    // ── AnyInvocable: move-only lambda capturing unique_ptr ───────────
    TaskQueue tq;

    // This lambda captures a unique_ptr — impossible with std::function
    auto resource = std::make_unique<int>(42);
    tq.push([r = std::move(resource)] {
        std::cout << "task with unique_ptr resource: " << *r << "\n";
    });

    tq.push([] { std::cout << "simple task\n"; });

    int counter = 0;
    tq.push([&counter] { counter += 10; });

    tq.run_all();
    std::cout << "counter after tasks: " << counter << "\n"; // 10

    // ── AnyInvocable as a stored callback ─────────────────────────────
    absl::AnyInvocable<int(int, int)> op = [](int a, int b) { return a + b; };
    std::cout << "stored op: " << op(3, 4) << "\n"; // 7

    // Reassign with a different callable
    op = [](int a, int b) { return a * b; };
    std::cout << "reassigned op: " << op(3, 4) << "\n"; // 12
}
