#include "person.pb.h"
#include <google/protobuf/util/time_util.h>
#include <iostream>
#include <string>

// build: cmake -B build && cmake --build build --target 01_proto_basics

// ─────────────────────────────────────────────────────────────────────
// Protocol Buffers basics.
//
// Accessors:  set_xxx() / xxx()     — scalar fields
//             mutable_xxx()         — get a mutable pointer to a sub-message
//             add_xxx()             — append to a repeated field
//             xxx_size()            — length of a repeated field
//             has_xxx()             — presence (message fields + optional)
//
// Serialise:  SerializeToString(&bytes)   — binary wire format
//             ParseFromString(bytes)      — deserialise
//             SerializeToFileDescriptor() / ParseFromFileDescriptor()
//
// Merge:      MergeFrom(other)  — set fields from other without clearing
//             CopyFrom(other)   — clear then MergeFrom
//             Clear()           — reset all fields to defaults
// ─────────────────────────────────────────────────────────────────────

using namespace demo;
using namespace google::protobuf;

int main()
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // ── Build a Person ────────────────────────────────────────────────
    Person alice;
    alice.set_id(1);
    alice.set_name("Alice");
    alice.set_age(30);
    alice.set_role(ROLE_ENGINEER);
    alice.set_email("alice@example.com");   // optional field

    // repeated string: add_tags() appends one element
    alice.add_tags("backend");
    alice.add_tags("grpc");
    alice.add_tags("hpc");

    // nested message: mutable_ returns pointer, allocating if needed
    alice.mutable_address()->set_street("1 Infinite Loop");
    alice.mutable_address()->set_city("Cupertino");
    alice.mutable_address()->set_country("US");

    // well-known type: Timestamp
    *alice.mutable_created_at() = util::TimeUtil::GetCurrentTime();

    // ── Read fields ───────────────────────────────────────────────────
    std::cout << "id:    " << alice.id()   << "\n";
    std::cout << "name:  " << alice.name() << "\n";
    std::cout << "role:  " << Role_Name(alice.role()) << "\n"; // ROLE_ENGINEER
    std::cout << "email: " << (alice.has_email() ? alice.email() : "(none)") << "\n";
    std::cout << "city:  " << alice.address().city() << "\n";
    std::cout << "has_address: " << std::boolalpha << alice.has_address() << "\n";

    std::cout << "tags:  ";
    for (const auto& t : alice.tags()) std::cout << t << " ";
    std::cout << "\n";

    // ── Serialise → bytes ─────────────────────────────────────────────
    std::string wire;
    alice.SerializeToString(&wire);
    std::cout << "\nserialized: " << wire.size() << " bytes\n";

    // ── Deserialise ───────────────────────────────────────────────────
    Person copy;
    copy.ParseFromString(wire);
    std::cout << "parsed name: " << copy.name() << "\n";
    std::cout << "parsed tags: " << copy.tags_size() << "\n";

    // ── Team (repeated sub-messages) ──────────────────────────────────
    Team team;
    team.set_name("Platform");
    *team.add_members() = alice;      // copy into repeated field

    Person bob;
    bob.set_id(2);
    bob.set_name("Bob");
    bob.set_role(ROLE_MANAGER);
    *team.add_members() = bob;

    std::cout << "\nteam: " << team.name()
              << " (" << team.members_size() << " members)\n";
    for (const auto& m : team.members())
        std::cout << "  " << m.id() << " " << m.name()
                  << " " << Role_Name(m.role()) << "\n";

    // ── MergeFrom: apply fields from one message onto another ─────────
    Person update;
    update.set_age(31);                   // only age is set
    update.add_tags("tbb");               // append to tags
    alice.MergeFrom(update);
    std::cout << "\nafter merge — age: " << alice.age()
              << " tags: " << alice.tags_size() << "\n"; // age=31, 4 tags

    google::protobuf::ShutdownProtobufLibrary();
}
