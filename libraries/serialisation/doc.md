# Zero-Copy Serialisation: FlatBuffers & Cap'n Proto

Traditional serialisers (protobuf, JSON, msgpack) decode the byte stream
into a heap-allocated object graph before any field can be read — a full
parse on every message. On hot paths this dominates latency and allocation.

Zero-copy serialisers lay data out in memory exactly as it will be accessed.
The reader works directly on the raw buffer — no allocation, no parse step.

---

## Install

```
macOS:  brew install flatbuffers capnp
Linux:  apt install libflatbuffers-dev capnproto libcapnp-dev
```

---

## Comparison

| | FlatBuffers | Cap'n Proto |
|---|---|---|
| Parse step | none | none |
| Schema | `.fbs` → `flatc --cpp` → `_generated.h` | `.capnp` → `capnp compile -oc++` → `.h + .c++` |
| Read cost | O(1) field access via offsets | O(1) field access via offsets |
| Write cost | builder accumulates offsets, writes one block | builder writes directly into final layout |
| Mutable in-place | no (immutable after `Finish()`) | yes (mutation API) |
| RPC support | no | yes (Cap'n Proto RPC / KJ async) |
| Schemaless variant | FlexBuffers | none |
| Best for | read-heavy, game state, IPC | IPC, mutable messages, RPC |

---

## FlexBuffers (file 01)

FlatBuffers' schemaless sibling. Build any map/vector/scalar tree with
`flexbuffers::Builder`; read via `flexbuffers::GetRoot()` which returns
a `Reference` that addresses the original buffer with no copy or alloc.
No schema or code generation required.

---

## Schema-based FlatBuffers (file 02)

Write a `.fbs` schema, run `flatc --cpp` to generate a C++ header.
The builder collects strings/vectors/tables then writes a single contiguous
block. The verifier checks alignment and bounds before the first read.
Forward/backward compatibility: new fields added at the end are safe.

---

## Cap'n Proto (file 03)

The wire format IS the in-memory format — there is no serialise step,
only a copy to the destination buffer. `MallocMessageBuilder` owns a
contiguous block; `messageToFlatArray` produces a word-aligned byte array
you can pass directly to `sendmsg`. The reader wraps a byte pointer with
zero allocation and zero parsing.
