#include <iostream>
#include <string>
#include <vector>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────
// Template specialisation lets you provide a completely different
// implementation for a specific type (or family of types).
//
// Full specialisation:    exactly one concrete type.
// Partial specialisation: a pattern (e.g. "any pointer type").
//
// Only CLASS templates support partial specialisation.
// For function templates, use overloading instead.
// ─────────────────────────────────────────────────────────────────────


// ── Full specialisation — class template ─────────────────────────────
//
// Primary template: generic storage for T.
template<typename T>
class Storage
{
public:
    explicit Storage(T v) : value_(v) {}
    void print() const { std::cout << "Storage<T>: " << value_ << "\n"; }
private:
    T value_;
};

// Full specialisation for bool: packs the bit into a single byte.
// The specialisation is a completely independent class — it can have
// different members, different methods, different layout.
template<>
class Storage<bool>
{
public:
    explicit Storage(bool v) : byte_(v ? 1 : 0) {}
    void print() const { std::cout << "Storage<bool>: " << (byte_ ? "true" : "false") << "\n"; }
    bool get() const { return byte_ != 0; }
private:
    unsigned char byte_;
};

// ── Partial specialisation — pointer types ────────────────────────────
//
// Matches Storage<T*> for any T.
template<typename T>
class Storage<T*>
{
public:
    explicit Storage(T* p) : ptr_(p) {}
    void print() const {
        if (ptr_) std::cout << "Storage<T*>: points to " << *ptr_ << "\n";
        else      std::cout << "Storage<T*>: null\n";
    }
private:
    T* ptr_;
};


// ── Building a type trait from specialisation ─────────────────────────
//
// The standard library's type traits all follow this pattern.
// Primary template: assume the property is false (or the default).
// Specialisation for the specific case: set it true.

// Is T a pointer type?
template<typename T>
struct is_pointer {
    static constexpr bool value = false;
};

template<typename T>
struct is_pointer<T*> {                   // partial spec: matches any T*
    static constexpr bool value = true;
};

// Convenience alias
template<typename T>
inline constexpr bool is_pointer_v = is_pointer<T>::value;


// ── Full specialisation — function template ───────────────────────────
//
// Function templates don't support partial specialisation.
// Use overloading to achieve the same effect.

template<typename T>
bool equal(const T& a, const T& b) { return a == b; }

// "Specialisation" for C-strings via overloading (not template specialisation).
bool equal(const char* a, const char* b) { return std::strcmp(a, b) == 0; }


int main()
{
    Storage<int>    si(42);     si.print();    // primary
    Storage<bool>   sb(true);   sb.print();    // full spec
    int x = 99;
    Storage<int*>   sp(&x);     sp.print();    // partial spec
    Storage<int*>   sn(nullptr); sn.print();   // partial spec, null

    std::cout << "\n";
    std::cout << "is_pointer<int>:   " << is_pointer_v<int>   << "\n"; // false
    std::cout << "is_pointer<int*>:  " << is_pointer_v<int*>  << "\n"; // true
    std::cout << "is_pointer<char*>: " << is_pointer_v<char*> << "\n"; // true

    std::cout << "\n";
    std::cout << "equal(1,1):       " << equal(1, 1)           << "\n"; // template
    std::cout << "equal(\"hi\",\"hi\"): " << equal("hi", "hi")   << "\n"; // overload
    std::cout << "equal(\"hi\",\"bye\"): " << equal("hi", "bye") << "\n"; // overload
}
