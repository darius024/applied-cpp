#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>

// compile: g++ -std=c++17 02_filesystem.cpp -o 02_filesystem -lboost_filesystem -lboost_system
// Note: std::filesystem (C++17) covers most of the same API.
//       Boost.Filesystem is useful for portability on older toolchains.

// ─────────────────────────────────────────────────────────────────────
// boost::filesystem: path manipulation, directory traversal, file status.
//
// path uses operator/ for composition, and automatically uses the
// correct path separator for the current OS.
// ─────────────────────────────────────────────────────────────────────

namespace fs = boost::filesystem;

int main()
{
    // ── Path decomposition ────────────────────────────────────────────
    fs::path p("/usr/local/lib/libboost_system.a");
    std::cout << "full:      " << p              << "\n";
    std::cout << "parent:    " << p.parent_path()  << "\n"; // /usr/local/lib
    std::cout << "filename:  " << p.filename()     << "\n"; // libboost_system.a
    std::cout << "stem:      " << p.stem()         << "\n"; // libboost_system
    std::cout << "extension: " << p.extension()    << "\n"; // .a

    // ── Path composition ──────────────────────────────────────────────
    fs::path dir  = "/tmp";
    fs::path file = dir / "data" / "output.csv";  // operator/ joins segments
    std::cout << "composed: " << file << "\n";     // /tmp/data/output.csv

    // ── Status queries ────────────────────────────────────────────────
    fs::path cwd = fs::current_path();
    std::cout << std::boolalpha;
    std::cout << "cwd exists:   " << fs::exists(cwd)       << "\n"; // true
    std::cout << "cwd is dir:   " << fs::is_directory(cwd) << "\n"; // true

    // ── Create / write / iterate ──────────────────────────────────────
    fs::path tmp = fs::temp_directory_path() / "boost_fs_demo";
    fs::create_directories(tmp);
    fs::create_directories(tmp / "sub");

    // Write test files
    std::ofstream(( tmp            / "a.txt").string()) << "hello\n";
    std::ofstream(( tmp / "sub"   / "b.txt").string()) << "world\n";

    // Flat directory iteration
    std::cout << "\ndirectory_iterator:\n";
    for (auto& entry : fs::directory_iterator(tmp))
        std::cout << "  " << entry.path().filename()
                  << (fs::is_directory(entry) ? "/" : "") << "\n";

    // Recursive iteration
    std::cout << "\nrecursive_directory_iterator:\n";
    for (auto& entry : fs::recursive_directory_iterator(tmp)) {
        auto rel = entry.path().lexically_relative(tmp);
        std::cout << "  " << rel;
        if (fs::is_regular_file(entry))
            std::cout << " (" << fs::file_size(entry) << " bytes)";
        std::cout << "\n";
    }

    // ── Rename and remove ─────────────────────────────────────────────
    fs::rename(tmp / "a.txt", tmp / "renamed.txt");
    std::cout << "\nrenamed exists: " << fs::exists(tmp / "renamed.txt") << "\n";

    fs::remove_all(tmp); // recursive delete
    std::cout << "tmp removed: " << !fs::exists(tmp) << "\n";
}
