# BSP User Manual

> [中文版](usage-zh.md)

## 0. Overview

### 0.1 System Requirements

BSP is a modern C++ single-header library with the following basic requirements for the compilation environment:

| Requirement        | Minimum                                     |
|:-------------------|:--------------------------------------------|
| **C++ Standard**   | C++20 or later                              |
| **Compiler**       | GCC 11+ / Clang 14+ / MSVC 2022 17.0+       |
| **Build System**   | None specific; simply copy `bsp.hpp` into your project and `#include` it |
| **Dependencies**   | C++ Standard Library only                   |

The core implementation of the library depends on the following C++20 features:

- **Concepts**: Used to define the `io::Reader` and `io::Writer` interface constraints, as well as to perform compile-time type checking.
- **`std::endian`**: Used to detect system endianness, combined with `constexpr` branches to eliminate dead code at compile time.
- **`std::bit_cast`**: Used to safely convert between floating-point and integer representations.
- **`consteval`**: Used to verify monotonically increasing Schema version numbers at compile time.

> **Note**: If the target platform does not support C++20, the code will fail to compile.

---

### 0.2 Scope of Application

BSP is a binary serialization protocol. Compared with text formats such as JSON:

| Comparison               | JSON              | BSP                                   |
|:-------------------------|:------------------|:--------------------------------------|
| Integer `123456789`      | 9 bytes           | 4 bytes (Fixed) or fewer (Varint)     |
| Floating-point precision | String conversion may lose precision | Exact byte copy |
| Binary data              | Requires Base64 encoding, bloated     | Raw bytes written directly |
| Parsing overhead         | Needs escape, Unicode, whitespace handling | Fixed layout or length-prefixed, unambiguous |
| Type safety              | Requires runtime null-checking, type checking | Determined at compile time |

| Scenario              | Recommended          | Reason                             |
|:----------------------|:---------------------|:-----------------------------------|
| Network communication | BSP                  | Cross-platform, attack-resistant, version-compatible |
| Cross-platform storage| BSP / Protobuf (multi-platform) | High density, unified endianness |
| Public API            | JSON                 | Better compatibility               |
| Configuration files   | JSON/TOML            | Human readability prioritized      |
| Local cache           | BSP Trivial          | memcpy optimization, high performance |

---

### 0.3 Quick Start

The following example demonstrates the most basic serialization and deserialization workflow. Suppose you need to save a struct containing an integer and a string to a file or memory.

**Step 1: Include the header and define the data structure**

```c++
#include "bsp.hpp"
#include <sstream>

struct Player {
    uint64_t id;
    std::string name;
    float score;
};

// Register Schema (the order of fields determines the storage order)
BSP_SCHEMA_SET(Player,
    BSP_SCHEMA(
        BSP_FIELD(id),
        BSP_FIELD(name),
        BSP_FIELD(score)
    )
)

// Note: BSP_SCHEMA_SET already registers the default protocol for Player as proto::Schema<>, so no additional BSP_DEFAULT_PROTO is needed
```

**Step 2: Serialization (Writing)**

```c++
int main() {
    Player player{ 1001, "Alice", 98.5f };

    // Use a standard library string stream as the output
    std::stringstream buffer;
    bsp::io::StreamWriter writer(buffer);

    // Write with a single line (using the default context)
    bsp::write(writer, player);

    // buffer now contains the serialized binary data
    return 0;
}
```

**Step 3: Deserialization (Reading)**

```c++
    // Continuing from the example above, read from the same buffer
    buffer.seekg(0);
    bsp::io::StreamReader reader(buffer);

    auto loaded_player = bsp::read<Player>(reader);

    // The content of loaded_player will be identical to player
    assert(loaded_player.id == 1001);
    assert(loaded_player.name == "Alice");
    assert(loaded_player.score == 98.5f);
```

Now you have learned the basic usage of BSP. The following chapters will elaborate on various protocol tags, I/O interfaces, Schema version management, and advanced customization features.

---

### 0.4 Version Differences (lite/bsp/nightly)

`bsp_lite.hpp`: Core functionality only, does not support runtime options-based adjustments. Fast compilation, low runtime overhead. Essentially no iteration.  
`bsp.hpp`: Full version, supports all stable features, occasionally updated with new functionalities, the most practical choice, recommended as default.  
`bsp_nightly.hpp`: Experimental version, provides macro-controlled feature toggles and experimental features, without stability guarantees.

The following table shows capability differences across versions:

| Section  | Feature             | lite | bsp | nightly |
|:---------|:--------------------|:----:|:---:|:--------:|
| -        | Basic serialization | ✅   | ✅  | ✅       |
| 1.3.1    | safety              | ✅   | ✅  | ✅       |
| 1.3.2    | options             | ❌   | ✅  | ✅+      |
| 3.3      | Limited I/O         | ❌   | ✅  | ✅       |
| 3.4      | Any I/O             | ❌   | ✅  | ✅       |
| 4.2      | CVal                | ❌   | ✅  | ✅       |
| 5.3.2    | DynSchema           | ❌   | ✅  | ✅       |
| 6.2      | Limited & Forced    | ❌   | ✅  | ✅       |
| 8.1      | traceback           | ❌   | ✅  | ✅       |
| 9.2      | Experimental features | ❌ | ❌  | ✅       |

---

## 1. Concepts

**Compile-Time Concepts:**

### 1.1 Protocol Tag

The core mechanism of BSP, used to specify how data is serialized, residing within the `bsp::proto` namespace.  
BSP advocates that **types are protocols**, while also maintaining that **protocols should not intrude upon types**; hence the introduction of the **protocol tag** as a central mechanism.

They are primarily divided into two categories:

**Encoding Protocol Tags:**

| Tag           | Purpose                                                            |
|:--------------|:-------------------------------------------------------------------|
| `Custom`      | Default protocol. Used for custom serialization.                   |
| `Fixed<N>`    | Fixed-length encoding. When `N=0`, the size is determined by the type itself. |
| `Varint`      | Variable-length encoding. LEB128 + ZigZag for integers; Varint length-prefix for containers. |
| `Trivial`     | Direct memory copy. Only for trivially copyable types, **no endianness conversion**; see 6.1. |
| `Schema<V>`   | Compile-time Schema, with `V` as version number; see 5.3.1.        |
| `DynSchema`   | Runtime Schema version selection; see 5.3.2 **[non-lite]**.        |

For **value types**, these protocols specify how the value itself should be encoded.  
For **container types**, these protocols specify how the container should accommodate its child elements, without specifying the encoding of the child elements themselves. Child elements are encoded using their default protocols.

**Decorator Protocol Tags:**

| Tag                       | Purpose                                                                         |
|:--------------------------|:--------------------------------------------------------------------------------|
| `Default`                 | Maps to the class's default protocol.                                           |
| `Limited<Len, Inner>`     | Limits read/write length; throws an exception if exceeded; see 6.2.1 **[non-lite]**. |
| `Forced<Len, Inner>`      | Enforces read/write length; throws on overflow, pads with zeros or skips on underflow; see 6.2.2 **[non-lite]**. |

Any decorator tag should only create a `Serializer<T, Wrapper>` specialization and should not know the concrete type of `T`.

All decorator tags inherit from `proto::WrapperProto`.

---

### 1.2 T-P Pair & Serializer

A `T-P` pair is the smallest serializable unit in BSP and uniquely corresponds to a `serialize::Serializer<T, P>` specialization.  
`serialize::Serializer<T, P>` defines the read/write implementation for a `T-P` pair:

```c++
// Interface signature (concept-constrained)
Serializer<T, P>::write(io::Writer auto &w, const T &v, context &ctx);
Serializer<T, P>::read(io::Reader auto &r, T &out, context &ctx);
```

The convenience API wraps the invocation of Serializer:

```c++
// Using the default protocol
bsp::write(writer, value);        // Equivalent to Serializer<T, DefaultProtocol<T>>::write
auto v = bsp::read<T>(reader);    // Equivalent to Serializer<T, DefaultProtocol<T>>::read

// Specifying a protocol
bsp::write<proto::Varint>(writer, value);
auto v = bsp::read<T, proto::Varint>(reader);
```

Each `T` corresponds to a `proto::DefaultProto<T>`, which specifies the default protocol selected for that type:

```c++
namespace proto {
    template<typename T>
    struct DefaultProtocol {
        using type = Custom;  // Default: requires user specialization
    };
}
```

| Category              | Type                                      | Protocol       |
|:----------------------|:------------------------------------------|:---------------|
| **Data Types**        | `bool`                                    | `Fixed<0>`     |
|                       | Integers and floating-point numbers        | `Fixed<0>`     |
| **Container Types**   | `std::string`                             | `Varint`       |
|                       | `types::bytes` (`std::vector<uint8_t>`)    | `Varint`       |
|                       | `std::vector<T>`                          | `Varint`       |
|                       | `std::bitset<N>`                          | `Fixed<0>`     |
|                       | `std::map<K, V>` & `unordered_map<K, V>`  | `Varint`       |
|                       | `std::set<T>` & `unordered_set<T>`        | `Varint`       |
|                       | `std::array<T, N>`                        | `Fixed<0>`     |
| **Structured Types**  | `std::pair<T1, T2>`                       | `Fixed<0>`     |
|                       | `std::tuple<Ts...>`                       | `Fixed<0>`     |
|                       | Structs registered via `BSP_DEFAULT_SCHEMA_SET()` | `Schema<MAX>` |
| **Trivially Copyable** | Non-pointer, single-byte trivially copyable types | `Trivial` |
| **Variant Types**     | `std::optional<T>`                        | `Varint`       |
|                       | `std::variant<Ts...>`                     | `Varint`       |
| **Pointers**          | `T*`                                      | `Varint`       |
|                       | `std::unique_ptr<T>`                      | `Varint`       |
| **Protocol-Specified**| `PVal`                                    | `Custom`       |
|                       | `CVal`                                    | `Custom`       |
| **Others**            | Types that do not match any of the above  | `Custom`       |

> For types that only support `Fixed<0>`, the `0` does not mean nothing is written; rather, the length is determined by the type itself.  
> `Default` can automatically map to the `DefaultProtocol_t<T>` protocol.

---

### 1.3 Compile-Time Configuration

Endianness and `traceback` enabling are controlled by `constexpr` variables.

```c++
static constexpr inline auto endian = std::endian::big;
static constexpr inline bool enable_traceback = true;   // [non-lite]
```

---

**Runtime Concepts:**

### 1.4 Context

`context` is the sole runtime environment input in BSP. It contains all the configuration and state information needed during serialization.

```c++
struct context {
   safety sf;   // Defensive configuration
   option opt;  // Functional configuration [non-lite]
   status st;   // Call-level status
};
```

The function `context::get_default_context()` constructs a `context` object with default behavior.  
**Different threads must not share the same context.**

#### 1.4.1 Safety

`safety` controls the **safety checks** during serialization:

```c++
struct safety {
   size_t max_depth;            // Maximum recursion depth, default 256
   size_t max_container_size;   // Maximum number of container elements, default 1M
   size_t max_string_size;      // Maximum string size in bytes, default 4MB
   errors::error_policy policy; // Error policy
};
```

There are three error policies:

| Policy      | Behavior                                  |
|:------------|:------------------------------------------|
| `STRICT`   | Strict mode: throws on any exception.     |
| `MEDIUM`   | Moderate mode: tolerates some recoverable exceptions (default). |
| `IGNORE`   | Ignore mode: skips exceptions silently.   |

The static variable `safety::default_safety` specifies the default defensive configuration, which can be modified at runtime.

#### 1.4.2 Options [non-lite]

`option` controls **functional choices** during serialization:

```c++
struct option {
    size_t target_schema_version; // Runtime target Schema version, default SIZE_MAX
};
```

`target_schema_version` is used by the `DynSchema` protocol to decide which Schema version to apply at runtime. See 5.3.2 for details.

The static variable `option::default_option` specifies the default functional configuration, which can be modified at runtime.

#### 1.4.3 Status

`status` records the state of a **single** serialization call:

```c++
struct status {
    size_t depth; // Current recursion depth
};
```

`depth` is automatically managed by `scope_guard` and is used to detect whether the recursion depth exceeds `safety::max_depth`.

---

## 2. Serialization — Primitive and STL Types

This section describes the concrete behavior of serialization. For the concept of serializers, see 1.2; for custom serializers, see 7.1.

> Types that contain child elements always read child elements using their default protocols.  
> Bold protocols in the tables indicate the default protocol.  
> To specify a protocol explicitly, you may use `types::PVal`; see Chapter 4.1 for details.

> Unless otherwise stated, serialization does not count toward recursion depth.

---

### 2.1 Primitive Data Types

#### 2.1.1 bool

| Protocol        | Structure                | Endianness | Notes |
|:----------------|:-------------------------|:-----------|:------|
| **Fixed\<>**   | Single byte `[0x00 / 0x01]` | N/A        |       |

> Under the `STRICT` policy, reading a value other than 0 or 1 will throw `invalid_bool`.

#### 2.1.2 Integers

| Protocol        | Structure                | Endianness | Notes                    |
|:----------------|:-------------------------|:-----------|:-------------------------|
| **Fixed\<>**   | Corresponding bytes `[Int]` | Relevant   |                          |
| Varint          | `[LEB128 encoded result]`  | N/A        | Signed integers use ZigZag encoding |

> **Varint protocol**: Under non-`IGNORE` policy, reading an excessively long value will throw `varint_overflow`.

#### 2.1.3 Floating Point

**Fixed\<>**

| Protocol        | Structure                | Endianness | Notes                              |
|:----------------|:-------------------------|:-----------|:-----------------------------------|
| **Fixed\<>**   | Corresponding bytes `[Float]` | Relevant   | Only supports IEEE 754 floating-point numbers |

---

### 2.2 Container Types

#### 2.2.1 std::string

**Varint**

| Protocol         | Structure                         | Notes |
|:-----------------|:----------------------------------|:------|
| **Varint**      | `[LEB128 length prefix][bytes]`   |       |
| Fixed\<N>        | `[N bytes]`                       |       |

> **Varint protocol**: Under non-`IGNORE` policy, reading a length exceeding `max_string_size` will throw `string_too_large`.  
> **Fixed\<N> protocol**: Writing a length that does not match N will throw `fixed_size_mismatch`.

#### 2.2.2 types::bytes (std::vector<uint8_t>)

Behaves identically to `std::string`.

#### 2.2.3 std::vector\<T>

Counts toward recursion depth.

| Protocol         | Structure                              | Notes              |
|:-----------------|:---------------------------------------|:-------------------|
| **Varint**      | `[LEB128 length prefix][Elem1][Elem2]...` |                    |
| Fixed\<N>        | `[Elem1][Elem2]...`                    |                    |
| Trivial          | `[LEB128 length prefix][corresponding bytes]` | See section 6.1.1 |

> **Varint protocol**: Under non-`IGNORE` policy, reading a length exceeding `max_container_size` will throw `container_too_large`.  
> **Fixed\<N> protocol**: Writing a length that does not match N will throw `fixed_size_mismatch`.

#### 2.2.4 std::vector\<bool>

**This implementation does NOT enable bit compression.  
If needed, use `std::bitset<N>` or `std::vector<bool>` with the `Trivial` protocol (see section 6.1.1).**

Each bool occupies 1 byte; all other behaviors are identical to `std::vector<T>`.

> Under the `STRICT` policy, reading a value other than 0 or 1 will throw `invalid_bool`.

#### 2.2.5 std::bitset\<N>

**This implementation enables bit compression.**

| Protocol         | Structure                 | Notes                       |
|:-----------------|:--------------------------|:----------------------------|
| **Fixed\<>**    | `[ceil(N/8) bytes]`       | Little-endian, protocol is `Fixed<0>` |

#### 2.2.6 std::map<K, V> & unordered_map<K, V>

Counts toward recursion depth.

| Protocol         | Structure                               | Notes |
|:-----------------|:----------------------------------------|:------|
| **Varint**      | `[LEB128 length prefix][K1][V1][K2][V2]...` |       |
| Fixed\<N>        | `[K1][V1][K2][V2]...`                   |       |

> **Varint protocol**: Under non-`IGNORE` policy, reading a length exceeding `max_container_size` will throw `container_too_large`.  
> **Fixed\<N> protocol**: Writing a length that does not match N will throw `fixed_size_mismatch`.

#### 2.2.7 std::set\<T> & unordered_set\<T>

Counts toward recursion depth.

| Protocol         | Structure                              | Notes |
|:-----------------|:---------------------------------------|:------|
| **Varint**      | `[LEB128 length prefix][Elem1][Elem2]...` |       |

> Under non-`IGNORE` policy, reading a length exceeding `max_container_size` will throw `container_too_large`.

#### 2.2.8 std::array<T, N>

Counts toward recursion depth.

| Protocol         | Structure                    | Notes            |
|:-----------------|:-----------------------------|:-----------------|
| **Fixed\<>**    | `[Elem 1][Elem 2]...`        | Protocol is `Fixed<0>` |
| Trivial          | `[corresponding bytes]`      | See section 6.1.1 |

---

### 2.3 Structured Types

#### 2.3.1 std::pair<T1, T2>

Counts toward recursion depth.

| Protocol         | Structure                 | Notes |
|:-----------------|:--------------------------|:------|
| **Fixed\<>**    | `[First][Second]`         |       |

#### 2.3.2 std::tuple<Ts...>

Counts toward recursion depth.

| Protocol         | Structure                    | Notes            |
|:-----------------|:-----------------------------|:-----------------|
| **Fixed\<>**    | `[Elem1][Elem2]...`          | Protocol is `Fixed<0>` |

---

### 2.4 Variant Types

#### 2.4.1 std::optional\<T>

| Protocol         | Structure                          | Notes |
|:-----------------|:-----------------------------------|:------|
| **Varint**      | `[Bool presence flag]([T, if present])` |       |

> Under the `STRICT` policy, reading a presence flag other than 0 or 1 will throw `invalid_bool`.

#### 2.4.2 std::variant\<Ts...>

| Protocol         | Structure                 | Notes |
|:-----------------|:--------------------------|:------|
| **Varint**      | `[Varint index][T]`       |       |

> Writing a valueless variant will throw `invalid_index`.  
> Reading an index that does not match any alternative will throw `invalid_index`.

---

### 2.5 Pointers

Counts toward recursion depth.  
**Copies the value pointed to by the pointer**. When disabling recursion depth limits, ensure the structure is not cyclic, or an infinite loop may occur.

| Protocol         | Structure                   | Notes                                 |
|:-----------------|:----------------------------|:--------------------------------------|
| **Varint**      | `[Bool presence flag][T]`   | A presence flag of 0x00 indicates a null pointer |

> Under the `STRICT` policy, reading a presence flag other than 0 or 1 will throw `invalid_bool`.

#### 2.5.1 T*

Memory must be managed manually.

```c++
MyStruct* s = read<>(reader);

delete s;
```

#### 2.5.2 std::unique_ptr\<T>

Memory is managed automatically by the smart pointer.

---

## 3. I/O Interfaces

### 3.1 Reader & Writer Concepts

BSP defines its I/O interfaces through C++20 Concepts. Any type satisfying the following concepts can serve as a serialization target or source:

```c++
template<typename R>
concept Reader = requires(R r, uint8_t* buf, std::streamsize n) {
    { r.read_bytes(buf, n) } -> std::same_as<void>;  // Read n bytes into buf
    { r.read_byte() }        -> std::same_as<uint8_t>; // Read and return 1 byte
};

template<typename W>
concept Writer = requires(W w, const uint8_t* buf, std::streamsize n, uint8_t b) {
    { w.write_bytes(buf, n) } -> std::same_as<void>;  // Write n bytes
    { w.write_byte(b) }       -> std::same_as<void>;  // Write 1 byte
};
```

---

### 3.2 General-Purpose I/O Interfaces

BSP provides three built-in I/O implementations:

#### StreamReader / StreamWriter

Wraps `std::istream` / `std::ostream`, suitable for files, stringstreams, etc.:

```c++
std::stringstream ss;
io::StreamWriter writer(ss);
io::StreamReader reader(ss);

// Writing to a file
std::ofstream file("data.bin", std::ios::binary);
io::StreamWriter file_writer(file);
```

#### BufferReader / BufferWriter

Memory-based I/O using `std::vector<uint8_t>`:

```c++
io::BufferWriter writer;
write(writer, value);  // Data is written to writer.buf

io::BufferReader reader(writer.buf);
auto v = read<T>(reader);
```

`BufferReader` stores a reference to `buf`, so the above code can form a pipeline.

`BufferWriter`'s `buf` is a public member and can be directly accessed or moved:

```c++
std::vector<uint8_t> data = std::move(writer.buf);
```

#### BytesReader

A read-only I/O based on raw memory pointers, suitable for reading from network buffers or pre-allocated memory:

```c++
uint8_t buffer[1024];
// ... fill buffer ...
io::BytesReader reader(buffer, sizeof(buffer));
auto v = read<T>(reader);
```

---

### 3.3 Byte-Limited I/O: Limited I/O [non-lite]

`LimitedReader` and `LimitedWriter` impose byte-count limits on read/write operations:

```c++
io::BufferWriter bw;
io::LimitedWriter lw(bw, 100);  // Write at most 100 bytes
// ... serialization operations ...
lw.pad_zero();  // Pad unused bytes with 0x00

io::BufferReader br(bw.buf);
io::LimitedReader lr(br, 100);  // Read at most 100 bytes
// ... deserialization operations ...
lr.skip_remaining();  // Skip unread bytes
```

> **Reading**: Exceeding the limit throws `unexpected_eof`.  
> **Writing**: Exceeding the limit throws `fixed_size_mismatch` (only `Fixed<N>`).

> **See also:** Section 6.2 introduces the protocol-level `Limited` and `Forced`, which are implemented based on the I/O-level `LimitedReader`/`LimitedWriter` described here.

---

### 3.4 Type Erasure: Any I/O [non-lite]

`AnyReader` and `AnyWriter` achieve type erasure through virtual functions, unifying any Reader/Writer satisfying the concept into a single type:

```c++
io::StreamReader sr(ss);
io::LimitedReader lr(sr, 100);

io::AnyReader any_r1(sr);   // Erase StreamReader
io::AnyReader any_r2(lr);   // Erase LimitedReader — same type as any_r1
```

`AnyReader` provides a `reader_type()` method that returns an enum identifier of the original type, useful for runtime type identification.

> **Note**: Virtual function calls incur runtime overhead. For typical environments, `concept auto` is recommended over `AnyReader`. `AnyReader`/`AnyWriter` are primarily intended for use with `CVal` (section 4.2) to enable polymorphic serialization.

---

## 4. Protocol Override Types

This chapter covers the types provided by `bsp::types`. For registering serialization methods within type declarations, see 7.3.

### 4.1 Protocol Override / PVal

`types::PVal<T, P>` provides the ability to temporarily override the default protocol of type `T` with protocol `P`.

**Decorator protocol tags** apply directly to `PVal`, with the same effect as if applied to the inner element.  
**Encoding protocol tags** have no effect on `PVal`.

```c++
int value = 0x1234;
types::PVal<int, proto::Varint> wrapped{value};   // Force use of Varint

// Serialize wrapped to encode the int using Varint
std::stringstream ss;
io::StreamWriter writer(ss);
write(writer, wrapped);
```

`PVal` supports implicit conversion to `T&` and `T*`, and can be used like a normal value:

```c++
int x = *wrapped;   // Dereference
wrapped->...        // If T is a class type, call members
```

> Implicit conversions may cause issues; it is recommended to use `pval.value` to access the value.

---

### 4.2 Runtime Polymorphism / CVal [non-lite]

`types::CVal` is an abstract base class for implementing runtime polymorphic serialization. Derived classes must implement two pure virtual functions:

```c++
class Animal : public types::CVal {
public:
    std::string species;
    int age;

    void write(bsp::io::AnyWriter& w, context &ctx) const override {
        serialize::DefaultSerializer<std::string>::write(w, species, ctx);
        serialize::DefaultSerializer<int>::write(w, age, ctx);
    }

    void read(bsp::io::AnyReader& r, context &ctx) override {
        serialize::DefaultSerializer<std::string>::read(r, species, ctx);
        serialize::DefaultSerializer<int>::read(r, age, ctx);
    }
};
```

`io::AnyReader` & `AnyWriter` provide type erasure, enabling `types::CVal` to accept any object satisfying the `Reader`/`Writer` concepts. See section 2.4 for details.

> Virtual function calls incur significant
> runtime overhead. It is recommended to avoid `types::CVal` unless necessary.

---

## 5. Schema and Version Management

### 5.1 Registration

Use the `BSP_SCHEMA_SET` macro to register a struct's serialization Schema:

```c++
#include "bsp.hpp"

struct Player {
    uint64_t id;
    std::string name;
    float score;
};

BSP_SCHEMA_SET(Player,
    BSP_SCHEMA(
        BSP_FIELD(id),
        BSP_FIELD(name),
        BSP_FIELD(score)
    )
)
```

`BSP_SCHEMA_SET` will:

1. Generate a `SchemaSet<Player>` specialization in the `bsp::schema` namespace.
2. Since `Player` satisfies the `types::schema_serializable` Concept, its default protocol becomes `proto::Schema<SIZE_MAX>`.

After that, direct serialization is possible:

```c++
Player player{1001, "Alice", 98.5f};

bsp::io::BufferWriter writer;
bsp::write(writer, player);  // Using the default protocol (Schema<SIZE_MAX>)

bsp::io::BufferReader reader(writer.buf);
auto loaded = bsp::read<Player>(reader);
```

**Field Protocol Override:** You can specify a protocol for individual fields:

```c++
BSP_SCHEMA_SET(Player,
    BSP_SCHEMA(
        BSP_FIELD_P(id, bsp::proto::Varint),  // id encoded using Varint
        BSP_FIELD(name),                       // Use the field's default protocol
        BSP_FIELD(score)
    )
)
```

> **Note:** `BSP_SCHEMA_SET` must be placed in the global namespace.

---

### 5.2 Versioning

Register different Schema versions using `BSP_SCHEMA_V`:

```c++
BSP_SCHEMA_SET(Message,
    BSP_SCHEMA_V(1,
        BSP_FIELD(content),
        BSP_FIELD(sender_id)
    ),
    BSP_SCHEMA_V(2,
        BSP_FIELD(content),
        BSP_FIELD(sender_id),
        BSP_FIELD(timestamp)
    )
)
```

**Version Number Rules:** Version numbers must be monotonically increasing; otherwise, the compiler will report an error.

**Version Matching Rules:** Selects the **largest version number that is less than or equal to the target version**.

| Target Version | Matched Schema Version |
|:---------------|:-----------------------|
| 0              | None (error)           |
| 1              | V1                     |
| 2              | V2                     |
| 3              | V2 (since V2 is the largest version ≤ 3) |

This rule implies that **only the same version range is compatible**: both the writer and reader should use a protocol that matches the same version.

---

### 5.3 Serialization

**Structure**: `[Field 1][Field 2]...`  
The **field order** is exactly the same as the **registration order**.

#### 5.3.1 Schema\<V>

Specify the version at compile time:

```c++
// Write with a specific version
bsp::write<proto::Schema<1>>(writer, msg);

// Read with a specific version
auto msg_v1 = bsp::read<Message, proto::Schema<1>>(reader);
```

The default value of `V` is `SIZE_MAX`, which matches the version with the largest version number.

#### 5.3.2 DynSchema [non-lite]

Select the version at runtime:

```c++
context ctx;
ctx.opt.target_schema_version = 2;  // Decide the version at runtime

bsp::write<proto::DynSchema>(writer, msg, ctx);

ctx.opt.target_schema_version = 1;
auto older = bsp::read<Message, proto::DynSchema>(reader, ctx);
```

`DynSchema` behaves identically to `Schema<V>`, the only difference being that the version number comes from `ctx.opt.target_schema_version` rather than a compile-time template parameter.

---

## 6. Advanced Serialization

This section covers the advanced protocols provided by `bsp::proto`.

### 6.1 Trivial / Trivially Copyable Types

For types satisfying `types::trivially_serializable` (trivially copyable + non-pointer), the library provides a high-performance direct memory copy implementation.

```c++
struct MyStruct {
    int32_t a;
    int16_t b;
    int8_t c;
} s;

bsp::write<bsp::proto::Trivial>(writer, s); // No schema registration required
```

> Note that the `Trivial` protocol does **not** account for endianness, memory layout, memory alignment, or other implementation-specific concerns.  
> In other words, on different platforms or compilers, this protocol may exhibit different behavior.  
> It is recommended to use `Trivial` only for local storage.

#### 6.1.1 Trivially Copyable Containers

For containers whose element type satisfies `types::trivially_serializable`, the library also provides a direct memory copy implementation.

```c++
struct Vec3 {
    float x, y, z;
};

Vec3 v{1.0f, 2.0f, 3.0f};

io::BufferWriter writer;

std::vector<Vec3> vec = {
    {1.0f, 2.0f, 3.0f},
    {4.0f, 5.0f, 6.0f}
};
write<proto::Trivial>(writer, vec);
// Writes: Varint(2) + 24-byte memory block

std::array<Vec3, 2> arr = {{
    {1.0f, 2.0f, 3.0f},
    {4.0f, 5.0f, 6.0f}
}};

write<proto::Trivial>(writer, arr);
// Writes: 24-byte memory block, no length prefix
```

**std::vector\<T>** under this protocol uses the `Varint` length strategy.  
Notably, for `std::vector<bool>`, under the `Trivial` protocol, **bit compression is enabled**. This bit compression is implemented internally by the library and is **cross-platform safe**.  
**std::array<T, N>** under this protocol uses the `Fixed\<>` length strategy.

The library only enables this feature for **single-byte trivially copyable types**; you must manually specify the `Trivial` protocol.  
Once enabled, the specialized encoding of the element type is bypassed.

---

### 6.2 Limited & Forced / Length Constraints

To facilitate forward compatibility of protocols, the library provides `proto::Limited<Len, Inner>` and `Forced<Len, Inner>` to constrain read/write length.  
These protocols are implemented based on `io::LimitedReader` & `io::LimitedWriter`; see section 2.3 for details.

The template parameter `Len` specifies the behavior of the protocol, while `Inner` specifies the inner serialization method.

#### 6.2.1 Limited<Len, Inner>

This protocol imposes read/write length limits but does not enforce a fixed size.

**Len: Varint**

- Structure: `[Varint length prefix][content]`

> **Writing**: Content is first written to a `BufferWriter` buffer; then a `Varint length prefix` is written based on the content length, followed by the content copy.  
> **Reading**: The read length is limited; if the limit is exceeded, `unexpected_eof` is thrown.

**Len: Fixed\<N>**

- Structure: `[content]`

> **Writing**: The write length is limited; if the limit is exceeded, `fixed_size_mismatch` is thrown.  
> **Reading**: The read length is limited; errors are the same as above.

#### 6.2.2 Forced<Len, Inner>

This protocol enforces a fixed read/write size.

If the written size is less than expected, the remaining bytes are padded with zeros after the written content.  
If the read size is less than expected, the remaining content is skipped.  
**Even if an error occurs during serialization, zero-padding or skipping will still be performed if the stream is usable.**  
Other behaviors are identical to `Limited<Len, Inner>`.

---

## 7. Customization

### 7.1 Custom Serializer

If the default generated behavior does not meet your requirements, you can specialize the Serializer for your custom types and protocols.

```c++
struct MyCustomType { /* ... */ };
struct MyCustomProto {}; // You may also use BSP's built-in protocols

template<>
struct bsp::serialize::Serializer<MyCustomType, MyCustomProto> {
    static void write(io::Writer auto& w, const MyCustomType& v, context &ctx) {
        // Implement your write logic
    }
    static void read(io::Reader auto& r, MyCustomType& out, context &ctx) {
        // Implement your read logic
    }
};
```

Please specialize within the `bsp::serialize` namespace.  
In your Serializer implementation, be sure to use `scope_guard` (see the next section).

---

### 7.1.1 Scope Guard

`scope_guard` is a RAII utility provided by BSP for managing **recursion depth**, **configuration lifecycle**, and **call stack tracing** during serialization.  
When serializing nested structures, it helps address three issues:

1. **Recursion depth control**: Increments depth by 1 when entering a container or struct, and decrements by 1 when leaving. Throws an exception when `safety::max_depth` is exceeded.
2. **Temporary configuration modifications**: Certain serialization operations may require temporarily modifying `safety` or `option` configurations; these are automatically restored upon leaving scope.
3. **Call stack tracing**: When an exception occurs, it automatically records the type and protocol information currently being serialized, forming a call chain.

```c++
auto g = ctx.guard<GetDeeper, RollbackSafety, RollbackOpts>(frame_fn);
```

#### Template Parameters

| Parameter                       | Type     | Meaning                     | When `true`                                           |
|:--------------------------------|:---------|:----------------------------|:------------------------------------------------------|
| `GetDeeper`                     | `bool`   | Whether to count toward recursion depth | Container, struct, and other types containing child elements |
| `RollbackSafety`                | `bool`   | Whether to restore `ctx.sf` on destruction | When safety configuration has been temporarily modified |
| `RollbackOpts` **[non-lite]**   | `bool`   | Whether to restore `ctx.opt` on destruction | When option configuration has been temporarily modified |

#### Parameter: Traceback Frame Generator [non-lite]

The fourth parameter is a callable object that returns a `traceback_frame`. It is invoked **if and only if an exception occurs during serialization**, incurring zero overhead in the normal path:

```c++
auto g = ctx.guard<true, false, false>([] {
    return errors::value_frame{"Player", "Schema<0>"};
});
```

The frame generator supports two frame types:

| Frame Type        | Purpose             | Parameters                                               |
|:------------------|:--------------------|:---------------------------------------------------------|
| `value_frame`     | Records type name, protocol name, child element info | `type`, `proto`, `child_label` (optional), `details` (optional) |
| `wrapper_frame`   | Records wrapper information | `wrapper_info`                                           |

If the frame generator itself throws an exception, the guard catches it and generates a `wrapper_frame{"[!!] error when generating traceback info"}`, ensuring the integrity of the traceback.

#### Usage Examples

**Container/Struct:**

```c++
template<typename T> requires types::default_serializable<T>
struct Serializer<std::vector<T>, proto::Varint> {
    static void write(io::Writer auto &w, const std::vector<T> &v, context &ctx) {
        size_t index = 0;                            // Declare child element info to capture
        auto g = ctx.guard<true, false, false>([&] { // GetDeeper set to true
            return errors::value_frame{
                "std::vector", "Varint", 
                detail::concat("Elem ", index),      // lambda captures current index
                detail::concat("length=", v.size())  // include length info
            };
        });
        detail::write_varint(w, v.size());
 
        for (; index < v.size(); ++index) {
            DefaultSerializer<T>::write(w, v[index], ctx);
        }
    }
}
```

**Primitive Type:**

```c++
template<>
struct Serializer<uint8_t, proto::Fixed<>> {
    static void write(Writer auto& w, int v, context& ctx) {
        auto g = ctx.guard<false, false, false>([] {
            return errors::value_frame{"uint8_t", "Fixed<>"};
        });
        // ...
    }
};
```

**Temporary Configuration Modification:**

```c++
static void write(Writer auto& w, const T& v, context& ctx) {
    auto g = ctx.guard<false, true, false>([] {         // Rollback safety config
        return errors::value_frame{"MyType", "Custom"};
    });
    
    // Temporarily relax length constraints
    ctx.sf.max_container_size = 4096;
    
    // ... serialization logic ...
    
    // Upon leaving scope, ctx.sf is automatically restored to its previous value, while ctx.opt is not
}
```

#### How It Works

Please refer to the library code for details.

When created, the `scope_guard` object stores the current information as needed and increments the depth by 1.  
Upon leaving scope, the object is destroyed; the custom destructor is called, restoring the stored information and decrementing the depth by 1. If `traceback` is enabled and an unhandled error exists, the provided lambda is invoked to generate a call frame stored in the traceback.

---

### 7.2 Custom Default Protocol

You can globally override using a macro:

```c++
// This causes all int32_t serializations that do not explicitly specify a protocol to use Varint.
BSP_DEFAULT_PROTO(int32_t, bsp::proto::Varint);
```

The registration macro must be placed in the global namespace.

For complex types such as template classes, you can specialize within `bsp::proto`:

```c++
template<typename T>
struct bsp::proto::DefaultProtocol<std::vector<T>> {
    using type = Varint;
};
```

---

### 7.3 Syntactic Sugar: In-Class Registration

Escaping to the global namespace can be somewhat inelegant, so we also allow direct serialization registration inside the type via tag dispatch:

```c++
struct MyStruct {
    using default_protocol = proto::Varint;                                                // Default protocol
    
    static void write(io::Writer auto &w, const MyStruct &v, context &ctx, proto::Varint); // Write logic
    
    static void read(io::Reader auto &r, MyStruct &out, context &ctx, proto::Varint);      // Read logic
}
```

> Due to implementation limitations, specializations registered in this manner are **partial specializations** rather than **full specializations**.  
> Therefore, if you perform both in-class and out-of-class registration, the compiler will **not** trigger an ODR violation, and it will prioritize the out-of-class full specialization.

---

## 8. Debugging and Safety

### 8.1 Exception Classes / errors

BSP uses runtime exceptions rather than propagating error codes.  
`errors::error` is BSP's exception object.  
It contains an error code and category, corresponding to the enum classes `code` and `kind`, respectively.

---

### 8.2 Call Stack / Traceback [non-lite]

`errors::traceback` is BSP's runtime error log printing utility.  
It exists as a `std::shared_ptr` within both the `context` and `error` classes, is initialized only when an exception occurs, and records call stack information through `scope_guard` during backtracing.

Use `traceback::format()` or `error::format_tb()` to retrieve the call stack information.

A typical Traceback message looks like this:

```text
[bsp::string_too_large] string size 46 larger than limit=10 bytes // what
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

#### Value Frame

```text
  - [label] | [type], [proto]
    (details)
```

**label**  
The `label` is specified by the previous value frame, indicating "what this object is within the previous object." If it is the root of the call, it is `[ROOT]`; if not specified, it is `UNKNOWN`.  
Concatenating the `label`s across the entire call stack yields the logical path of the element:

```text
[ROOT] -> Elem 2 -> Field "username"
```

**details**  
`details` is an optional field that provides additional information for more precisely determining the cause of the error.

#### Wrapper Frame

```text
  @ [info]
```

The wrapper's scope encompasses all frames in the stack that follow the wrapper frame.  
It is commonly used with `WrapperProto` and wrapper classes.

---

## 9. Appendix

### 9.1 Provided Concepts

`bsp::types` provides several Concepts for determining serialization capabilities.

```c++
// Can be serialized via the Trivial protocol
template<typename T>
concept trivial_serializable;

// Has serialization methods registered inside the class
template<typename T, typename Proto>
concept internal_serializable;

// Has a default protocol registered inside the class
template<typename T>
concept internal_default_protocol;

// Has a corresponding SchemaSet defined
template<typename T>
concept schema_serializable;

// A T-P pair that can be serialized, based on detection of Serializer specialization
template<typename T, typename Proto>
concept serializable;

// A type that can be serialized using its default protocol
template<typename T>
concept default_serializable = serializable<T, proto::DefaultProtocol_t<T>>;

// All types in Ts can be serialized using their default protocols
template<typename... Ts>
concept all_serializable = (default_serializable<Ts> && ...);
```

---

### 9.2 bsp_nightly Feature Overview

None at present.

---

### 9.3 Examples Guide

None at present.

---

### 9.4 Tips

#### 9.4.1 Quick Reference

| Goal                              | Code                                                                   |
|:----------------------------------|:-----------------------------------------------------------------------|
| Serialize an int                  | `bsp::write(writer, 42)`                                               |
| Serialize a struct                | `bsp::write(writer, player)`                                           |
| Encode an integer with `Varint`   | `bsp::write<bsp::proto::Varint>(writer, 42)`                           |
| Encode with `Trivial`             | `bsp::write<bsp::proto::Trivial>(writer, vec)`                         |
| Specify a `Schema` version        | `bsp::write<bsp::proto::Schema<2>>(writer, msg)`                       |
| Select version at runtime         | `bsp::write<bsp::proto::DynSchema>(writer, msg, ctx)` **[non-lite]**   |
| Limit serialization length        | `bsp::write<bsp::proto::Limited<...>>(writer, data)` **[non-lite]**    |
| Enforce serialization length      | `bsp::write<bsp::proto::Forced<...>>(writer, data)` **[non-lite]**     |
| Custom type serialization         | Specialize `bsp::serialize::Serializer<T, P>`                          |
| Custom I/O                        | Implement the `bsp::io::Writer` / `bsp::io::Reader` concept            |

#### 9.4.2 Protocol Version Compatibility

As long as higher-version `Schema`s only append fields at the end, you can read a higher-version `Schema` using a lower-version `Schema` by employing `proto::Forced`:

```c++
io::BufferWriter w;
io::BufferReader r(w.buf);

MyStruct item = ...;

write<proto::Forced<proto::Varint, proto::Schema<2>>>(w, item);
read<proto::Forced<proto::Varint, proto::Schema<1>>>(r, item); // Still reads the corresponding fields correctly without stream misalignment
```

#### 9.4.4 Switching Serialization Libraries

BSP is largely non-intrusive to types, making it straightforward to migrate to other serialization libraries.  
If BSP does not meet your needs, here is a guide for removing residual BSP code:

1. Delete Schema registrations.
2. Delete Serializer & DefaultProto registrations.
3. Remove `PVal` usage: it is recommended to replace manually. If manual replacement is cumbersome, you may define:
   ```c++
   template<typename T, typename P>
   using bsp::types::PVal = T;
   ```

---

## 10. FAQ

The following are some frequently asked questions for your reference.

**Q: Why are `endian` and `enable_traceback` fixed as `constexpr`?**  
A:  
Basic types (such as integers and floating-point numbers) are serialized very frequently. Performing a runtime endianness check on every read or write would introduce unnecessary branch overhead. By defining `endian` as `static constexpr`, the compiler can determine the platform's endianness at compile time and completely eliminate irrelevant conversion branches, achieving zero-overhead endianness adaptation. Big-endian is the convention for most network protocols, so the library defaults to big-endian. If your application requires little-endian storage, simply modify the `endian` definition in the header (MIT license permits such modifications).  
The same reasoning applies to `enable_traceback`; this feature appears in virtually all `Serializer` implementations, incurs greater performance cost, and typically does not require a runtime toggle.

---

**Q: Why is there no support for `std::span` / `std::string_view`?**  
A:  
These containers are read-only view types and cannot be deserialized; the behavior of `read` would likely be unexpected.

---

**Q: Why does `std::vector<bool>` not enable bit compression by default?**  
A:  
`std::vector<bool>` is a specialized "pseudo-container" in the standard library. Its elements are not actual `bool` objects but proxy references to bit-fields. Enabling bit compression under the default protocol would require the library to handle these proxy semantics internally, increasing complexity and potentially diverging from user expectations of byte-by-byte behavior. Furthermore, in many business scenarios,  
`std::vector<bool>` is used as a set of flag bits; it is recommended to use `std::bitset<N>` (fixed size at compile time) or explicitly specify the `proto::Trivial` protocol to obtain bit compression. This design keeps the default behavior simple and predictable.

---

**Q: What are the specific thresholds for Varint overflow checks?**  
A:  
Overflow checks are triggered in two cases:

1. **Length too large to fit in the target type**: For example, reading an LEB128 sequence exceeding 32 bits into a `uint32_t`. The threshold is determined by the bit width of the target type (e.g., 32 bits for a 32-bit type).
2. **Container element count or string length exceeds the limits in `safety`**: Controlled via `max_container_size` and `max_string_size`, defaulting to 1 MiB elements and 4 MiB bytes. Under the `MEDIUM` or `STRICT` error policy, these throw an exception when exceeded.

---

**Q: What is the difference between `types::bytes` and `std::vector<char>`?**  
A:  
`types::bytes` is an alias for `std::vector<uint8_t>`; whether `char` is `unsigned char` or `signed char` is implementation-defined behavior.  
On platforms where `char` is **unsigned**, there is no difference between the two.

On platforms where `char` is **signed**, the differences are as follows:  
`types::bytes` uses a high-performance byte-copy implementation under the `Varint`, `Fixed<N>`, and `Trivial` protocols.  
`std::vector<char>` uses a byte-copy implementation only under the `Trivial` protocol.

---

**Q: Why did the element order change after I deserialized a `std::unordered_map`?**  
A:  
`std::unordered_map` is inherently an unordered container based on a hash table. Its iteration order depends on the hash function and bucket distribution and does not guarantee any particular order. During serialization, elements are written in iteration order; during deserialization, they are re-inserted in write order. Consequently, the final order depends on the insertion order and the internal state of the hash table.  
Since the standard library does not provide a fixed-order dictionary container, if order preservation is required, please use `std::vector<std::pair<K, V>>` instead.

---

**Q: How can I safely serialize graph structures containing pointers (e.g., trees, linked lists)?**  
A:  
BSP's default handling of pointers is "deep copy by value," meaning it recursively serializes the object pointed to by the pointer. This approach **cannot handle circular references** (it would lead to infinite recursion or stack overflow) nor does it support shared ownership (the same object being serialized multiple times).  
For complex graph structures, the following approaches are recommended:

- Pool the objects and write object IDs instead of raw pointers during serialization; reconstruct reference relationships during deserialization.
- If the graph structure is acyclic, you can temporarily increase `safety::max_depth`.

---

**Q: Why does my custom struct still cause compilation errors after registering with `BSP_SCHEMA_SET`?**  
A:  
Common causes:

1. **Macro not placed in the global namespace**: `BSP_SCHEMA_SET` must be expanded at global scope; it cannot be used inside functions or namespaces.
2. **Incomplete field types**: Ensure the struct definition is complete before the macro.

---

**Q: How can I achieve cross-language compatibility (e.g., interaction with Python/Java)?**  
A:  
Since BSP's implementation is heavily dependent on C++ language features, it does not provide direct cross-language support. However, interoperability can be achieved through the following methods:

- **Implement serialization yourself**: Implement protocols that have no language dependencies, such as `Fixed<>`, `Varint`, and `Schema<>`, and avoid `Trivial`.
- **Write an IDL compiler**: Generate parsing code for other languages based on BSP's Schema macros.

Typically, if cross-language interoperability is a core requirement, it is recommended to use Protobuf or Cap'n Proto directly.