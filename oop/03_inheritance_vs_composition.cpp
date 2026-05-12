#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────
// Inheritance models is-a: "A Circle IS A Shape" — the derived class IS
// a more specific kind of the base. Substitutability (Liskov) must hold:
// any code expecting a Shape must work correctly with a Circle.
//
// Composition models has-a: "A Car HAS AN Engine". The class delegates
// to its members. Prefer it by default: looser coupling, easier to test,
// avoids the fragile base class problem.
// ─────────────────────────────────────────────────────────────────────


// ── Bad: inheriting from a concrete class ───────────────────────────
//
// std::vector is not designed for inheritance. Its destructor is non-virtual,
// so deleting a LoggingVector through a vector<int>* leaks the derived part.
// Also, adding an overriding push_back() shadows (but does not override) the
// base version — callers using vector<int>* silently skip the logging.

// class LoggingVector : public std::vector<int> { // DON'T do this
//     void push_back(int v) {
//         std::cout << "push " << v << "\n";
//         std::vector<int>::push_back(v);
//     }
// };


// ── Good: composition — has-a ────────────────────────────────────────
//
// LoggingVector wraps a vector. It exposes only the interface it wants,
// and the logging is guaranteed regardless of which pointer type is used.

class LoggingVector
{
public:
    void push_back(int v)
    {
        std::cout << "[log] push " << v << "\n";
        data_.push_back(v);
    }

    int  operator[](std::size_t i) const { return data_[i]; }
    std::size_t size()             const { return data_.size(); }

private:
    std::vector<int> data_; // has-a: owns the vector
};


// ── Correct use of inheritance (is-a + Liskov) ───────────────────────
//
// A SortedInserter IS A specialisation of LoggingVector only if it upholds
// the same contract. Here we just show a clean is-a with an abstract base.

struct ILogger {
    virtual void log(const std::string& msg) const = 0;
    virtual ~ILogger() = default;
};

struct ConsoleLogger : ILogger {
    void log(const std::string& msg) const override {
        std::cout << "[console] " << msg << "\n";
    }
};

struct FileLogger : ILogger {
    // In a real impl: write to a file. Here just labelled differently.
    void log(const std::string& msg) const override {
        std::cout << "[file]    " << msg << "\n";
    }
};

// ── Composition: inject behaviour through an interface ───────────────
//
// DataPipeline does not inherit from ILogger — it HAS one.
// The logger implementation can be swapped at construction (dependency injection).

class DataPipeline
{
public:
    explicit DataPipeline(const ILogger& logger) : logger_(logger) {}

    void run(const std::string& data)
    {
        logger_.log("starting pipeline");
        // ... process data ...
        logger_.log("pipeline done: " + data);
    }

private:
    const ILogger& logger_; // has-a (non-owning; lifetime managed by caller)
};

int main()
{
    LoggingVector v;
    v.push_back(1);
    v.push_back(2);
    std::cout << "size = " << v.size() << "\n\n";

    ConsoleLogger cl;
    FileLogger    fl;

    DataPipeline p1(cl);
    p1.run("batch-42");

    std::cout << "\n";

    DataPipeline p2(fl);
    p2.run("batch-43");
}
