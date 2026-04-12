# BSP Usage Documentation

> [中文版](usage-zh.md)

## 0. Overview

### 0.1 Requirements

BSP is a modern C++ single-header library with the following basic requirements:

| Requirement      | Minimum                                                                           |
|:-----------------|:----------------------------------------------------------------------------------|
| **C++ Standard** | C++20 or higher                                                                   |
| **Compiler**     | GCC 11+ / Clang 14+ / MSVC 2022 17.0+                                             |
| **Build System** | No special requirements; copy `bsp.hpp` into your project and `#include` it       |
| **Dependencies** | C++ standard library only                                                         |
| **Threading**    | Supports `thread_local` storage (used for recursion depth guard and option stack) |

The library implementation relies on the following C++20 features:

- **Concepts**: Used to define `io::Reader` and `io::Writer` interface constraints for compile-time type checking.
- **`std::endian`**: Used to detect system endianness; combined with `constexpr` branching to eliminate dead code at
  compile time.
- **`std::bit_cast`**: Used for safe conversion between floating-point and integer representations.
- **`std::make_unsigned_t` / `std::type_identity` etc.**: Enhance template metaprogramming capabilities.

> **Note**: If your target platform does not support C++20, the code will not compile. The development team may release
> a C++17 version in the future, but C++14 and below are not considered.

---

### 0.2 Use Cases

Why choose a binary stream format over JSON?

JSON has significant drawbacks in many domains:

- **Space Bloat**: The integer `123456789` occupies 9 bytes in JSON, but only 4 bytes in BSP.
- **Floating-Point Precision Loss**: JSON converts binary floats to decimal strings; round-trip conversion may lose
  precision.
- **Binary Data**: JSON requires Base64 encoding, inflating size by about 33%.
- **Parsing Overhead**: Text parsers must handle escaping, Unicode, whitespace, incurring CPU cost.
- **Low Determinism**: JSON lacks format determinism; null-checking is often required in business logic for safety.

As a binary protocol, BSP is better suited for high-latency communication protocols and local cache storage.

| Scenario               | Recommended | Reason                                               |
|:-----------------------|:------------|:-----------------------------------------------------|
| Network Communication  | BSP         | Cross-platform, attack-resistant, version-compatible |
| Cross-platform Storage | BSP         | High density, cross-platform                         |
| Configuration Files    | JSON/TOML   | Human readability first                              |
| Local Cache            | BSP Trivial | High compression, performance-sensitive              |
| Game Saves             | BSP Trivial | Same as above, plus binary tamper-resistance         |

---

### 0.3 Quick Start

The following example demonstrates the most basic serialization and deserialization workflow. Suppose you need to save a
struct containing an integer and a string to a file or memory.

**Step 1: Include the header and define your data structure**

```c++
#include "bsp.hpp"

struct Player {
    uint64_t id;
    std::string name;
    float score;
};

// Register the Schema (field order determines storage order)
BSP_SCHEMA(Player,
    BSP_FIELD(id),
    BSP_FIELD(name),
    BSP_FIELD(score)
)

// Set Player's default serialization protocol to Schema
BSP_DEFAULT_PROTO(Player, bsp::proto::Schema<>)
```

**Step 2: Serialize (Write)**

```c++
#include <sstream>

int main() {
    Player player{ 1001, "Alice", 98.5f };
    
    // Use a standard library string stream as output
    std::stringstream buffer;
    bsp::io::StreamWriter writer(buffer);
    
    // Write with a single line
    bsp::write(writer, player);
    
    // buffer.str() now contains the serialized binary data
    return 0;
}
```

**Step 3: Deserialize (Read)**

```c++
// Continuing from above, read from the same buffer
buffer.seekg(0); // Reset read position
bsp::io::StreamReader reader(buffer);

Player loaded_player;
bsp::read(reader, loaded_player);

// loaded_player contents will match player exactly
assert(loaded_player.id == 1001);
assert(loaded_player.name == "Alice");
assert(loaded_player.score == 98.5f);
```

You now understand the basic usage of BSP. Subsequent sections will expand on various protocol tags, container handling,
error policies, and advanced customization features.

## 1. Concepts

BSP advocates **type as protocol**, while also maintaining that **protocols should not intrude upon types**, thus
introducing the concept of **protocol tags**.

### 1.1 Protocol Tags

The core mechanism of this library, used to specify how data is (de)serialized, resides in the `bsp::proto` namespace.  
They fall into two categories:

**Encoding Method Tags:**  
These include `Fixed<N>`, `Varint`, `Schema<V>`, `Trivial`, `CVal`.

- When applied to a plain type, it dictates how that data should be represented.  
  Example: `int32` under `Fixed<>` is fixed 4 bytes; under `Varint` it uses ZigZag + LEB128 encoding.
- When applied to a container/wrapper type, it dictates how the container itself holds elements, but does not specify
  the encoding of the child elements.  
  Example: `vector<int>` under `Fixed<4>` holds exactly 4 elements; under `Varint` it uses a LEB128 length prefix.

**Modifier Tags:**  
These include `Limited<Len, Inner>`, `Forced<Len, Inner>`, `optmod::WithOptions<Inner, Modifiers...>`.  
These tags do not specify an encoding method directly; instead, they apply modifications/restrictions during encoding
and allow an inner protocol to be specified. They must inherit from `proto::WrapperProto`.  
Example: Using `Limited<Fixed<256>, Varint>` to read a `vector<int>` uses the same reading method as `Varint`, but
limits total bytes read to 256.

### 1.2 Default Protocol Mapping

`proto::DefaultProtocol_t<T>` provides the mapping from type to default protocol:

| Category                          | Type                                           | Protocol   |
|:----------------------------------|:-----------------------------------------------|:-----------|
| **Data Types**                    | bool                                           | Fixed<0>   |
|                                   | Integers and Floating-point                    | Fixed<0>   |
| **Container Types**               | std::string                                    | Varint     |
|                                   | bytes (std::vector<uint8_t>)                   | Varint     |
|                                   | std::vector\<T>                                | Varint     |
|                                   | std::bitset\<N>                                | Fixed<0>   |
|                                   | std::map<K, V> & unordered_map<K, V>           | Varint     |
|                                   | std::set\<T> & unordered_set\<T>               | Varint     |
|                                   | std::array<T, N>                               | Varint     |
| **Structured Types**              | std::pair<T1, T2>                              | Fixed<0>   |
|                                   | std::tuple<Ts...>                              | Fixed<0>   |
|                                   | Structs registered via BSP_DEFAULT_SCHEMA(\_V) | Schema\<V> |
| **Trivially Copyable Types**      | Types satisfying `types::trivial_serializable` | Trivial    |
| **Variable Types**                | std::optional\<T>                              | Varint     |
|                                   | std::variant<Ts...>                            | Varint     |
| **Pointers**                      | T*                                             | Varint     |
|                                   | std::unique_ptr\<T>                            | Varint     |
| **Types with Specified Protocol** | PVal                                           | Default    |
|                                   | CVal                                           | CVal       |
| **Other**                         | Types not matching any above                   | Default    |

> Types that only support `Fixed<0>` use `0` to indicate that the length is determined by the type itself, not that zero
> bytes are written.  
> `Default`, when not specialized, automatically maps to the `DefaultProtocol_t<T>` protocol.

### 1.3 Serializer

`serialize::Serializer<T, Protocol>` defines how type `T` is read/written under protocol `Protocol`:

```c++
Serializer<T, P>::write(io::Writer &w, const T &v);
Serializer<T, P>::read(io::Reader &r, T &out);
```

Convenience functions are also provided:

```c++
bsp::read<P>(io::Reader &r, T &out);
bsp::read<T, P>(io::Reader &r); // -> T
// If P is omitted, the default protocol is used
```

### 1.4 Options

`bsp::options` provides configuration settings.

```c++
struct options {
    static constexpr std::endian endian = std::endian::big; // Fixed for compile-time pruning
    std::optional<size_t> max_depth;            // Maximum recursion depth
    std::optional<size_t> max_container_size;   // Maximum container length (elements)
    std::optional<size_t> max_string_size;      // Maximum string/byte array length (bytes)
  
    std::optional<ErrorPolicy> error_policy;    // Error handling: STRICT, MEDIUM, IGNORE
};
```

You can manage options via an option stack:

```c++
options::push(options); // If some fields are empty, they inherit from the previous level
options::pop();
options::current(); // -> const options&
options::reset();
```

However, using the RAII `OptionsGuard` is preferred:

```c++
OptionsGuard g(options);
```

This modifies the thread-local global options and restores the option stack to its original depth when the scope ends.

## 2. I/O Interface

`bsp` operates on byte streams and provides convenient interfaces for various stream types.

### 2.1 Reader & Writer Concepts

`io::Reader` and `io::Writer` are C++20 concepts that define stream operations:

```c++
template<typename R> concept Reader = requires(R r, uint8_t *buf, std::streamsize n)
{
    { r.read_bytes(buf, n) } -> std::same_as<void>; // Read n bytes into *buf
    { r.read_byte() } -> std::same_as<uint8_t>;     // Read and return 1 byte
};
template<typename W> concept Writer = requires(W w, const uint8_t *buf, std::streamsize n, uint8_t b)
{
    { w.write_bytes(buf, n) } -> std::same_as<void>; // Write n bytes from *buf
    { w.write_byte(b) } -> std::same_as<void>;       // Write 1 byte
};
```

By providing these interfaces, you can support operations on any stream.

### 2.2 StreamReader & Writer / STL Streams

`io::StreamReader` and `io::StreamWriter` provide operations on STL streams:

```c++
std::stringstream ss;

io::StreamReader sr(ss);
io::StreamWriter sw(ss);
```

> When EOF is encountered, `errors::EOFError` is thrown.

### 2.3 LimitedReader & Writer / Byte Limiting

`io::LimitedReader` and `io::LimitedWriter` provide a limited view of a stream:

```c++
io::Writer reader;
io::LimitedReader lr(reader, 16); // Allow reading only 16 bytes
lr.skip_remaining();              // Skip any unread bytes

io::Reader writer;
io::LimitedWriter lw(writer, 16); // Allow writing only 16 bytes
lw.pad_zero();                    // Pad unwritten portion with 0x00
```

> Exceeding the read/write limit throws `errors::EOFError`.

### 2.4 AnyReader & Writer / Type Erasure

`io::AnyReader` and `io::AnyWriter` provide type erasure for streams, typically used with `types::CVal`.

```c++
io::StreamReader sr;
io::LimitedReader lr(sr);

io::AnyReader* any_r = new io::AnyReader(sr); 
any_r = new io::AnyReader(lr); // Both become the same type
```

See section 4.2 for more on `types::CVal`.

> This involves runtime polymorphism and incurs runtime overhead.  
> Using `concept auto io::Reader` is recommended over `io::AnyReader` in typical scenarios.

## 3. Serialization – Native and STL Types

This section describes the specific serialization behavior. For serializer concepts, refer to section 1.3; for custom
serializers, see section 7.1.

> For types containing child elements, the child elements are always read using their default protocol.  
> To specify a protocol, use `types::PVal` (see section 4.1).

> Unless otherwise stated, serialization does not count toward recursion depth.

### 3.1 Data Types

#### 3.1.1 bool

| Protocol    | Structure                   | Endian | Notes |
|:------------|:----------------------------|:-------|:------|
| **Fixed<>** | Single byte `[0x00 / 0x01]` | N/A    |       |

> Under `STRICT` policy, reading a value other than 0/1 throws `InvalidBool`.

#### 3.1.2 Integers

| Protocol    | Structure                   | Endian | Notes                               |
|:------------|:----------------------------|:-------|:------------------------------------|
| **Fixed<>** | Corresponding bytes `[Int]` | Yes    |                                     |
| Varint      | `[LEB128 encoded result]`   | N/A    | Signed integers use ZigZag encoding |

> **Varint Protocol**: Under non-`IGNORE` policy, excessive length throws `VarintOverflow`.

#### 3.1.3 Floating-point

**Fixed<>**

| Protocol    | Structure                     | Endian | Notes                          |
|:------------|:------------------------------|:-------|:-------------------------------|
| **Fixed<>** | Corresponding bytes `[Float]` | Yes    | Only supports `IEEE754` floats |

### 3.2 Container Types

#### 3.2.1 std::string

**Varint**

| Protocol   | Structure                               | Notes |
|:-----------|:----------------------------------------|:------|
| **Varint** | `[LEB128 length prefix][content bytes]` |       |
| Fixed\<N>  | `[N bytes]`                             |       |

> **Varint Protocol**: Under non-`IGNORE` policy, length exceeding `max_string_size` throws `StringTooLarge`.  
> **Fixed\<N> Protocol**: On write, length mismatch throws `FixedSizeMismatch`.

#### 3.2.2 types::bytes (std::vector<uint8_t>)

Behaves identically to `std::string`.

#### 3.2.3 std::vector\<T>

Counts toward recursion depth.

| Protocol   | Structure                                     | Notes             |
|:-----------|:----------------------------------------------|:------------------|
| **Varint** | `[LEB128 length prefix][Elem1][Elem2]...`     |                   |
| Fixed\<N>  | `[Elem1][Elem2]...`                           |                   |
| Trivial    | `[LEB128 length prefix][corresponding bytes]` | See section 6.1.1 |

> **Varint Protocol**: Under non-`IGNORE` policy, length exceeding `max_container_size` throws `ContainerTooLarge`.  
> **Fixed\<N> Protocol**: On write, length mismatch throws `FixedSizeMismatch`.

#### 3.2.4 std::vector\<bool>

**This implementation does NOT use bit compression.  
If needed, use `std::bitset<N>` or the `Trivial` protocol for `std::vector<bool>` (see section 6.1.1).**

Each bool occupies 1 byte; otherwise behaves like `std::vector<T>`.

> Under `STRICT` policy, reading a bool value other than 0/1 throws `InvalidBool`.

#### 3.2.5 std::bitset\<N>

**This implementation uses bit compression.**

| Protocol    | Structure           | Notes                                 |
|:------------|:--------------------|:--------------------------------------|
| **Fixed<>** | `[ceil(N/8) bytes]` | Little-endian, protocol is `Fixed<0>` |

#### 3.2.6 std::map<K, V> & unordered_map<K, V>

Counts toward recursion depth.

| Protocol   | Structure                                   | Notes |
|:-----------|:--------------------------------------------|:------|
| **Varint** | `[LEB128 length prefix][K1][V1][K2][V2]...` |       |
| Fixed\<N>  | `[K1][V1][K2][V2]...`                       |       |

> **Varint Protocol**: Under non-`IGNORE` policy, length exceeding `max_container_size` throws `ContainerTooLarge`.  
> **Fixed\<N> Protocol**: On write, length mismatch throws `FixedSizeMismatch`.

#### 3.2.7 std::set\<T> & unordered_set\<T>

Counts toward recursion depth.

| Protocol   | Structure                                 | Notes |
|:-----------|:------------------------------------------|:------|
| **Varint** | `[LEB128 length prefix][Elem1][Elem2]...` |       |

> Under non-`IGNORE` policy, length exceeding `max_container_size` throws `ContainerTooLarge`.

#### 3.2.8 std::array<T, N>

Counts toward recursion depth.

| Protocol    | Structure               | Notes                  |
|:------------|:------------------------|:-----------------------|
| **Fixed<>** | `[Elem 1][Elem 2]...`   | Protocol is `Fixed<0>` |
| Trivial     | `[corresponding bytes]` | See section 6.1.1      |

### 3.3 Structured Types

#### 3.3.1 std::pair<T1, T2>

| Protocol    | Structure         | Notes |
|:------------|:------------------|:------|
| **Fixed<>** | `[First][Second]` |       |

#### 3.3.2 std::tuple<Ts...>

Counts toward recursion depth.

| Protocol    | Structure           | Notes                  |
|:------------|:--------------------|:-----------------------|
| **Fixed<>** | `[Elem1][Elem2]...` | Protocol is `Fixed<0>` |

### 3.4 Variable Types

#### 3.4.1 std::optional\<T>

| Protocol   | Structure                              | Notes |
|:-----------|:---------------------------------------|:------|
| **Varint** | `[Bool presence flag]([T if present])` |       |

> Under `STRICT` policy, presence flag other than 0/1 throws `InvalidBool`.

#### 3.4.2 std::variant<Ts...>

| Protocol   | Structure           | Notes |
|:-----------|:--------------------|:------|
| **Varint** | `[Varint index][T]` |       |

> On write, if valueless by exception, throws `InvalidVariantIndex`.  
> On read, if index cannot be matched, throws `InvalidVariantIndex`.

### 3.5 Pointers

Counts toward recursion depth.  
**Copies the pointed-to value**. When recursion depth limiting is disabled, ensure the structure contains no cycles, or
it will loop indefinitely.

| Protocol   | Structure                 | Notes                            |
|:-----------|:--------------------------|:---------------------------------|
| **Varint** | `[Bool presence flag][T]` | Flag 0x00 indicates null pointer |

> Under `STRICT` policy, presence flag other than 0/1 throws `InvalidBool`.

#### 3.5.1 T*

Memory must be managed manually.

```c++
MyStruct* s = read<>(reader);

delete s;
```

#### 3.5.2 std::unique_ptr\<T>

Memory is automatically managed by the smart pointer.

## 4. Types with Specified Protocol

When the default protocol is insufficient, you may need types to carry protocol annotations.  
`bsp` provides `PVal` and `CVal` as compile-time and runtime protocol-intrusive interfaces, respectively.

### 4.1 types::PVal<T, P>

`types::PVal` is a lightweight wrapper that **overrides** the serialization protocol for type `T` without modifying the
global default mapping.

**Modifier protocols** applied to `PVal` behave exactly as if applied directly to the inner element.  
**Encoding method protocols** have no effect on `PVal`.

```c++
int value = 0x1234;
bsp::PVal<int, bsp::proto::Varint> wrapped{value};   // Force Varint

// Serializing wrapped will encode the int using Varint
std::stringstream ss;
bsp::io::StreamWriter writer(ss);
bsp::write(writer, wrapped);
```

PVal supports implicit conversion to `T&` and `T*`, allowing use like a normal value:

```c++
int x = *wrapped;   // Dereference
wrapped->...        // If T is a class type, can call members
```

> Implicit conversion may cause issues; using `pval.value` to access the value is recommended.

### 4.2 types::CVal

`types::CVal` is an abstract base class for runtime polymorphic serialization. Derived classes must implement two pure
virtual functions:

```c++
class Animal : public bsp::types::CVal {
public:
    std::string species;
    int age;

    void write(bsp::io::AnyWriter& w) const override {
        bsp::write(w, species);
        bsp::write(w, age);
    }

    void read(bsp::io::AnyReader& r) override {
        bsp::read(r, species);
        bsp::read(r, age);
    }
};
```

Then, you can serialize/deserialize through base class pointers or references:

```c++
Animal cat{"cat", 3};
std::stringstream ss;
bsp::io::StreamWriter writer(ss);
bsp::write(writer, cat); // Automatically calls polymorphic write

bsp::io::StreamReader reader(ss);
Animal dog;
bsp::read(reader, dog); // Automatically calls polymorphic read
```

`io::AnyReader` and `AnyWriter` provide type erasure so that `types::CVal` can accept any object satisfying the
`Reader/Writer` concepts. See section 2.4.

> Virtual function calls incur runtime overhead; avoid `types::CVal` unless necessary.

## 5. Schema

Structs are an important way to store data in C++. `bsp` introduces **schemas** for their serialization.  
For user-defined structs, it is recommended to register them using **schema macros**; the library will automatically
generate a serializer for the `Schema<Version>` protocol.

- Structure: `[Field 1][Field 2]...`
- **Fields are serialized in registration order.**

### 5.1 Basic Registration

Example:

```c++
struct Message {
    uint64_t timestamp;
    std::string content;
    uint64_t sender_id;
};

// Register the schema for the default version
BSP_SCHEMA(Message,
    BSP_FIELD_P(timestamp, bsp::proto::Varint), // Use variable-length encoding
    BSP_FIELD(content),                         // Default protocol for string is Varint
    BSP_FIELD(sender_id)                        // Default for int is Fixed<>
);

// Set the struct's default protocol to Schema
BSP_DEFAULT_PROTO(Point, proto::Schema<>);
```

> `BSP_FIELD(Field)` uses the default protocol of the field type.  
> `BSP_FIELD_P(Field, Protocol)` specifies a particular protocol for the field.  
> `BSP_SCHEMA(Type, ...)` registers as `proto::Schema<proto::Default>`.  
> `BSP_SCHEMA(Type, Version, ...)` registers as `proto::Schema<V>`.

### 5.2 Versioned Schemas

> More control methods are under development.

Use `BSP_SCHEMA_V(Type, Version, ...)` to register schemas with version tags, facilitating protocol evolution:

```c++
BSP_SCHEMA_V(Message, V1,
    BSP_FIELD(content),
    BSP_FIELD(sender_id)
);

BSP_SCHEMA_V(Message, V2,
    BSP_FIELD(content),
    BSP_FIELD(sender_id),
    BSP_FIELD(timestamp)
);

BSP_DEFAULT_PROTOCOL(Point, proto::Schema<V2>);

// Specify version when using
bsp::write<bsp::proto::Schema<V1>>(writer, {0, content, sender});
```

The registration macros must be placed in the global namespace.

## 6. Advanced Serialization

This section covers advanced protocols provided by `bsp::proto`.

### 6.1 Trivial / Trivially Copyable Types

For types that satisfy `types::trivial_serializable` (trivially copyable and non-pointer), the library provides a
high-efficiency direct memory copy implementation.

```c++
struct MyStruct {
    int32_t a;
    int16_t b;
    int8_t c;
} s;

bsp::write<bsp::proto::Trivial>(writer, s); // No schema registration needed
```

> Note: The `Trivial` protocol **does not** consider endianness, memory layout, alignment, or other implementation
> details.  
> Consequently, this protocol may behave differently across platforms and compilers.  
> It is recommended to use `Trivial` only for local storage.

#### 6.1.1 Trivially Copyable Containers

For containers whose element type satisfies `types::trivial_serializable`, the library also provides direct memory copy
implementations.

**std::vector\<T>** under this protocol uses a `Varint` length strategy.  
Notably, for `std::vector<bool>`, the `Trivial` protocol **enables bit compression**. This bit compression is
implemented internally by the library and **is cross-platform safe**.  
**std::array<T, N>** under this protocol uses a `Fixed<>` length strategy.

This feature is not enabled by default; you must explicitly specify the `Trivial` protocol.  
When enabled, any specialized encoding for the element type is ignored.

### 6.2 Limited & Forced / Length Constraints

To facilitate forward compatibility, the library provides `proto::Limited<Len, Inner>` and `Forced<Len, Inner>` to
restrict read/write length.  
These protocols are implemented based on `io::LimitedReader` and `LimitedWriter` (see section 2.3).

The template parameter `Len` dictates the protocol's length behavior, and `Inner` specifies the inner serialization
method.

#### 6.2.1 Limited<Len, Inner>

This protocol enforces read/write limits but does not enforce a fixed size.

**Len: Varint**

- Structure: `[Varint length prefix][payload]`

> On write, data is first written to a `StreamWriter` buffer; then the length is written as a Varint prefix, followed by
> the buffered content.  
> On read, the read length is limited; exceeding the limit throws `errors::EOFError`.

**Len: Fixed\<N>**

- Structure: `[payload]`

> On write, the write length is limited; exceeding throws `errors::EOFError`.  
> On read, the read length is limited; same error on exceed.

#### 6.2.2 Forced<Len, Inner>

This protocol enforces a fixed read/write size.

If the written size is less than expected, the remainder is padded with `0x00` bytes.  
If the read size is less than expected, the remaining bytes are skipped.  
Otherwise, behavior matches `Limited<Len, Inner>`.

### 6.3 WithOptions / Temporary Option Modification

`bsp::proto::optmod` provides protocol tags for temporarily modifying configuration options.  
This is useful for serializing structural data (e.g., trees) or large files (e.g., images) where per-field option
overrides are desired.

**Definition:**

```c++
template<typename InnerProto, typename... Modifiers>
struct WithOptions {};
```

When multiple modifiers are present, they are applied left-to-right.

**Option Modifiers:**

- `MaxDepth<Policy>`: Modifies `max_depth`.
- `MaxContainerSize<Policy>`: Modifies `max_container_size`.
- `MaxStringSize<Policy>`: Modifies `max_string_size`.
- `ErrorPolicyMod<Value>`: Modifies the error policy.

Here, `Policy` should be a `ValueModifier` that describes the numeric change.

**Policy Calculation:**

```c++
template<size_t Mul, size_t Div, size_t Add>
struct ValueModifier<Mul, Div, Add> {}; // new = old * Mul / Div + Add
```

`ValueModifier` implementations include overflow prevention.  
`Unlimited` is an alias for `ValueModifier<0, 1, SIZE_MAX>`.

## 7. Customization

### 7.1 Custom Serializers

If the default behavior does not meet your needs, you can specialize `Serializer` for custom types and custom protocols.

```c++
struct MyCustomType { /* ... */ };
struct MyCustomProto {}; // Or use a built-in protocol

template<>
struct bsp::serialize::Serializer<MyCustomType, MyCustomProto> {
    static void write(bsp::io::Writer auto& w, const MyCustomType& v) {
        // Implement your write logic
    }
    static void read(bsp::io::Reader auto& r, MyCustomType& out) {
        // Implement your read logic
    }
};
```

#### 7.1.1 Adding Recursion Depth Protection

If you define a container-like type, you **must** include an RAII recursion depth guard at the beginning of the
serializer's read/write methods.

```c++
bsp::detail::DepthGuard guard;
```

This automatically increments a thread_local counter and decrements it on destruction.

> If the depth exceeds `options::current().max_depth`, the constructor throws `errors::DepthExceeded`.

### 7.2 Custom Default Protocol

You can globally override the default protocol via a macro:

```c++
// This makes all int32_t serializations without an explicit protocol use Varint.
BSP_DEFAULT_PROTO(int32_t, bsp::proto::Varint);
```

The registration macro must be placed in the global namespace.

## 8. FAQ

Here are some frequently asked questions.

**Q: Why is `options::endian` fixed as `constexpr`?**  
A:  
Serialization of basic types (integers, floats) is very frequent. Performing runtime endianness checks on every
read/write would introduce unnecessary branch overhead. By defining `endian` as `static constexpr`, the compiler can
determine the platform's endianness at compile time and completely prune irrelevant conversion branches, achieving
zero-overhead endianness adaptation. Big-endian is the convention for most network protocols, so the library defaults to
big-endian. If your application requires little-endian storage, simply modify the `endian` definition in the header
file (MIT license permits such modifications).

---

**Q: Why doesn't `std::vector<bool>` use bit compression by default?**  
A:  
`std::vector<bool>` is a specialized "pseudo-container" in the standard library; its elements are not actual `bool`
objects but proxy references to bits. If bit compression were enabled by default, the library would need to handle this
proxy semantics specially, increasing complexity and potentially conflicting with user expectations of byte-per-element
behavior. Moreover, in many business scenarios where `std::vector<bool>` is used for flag collections,
`std::bitset<N>` (compile-time fixed size) or explicit `proto::Trivial` protocol is recommended for bit compression.
This design keeps default behavior simple and predictable.

---

**Q: What is the specific threshold for Varint read overflow checks?**  
A:  
Overflow checks trigger in two cases:

1. **Length too large to fit in target type**: e.g., reading a LEB128 sequence larger than 32 bits into a `uint32_t`.
   The threshold is determined by the bit width of the target type (e.g., 32 bits for a 32-bit type).
2. **Container element count or string length exceeds limits in `options`**: Controlled by `max_container_size` and
   `max_string_size`, which default to 1 MiB elements and 4 MiB bytes respectively. Under `MEDIUM` or `STRICT` error
   policies, exceeding these limits throws an exception.

---

**Q: What is the difference between `types::bytes` and `std::vector<char>`?**  
A:  
`types::bytes` is an alias for `std::vector<uint8_t>`; its default protocol is `Varint` (length prefix + raw bytes).  
`std::vector<char>` has no specialized default protocol, falling back to the `std::vector<T>` `Varint` protocol, which
serializes each element individually using its default serializer. For `char`, the default protocol is `Fixed<>`,
meaning each character is treated as a single-byte integer, resulting in serialization identical to `types::bytes`.

---

**Q: Why does the order of elements change after deserializing `std::unordered_map`?**  
A:  
`std::unordered_map` is fundamentally an unordered container based on hash tables; its iteration order depends on the
hash function and bucket distribution and does not guarantee any specific order. Serialization writes elements in
iteration order, and deserialization re-inserts them in that order, so the final order depends on insertion order and
the hash table's internal state.  
Since the standard library does not provide a fixed-order dictionary container, use `std::vector<std::pair<K, V>>` if
order must be preserved.

---

**Q: How can I safely serialize graph structures containing pointers (e.g., trees, linked lists)?**  
A:  
BSP's default handling of pointers is "deep copy by value", i.e., recursively serializing the pointed-to object. This
approach **cannot handle cyclic references** (leading to infinite recursion or stack overflow) and does not support
shared ownership (the same object serialized multiple times).  
For complex graph structures, the following approaches are recommended:

- Pool objects and serialize object IDs instead of raw pointers, then reconstruct references during deserialization.
- If the graph is acyclic, temporarily increase `options::max_depth`.

---

**Q: Why does my custom struct still cause compilation errors after registering with `BSP_SCHEMA`?**  
A:  
Common reasons:

1. **Macro not placed in global namespace**: `BSP_SCHEMA` must be expanded at global scope, not inside a function or
   namespace.
2. **Field type incomplete**: Ensure the struct definition is complete before the macro.
3. **Default protocol not set**: After registering the Schema, you must also inform the library to use it as the default
   protocol via `BSP_DEFAULT_PROTO(MyStruct, bsp::proto::Schema<>);`, or explicitly specify `bsp::proto::Schema<>` when
   serializing.

---

**Q: How to achieve cross-language compatibility (e.g., with Python/Java)?**  
A:  
Because BSP's implementation heavily relies on C++ language features, it does not directly provide cross-language
support. However, you can achieve interoperability via:

- **Implementing the serialization yourself**: Implement the language-agnostic protocols like `Fixed<>`, `Varint`,
  `Schema<>`, and avoid `Trivial`.
- **Writing an IDL compiler**: Generate parsing code for other languages based on BSP's Schema macros.

If cross-language support is a core requirement, consider using Protobuf or Cap'n Proto directly.  
~~Perhaps one day the development team will need it themselves and write implementations for other languages.~~

## 9. Tips

Here are some practical tips developed during the library's creation.

### 9.1 Safely Handling Unknown Fields for Forward Compatibility

You can combine `proto::Forced` with multi-version Schemas:

```c++
struct V1 {};
struct V2 {};

struct Message {
    uint64_t timestamp;
    std::string content;
    uint64_t sender_id;
};

BSP_SCHEMA_V(Message, V1,
    BSP_FIELD(content),
    BSP_FIELD(sender_id)
);

BSP_SCHEMA_V(Message, V2,
    BSP_FIELD(content),
    BSP_FIELD(sender_id),
    BSP_FIELD(timestamp)
);

// Specify version when using
using MsgPackage = bsp::types::PVal<
    std::variant<
        bsp::types::PVal<Message, bsp::proto::Schema<V1> >,
        bsp::types::PVal<Message, bsp::proto::Schema<V2> >,
    >,
    bsp::proto::Forced<bsp::proto::Varint, bsp::proto::Varint>
>;

bsp::write<>(writer, MsgPackage{0, content, sender});
```

Smarter Schema matching and a `Multi` protocol may be introduced in the future to simplify this pattern.