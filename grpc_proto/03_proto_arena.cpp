#include "person.pb.h"
#include <google/protobuf/arena.h>
#include <iostream>
#include <vector>
#include <chrono>

// build: cmake -B build && cmake --build build --target 03_proto_arena

// ─────────────────────────────────────────────────────────────────────
// google::protobuf::Arena — bump-pointer allocator for messages.
//
// Without arena:
//   new Person() → global allocator → per-object free() on destruction
//   For a server handling 100k req/s creating 10 messages each,
//   that is 1M alloc/free pairs per second on the global heap.
//
// With arena:
//   Arena::CreateMessage<T>(&arena) → O(1) pointer bump
//   arena destructor → single deallocation regardless of object count
//   No per-object destructor called (protobuf messages are POD-like)
//
// Rules:
//   - Messages created on an arena MUST NOT be deleted manually.
//   - An arena-allocated message can only contain other arena-allocated
//     sub-messages from the SAME arena (set via mutable_ accessors).
//   - Arena is not thread-safe; use one arena per thread/request.
//   - string fields are stored in the arena when SetArenaOwned is used
//     (the default for arena-created messages).
//
// Typical pattern: one arena per incoming request, destroyed at the end.
// ─────────────────────────────────────────────────────────────────────

using namespace demo;
using namespace google::protobuf;

// Simulate a request handler: build N messages, process, discard.
void handle_request(Arena& arena, int n)
{
    for (int i = 0; i < n; ++i) {
        Person* p = Arena::CreateMessage<Person>(&arena);
        p->set_id(i);
        p->set_name("worker-" + std::to_string(i));
        p->set_role(ROLE_ENGINEER);
        p->add_tags("grpc");
        p->mutable_address()->set_city("London");
        (void)p; // in real code: process p here
    }
    // arena.Reset() frees everything and readies the arena for reuse
    arena.Reset();
}

int main()
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // ── Basic arena usage ─────────────────────────────────────────────
    {
        Arena arena;

        Person* alice = Arena::CreateMessage<Person>(&arena);
        alice->set_id(1);
        alice->set_name("Alice");
        alice->add_tags("backend");

        // Sub-messages allocated via mutable_* are also arena-owned
        alice->mutable_address()->set_city("New York");

        std::cout << "arena-owned: " << alice->name() << "\n";
        std::cout << "address: "     << alice->address().city() << "\n";

        // Build a Team whose members are arena-allocated
        Team* team = Arena::CreateMessage<Team>(&arena);
        team->set_name("Platform");
        *team->add_members() = *alice; // copy fields; new member is arena-owned

        std::cout << "team members: " << team->members_size() << "\n";

        // arena goes out of scope → all messages freed in one shot
    }

    // ── Arena reuse across requests ───────────────────────────────────
    // ArenaOptions lets you tune initial block size to reduce first-request
    // allocation overhead.
    ArenaOptions opts;
    opts.start_block_size  = 4096;
    opts.max_block_size    = 1024 * 1024; // 1 MB
    Arena arena(opts);

    const int REQUESTS = 1000;
    const int MSGS_PER_REQUEST = 20;

    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < REQUESTS; ++r)
        handle_request(arena, MSGS_PER_REQUEST);
    auto t1 = std::chrono::steady_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::cout << REQUESTS << " requests × " << MSGS_PER_REQUEST
              << " messages = " << REQUESTS * MSGS_PER_REQUEST << " msgs\n";
    std::cout << "total: " << us << " µs  ("
              << us / REQUESTS << " µs/req)\n";

    // ── SpaceUsed / SpaceAllocated ────────────────────────────────────
    // After Reset(), SpaceUsed is 0 but SpaceAllocated retains the
    // previously allocated blocks — reuse avoids any system call on
    // subsequent requests.
    handle_request(arena, 10);
    std::cout << "SpaceUsed:      " << arena.SpaceUsed()      << " bytes\n";
    std::cout << "SpaceAllocated: " << arena.SpaceAllocated() << " bytes\n";

    google::protobuf::ShutdownProtobufLibrary();
}
