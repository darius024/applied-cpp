#include <iostream>
#include <string>
#include <stdexcept>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────
// Exception safety guarantees.
//
//   No-throw  — never throws; always succeeds. (noexcept)
//   Strong    — if it throws, observable state is unchanged ("commit or rollback").
//   Basic     — if it throws, invariants hold but state may differ.
//   None      — no guarantee; object may be in a broken state after a throw.
//
// The copy-and-swap idiom achieves the STRONG guarantee for assignment:
//   1. Copy the right-hand side (may throw — original untouched).
//   2. swap(*this, copy)  — noexcept, so it never throws.
//   3. The old resources are destroyed when the copy goes out of scope.
//
// If step 1 throws, *this is unchanged → strong guarantee.
// ─────────────────────────────────────────────────────────────────────

class Buffer
{
public:
    explicit Buffer(std::size_t n)
        : data_(new int[n]), size_(n)
    {
        std::fill(data_, data_ + n, 0);
    }

    // Copy constructor — may throw (allocation)
    Buffer(const Buffer& other)
        : data_(new int[other.size_]), size_(other.size_)
    {
        std::copy(other.data_, other.data_ + size_, data_);
    }

    // swap: noexcept — just pointer and size swaps
    friend void swap(Buffer& a, Buffer& b) noexcept
    {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
    }

    // Copy-and-swap assignment: strong guarantee.
    // The copy of `other` happens in the parameter, before we touch *this.
    // If it throws, this function is never entered — *this is unchanged.
    Buffer& operator=(Buffer other) noexcept // other is already a copy
    {
        swap(*this, other); // noexcept: exchange internals
        return *this;       // old resources freed when `other` destructs
    }

    ~Buffer() { delete[] data_; }

    std::size_t size() const noexcept { return size_; }
    int& operator[](std::size_t i) { return data_[i]; }

private:
    int*        data_;
    std::size_t size_;
};

// ── Basic guarantee example ───────────────────────────────────────────
// The vector may have grown (state changed) before the throw, but the
// object is still in a valid, destructible state — just not the original.
void basic_guarantee_demo()
{
    std::vector<std::string> v = {"a", "b", "c"};
    try {
        v.push_back("d"); // if this threw (OOM), v is still valid (basic)
        throw std::runtime_error("simulated error");
    }
    catch (...) {
        // v is in a valid state; "d" may or may not be present
        std::cout << "vector size after throw: " << v.size() << "\n";
    }
}

int main()
{
    // ── Strong guarantee via copy-and-swap ────────────────────────────
    Buffer b1(4); b1[0]=1; b1[1]=2; b1[2]=3; b1[3]=4;
    Buffer b2(4); b2[0]=9;

    b2 = b1; // strong: if copy ctor threw, b2 would be unchanged
    std::cout << "b2[0]=" << b2[0] << " b2[3]=" << b2[3] << "\n"; // 1 4

    // ── Self-assignment: safe because parameter is already a copy ─────
    b1 = b1; // the parameter is a copy of b1 — safe
    std::cout << "b1[0] after self-assign=" << b1[0] << "\n"; // 1

    basic_guarantee_demo();
}
