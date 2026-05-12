#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────
// Factory Method pattern.
//
// Goal: callers create objects through an interface without knowing or
// depending on the concrete type. The factory returns unique_ptr<Base>,
// so the caller gets ownership without ever seeing the derived class.
//
// Two levels shown here:
//   1. Simple factory function — a switch/map, good for small sets.
//   2. Registry-based factory — new types register themselves; the
//      factory doesn't need to change when a new type is added.
// ─────────────────────────────────────────────────────────────────────

struct IShape {
    virtual double area()  const = 0;
    virtual void   print() const = 0;
    virtual ~IShape() = default;
};

class Circle final : public IShape {
    double r_;
public:
    explicit Circle(double r) : r_(r) {}
    double area()  const override { return M_PI * r_ * r_; }
    void   print() const override { std::cout << "Circle r=" << r_ << " area=" << area() << "\n"; }
};

class Rectangle final : public IShape {
    double w_, h_;
public:
    Rectangle(double w, double h) : w_(w), h_(h) {}
    double area()  const override { return w_ * h_; }
    void   print() const override { std::cout << "Rect " << w_ << "x" << h_ << " area=" << area() << "\n"; }
};

// ─────────────────────────────────────────────────────────────────────
// Level 1: simple factory function.
// The caller passes a type tag; the factory returns the right object.
// Callers only need to include this header — not Circle.h or Rectangle.h.
// ─────────────────────────────────────────────────────────────────────

std::unique_ptr<IShape> make_shape(const std::string& type, double a, double b = 0)
{
    if (type == "circle")    return std::make_unique<Circle>(a);
    if (type == "rectangle") return std::make_unique<Rectangle>(a, b);
    throw std::invalid_argument("unknown shape: " + type);
}

// ─────────────────────────────────────────────────────────────────────
// Level 2: registry-based factory.
// Each type registers a creator lambda under a string key.
// Adding a new shape = one register() call; the factory is untouched.
// Common in plugin architectures and deserialisation systems.
// ─────────────────────────────────────────────────────────────────────

class ShapeRegistry
{
public:
    using Creator = std::function<std::unique_ptr<IShape>(double, double)>;

    static ShapeRegistry& instance()
    {
        static ShapeRegistry reg; // thread-safe in C++11
        return reg;
    }

    void register_shape(const std::string& key, Creator creator)
    {
        registry_[key] = std::move(creator);
    }

    std::unique_ptr<IShape> create(const std::string& key, double a, double b = 0) const
    {
        auto it = registry_.find(key);
        if (it == registry_.end())
            throw std::invalid_argument("not registered: " + key);
        return it->second(a, b);
    }

private:
    std::unordered_map<std::string, Creator> registry_;
};

int main()
{
    // ── Level 1: simple factory ──────────────────────────────────────
    auto c = make_shape("circle",    5.0);
    auto r = make_shape("rectangle", 3.0, 4.0);
    c->print();
    r->print();

    // ── Level 2: registry factory ────────────────────────────────────
    auto& reg = ShapeRegistry::instance();
    reg.register_shape("circle",    [](double a, double) { return std::make_unique<Circle>(a);         });
    reg.register_shape("rectangle", [](double a, double b){ return std::make_unique<Rectangle>(a, b); });

    auto c2 = reg.create("circle",    2.0);
    auto r2 = reg.create("rectangle", 6.0, 7.0);
    c2->print();
    r2->print();

    try { reg.create("triangle", 1, 2); }
    catch (const std::invalid_argument& e) { std::cout << "caught: " << e.what() << "\n"; }
}
