#include <iostream>
#include <algorithm>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────
// Problem: writing a correct copy-assignment operator for a resource-
// owning class is surprisingly hard to get right naively.
//
// Two requirements:
//   1. Self-assignment safe:  a = a  must not free memory still in use.
//   2. Exception safe: if the allocation throws, *this must be unchanged
//      (strong guarantee — either the assignment fully succeeds or nothing changes).
//
// The copy-and-swap idiom satisfies both for free.
// ─────────────────────────────────────────────────────────────────────

class String
{
public:
    explicit String(const char* s = "")
        : size_(std::strlen(s)), data_(new char[size_ + 1])
    {
        std::memcpy(data_, s, size_ + 1);
    }

    // ── Copy constructor ──────────────────────────────────────────────
    // Allocates fresh storage and copies the bytes.
    // If new throws, the object was never fully constructed → no leak.
    String(const String& other)
        : size_(other.size_), data_(new char[other.size_ + 1])
    {
        std::memcpy(data_, other.data_, size_ + 1);
    }

    // ── Move constructor ──────────────────────────────────────────────
    String(String&& other) noexcept
        : size_(other.size_), data_(other.data_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    // ── Destructor ────────────────────────────────────────────────────
    ~String() { delete[] data_; }

    // ── swap — noexcept, just exchanges pointers ──────────────────────
    // Defined as a member so argument-dependent lookup finds it.
    void swap(String& other) noexcept
    {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }

    // ── Copy-and-swap assignment ──────────────────────────────────────
    //
    // 1. `other` is passed BY VALUE → the copy constructor runs here.
    //    If the allocation inside the copy ctor throws, we never enter
    //    this function — *this is untouched. (Strong exception guarantee.)
    //
    // 2. We swap *this with the local copy. Swap is noexcept — no throw.
    //
    // 3. The local `other` goes out of scope → destructor frees the OLD
    //    buffer that was in *this.
    //
    // Self-assignment (a = a): the copy ctor makes a fresh copy of a,
    // we swap, then the destructor frees the copy — safe and correct.
    //
    // This also serves as the move-assignment operator if `other` is
    // passed as an rvalue, because the compiler picks the move ctor
    // to construct the by-value parameter — no extra allocation.
    String& operator=(String other) noexcept  // by value — intentional
    {
        swap(other);
        return *this;
    }

    const char* c_str() const { return data_; }
    std::size_t size()  const { return size_; }

private:
    std::size_t size_;
    char*       data_;
};

int main()
{
    String a("hello");
    String b("world");

    std::cout << "a = " << a.c_str() << "\n";
    std::cout << "b = " << b.c_str() << "\n";

    b = a;                          // copy assignment
    std::cout << "after b = a: b = " << b.c_str() << "\n";

    b = std::move(a);               // move assignment (move ctor called for param)
    std::cout << "after b = move(a): b = " << b.c_str() << "\n";

    b = b;                          // self-assignment — safe
    std::cout << "after b = b: b = " << b.c_str() << "\n";
}
