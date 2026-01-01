# ByteSchema

> **C++ High-Performance Binary Encoding + Schema Library**  
> Lightweight, zero dependencies, strong typing  
> Define schemas directly with C++ types  
> [中文版本](./README-zh.md)

---

## Core Features

* Header-only, no runtime reflection
* Supports multiple protocols & complex data structures
* Optimized for high-frequency communication & large-scale data
* Requires C++20 or later
* Type-safe with full IDE autocompletion support

---

## Core Concepts

For more detailed usage, see [`usage.md`](./usage.md).

### Protocol

ByteSchema supports multiple protocols, each defining binary encoding rules:

| Protocol        | Description                                              | Use Case                                       |
|-----------------|----------------------------------------------------------|------------------------------------------------|
| `Varint`        | Variable-length integer encoding, saves space            | Small integers or variable-length containers   |
| `Fixed<N>`      | Fixed-length encoding                                    | Fixed-size arrays, fixed-width structures      |
| Custom Protocol | Implement `read/write` yourself for special requirements | Special protocols or performance optimizations |

> Supports **nested composition** to express arbitrarily complex data structures.

---

### PVal<T, Protocol>

Wrapper type that binds a plain type to a specific Protocol:

```c++
bsp::PVal<int, bsp::proto::Varint> age{18}; // Single value
bsp::PVal<std::vector<int>, bsp::proto::Fixed<2>> scores{{90, 80, 70}}; // Single-layer vector
```

Nested composition example:

```c++
bsp::PVal<
    std::vector<bsp::PVal<int, bsp::proto::Varint>>,
    bsp::proto::Fixed<2>
> nestedScores{{ {100, 90}, {80, 70} }};
```

> PVal supports multi-dimensional arrays and protocol binding, avoiding ambiguity in container element protocols.

---

### Serializer<T, Protocol>

Template that statically generates read/write logic:

```c++
template<typename T, typename Protocol>
struct Serializer {
    static void write(io::Writer&, const T&);
    static void read(io::Reader&, T&);
};
```

Features:

* **Static type binding**, no runtime reflection
* **High performance**, naturally supports nested composition
* Supports arbitrary type combinations, including `vector`, `PVal`, and custom structures

---

### Any – Dynamic Type

When the type is unknown at compile time, use `Any`:

```c++
bsp::types::Any msg;
msg.read(reader);
msg.write(writer);
```

> Suitable for generic messages, plugin-style data structures.

---

### Schema – Define Structures with Types

```c++
struct Player {
    bsp::PVal<int, bsp::proto::Varint> id;
    bsp::PVal<std::string, bsp::proto::Varint> name;
};
```

Features:

* Efficient, no runtime parsing
* Type-safe, IDE supports autocompletion
* Serialization behavior entirely determined by types
* Can register fields via macros, supports custom protocols and default protocol mapping

---

## Quick Start

### 1. Add Header

```c++
#include "../include/bsp.hpp"
```

### 2. Define Type

```c++
struct Msg {
    int id;
    std::string content;
};

// Define message structure Msg
BSP_SCHEMA(Msg,
    BSP_FIELD(id, int),
    BSP_FIELD(content, std::string)
);
```

### 3. Write / Read Example

```c++
#include <iostream>
#include <sstream>

int main() {
    std::stringstream ss;

    // Write
    bsp::io::Writer writer(ss);
    Msg msg{{1}, {"Hello World"}};
    bsp::write(writer, msg);

    // Read
    bsp::io::Reader reader(ss);
    Msg msg2;
    bsp::read(reader, msg2);

    std::cout << "id=" << msg2.id.value
              << " content=" << msg2.content.value << "\n";
}
```

> Compile and run directly to verify serialization and deserialization.

---

## Examples Guide

ByteSchema provides rich examples covering common and advanced usage:

```
examples/
├── 01_basic.cpp           // Basic type serialization
├── 02_vector_map.cpp      // Container serialization
├── 03_option_variant.cpp  // Option and Variant
├── 04_pval.cpp            // PVal / Multi-dimensional arrays
├── 05_schema.cpp          // Custom Schema
├── 06_custom_serializer.cpp // Custom Serializer example
```

Compile examples:

```bash
cd examples
g++ -std=c++20 -I../include 01_basic.cpp -o 01_basic
./01_basic
```

Or use the provided `Makefile` / `CMakeLists.txt` to batch compile all examples.

> In all examples, the path to `bsp.hpp` is `../include/bsp.hpp`.

---

## Design Philosophy

* Inspired by **Minecraft communication protocols**
* High performance + type binding
* Supports complex data structures and high-frequency communication
* Zero runtime overhead using C++20 template features

---

## Contribution Guide

Welcome to submit issues / PRs:

1. Fix bugs
2. Add new Protocols or Codecs
3. Provide more usage examples
4. Performance optimizations

**Notes**:

* Ensure `examples/*.cpp` compiles before submitting a PR
* Follow C++20 standards
* Adhere to naming conventions and type conventions

---

## License

MIT LICENSE

Use and modification under MIT license.
