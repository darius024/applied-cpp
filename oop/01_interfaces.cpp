#include <iostream>
#include <vector>
#include <memory>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────
// An interface in C++ is a class with:
//   - only pure virtual functions (= 0)
//   - a virtual destructor
//   - no data members
//
// It defines a contract. Any class implementing it must provide all methods.
// The virtual destructor is not optional: deleting a derived object through
// a base pointer without one is undefined behaviour.
// ─────────────────────────────────────────────────────────────────────

class IShape
{
public:
    virtual ~IShape() = default;       // mandatory — ensures derived dtors run

    virtual double area()    const = 0;
    virtual void   draw()    const = 0;
    virtual IShape* clone()  const = 0; // prototype pattern: deep-copy through interface
};

// ─────────────────────────────────────────────────────────────────────
// Concrete implementations. Each must implement every pure virtual.
// Failing to do so makes the derived class abstract too (compile error
// when you try to instantiate it).
// ─────────────────────────────────────────────────────────────────────

class Circle : public IShape
{
public:
    explicit Circle(double radius) : radius_(radius) {}

    double area() const override { return M_PI * radius_ * radius_; }
    void   draw() const override { std::cout << "Circle r=" << radius_ << "\n"; }
    Circle* clone() const override { return new Circle(*this); } // covariant return type

private:
    double radius_;
};

class Rectangle : public IShape
{
public:
    Rectangle(double w, double h) : w_(w), h_(h) {}

    double area() const override { return w_ * h_; }
    void   draw() const override { std::cout << "Rectangle " << w_ << "x" << h_ << "\n"; }
    Rectangle* clone() const override { return new Rectangle(*this); }

private:
    double w_, h_;
};

// ─────────────────────────────────────────────────────────────────────
// Client code works entirely through the IShape interface.
// It never mentions Circle or Rectangle — only the contract.
// ─────────────────────────────────────────────────────────────────────

void print_all(const std::vector<std::unique_ptr<IShape>>& shapes)
{
    for (const auto& s : shapes) {
        s->draw();
        std::cout << "  area = " << s->area() << "\n";
    }
}

int main()
{
    std::vector<std::unique_ptr<IShape>> shapes;
    shapes.push_back(std::make_unique<Circle>(3.0));
    shapes.push_back(std::make_unique<Rectangle>(4.0, 5.0));
    shapes.push_back(std::make_unique<Circle>(1.5));

    print_all(shapes);

    // clone() through the interface — no concrete type needed
    std::unique_ptr<IShape> copy(shapes[0]->clone());
    std::cout << "\ncloned: ";
    copy->draw();
}
