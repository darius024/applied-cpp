#include <iostream>
#include <vector>
#include <deque>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// Policy classes — parameterise behaviour at compile time via template
// template parameters or regular type parameters.
//
// A "policy" is a type (or template) that encapsulates one aspect of
// behaviour. The host class delegates to its policy.
// Zero runtime overhead: the compiler inlines the policy's methods.
//
// Classic example: STL allocators, std::basic_string, Loki library.
// ─────────────────────────────────────────────────────────────────────


// ── Template template parameter ───────────────────────────────────────
//
// The Container policy itself is a template (it needs to be instantiated
// with T). We declare it with a variadic template parameter list so any
// standard container (vector, deque, ...) works — they take T plus a
// hidden Allocator parameter.

template<
    typename T,
    template<typename...> class Container = std::vector  // default: vector
>
class Stack
{
public:
    void push(const T& v) { data_.push_back(v); }

    T pop()
    {
        if (data_.empty()) throw std::underflow_error("stack is empty");
        T top = data_.back();
        data_.pop_back();
        return top;
    }

    bool        empty() const { return data_.empty(); }
    std::size_t size()  const { return data_.size(); }

private:
    Container<T> data_;   // instantiate the policy with our T
};


// ── Regular type parameter as policy ─────────────────────────────────
//
// Policies don't have to be templates themselves. Passing a plain type
// (whose static methods define the policy) is simpler and more common.

struct LoggingPolicy {
    static void on_push(int v) { std::cout << "[log] push " << v << "\n"; }
    static void on_pop (int v) { std::cout << "[log] pop  " << v << "\n"; }
};

struct SilentPolicy {
    static void on_push(int) {}
    static void on_pop (int) {}
};

template<typename T, typename LogPolicy = SilentPolicy>
class InstrumentedStack
{
public:
    void push(const T& v)
    {
        LogPolicy::on_push(v);
        data_.push_back(v);
    }

    T pop()
    {
        if (data_.empty()) throw std::underflow_error("empty");
        T top = data_.back();
        data_.pop_back();
        LogPolicy::on_pop(top);
        return top;
    }

    std::size_t size() const { return data_.size(); }

private:
    std::vector<T> data_;
};


int main()
{
    // ── Template template parameter ───────────────────────────────────
    Stack<int>                  vs;  // vector backend (default)
    Stack<int, std::deque>      ds;  // deque backend

    vs.push(1); vs.push(2); vs.push(3);
    ds.push(10); ds.push(20);

    std::cout << "vector stack pop: " << vs.pop() << "\n"; // 3
    std::cout << "deque  stack pop: " << ds.pop() << "\n"; // 20

    // ── Type policy ───────────────────────────────────────────────────
    std::cout << "\n";
    InstrumentedStack<int, LoggingPolicy> logged;
    logged.push(5);
    logged.push(10);
    logged.pop();

    std::cout << "\n";
    InstrumentedStack<int> silent; // SilentPolicy — no output
    silent.push(99);
    silent.pop();
    std::cout << "silent stack done, size=" << silent.size() << "\n";
}
