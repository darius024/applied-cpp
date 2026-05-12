#include <iostream>
#include <utility>

// ─────────────────────────────────────────────────────────────────────
// Why move semantics are essential for RAII types.
//
// An RAII type owns a unique resource. Copying is often meaningless or
// dangerous (two objects sharing the same raw pointer → double-free).
// Moving transfers ownership: the source is left in a valid, empty state
// and its destructor becomes a no-op.
//
// This is the Rule of Five applied to a resource-owning class.
// ─────────────────────────────────────────────────────────────────────

class UniqueBuffer
{
public:
    explicit UniqueBuffer(std::size_t n)
        : data_(new int[n]), size_(n)
    {
        std::cout << "[construct]  acquired " << size_ << " ints\n";
    }

    // 1. Destructor — release the resource.
    ~UniqueBuffer()
    {
        if (data_) {
            delete[] data_;
            std::cout << "[destruct]   released\n";
        } else {
            std::cout << "[destruct]   moved-from, no-op\n";
        }
    }

    // 2 & 3. Copy — deleted. Owning a raw pointer can't be naively copied.
    UniqueBuffer(const UniqueBuffer&)            = delete;
    UniqueBuffer& operator=(const UniqueBuffer&) = delete;

    // 4. Move constructor — steal the resource; null out source.
    UniqueBuffer(UniqueBuffer&& other) noexcept
        : data_(other.data_), size_(other.size_)
    {
        other.data_ = nullptr; // crucial: prevents double-free when source destructs
        other.size_ = 0;
        std::cout << "[move ctor]  ownership transferred\n";
    }

    // 5. Move assignment — release what we own, then steal from source.
    UniqueBuffer& operator=(UniqueBuffer&& other) noexcept
    {
        if (this == &other) return *this;

        delete[] data_;      // release current resource first
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
        std::cout << "[move assign] ownership transferred\n";
        return *this;
    }

    std::size_t size() const { return size_; }

private:
    int*        data_;
    std::size_t size_;
};

// Factory returning a UniqueBuffer.
// NRVO (Named Return Value Optimization) usually elides the move entirely,
// but the type must be movable for the compiler to guarantee it.
UniqueBuffer make_buffer(std::size_t n)
{
    return UniqueBuffer(n);
}

int main()
{
    UniqueBuffer a = make_buffer(4);     // construct (NRVO likely elides move)

    UniqueBuffer b = std::move(a);       // move ctor: b owns, a is empty
    std::cout << "b.size=" << b.size() << ", a is moved-from\n\n";

    UniqueBuffer c(8);
    c = std::move(b);                    // move assign: c releases old buffer, takes b's
    std::cout << "c.size=" << c.size() << "\n\n";

    // Destructors run in reverse declaration order: c, b (no-op), a (no-op)
}
