#include <iostream>
#include <memory>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────
// The hand-rolled RAII wrapper in 02_file.cpp is a lot of boilerplate
// for a common problem: wrapping a C API that has an opaque handle
// and a paired open/close (or create/destroy) function.
//
// unique_ptr accepts a custom deleter, turning any such pair into
// a single line — no class required.
// ─────────────────────────────────────────────────────────────────────

// Helper: a deleter for FILE* that calls fclose.
// Written as a struct so it can be stored without overhead (empty base opt).
struct FCloseDeleter {
    void operator()(FILE* f) const
    {
        if (f) {
            std::fclose(f);
            std::cout << "[deleter] fclose called\n";
        }
    }
};

using FilePtr = std::unique_ptr<FILE, FCloseDeleter>;

FilePtr open_file(const char* path, const char* mode)
{
    FILE* raw = std::fopen(path, mode);
    if (!raw) throw std::runtime_error(std::string("cannot open: ") + path);
    return FilePtr(raw); // ownership transferred to unique_ptr
}

// ─────────────────────────────────────────────────────────────────────
// The same pattern works for any C API, e.g.:
//   using WindowPtr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
//   WindowPtr win(SDL_CreateWindow(...), SDL_DestroyWindow);
//
// Prefer a lambda deleter when the function signature doesn't match exactly:
// ─────────────────────────────────────────────────────────────────────

using FilePtr2 = std::unique_ptr<FILE, decltype(&std::fclose)>;

FilePtr2 open_file2(const char* path, const char* mode)
{
    FILE* raw = std::fopen(path, mode);
    if (!raw) throw std::runtime_error(std::string("cannot open: ") + path);
    return FilePtr2(raw, std::fclose); // pass deleter at runtime
}

int main()
{
    {
        auto f = open_file("/tmp/raii_custom_del.txt", "w");
        std::fputs("custom deleter\n", f.get());
    } // fclose called here via deleter

    {
        auto f = open_file2("/tmp/raii_custom_del2.txt", "w");
        std::fputs("function pointer deleter\n", f.get());
    } // fclose called here

    std::cout << "done\n";
}
