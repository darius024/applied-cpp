#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <vector>

// ─────────────────────────────────────────────────────────────────────
// Rule of Zero: a class that composes its resources from existing RAII
// types needs none of the five special members — the compiler generates
// correct copy, move, and destruction automatically.
//
// This is the *goal* of RAII design. Writing Rule-of-Five boilerplate
// is a signal you should reach for an existing wrapper instead.
// ─────────────────────────────────────────────────────────────────────

// BAD — owns a raw pointer, forces you to write all five members.
class BadNode {
    int* value_;
public:
    explicit BadNode(int v) : value_(new int(v)) {}
    ~BadNode()                              { delete value_; }
    BadNode(const BadNode& o)               : value_(new int(*o.value_)) {}
    BadNode& operator=(const BadNode& o)    { if (this != &o) { delete value_; value_ = new int(*o.value_); } return *this; }
    BadNode(BadNode&& o) noexcept           : value_(o.value_) { o.value_ = nullptr; }
    BadNode& operator=(BadNode&& o) noexcept{ delete value_; value_ = o.value_; o.value_ = nullptr; return *this; }
};

// GOOD — composes from unique_ptr; zero special members needed.
// unique_ptr makes the class move-only (which is usually right for a node).
class GoodNode {
    std::unique_ptr<int> value_;
public:
    explicit GoodNode(int v) : value_(std::make_unique<int>(v)) {}
    int value() const { return *value_; }
    // copy is implicitly deleted (unique_ptr is not copyable) — correct.
    // move is implicitly generated — correct.
};

// ─────────────────────────────────────────────────────────────────────
// A realistic value-type: compose everything from stdlib RAII types.
// Copy, move, and destruction all work correctly with zero effort.
// ─────────────────────────────────────────────────────────────────────

class Config {
public:
    Config(std::string name, std::vector<std::string> keys)
        : name_(std::move(name)), keys_(std::move(keys)) {}

    // No destructor, no copy/move defined — compiler generates all five
    // correctly because string and vector manage their own memory.

    void print() const {
        std::cout << name_ << ": ";
        for (const auto& k : keys_) std::cout << k << " ";
        std::cout << '\n';
    }

private:
    std::string              name_;
    std::vector<std::string> keys_;
};

int main()
{
    Config a("db", {"host", "port", "user"});
    Config b = a;            // copy — deep, correct, free
    Config c = std::move(a); // move — transfers heap buffers, no alloc

    b.print();
    c.print();

    GoodNode n(42);
    GoodNode m = std::move(n); // move-only, correct
    std::cout << "GoodNode value: " << m.value() << '\n';
}
