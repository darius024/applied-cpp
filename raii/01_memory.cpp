#include <iostream>
#include <memory>
#include <stdexcept>

// ─────────────────────────────────────────────
// Problem: raw pointer leaks when an exception is thrown before delete[].
// ─────────────────────────────────────────────

void raw_leak()
{
    int* p = new int[100];
    throw std::runtime_error("oops"); // p is never deleted → leak
    delete[] p;
}

// ─────────────────────────────────────────────
// Solution A: hand-rolled RAII wrapper.
// Illustrates the pattern. In real code, prefer unique_ptr (see below).
// ─────────────────────────────────────────────

class Buffer
{
public:
    explicit Buffer(std::size_t n)
        : data_(new int[n]), size_(n) {}

    // Destructor always runs → no leak, even on exception.
    ~Buffer() { delete[] data_; }

    // Copy is deleted: two Buffers owning the same pointer → double-free.
    Buffer(const Buffer&)            = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Move: transfer ownership; null out source so its destructor is a no-op.
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    int*        data() const { return data_; }
    std::size_t size() const { return size_; }

private:
    int*        data_;
    std::size_t size_;
};

void raii_safe()
{
    Buffer buf(100);
    throw std::runtime_error("oops"); // destructor runs here → no leak
}

// ─────────────────────────────────────────────
// Solution B: std::unique_ptr — same guarantee, zero boilerplate.
// This is the right answer for production code.
// ─────────────────────────────────────────────

void preferred()
{
    auto p = std::make_unique<int[]>(100);
    throw std::runtime_error("oops"); // cleaned up automatically
}

int main()
{
    try { raii_safe(); }  catch (...) { std::cout << "raii_safe:  no leak\n"; }
    try { preferred(); }  catch (...) { std::cout << "preferred:  no leak\n"; }
    // raw_leak() is intentionally not called — it leaks.
}
