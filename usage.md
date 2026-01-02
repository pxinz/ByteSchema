# BSP (ByteSchema) Usage Documentation

[中文版本](./usage-zh.md)

---

## 1. Introduction

`bsp` is a lightweight, template-based **byte serialization/deserialization framework** for efficient binary serialization of C++ native types, containers, and custom structures.

Main features:

* **Fixed-width types** (`Fixed<>`) support byte-level representation of integers, floating-point numbers, and booleans
* **Variable-length types** (`Varint`) support variable-length integers, strings, byte arrays, and containers
* **Structured types** (`Schema`) support user-defined struct registration
* Support for `Option<T>` and `std::variant<Ts...>`
* Global configurable options (endianness, maximum container size, recursion depth, safety policies)
* Selectable error handling policies: strict, medium, ignore
* Requires C++20 or later

Goal: A **cross-platform, controllable, safe, flexible** binary protocol framework.

---

## 2. Basic Concepts

### 2.1 Protocol Tags

`bsp::proto` provides built-in protocol tags:

| Tag         | Description                    | Default Applied Types                |
|-------------|--------------------------------|--------------------------------------|
| `Fixed<N>`  | Fixed-width type, `N` only meaningful when T is a container | Integers, floats, booleans, tuples |
| `Varint`    | Variable-length type, suitable for integers, containers, strings | vector, map, string, ByteArray |
| `Schema`    | User-defined structures        | Registered structs                  |
| `Default`   | Default protocol, placeholder  | All unspecified types                |

> For containers, `Fixed<0>` represents an empty container, which in the default Serializer behaves as no read/no write.

### 2.2 Default Protocol Mapping

`proto::DefaultProtocol_t<T>` provides default type-to-protocol mapping:

| Type                     | Default Protocol |
|--------------------------|------------------|
| bool                     | Fixed<>          |
| Integers (signed/unsigned) | Fixed<>          |
| Floating-point numbers   | Fixed<>          |
| std::string              | Varint           |
| bsp::types::ByteArray    | Varint           |
| std::vector\<T>          | Varint           |
| std::map<K,V>            | Varint           |
| std::tuple<Ts...>        | Fixed<>          |
| std::variant<Ts...>      | Varint           |
| bsp::types::Option\<T>   | Varint           |
| bsp::types::PVal<T, P>   | P                |
| struct T (registered Schema) | Schema        |
| Others                   | Default          |

> Default is eventually mapped to a concrete protocol by `DefaultProtocol_t<T>`. You can also customize Serializer implementations under the Default protocol.

### 2.3 `Serializer<T, Protocol>`

`bsp::serialize::Serializer<T, Protocol>` defines read/write operations for type T under protocol Protocol:

```c++
Serializer<T, P>::write(io::Writer &w, const T &v);
Serializer<T, P>::read(io::Reader &r, T &v);
```

> ⚠ Note: Protocol must be explicit or mapped via DefaultProtocol.

### 2.4 Global Options `GlobalOptions`

Controls serialization behavior, safety limits, and ABI:

```c++
struct GlobalOptions {
    std::endian endian = std::endian::big;  // Byte order
    size_t max_depth = 64;                  // Maximum recursion depth
    size_t max_container_size = 1 << 20;    // Maximum container elements
    size_t max_string_size = 1 << 20;       // Maximum string/ByteArray length
    bool strict_eof = false;                // Require EOF after reading object
    bsp::ErrorPolicy error_policy = STRICT; // Error policy

    static GlobalOptions &instance();
};
```

* Error policies: `STRICT` / `MEDIUM` / `IGNORE`
* Safety constraints: Overly large containers or strings throw `LengthOverflow`

```c++
bsp::GlobalOptions::instance().max_depth = 128;
bsp::GlobalOptions::instance().max_container_size = 1 << 16;
```

---

## 3. Native Type Serialization

### 3.1 Boolean

```c++
bool b = true;
bsp::io::Writer w(os);
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::write(w, b);

bsp::io::Reader r(is);
bool read_b;
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::read(r, read_b);
```

* Writes single byte
* Only supports `Fixed<>`

### 3.2 Integer Types

* Supports signed/unsigned integers
* Protocols support `Fixed<>` and `Varint` (ZigZag encoding for signed, LEB128 for unsigned)

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

* Writes IEEE754 byte-by-byte copy
* Only supports `Fixed<>`

```c++
float f = 3.14f;
bsp::write<bsp::proto::Fixed<>>(w, f);
```

---

## 4. Container Type Serialization

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
* Fixed: Writes N bytes, padded if insufficient

```c++
std::string s = "hello";
bsp::write(bsp::proto::Varint, w, s);

bsp::types::ByteArray ba = {1,2,3};
bsp::write(bsp::proto::Varint, w, ba);
```

---

## 5. Variant Type `std::variant<Ts...>` Serialization

* Writes prefix index (varint)
* Then writes value of corresponding type

```c++
std::variant<int, std::string> var = "hi";
bsp::write(w, var);
bsp::read(r, var);
```

* Out-of-range index throws `VariantOutOfRange`

---

## 6. Types Provided by bsp::types

### 6.1 Optional Type `Option<T>`

```c++
bsp::types::Option<int> opt{42};
if (opt.has_value) std::cout << opt.value;

bsp::write(w, opt);  // Prefix flag 0/1 + optional value
```

* Flag=0 indicates no value
* Flag=1 indicates value present

### 6.2 Zero-Copy View `ByteView`

```c++
bsp::types::ByteView view;
std::cout << view.size;

bsp::read(r, view);
```

* Non-owning view: data points to buffer allocated during read
* Caller responsible for deallocation (`delete[]`)
* To avoid allocation, use ByteArray or custom buffer

### 6.3 Protocol-Bound Value Type `PVal<T, Protocol>`

```c++
using Layer3 = types::PVal<int, proto::Varint>;
using Layer2 = types::PVal<std::vector<Layer3>, proto::Fixed<4>>;
using Layer1 = types::PVal<std::vector<Layer2>, proto::Varint>;

Layer1 arr; // 3D array example
bsp::read(r, arr);
std::cout << arr[0][1][2]; // Direct access to underlying type
std::cout << arr.value[0].value[1].value[2]; // Safer usage
```

* Default protocol determined by template parameter `Protocol`
* `Serializer<PVal<T, ProtocolT>>` corresponds to `Serializer<T, Protocol=ProtocolT>`
* Can be accessed as type `T`, but using `instance.value` is safer

> ⚠ Potential implicit conversion errors when using `PVal<T, Protocol> instance` as `T` type instance:
>
> ```c++
> void f(int&);
> void f(types::PVal<int, proto::Fixed<16>>&);
>
> types::PVal<int, Fixed<16>> x;
> f(x); // Might call f(int&) instead of f(PVal<...>&)
> ```

---

## 7. Custom Structure Schema

### 7.1 Define Structure

```c++
struct Point { int x; int y; };
struct Rect { Point p1; Point p2; };
```

### 7.2 Register Structure

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

* `BSP_FIELD` defaults to `DefaultProtocol_t<T>`
* `BSP_FIELD_WITH` allows custom protocol

> ⚠ When registering Schema with macros, Clangd may show red errors:
> ```
> Clangd: In template: static assertion failed due to requirement '!std::is_same_v<bsp::proto::Default, bsp::proto::Default>': No concrete DefaultProtocol for this type
> ```
> This **does not** affect normal compilation and execution.
>
> **Reason:**
> 1. **Macro expansion generates specializations**:  
     `BSP_REGISTER_STRUCT(T, ...)` generates `Serializer` and `DefaultProtocol` partial specializations for compile-time protocol binding and serialization logic.
> 2. **Clangd static analysis misjudgment**:
     Clangd, when analyzing templates, finds `DefaultProtocol<T>` is still `bsp::proto::Default`, triggering template constraint checks (concept or `static_assert`). Actually, **the compiler itself won't error**; it's just Clangd's template parser failing to correctly deduce macro-generated partial specializations in the IDE.
>
> **Solutions:**
> 1. **Reload IDE after successful compilation**: When the compiler (GCC/Clang/MSVC) passes compilation, modern IDEs (like CLion) usually update static check results
> 2. **Ignore IDE hints**: May lead to unexpected consequences
> 3. **Explicitly provide DefaultProtocol specialization**
>   ```c++
>   struct MyStruct { int a; int b; };
>
>   BSP_REGISTER_STRUCT(MyStruct,
>       BSP_FIELD(MyStruct, a),
>       BSP_FIELD(MyStruct, b)
>   );
>
>   // Explicitly specify default protocol as Schema
>   namespace bsp::proto {
>       template<>
>       struct DefaultProtocol<MyStruct> {
>           using type = proto::Schema;
>       };
>   }
>   ```
> 4. **Update Clangd / IDE**: Clangd 16+ has more stable template macro analysis

### 7.3 Custom Protocol Fields

```c++
BSP_FIELD_WITH(Point, x, bsp::proto::Varint);
```

### 7.4 Serializing Structures

```c++
Point pt{1,2};
bsp::write(w, pt);
bsp::read(r, pt);
```

* Serializes in field order
* Default protocol determined by `DefaultProtocol` (defaults to `Schema` after registration)

---

## 8. Advanced Customization

### 8.1 Custom DefaultProtocol

Define in the `bsp::proto` namespace for global usage

```c++
namespace bsp::proto {
    template<>
    struct DefaultProtocol<MyType> {
        using type = Fixed<4>; // Default serialize as Fixed<4>
    };
}
```

### 8.2 Custom Serializer

Define in the `bsp::serializer` namespace for global usage

```c++
struct Encrypt {};

namespace bsp::serialize {
    template<>
    struct Serializer<int, Encrypt> {
        static void write(io::Writer &w, const int &s) {
            int encrypted = s ^ 0x55AA;
            utils::write_uleb128(w, encrypted);
        }
        static void read(io::Reader &r, int &out) {
            int encrypted = static_cast<int>(utils::read_uleb128(r));
            out = encrypted ^ 0x55AA;
        }
    };
}
```

---

## 9. I/O Interface

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

// Functional style
bsp::write(w, value);                     // Uses DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value); // Specify protocol

// Object-oriented style (equivalent)
w.write(value);                           // Equivalent functional call
w.write<bsp::proto::Varint>(value);       // Equivalent with explicit protocol

// Functional style
bsp::read(r, value);
bsp::read<bsp::proto::Fixed<4>>(r, value);

// Object-oriented style (equivalent)
r.read(value);
r.read<bsp::proto::Fixed<4>>(value);
```

> Both functional and object-oriented styles are supported. Choose based on preference or context.

---

## 9. I/O Interface

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

bsp::write(w, value);                   // Uses DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value); // Specified protocol

bsp::read(r, value);
bsp::read<bsp::proto::Fixed<4>>(r, value);
```

---

## 10. Error Handling

* Base class: `bsp::error::ProtocolError`

* Derived exceptions:

  * `UnexpectedEOF`
  * `InvalidVarint`
  * `LengthOverflow`
  * `VariantOutOfRange`
  * `ABIError`

* Global policy:

```c++
bsp::GlobalOptions::instance().error_policy = bsp::MEDIUM;
```

* Default is strict mode: throws exception on any error

---

## 11. Common Issues

* **Container element protocol**: Use `PVal` to explicitly bind element protocol, avoiding DefaultProtocol ambiguity
* **PVal implicit conversion**: Refer to warning in 6.3
* **ByteView memory management**: read allocates with new, caller must delete[]
* **Clangd red errors**: See 7.2
* **Fixed container size assertion**: Mismatch throws `LengthOverflow`
* **Version compatibility**: Schema is order-sensitive, no field IDs; field order changes affect compatibility

---

## 12. Examples Guide

ByteSchema provides examples covering common and advanced usage:

```
examples/
├── 01_basic.cpp           // Native type serialization
├── 02_vector_map.cpp      // vector / map serialization
├── 03_option_variant.cpp  // Option / variant
├── 04_pval.cpp            // PVal / multi-dimensional arrays
├── 05_schema.cpp          // Custom Schema
├── 06_custom_serializer.cpp // Custom Serializer
```

Compile examples:

```bash
cd examples
g++ -std=c++20 -I../include 01_basic.cpp -o 01_basic
./01_basic
```

> In all examples, the path to `bsp.hpp` is `../include/bsp.hpp`.