#include <flatbuffers/flexbuffers.h>
#include <iostream>
#include <vector>

// compile: g++ -std=c++17 01_flexbuffers.cpp -o 01_flexbuffers -lflatbuffers

// ─────────────────────────────────────────────────────────────────────
// FlexBuffers: FlatBuffers' schemaless variant.
// No schema, no code generation — the builder writes a self-describing
// binary blob. GetRoot() returns a Reference into the original buffer;
// field access is a direct offset dereference — zero allocation.
// ─────────────────────────────────────────────────────────────────────

int main()
{
    // ── Build ─────────────────────────────────────────────────────────
    flexbuffers::Builder b;
    b.Map([&] {
        b.Int("id",     42);
        b.String("host", "10.0.0.1");
        b.Int("port",   9000);
        b.Double("load", 0.73);
        b.Vector("tags", [&] {
            b.String("web");
            b.String("primary");
        });
    });
    b.Finish();

    std::vector<uint8_t> buf = b.GetBuffer();
    std::cout << "buffer: " << buf.size() << " bytes\n";

    // ── Read — no parse, direct offset into buf ───────────────────────
    auto root = flexbuffers::GetRoot(buf);
    auto map  = root.AsMap();

    std::cout << "id:   " << map["id"].AsInt64()          << "\n"; // 42
    std::cout << "host: " << map["host"].AsString().c_str()<< "\n";
    std::cout << "port: " << map["port"].AsInt64()         << "\n";
    std::cout << "load: " << map["load"].AsDouble()        << "\n";

    auto tags = map["tags"].AsVector();
    std::cout << "tags: ";
    for (size_t i = 0; i < tags.size(); ++i)
        std::cout << tags[i].AsString().c_str() << " ";
    std::cout << "\n";

    // ── Nested map ────────────────────────────────────────────────────
    flexbuffers::Builder b2;
    b2.Map([&] {
        b2.Map("metrics", [&] {
            b2.Int("rps",    12500);
            b2.Double("p99", 1.4);
        });
    });
    b2.Finish();

    auto m = flexbuffers::GetRoot(b2.GetBuffer()).AsMap()["metrics"].AsMap();
    std::cout << "rps=" << m["rps"].AsInt64()
              << " p99=" << m["p99"].AsDouble() << "\n";
}
