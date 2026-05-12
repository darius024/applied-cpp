#include <iostream>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────
// Operator overloading for a value type Vec2.
//
// Rules:
//  - Operators that modify *this (+=, -=) → member functions
//  - Symmetric binary operators (+, ==, <) → free functions
//    Reason: as members, the left operand must be Vec2, which breaks
//    expressions like `scalar + vec`. As free functions both sides are equal.
//  - << and >> → must be free functions (left side is ostream/istream)
//
//  Implement compound operators (+=) first, then derive the binary
//  version (+) from them. This keeps logic in one place.
// ─────────────────────────────────────────────────────────────────────

struct Vec2 {
    double x, y;

    explicit Vec2(double x = 0, double y = 0) : x(x), y(y) {}

    // ── Compound assignment — member (modifies *this) ────────────────
    Vec2& operator+=(const Vec2& rhs) { x += rhs.x; y += rhs.y; return *this; }
    Vec2& operator-=(const Vec2& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
    Vec2& operator*=(double s)        { x *= s;      y *= s;      return *this; }

    // ── Unary negation — member (acts on *this only) ─────────────────
    Vec2 operator-() const { return Vec2(-x, -y); }

    double length() const { return std::sqrt(x*x + y*y); }
};

// ── Binary arithmetic — free functions derived from compound ─────────
// Passing `lhs` by value gives us the copy for free; then mutate and return.
inline Vec2 operator+(Vec2 lhs, const Vec2& rhs) { return lhs += rhs; }
inline Vec2 operator-(Vec2 lhs, const Vec2& rhs) { return lhs -= rhs; }
inline Vec2 operator*(Vec2 v,   double s)         { return v  *= s;    }
inline Vec2 operator*(double s, Vec2 v)            { return v  *= s;    } // s * v

// ── Equality — free function (symmetric: neither Vec2 is "primary") ──
inline bool operator==(const Vec2& a, const Vec2& b) {
    return a.x == b.x && a.y == b.y;
}
inline bool operator!=(const Vec2& a, const Vec2& b) { return !(a == b); }

// ── Ordering — free function; enables use in std::set, std::map, std::sort
// Lexicographic: compare x first, then y.
inline bool operator<(const Vec2& a, const Vec2& b) {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
}

// ── Stream output — must be free (left side is ostream, not Vec2) ────
inline std::ostream& operator<<(std::ostream& os, const Vec2& v) {
    return os << "(" << v.x << ", " << v.y << ")";
}

int main()
{
    Vec2 a(1, 2), b(3, 4);

    std::cout << "a        = " << a            << "\n";
    std::cout << "b        = " << b            << "\n";
    std::cout << "a + b    = " << (a + b)      << "\n";
    std::cout << "a - b    = " << (a - b)      << "\n";
    std::cout << "a * 2    = " << (a * 2)      << "\n";
    std::cout << "3 * b    = " << (3.0 * b)    << "\n";
    std::cout << "-a       = " << (-a)          << "\n";
    std::cout << "|b|      = " << b.length()   << "\n";
    std::cout << "a == a   = " << (a == a)     << "\n";
    std::cout << "a == b   = " << (a == b)     << "\n";
    std::cout << "a < b    = " << (a < b)      << "\n";

    a += b;
    std::cout << "a += b   = " << a            << "\n";
}
