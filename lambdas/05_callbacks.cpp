#include <iostream>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────
// Callbacks — a callable passed to a function to be invoked later.
//
// Two ownership models:
//
//   Non-owning (template param): the callback is only used during the
//   call. The caller guarantees the callback lives long enough. Zero cost.
//
//   Owning (std::function): the callee STORES the callback and can invoke
//   it later, after the registering call has returned. Requires type erasure.
//
// The distinction matters: a non-owning callback can safely capture local
// variables by reference. An owning callback must not — those locals may
// be gone by the time it fires.
// ─────────────────────────────────────────────────────────────────────


// ── Non-owning callback: template parameter ───────────────────────────
// Called synchronously inside the function. No storage needed.
// F is deduced — the lambda is inlined, captures by ref are safe.

template<typename F>
void for_each_file(const std::vector<std::string>& files, F on_file)
{
    for (const auto& f : files) on_file(f);
}


// ── Owning callback: std::function ────────────────────────────────────
// The EventEmitter stores handlers and fires them later.
// Captures must not refer to locals that may have been destroyed.

class EventEmitter
{
public:
    using Handler = std::function<void(const std::string&)>;

    // Subscribe: store the handler.
    // Returns an id so the caller can unsubscribe later.
    int on(const std::string& event, Handler h)
    {
        int id = next_id_++;
        handlers_[event].push_back({id, std::move(h)});
        return id;
    }

    // Unsubscribe by id.
    void off(const std::string& event, int id)
    {
        auto& vec = handlers_[event];
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                  [id](const auto& entry){ return entry.first == id; }),
                  vec.end());
    }

    // Fire all handlers for this event.
    void emit(const std::string& event, const std::string& data)
    {
        auto it = handlers_.find(event);
        if (it == handlers_.end()) return;
        for (auto& [id, h] : it->second) h(data);
    }

private:
    std::map<std::string, std::vector<std::pair<int, Handler>>> handlers_;
    int next_id_ = 0;
};

// ── Simpler EventEmitter (cleaner internal layout) ────────────────────
class Emitter
{
public:
    using Handler = std::function<void(const std::string&)>;

    int on(const std::string& event, Handler h)
    {
        int id = next_id_++;
        slots_[event].emplace_back(id, std::move(h));
        return id;
    }

    void emit(const std::string& event, const std::string& data)
    {
        if (auto it = slots_.find(event); it != slots_.end())
            for (auto& [id, h] : it->second) h(data);
    }

    void off(const std::string& event, int id)
    {
        auto& v = slots_[event];
        v.erase(std::remove_if(v.begin(), v.end(),
                [id](const auto& p){ return p.first == id; }), v.end());
    }

private:
    std::unordered_map<std::string,
        std::vector<std::pair<int, Handler>>> slots_;
    int next_id_ = 0;
};


int main()
{
    // ── Non-owning callback ───────────────────────────────────────────
    std::vector<std::string> files = {"main.cpp", "util.cpp", "test.cpp"};
    int count = 0;
    for_each_file(files, [&count](const std::string& f){
        std::cout << "processing: " << f << "\n";
        ++count;
    });
    std::cout << "processed " << count << " files\n\n";

    // ── Owning callbacks via Emitter ──────────────────────────────────
    Emitter em;

    // Capture by value — safe for stored callbacks.
    std::string prefix = "LOG";
    int id1 = em.on("data", [prefix](const std::string& d){  // copy of prefix
        std::cout << "[" << prefix << "] data: " << d << "\n";
    });

    int id2 = em.on("data", [](const std::string& d){
        std::cout << "[raw] " << d << "\n";
    });

    em.emit("data", "payload-1");
    em.emit("data", "payload-2");

    std::cout << "\n(unsubscribing id1)\n";
    em.off("data", id1);
    em.emit("data", "payload-3"); // only id2 fires now
}
