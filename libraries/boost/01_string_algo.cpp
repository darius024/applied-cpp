#include <boost/algorithm/string.hpp>
#include <iostream>
#include <vector>
#include <string>

// compile: g++ -std=c++17 01_string_algo.cpp -o 01_string_algo
// (header-only — no link flags needed)

// ─────────────────────────────────────────────────────────────────────
// boost/algorithm/string: string utilities absent from the stdlib.
//
// Most functions come in two flavours:
//   in-place  — modifies the argument: trim(s)
//   _copy     — returns a new string: trim_copy(s)
//
// Case-insensitive variants are prefixed with 'i': icontains, istarts_with…
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Trim ──────────────────────────────────────────────────────────
    std::string s = "  hello world  ";
    boost::trim(s);                              // in-place
    std::cout << "[" << s << "]\n";              // [hello world]

    std::string t = "\t  spaces  \n";
    std::cout << "[" << boost::trim_copy(t) << "]\n"; // non-modifying

    // ── Case conversion ───────────────────────────────────────────────
    std::string u = "Hello World";
    boost::to_upper(u);
    std::cout << u << "\n";                      // HELLO WORLD
    std::cout << boost::to_lower_copy(u) << "\n"; // hello world

    // ── split ─────────────────────────────────────────────────────────
    std::vector<std::string> tokens;
    boost::split(tokens, "one,two,,three", boost::is_any_of(","));
    for (auto& tok : tokens) std::cout << "[" << tok << "]"; // [one][two][][three]
    std::cout << "\n";

    // token_compress_on: consecutive delimiters treated as one — no empty tokens
    tokens.clear();
    boost::split(tokens, "one  two   three", boost::is_any_of(" "),
                 boost::token_compress_on);
    for (auto& tok : tokens) std::cout << "[" << tok << "]"; // [one][two][three]
    std::cout << "\n";

    // ── join ──────────────────────────────────────────────────────────
    std::cout << boost::join(tokens, " | ") << "\n"; // one | two | three

    // ── Predicates ────────────────────────────────────────────────────
    std::string text = "Hello, World!";
    std::cout << std::boolalpha;
    std::cout << "contains 'World':      " << boost::contains  (text, "World") << "\n"; // true
    std::cout << "starts_with 'Hello':   " << boost::starts_with(text, "Hello") << "\n"; // true
    std::cout << "ends_with '!':         " << boost::ends_with  (text, "!")     << "\n"; // true

    // Case-insensitive predicates
    std::cout << "icontains 'world':     " << boost::icontains  (text, "world") << "\n"; // true
    std::cout << "istarts_with 'hello':  " << boost::istarts_with(text, "hello") << "\n"; // true

    // ── Replace ───────────────────────────────────────────────────────
    std::string r = "foo bar foo baz foo";
    boost::replace_all(r, "foo", "qux");
    std::cout << r << "\n"; // qux bar qux baz qux

    // replace_first: only the first occurrence
    std::string r2 = "aabbcc aabbcc";
    boost::replace_first(r2, "bb", "XX");
    std::cout << r2 << "\n"; // aaXXcc aabbcc

    // ── Erase ─────────────────────────────────────────────────────────
    std::string r3 = "Hello   World";
    boost::erase_all(r3, " ");
    std::cout << r3 << "\n"; // HelloWorld
}
