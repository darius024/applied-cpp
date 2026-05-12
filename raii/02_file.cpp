#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdio>

// ─────────────────────────────────────────────
// Hand-rolled RAII wrapper around a C FILE*.
// Useful for understanding the pattern; in practice use std::fstream.
// ─────────────────────────────────────────────

class FileHandle
{
public:
    explicit FileHandle(const char* path, const char* mode)
    {
        file_ = std::fopen(path, mode);
        if (!file_)
            throw std::runtime_error(std::string("cannot open: ") + path);
        // Constructor succeeded → destructor will run.
    }

    ~FileHandle()
    {
        if (file_) std::fclose(file_); // always closed, even on exception
    }

    // A file handle is a unique resource — copy makes no sense.
    FileHandle(const FileHandle&)            = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // Move: transfer ownership; source no longer owns the handle.
    FileHandle(FileHandle&& other) noexcept : file_(other.file_)
    {
        other.file_ = nullptr;
    }

    FILE* get() const { return file_; }

private:
    FILE* file_;
};

void write_c_style(const char* path)
{
    FileHandle f(path, "w");
    std::fputs("hello from FileHandle\n", f.get());
    // f goes out of scope → fclose called → data flushed
}

// ─────────────────────────────────────────────
// std::fstream is already RAII — prefer it.
// Constructor opens, destructor closes and flushes, even on exception.
// ─────────────────────────────────────────────

void write_fstream(const char* path)
{
    std::ofstream f(path);
    if (!f) throw std::runtime_error(std::string("cannot open: ") + path);
    f << "hello from fstream\n";
} // destructor closes and flushes

int main()
{
    write_c_style("/tmp/raii_file1.txt");
    write_fstream("/tmp/raii_file2.txt");
    std::cout << "both files written and closed\n";
}
