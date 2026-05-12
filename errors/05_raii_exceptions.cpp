#include <iostream>
#include <fstream>
#include <stdexcept>
#include <functional>

// ─────────────────────────────────────────────────────────────────────
// RAII and exceptions.
//
// RAII objects destroy themselves correctly regardless of how the scope
// exits — normal return, exception, or early return.
// You do NOT need try/finally in C++: just let destructors run.
//
// A scope guard generalises this: it runs an arbitrary cleanup callback
// on scope exit. Useful when there is no dedicated RAII wrapper for a
// resource and writing one is overkill.
// ─────────────────────────────────────────────────────────────────────

// ── RAII file wrapper ─────────────────────────────────────────────────
// std::fstream already does this; shown here to illustrate the pattern.
class ManagedFile
{
public:
    explicit ManagedFile(const std::string& path)
        : f_(path) // may throw std::ios::failure
    {
        if (!f_) throw std::runtime_error("cannot open " + path);
    }
    // Destructor runs on normal exit AND on exception unwind — file always closed.
    ~ManagedFile() { /* f_ closes itself */ }

    std::fstream& get() { return f_; }
private:
    std::fstream f_;
};

// ── Scope guard ───────────────────────────────────────────────────────
// Calls a cleanup function when it goes out of scope, regardless of how.
// Copy/move disabled — it should only live on the stack where it is created.
class ScopeGuard
{
public:
    explicit ScopeGuard(std::function<void()> cleanup)
        : cleanup_(std::move(cleanup)), active_(true) {}

    // dismiss() cancels the cleanup — for "commit" semantics.
    void dismiss() noexcept { active_ = false; }

    ~ScopeGuard() { if (active_) cleanup_(); }

    ScopeGuard(const ScopeGuard&)            = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    std::function<void()> cleanup_;
    bool active_;
};

// ── Showing RAII beats manual cleanup ─────────────────────────────────
void process(bool fail)
{
    std::cout << "  acquiring resource\n";
    ScopeGuard release([]{ std::cout << "  releasing resource\n"; });

    if (fail) throw std::runtime_error("processing failed");

    std::cout << "  work done\n";
    // `release` destructor runs here (normal return) — cleanup called.
    // If an exception was thrown above, the destructor also runs during unwind.
}

// ── Dismiss: only release if something went wrong ─────────────────────
void transact(bool fail)
{
    std::cout << "  begin transaction\n";
    ScopeGuard rollback([]{ std::cout << "  rolling back\n"; });

    // ... do work ...
    if (fail) throw std::runtime_error("transact failed");

    rollback.dismiss(); // success path: don't roll back
    std::cout << "  commit\n";
}

int main()
{
    for (bool fail : {false, true}) {
        std::cout << (fail ? "fail" : "success") << " path:\n";
        try { process(fail); }
        catch (const std::exception& e) {
            std::cout << "  caught: " << e.what() << "\n";
        }
    }

    std::cout << "\ntransaction:\n";
    for (bool fail : {false, true}) {
        std::cout << (fail ? "fail" : "success") << " path:\n";
        try { transact(fail); }
        catch (const std::exception& e) {
            std::cout << "  caught: " << e.what() << "\n";
        }
    }
}
