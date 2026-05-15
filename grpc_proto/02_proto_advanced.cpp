#include "advanced.pb.h"
#include "person.pb.h"
#include <google/protobuf/util/time_util.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection.h>
#include <iostream>

// build: cmake -B build && cmake --build build --target 02_proto_advanced

// ─────────────────────────────────────────────────────────────────────
// Advanced protobuf features:
//
// map<K,V>   — generated as a std::map-like accessor with Insert/at/find.
//              On the wire it is a repeated message with key+value fields.
//
// oneof      — only one field in the group can be set at a time.
//              Setting a second field clears the first.
//              xxx_case() returns which field is currently set.
//
// Any        — a typed container for any registered message type.
//              PackFrom(msg) serialises msg + its type URL.
//              UnpackTo(msg) deserialises; Is<T>() checks the type.
//
// Duration   — google.protobuf.Duration (seconds + nanos).
//              util::TimeUtil helpers for conversion.
//
// Reflection — iterate all fields of a message at runtime without
//              knowing its type at compile time (useful for logging,
//              generic serialisers, debugging).
// ─────────────────────────────────────────────────────────────────────

using namespace demo;
using namespace google::protobuf;

int main()
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // ── map<string, string> ───────────────────────────────────────────
    Config cfg;
    (*cfg.mutable_labels())["env"]     = "prod";
    (*cfg.mutable_labels())["region"]  = "eu-west-1";
    (*cfg.mutable_labels())["version"] = "2.4.1";
    cfg.set_max_conns(512);

    std::cout << "labels:\n";
    for (const auto& [k, v] : cfg.labels())
        std::cout << "  " << k << " = " << v << "\n";

    // ── oneof ─────────────────────────────────────────────────────────
    cfg.set_tcp_host("10.0.0.1:9000");
    std::cout << "backend case: " << cfg.backend_case() << "\n";  // BACKEND_TCP_HOST

    cfg.set_socket_path("/var/run/app.sock"); // clears tcp_host automatically
    std::cout << "after socket_path set, tcp_host empty: "
              << std::boolalpha << cfg.tcp_host().empty() << "\n"; // true
    std::cout << "socket_path: " << cfg.socket_path() << "\n";

    // ── Duration (well-known type) ────────────────────────────────────
    *cfg.mutable_timeout() =
        util::TimeUtil::MillisecondsToDuration(5000); // 5 seconds
    std::cout << "timeout_ms: "
              << util::TimeUtil::DurationToMilliseconds(cfg.timeout()) << "\n";

    // ── Any: pack / unpack ────────────────────────────────────────────
    Event ev;
    ev.set_type("metric.recorded");
    ev.set_time_ns(1700000000LL * 1'000'000'000LL);

    MetricValue mv;
    mv.set_name("cpu_usage");
    mv.set_value(0.73);
    ev.mutable_payload()->PackFrom(mv); // type URL stored alongside bytes

    std::cout << "\nevent type: " << ev.type() << "\n";
    std::cout << "payload type_url: " << ev.payload().type_url() << "\n";

    // Unpack — check type first, then extract
    if (ev.payload().Is<MetricValue>()) {
        MetricValue out;
        ev.payload().UnpackTo(&out);
        std::cout << "unpacked metric: " << out.name()
                  << " = " << out.value() << "\n";
    }

    // ── Reflection: iterate all set fields ───────────────────────────
    // Works on any Message — useful for generic loggers, diff tools.
    const Descriptor*   desc  = cfg.GetDescriptor();
    const Reflection*   refl  = cfg.GetReflection();

    std::vector<const FieldDescriptor*> set_fields;
    refl->ListFields(cfg, &set_fields);

    std::cout << "\nConfig set fields:\n";
    for (const auto* fd : set_fields) {
        std::cout << "  " << fd->name() << " (type "
                  << fd->type_name() << ")\n";
    }

    google::protobuf::ShutdownProtobufLibrary();
}
