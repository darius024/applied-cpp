# gRPC & Protocol Buffers

Protocol Buffers (protobuf) is Google's binary serialisation format.
gRPC is Google's RPC framework built on top of protobuf and HTTP/2.
Together they are the de-facto standard for high-performance inter-service
communication in production systems at Google, Uber, Netflix, and most
hyperscalers.

---

## Install

```
macOS:  brew install grpc protobuf
Linux:  apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
```

---

## Build

This folder uses CMake — it handles proto code generation automatically:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Each proto file in `protos/` is compiled by `protoc` to `*.pb.cc`/`*.pb.h`
(message types) and `*.grpc.pb.cc`/`*.grpc.pb.h` (service stubs).

---

## Protocol Buffers

### Why binary over JSON / XML?

| | protobuf | JSON |
|---|---|---|
| Wire size | 5–10× smaller | verbose |
| Parse speed | 5–10× faster | slow |
| Schema | enforced at compile time | optional (JSON schema) |
| Versioning | field numbers are stable | field names must not change |
| Types | strong (uint32, sint64, float…) | weak (all numbers are float64) |

### .proto3 syntax

```protobuf
syntax = "proto3";
package myapp;

enum Status { UNKNOWN = 0; ACTIVE = 1; }

message Address { string city = 1; string country = 2; }

message Person {
  uint32          id      = 1;   // field numbers are the wire identity
  string          name    = 2;   // never reuse a deleted number
  repeated string tags    = 3;   // dynamic-length list
  optional string email   = 4;   // has_email() available
  Status          status  = 5;
  Address         address = 6;   // nested message; has_address() available
}
```

Field number rules:
- 1–15: encoded in 1 byte (use for frequent fields)
- 16–2047: 2 bytes
- Never reuse deleted numbers — add reserved declarations instead

### Code generation

```
protoc --proto_path=protos --cpp_out=gen protos/person.proto
```

Generates `person.pb.h` and `person.pb.cc`. For gRPC service stubs add:
```
--grpc_out=gen --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
```

### Arena allocation (file 03)

By default each `new Person` uses the global allocator. For servers
that create thousands of messages per request, `google::protobuf::Arena`
gives a bump allocator: all messages created from one arena are freed
in a single `delete`. No per-message `free()`. See `03_proto_arena.cpp`.

---

## gRPC

### Architecture

```
Client                             Server
 │                                   │
 │── HTTP/2 stream (protobuf) ──────▶│
 │                                   │  Service implementation
 │◀── HTTP/2 stream (protobuf) ───── │
```

`Channel`: a connection to a server address. Reuse channels; creating one
is expensive (TLS handshake, HTTP/2 setup).

`Stub`: generated client API. Created from a channel. Cheap to copy.

`ServerContext` / `ClientContext`: per-call metadata, deadlines, cancellation.

### Four RPC types

| Type | Client sends | Server sends | Use case |
|------|-------------|-------------|---------|
| Unary | 1 message | 1 message | Standard request/response |
| Server streaming | 1 message | N messages | Live feed, large result sets |
| Client streaming | N messages | 1 message | Batch upload, aggregation |
| Bidi streaming | N messages | N messages | Full-duplex: chat, monitoring |

### Status codes

`grpc::Status` wraps a `StatusCode` and a message string.
Common codes: `OK`, `NOT_FOUND`, `INVALID_ARGUMENT`, `DEADLINE_EXCEEDED`,
`CANCELLED`, `UNAVAILABLE`, `INTERNAL`.

The server returns `Status::OK` or a specific error; the client checks
`status.ok()` after each call.

### Sync vs Async server

**Sync** (files 04–07): one thread per concurrent RPC. Simple.
Good for ≤ a few hundred concurrent RPCs.

**Async** (file 08): one `CompletionQueue` + N handler threads.
A `CallData` object drives a state machine (`CREATE → PROCESS → FINISH`).
Scales to tens of thousands of concurrent RPCs. Used in all Google
production gRPC services.

### Interceptors (file 09)

Client and server interceptors fire at defined hook points
(`PRE_SEND_INITIAL_METADATA`, `POST_RECV_MESSAGE`, `PRE_SEND_STATUS`, …).
Used for: auth token injection, request logging, distributed tracing,
retry logic — all without modifying service implementation code.

### Deadlines and cancellation (file 10)

Every RPC should have a deadline. Set it on the `ClientContext` before
the call. The server receives it propagated on its `ServerContext`.
`ctx->IsCancelled()` lets the server short-circuit work early.
Deadline cancellations propagate through the entire call chain.
