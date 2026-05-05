# BSP (Byte Schema Protocol)

> [中文版](README-zh.md)  
> [Usage Doc](usage.md)

**A C++20 binary serialization library driven by protocol tags.**

BSP is a **single-header**, **zero-dependency
** (C++ standard library only) binary serialization library. Its core design philosophy: **types imply protocols, while
protocols do not intrude into types**.
---

## Features at a Glance

| Feature                         | Description                                                                                             |
|:--------------------------------|:--------------------------------------------------------------------------------------------------------|
| **Protocol-Tag Driven**         | Define serialization behavior externally via `Serializer<T, P>` partial specialization — zero intrusion |
| **Multiple Encoding Protocols** | `Fixed` (fixed-length), `Varint` (LEB128+ZigZag), `Trivial` (memcpy)                                    |
| **Full STL Support**            | `vector`, `string`, `map`, `set`, `optional`, `variant`, `unique_ptr`, and more                         |
| **Schema Versioning**           | Compile-time monotonic version validation + runtime version selection, forward-compatible               |
| **Multi-Layer Safety**          | Recursion depth limit, container size limit, three-level error policy (STRICT/MEDIUM/IGNORE)            |
| **Traceback Diagnostics**       | Automatically records the serialization call stack on exceptions, pinpoints error locations             |
| **Zero-Cost Abstractions**      | Endian conversion, traceback, etc. are all pruned at compile time — no runtime branches                 |
| **Custom Extensions**           | Custom protocols, custom serializers, custom I/O — everything is extensible                             |

---

## Quick Start

### Requirements

| Requirement      | Minimum                               |
|:-----------------|:--------------------------------------|
| **C++ Standard** | C++20 or later                        |
| **Compiler**     | GCC 11+ / Clang 14+ / MSVC 2022 17.0+ |
| **Dependencies** | C++ standard library only             |

### Installation

Copy `bsp.hpp` into your project and `#include` it. No build configuration needed.

### Example

```c++
#include "bsp.hpp"
#include <sstream>
#include <cassert>

// 1. Define data structure
struct Player {
    uint64_t id;
    std::string name;
    float score;
};

// 2. Register Schema (field order determines storage order)
BSP_SCHEMA_SET(Player,
    BSP_SCHEMA(
        BSP_FIELD(id),
        BSP_FIELD(name),
        BSP_FIELD(score)
    )
)

int main() {
    Player player{ 1001, "Alice", 98.5f };

    // 3. Serialize
    std::stringstream buffer;
    bsp::io::StreamWriter writer(buffer);
    bsp::write(writer, player);

    // 4. Deserialize
    buffer.seekg(0);
    bsp::io::StreamReader reader(buffer);
    auto loaded = bsp::read<Player>(reader);

    assert(loaded.id == 1001);
    assert(loaded.name == "Alice");
    assert(loaded.score == 98.5f);
}
```

---

## Core Concepts

### Protocol Tag

Protocol Tags are the core mechanism of BSP, located in the `bsp::proto` namespace. They **do not intrude into data
types** — serialization behavior is defined externally via `Serializer<T, P>` specializations.

**Encoding Protocols** (specify how values/containers are encoded):

| Tag         | Description                                                                                 |
|:------------|:--------------------------------------------------------------------------------------------|
| `Fixed<N>`  | Fixed-length encoding. When `N=0`, the size is deduced from the type itself                 |
| `Varint`    | Variable-length encoding. LEB128 + ZigZag for integers, Varint length prefix for containers |
| `Trivial`   | Direct memory copy. Trivially copyable types only, **no endian conversion**                 |
| `Schema<V>` | Compile-time schema version, `V` is the version number                                      |
| `DynSchema` | Runtime schema version selection                                                            |
| `Custom`    | Default protocol, for user-defined serialization                                            |

**Wrapper Protocols** (wrap other protocols):

| Tag                   | Description                                                             |
|:----------------------|:------------------------------------------------------------------------|
| `Default`             | Maps to the type's default protocol                                     |
| `Limited<Len, Inner>` | Limits read/write length, throws on overflow                            |
| `Forced<Len, Inner>`  | Enforces read/write length, throws on overflow, pads/skips on underflow |

### T-P Pair & Serializer

`(T, P)` is the smallest serializable unit in BSP, uniquely mapped to a `Serializer<T, P>` specialization:

```c++
// Interface signatures
Serializer<T, P>::write(io::Writer auto &w, const T &v, context &ctx);
Serializer<T, P>::read(io::Reader auto &r, T &out, context &ctx);
```

Convenient API:

```c++
bsp::write(writer, value);                    // Uses default protocol
auto v = bsp::read<T>(reader);

bsp::write<proto::Varint>(writer, value);     // Specify protocol
auto v = bsp::read<T, proto::Varint>(reader);
```

### Context

`context` is the only runtime environment input during serialization:

```c++
struct context {
    safety sf;   // Defensive config: max depth, container size, string size, error policy
    option opt;  // Functional config: target schema version (used by DynSchema)
    status st;   // Call-level status: current recursion depth (auto-managed)
};
```

---

## Supported Encoding Formats

### Primitive Types

| Type              | Default Protocol | Format                                   |
|:------------------|:-----------------|:-----------------------------------------|
| `bool`            | `Fixed<>`        | Single byte `[0x00 / 0x01]`              |
| Integers          | `Fixed<>`        | Fixed-length bytes, big-endian           |
| Integers (Varint) | —                | LEB128 + ZigZag                          |
| Floating point    | `Fixed<>`        | IEEE 754 byte representation, big-endian |

### Container Types

| Type              | Default Protocol | Format                                     |
|:------------------|:-----------------|:-------------------------------------------|
| `std::string`     | `Varint`         | `[LEB128 length][data]`                    |
| `types::bytes`    | `Varint`         | Same as above (high-performance byte copy) |
| `std::vector<T>`  | `Varint`         | `[LEB128 length][elements]`                |
| `std::map<K,V>`   | `Varint`         | `[LEB128 length][K1][V1][K2][V2]...`       |
| `std::set<T>`     | `Varint`         | `[LEB128 length][elements]`                |
| `std::array<T,N>` | `Fixed<>`        | `[elements]` (no length prefix)            |
| `std::bitset<N>`  | `Fixed<>`        | `[ceil(N/8) bytes]` (bit-packed)           |

### Structured & Variant Types

| Type                   | Default Protocol | Format                             |
|:-----------------------|:-----------------|:-----------------------------------|
| `std::pair<T1,T2>`     | `Fixed<>`        | `[First][Second]`                  |
| `std::tuple<Ts...>`    | `Fixed<>`        | `[Elem1][Elem2]...`                |
| `std::optional<T>`     | `Varint`         | `[presence flag][T (if present)]`  |
| `std::variant<Ts...>`  | `Varint`         | `[Varint index][T]`                |
| `T*` / `unique_ptr<T>` | `Varint`         | `[presence flag][T (if non-null)]` |

### Schema Structs

Structs registered via `BSP_SCHEMA_SET` use `Schema<SIZE_MAX>` as the default protocol (matches the highest version):

```
[Field 1][Field 2]...   ← field order matches registration order
```

---

## Schema Versioning

### Registering Multiple Versions

```c++
BSP_SCHEMA_SET(Message,
    BSP_SCHEMA_V(1,
        BSP_FIELD(content),
        BSP_FIELD(sender_id)
    ),
    BSP_SCHEMA_V(2,
        BSP_FIELD(content),
        BSP_FIELD(sender_id),
        BSP_FIELD(timestamp)    // new field in v2
    )
)
```

- Version numbers must be **monotonically increasing** (validated at compile time via `consteval`)
- Version matching rule: selects the **largest version ≤ target version**
- Field protocol override: `BSP_FIELD_P(field_name, proto::Varint)`

### Compile-Time Version Selection

```c++
bsp::write<proto::Schema<1>>(writer, msg);
auto v1 = bsp::read<Message, proto::Schema<1>>(reader);
```

### Runtime Version Selection

```c++
context ctx;
ctx.opt.target_schema_version = 2;
bsp::write<proto::DynSchema>(writer, msg, ctx);
```

---

## I/O Interface

BSP defines I/O interfaces via C++20 Concepts. Any type satisfying these concepts can be used as a serialization target:

```c++
template<typename R>
concept Reader = requires(R r, uint8_t* buf, std::streamsize n) {
    { r.read_bytes(buf, n) } -> std::same_as<void>;
    { r.read_byte() }        -> std::same_as<uint8_t>;
};

template<typename W>
concept Writer = requires(W w, const uint8_t* buf, std::streamsize n, uint8_t b) {
    { w.write_bytes(buf, n) } -> std::same_as<void>;
    { w.write_byte(b) }       -> std::same_as<void>;
};
```

### Built-in Implementations

| Type                              | Description                                                   |
|:----------------------------------|:--------------------------------------------------------------|
| `StreamWriter` / `StreamReader`   | Wraps `std::ostream` / `std::istream`                         |
| `BufferWriter` / `BufferReader`   | In-memory I/O backed by `std::vector<uint8_t>`                |
| `BytesReader`                     | Read-only I/O backed by a raw memory pointer                  |
| `LimitedWriter` / `LimitedReader` | Limits the number of readable/writable bytes                  |
| `AnyWriter` / `AnyReader`         | Type-erased (virtual dispatch), for polymorphic serialization |

---

## Safety & Debugging

### Three-Level Error Policy

| Policy   | Behavior                                                    |
|:---------|:------------------------------------------------------------|
| `STRICT` | Strict mode: throws on any anomaly                          |
| `MEDIUM` | Medium mode: tolerates some recoverable anomalies (default) |
| `IGNORE` | Ignore mode: skips anomalies silently                       |

### Traceback Call Stack

With `enable_traceback = true`, the serialization path is automatically recorded on exceptions:

```
[bsp::string_too_large] string size 46 larger than limit=10 bytes
Traceback:
  - [ROOT] | std::vector, Varint
    (length=3)
  - Elem 2 | User, Schema<MAX>
    (exact version 0)
  @ std::optional
  - Field "username" | std::string, Varint
    (length=46)
  ^ Error Here
```

---

## Version Branches

| Version           | Description                                                                             |
|:------------------|:----------------------------------------------------------------------------------------|
| `bsp_lite.hpp`    | Core functionality only, fast compilation, minimal runtime overhead, no runtime options |
| `bsp.hpp`         | Full version with all stable features, recommended as default                           |
| `bsp_nightly.hpp` | Experimental version with macro-controlled feature toggles and experimental features    |

---

## Custom Extensions

### Custom Serializer

```c++
struct MyCustomType { /* ... */ };
struct MyCustomProto {};

template<>
struct bsp::serialize::Serializer<MyCustomType, MyCustomProto> {
    static void write(io::Writer auto& w, const MyCustomType& v, context &ctx) {
        // implement write logic
    }
    static void read(io::Reader auto& r, MyCustomType& out, context &ctx) {
        // implement read logic
    }
};
```

### Custom Default Protocol

```c++
BSP_DEFAULT_PROTO(int32_t, bsp::proto::Varint);
```

### In-Class Registration

```c++
struct MyStruct {
    using default_protocol = proto::Varint;

    static void write(io::Writer auto &w, const MyStruct &v, context &ctx, proto::Varint);
    static void read(io::Reader auto &r, MyStruct &out, context &ctx, proto::Varint);
};
```

---

## Comparison with JSON

| Aspect                   | JSON                                             | BSP                                          |
|:-------------------------|:-------------------------------------------------|:---------------------------------------------|
| Integer `123456789`      | 9 bytes                                          | 4 bytes (Fixed) or fewer (Varint)            |
| Floating-point precision | String conversion may lose precision             | Exact byte copy                              |
| Binary data              | Requires Base64 encoding, size bloat             | Raw bytes written directly                   |
| Parsing overhead         | Must handle escaping, Unicode, whitespace        | Fixed layout or length-prefixed, unambiguous |
| Type safety              | Runtime null checks and type validation required | Determined at compile time                   |

### Scenario Recommendations

| Scenario               | Recommended    | Reason                                               |
|:-----------------------|:---------------|:-----------------------------------------------------|
| Network communication  | BSP            | Cross-platform, attack-resistant, version-compatible |
| Cross-platform storage | BSP / Protobuf | High density, unified endianness                     |
| Public API             | JSON           | Better compatibility                                 |
| Configuration files    | JSON / TOML    | Human readability first                              |
| Local cache            | BSP Trivial    | memcpy optimization, high performance                |

---

## FAQ

**Q: Why does `std::unordered_map` have a different element order after deserialization?**

A: `unordered_map` is fundamentally an unordered hash-based container. Its iteration order is determined by the hash
function and bucket distribution. Serialization writes in iteration order, and deserialization re-inserts in write
order. If you need to preserve order, use `std::vector<std::pair<K, V>>` instead.

**Q: How do I safely serialize pointer-based graph structures (e.g., trees, linked lists)?**

A: BSP's default handling of pointers is "deep copy by value", which **cannot handle circular references**. It is
recommended to pool objects and write object IDs instead of raw pointers during serialization, then rebuild the
reference relationships during deserialization.

**Q: How do I achieve cross-language compatibility?**

A: BSP is heavily dependent on C++ features and does not directly provide cross-language support. You can implement
language-independent protocols (`Fixed<>`, `Varint`, `Schema<>`) yourself, or write an IDL compiler. If cross-language
support is a core requirement, Protobuf or Cap'n Proto are recommended instead.

**Q: What is the difference between `types::bytes` and `std::vector<char>`?**

A: `types::bytes` is an alias for `std::vector<uint8_t>`. On platforms where `char` is signed, `types::bytes` uses
high-performance byte copy implementations under `Varint`, `Fixed<N>`, and `Trivial` protocols, while
`std::vector<char>` only uses byte copy under the `Trivial` protocol.

---

## License

MIT License