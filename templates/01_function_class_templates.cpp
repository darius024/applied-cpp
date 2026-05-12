#include <iostream>
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────
// Function templates
//
// The compiler generates a separate function for each type T it sees.
// `typename` and `class` are interchangeable in the parameter list.
// Type deduction happens from the call arguments; you rarely need to
// write the type explicitly.
// ─────────────────────────────────────────────────────────────────────

template<typename T>
T max_of(T a, T b)
{
    return (a > b) ? a : b;
}

// Multiple type parameters — deduction applies to each independently.
template<typename T, typename U>
void print_pair(const T& a, const U& b)
{
    std::cout << a << ", " << b << "\n";
}

// Explicit instantiation: tells the compiler to stamp out this version
// now (in this translation unit). Useful to control where code lives
// in large projects.
template int max_of<int>(int, int);

// ─────────────────────────────────────────────────────────────────────
// Non-type template parameters
//
// Template parameters don't have to be types — they can be compile-time
// integer values, pointers, etc.
// ─────────────────────────────────────────────────────────────────────

// A fixed-size ring buffer whose capacity is a compile-time constant.
// No heap allocation; size is part of the type.
template<typename T, std::size_t N>
class RingBuffer
{
public:
    void push(const T& val)
    {
        buf_[head_] = val;
        head_ = (head_ + 1) % N;
        if (size_ < N) ++size_;
    }

    T& operator[](std::size_t i) { return buf_[i % N]; }
    std::size_t size()     const { return size_; }
    std::size_t capacity() const { return N; }

private:
    std::array<T, N> buf_{};
    std::size_t      head_ = 0;
    std::size_t      size_ = 0;
};

// ─────────────────────────────────────────────────────────────────────
// Class template
//
// Each member function is itself a template — defined outside the class
// the template parameter list must be repeated.
// ─────────────────────────────────────────────────────────────────────

template<typename T>
class Pair
{
public:
    Pair(T first, T second) : first_(first), second_(second) {}

    T first()  const { return first_; }
    T second() const { return second_; }

    // Swap — only makes sense when T is the same for both Pairs.
    void swap(Pair<T>& other) noexcept
    {
        std::swap(first_,  other.first_);
        std::swap(second_, other.second_);
    }

    // A member function template: Pair<T> can convert to Pair<U>.
    template<typename U>
    Pair<U> as() const
    {
        return Pair<U>(static_cast<U>(first_), static_cast<U>(second_));
    }

private:
    T first_, second_;
};

// Member defined outside the class — template header repeated.
template<typename T>
std::ostream& operator<<(std::ostream& os, const Pair<T>& p)
{
    return os << "(" << p.first() << ", " << p.second() << ")";
}

int main()
{
    // ── Function template deduction ──────────────────────────────────
    std::cout << max_of(3, 7)          << "\n"; // T = int,  deduced
    std::cout << max_of(3.14, 2.71)    << "\n"; // T = double, deduced
    std::cout << max_of<std::string>("apple", "banana") << "\n"; // explicit

    print_pair(42, "hello");
    print_pair(3.14, true);

    // ── Class template ───────────────────────────────────────────────
    Pair<int> pi(1, 2);
    Pair<int> pj(10, 20);
    std::cout << pi << "\n";

    pi.swap(pj);
    std::cout << "after swap: " << pi << "\n";

    Pair<double> pd = pi.as<double>();
    std::cout << "as double: " << pd << "\n";

    // ── Non-type parameter ───────────────────────────────────────────
    RingBuffer<int, 4> rb;
    for (int i = 0; i < 6; ++i) rb.push(i * 10);
    std::cout << "ring[0]=" << rb[0] << " ring[1]=" << rb[1] << "\n";
    std::cout << "capacity=" << rb.capacity() << " size=" << rb.size() << "\n";
}
