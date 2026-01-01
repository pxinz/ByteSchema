# BSP (ByteSchema) Usage Documentation

[中文版](./README-zh.md)

## 1. Introduction

`bsp` is a lightweight, templated **byte serialization/deserialization framework** that provides efficient serialization support for C++ native types, containers, and custom structs. Key features:

* **Fixed-width types** (`Fixed<>`) support byte-level representation of integers, floating-point numbers, and booleans
* **Variable-length types** (`Varint`) support variable-length integers, strings, byte arrays, and containers
* **Structured types** (`Schema`) support user-defined structs
* Supports `Option<T>` and `std::variant<Ts...>`
* Globally configurable (endianness, maximum container size, recursion depth, safety policies)
* Optional error handling (strict, medium, ignore)
* Requires `C++20` for `bsp.hpp`

The goal is a **cross-platform, controllable, safe, and flexible** binary protocol framework.

---

## 2. Basic Concepts

### 2.1 Protocol Tags

| Tag        | Description                                                       | Default Applied Types              |
|------------|-------------------------------------------------------------------|------------------------------------|
| `Fixed<N>` | Fixed-width type; `N` is meaningful only when T is a container    | integers, floats, booleans, tuples |
| `Varint`   | Variable-length type; suitable for integers, containers, strings  | vector, map, string, ByteArray     |
| `Schema`   | User-defined struct                                               | User-registered structs            |
| `Default`  | Default protocol, a placeholder mapping to `DefaultProtocol_t<T>` | All unspecified types              |

> Note: When `T` is a container, `Fixed<0>` indicates the container is empty and, in all default container `Serializer` implementations, results in no reading or writing.

### 2.2 Default Protocol Mapping

`proto::DefaultProtocol_t<T>` provides the default mapping from types to protocols:

| Type                              | Default Protocol |
|-----------------------------------|------------------|
| bool                              | Fixed<>          |
| Integers (signed/unsigned)        | Fixed<>          |
| Floating-point                    | Fixed<>          |
| std::string                       | Varint           |
| ByteArray                         | Varint           |
| std::vector<T>                    | Varint           |
| std::map<K,V>                     | Varint           |
| std::tuple<Ts...>                 | Fixed<>          |
| std::variant<Ts...>               | Varint           |
| Option<T>                         | Varint           |
| User struct T (registered Schema) | Schema           |
| Else                              | Default          |

> Note: `Default` is a placeholder, ultimately mapped to a concrete protocol by `DefaultProtocol_t<T>`.  
> Of course, you can customize the implementation of the Serializer under the Default protocol, in which case no further mapping will occur.
---

### 2.3 Global Options `GlobalOptions`

Used for global control of serialization behavior, safety limits, and ABI:

```c++
struct GlobalOptions {
    std::endian endian = std::endian::big; // Endianness
    size_t max_depth = 64;                 // Maximum recursion depth
    size_t max_container_size = 1 << 20;   // Maximum container element count
    size_t max_string_size = 1 << 20;      // Maximum string/ByteArray length
    bool strict_eof = false;               // Require EOF after reading the object
    ErrorPolicy error_policy = STRICT;     // Error policy
    static GlobalOptions &instance();
};
```

* **Error policies**: `STRICT` / `MEDIUM` / `IGNORE`
* **Safety constraints**: Overly long containers or strings will throw `LengthOverflow`

You can customize options as follows:

```c++
bsp::GlobalOptions::instance().max_depth = 128;
bsp::GlobalOptions::instance().max_container_size = 1<<16;
```

---

## 3. Basic Type Serialization

### 3.1 Boolean

```c++
bool b = true;
bsp::io::Writer w(os);
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::write(w, b);

bsp::io::Reader r(is);
bool read_b;
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::read(r, read_b);
```

* Written as **single byte**
* Only supports `Fixed<>`

### 3.2 Integer Types

* **Unsigned/Signed integers**
* Supports `Fixed<>` and `Varint` (variable-length encoding)
* Signed integers under Varint use **ZigZag encoding**

```c++
uint32_t u = 123456;
int32_t s = -42;

// Fixed
bsp::write<bsp::proto::Fixed<>>(w, u);
bsp::write<bsp::proto::Fixed<>>(w, s);

// Varint
bsp::write<bsp::proto::Varint>(w, u);
bsp::write<bsp::proto::Varint>(w, s);
```

### 3.3 Floating-Point Types

* Written by byte-copying IEEE754 representation
* Only supports `Fixed<>`

```c++
float f = 3.14f;
bsp::write<bsp::proto::Fixed<>>(w, f);
```

---

## 4. Container Types

### 4.1 vector

```c++
std::vector<int> v = {1, 2, 3};

// Variable-length serialization
bsp::write(bsp::proto::Varint, w, v);
bsp::read(bsp::proto::Varint, r, v);

// Fixed-length
bsp::write<bsp::proto::Fixed<3>>(w, v);
```

### 4.2 map

```c++
std::map<std::string, int> m = {{"a",1}, {"b",2}};
bsp::write(bsp::proto::Varint, w, m);
bsp::read(bsp::proto::Varint, r, m);
```

### 4.3 string / ByteArray

* Variable-length: Prefixed with varint length
* Fixed-length: Write N bytes, pad if insufficient

```c++
std::string s = "hello";
bsp::write(bsp::proto::Varint, w, s);

bsp::types::ByteArray ba = {1,2,3};
bsp::write(bsp::proto::Varint, w, ba);
```

---

## 5. Optional Type `Option<T>`

```c++
bsp::types::Option<int> opt{42};
bsp::write(w, opt);  // Prefixed flag 0/1 + optional value
```

* Flag = 0 indicates no value
* Flag = 1 indicates a value

---

## 6. Variant Type `std::variant<Ts...>`

* Write prefixed index (varint)
* Then write the value of the corresponding type

```c++
std::variant<int, std::string> var = "hi";
bsp::write(w, var);
bsp::read(r, var);
```

* Index out of range throws `VariantOutOfRange`

---

## 7. Custom Struct `Schema`

### 7.1 Define Struct

```c++
struct Point { int x; int y; };
struct Rect { Point p1; Point p2; };
```

### 7.2 Register Struct

```c++
BSP_REGISTER_STRUCT(Point,
    BSP_FIELD(Point, x),
    BSP_FIELD(Point, y)
);

BSP_REGISTER_STRUCT(Rect,
    BSP_FIELD(Rect, p1),
    BSP_FIELD(Rect, p2)
);
```

* `BSP_FIELD` automatically uses `DefaultProtocol_t<T>`
* `BSP_FIELD_WITH` allows custom protocols

### 7.3 Serialize Struct

```c++
Point pt{1,2};
bsp::write(w, pt);
bsp::read(r, pt);
```

* Automatically serializes in field order
* Default protocol determined by `DefaultProtocol`; becomes `Schema` after registration

### 7.4 Custom Protocol Fields

Sometimes sub-fields of a struct require special protocols:

```c++
BSP_FIELD_WITH(Point, x, bsp::proto::Varint)
```

---

## 8. Custom DefaultProtocol

Sometimes you may want to modify the default protocol for a type:

```c++
namespace bsp::proto {
    template<>
    struct DefaultProtocol<MyType> {
        using type = Fixed<4>; // Make MyType default to Fixed<4>
    };
}
```

* No need to modify Serializer code
* Can still explicitly specify a protocol during specific calls

```c++
bsp::write<Fixed<8>>(w, my_obj);  // Explicit protocol overrides default
```

---

## 9. Custom Serializer

If the default protocol is insufficient, you can customize Serializer by implementing `write` and `read`.

### 9.1 Example: Encrypted Integer

```c++
#include <iostream>
#include <sstream>
#include "bsp.hpp"

struct Encrypt {};

namespace bsp::serialize {
    template<>
    struct Serializer<int, Encrypt> {
        static void write(io::Writer &w, const int &s) {
            int encrypted = s ^ 0x55AA; // XOR encryption
            utils::write_uleb128(w, encrypted);
        }

        static void read(io::Reader &r, int &out) {
            int encrypted = static_cast<int>(utils::read_uleb128(r));
            out = encrypted ^ 0x55AA;
        }
    };
}

int main() {
    std::stringstream ss;
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    int s1 = 12345;
    bsp::write<Encrypt>(w, s1);

    int s2;
    bsp::read<Encrypt>(r, s2);
    std::cout << s2 << "\n"; // Outputs 12345
}
```

### 9.2 Characteristics

* Must provide `write(io::Writer&, const T&)` and `read(io::Reader&, T&)`
* Supports any type `T`
* Can be used together with the previous method to override the default protocol (`DefaultProtocol`) or directly specify a protocol in the `Protocol` tag

---

## 10. I/O Interface

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

bsp::write(w, value);          // Automatically uses DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value); // Specify protocol

bsp::read(r, value);
bsp::read<bsp::proto::Fixed<4>>(r, value);
```

---

## 11. Error Handling

* `bsp::error::ProtocolError` base class

* Common derived errors:
    * `UnexpectedEOF`
    * `InvalidVarint`
    * `LengthOverflow`
    * `VariantOutOfRange`
    * `ABIError`

* Global error policy:

```c++
bsp::GlobalOptions::instance().error_policy = bsp::MEDIUM;
```

* Default strict mode (throws exception on error)

---

## 12. Small Example

```c++
#include "bsp.hpp"
#include <sstream>
#include <iostream>

struct Point { int x; int y; };
BSP_REGISTER_STRUCT(Point,
    BSP_FIELD(Point, x),
    BSP_FIELD(Point, y)
);

int main() {
    std::stringstream ss;

    Point pt1{10, 20};
    bsp::write(ss, pt1);

    Point pt2{};
    bsp::read(ss, pt2);

    std::cout << "Point: " << pt2.x << ", " << pt2.y << "\n";

    std::vector<int> vec{1,2,3};
    bsp::write(ss, vec);

    std::vector<int> vec2;
    bsp::read(ss, vec2);

    for(auto v: vec2) std::cout << v << " ";  // 1 2 3
}
```

* Demonstrates struct serialization, vector serialization
* Automatically uses `DefaultProtocol`
* Safety checks (overly long containers/strings)