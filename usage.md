# BSP (ByteSchema) User Documentation

[中文版](./usage-zh.md)

---

## 1. Introduction

`bsp` is a lightweight, templated **byte serialization/deserialization framework** for efficient binary serialization of C++ native types, containers, and custom structs.

Key features:

* **Fixed-width types** (`Fixed<>`) support byte-level representation of integers, floating-point numbers, and booleans
* **Variable-length types** (`Varint`) support variable-length integers, strings, byte arrays, and containers
* **Structured types** (`Schema`) support user-defined struct registration
* Supports `Option<T>`, `std::variant<Ts...>`
* Globally configurable (endianness, maximum container size, recursion depth, safety policies)
* Error handling policies: strict, medium, ignore
* Depends only on C++20 or above

Goal: A **cross-platform, controllable, safe, flexible** binary protocol framework.

---

## 2. Basic Concepts

### 2.1 Protocol Tags

`bsp::proto` provides built-in protocol tags:

| Tag        | Description                                                      | Default Applied Types              |
|------------|------------------------------------------------------------------|------------------------------------|
| `Fixed<N>` | Fixed-width type; `N` meaningful only when T is a container      | integers, floats, booleans, tuples |
| `Varint`   | Variable-length type; suitable for integers, containers, strings | vector, map, string, ByteArray     |
| `Schema`   | User-defined struct                                              | Registered structs                 |
| `Default`  | Default protocol, a placeholder                                  | All unspecified types              |

> Note: `Fixed<0>` for containers indicates an empty container; default Serializer implementations perform no read/write.

### 2.2 Default Protocol Mapping

`proto::DefaultProtocol_t<T>` provides default mapping from types to protocols:

| Type                         | Default Protocol |
|------------------------------|------------------|
| bool                         | Fixed<>          |
| Integers (signed/unsigned)   | Fixed<>          |
| Floating-point               | Fixed<>          |
| std::string                  | Varint           |
| bsp::types::ByteArray        | Varint           |
| std::vector\<T>              | Varint           |
| std::map<K,V>                | Varint           |
| std::tuple<Ts...>            | Fixed<>          |
| std::variant<Ts...>          | Varint           |
| bsp::types::Option\<T>       | Varint           |
| bsp::types::PVal<T, P>       | P                |
| struct T (registered Schema) | Schema           |
| Others                       | Default          |

> `Default` is ultimately mapped to a concrete protocol by `DefaultProtocol_t<T>`. You can also customize `Serializer` implementation under the `Default` protocol to skip mapping.

### 2.3 `Serializer<T, Protocol>`

`bsp::serialize::Serializer<T, Protocol>` defines read/write operations for type T under protocol Protocol:

```c++
Serializer<T, P>::write(io::Writer &w, const T &v);
Serializer<T, P>::read(io::Reader &r, T &v);
```

> ⚠ Note: Protocol must be explicit or mapped through DefaultProtocol.

### 2.4 Global Options `GlobalOptions`

Controls serialization behavior, safety limits, and ABI:

```c++
struct GlobalOptions {
    std::endian endian = std::endian::big;  // Endianness
    size_t max_depth = 64;                  // Maximum recursion depth
    size_t max_container_size = 1 << 20;    // Maximum container element count
    size_t max_string_size = 1 << 20;       // Maximum string/ByteArray length
    bool strict_eof = false;                // Require EOF after reading object
    bsp::ErrorPolicy error_policy = STRICT; // Error policy

    static GlobalOptions &instance();
};
```

* Error policies: `STRICT` / `MEDIUM` / `IGNORE`
* Safety constraints: Overly long containers or strings throw `LengthOverflow`

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

* Written as single byte
* Only supports `Fixed<>`

### 3.2 Integer Types

* Supports signed/unsigned integers
* Protocols: `Fixed<>` and `Varint` (ZigZag encoding for signed, LEB128 for unsigned)

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
* Fixed-length: Write N bytes, pad if insufficient

```c++
std::string s = "hello";
bsp::write(bsp::proto::Varint, w, s);

bsp::types::ByteArray ba = {1,2,3};
bsp::write(bsp::proto::Varint, w, ba);
```

---

## 5. Variant Type `std::variant<Ts...>` Serialization

* Write prefixed index (varint)
* Then write value of corresponding type

```c++
std::variant<int, std::string> var = "hi";
bsp::write(w, var);
bsp::read(r, var);
```

* Index out of range throws `VariantOutOfRange`

---

## 6. Types Provided by bsp::types

### 6.1 Optional Type `Option<T>`

```c++
bsp::types::Option<int> opt{42};
if (opt.has_value) std::cout << opt.value;

bsp::write(w, opt);  // Prefixed flag 0/1 + optional value
```

* Flag=0 indicates no value
* Flag=1 indicates a value

### 6.2 Zero-Copy View `ByteView`

```c++
bsp::types::ByteView view;
std::cout << view.size;

bsp::read(r, view);
```

* Non-owning view: data points to buffer allocated during read
* Caller responsible for deallocation (`delete[]`)
* Use ByteArray or custom buffer to avoid allocation

### 6.3 Protocol-Carrying Value Type `PVal<T, Protocol>`

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
* Can be directly accessed as type `T`, but using `instance.value` is safer

> ⚠ Potential implicit conversion errors when using `PVal<T, Protocol> instance` as a `T` type instance:
>
> ```c++
> void f(int&);
> void f(types::PVal<int, proto::Fixed<16>>&);
> 
> types::PVal<int, Fixed<16>> x;
> f(x); // May call f(int&) instead of f(PVal<...>&)
> ```

---

## 7. Custom Struct Schema

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

* `BSP_FIELD` uses `DefaultProtocol_t<T>` by default
* `BSP_FIELD_WITH` allows custom protocol

> ⚠ clangd false error example:
>
> ```
> Clangd: In template: static assertion failed due to requirement '!std::is_same_v<bsp::proto::Default, bsp::proto::Default>': No concrete DefaultProtocol for this type
> ```
>
> Reason: Macros generate Schema specialization, but `DefaultProtocol<T>` partial specialization triggered via concept may confuse clangd static analyzer.
> **Solutions**:
>
> 1. Recompile and reload IDE
> 2. Ignore/disable clangd warning
> 3. Explicitly provide `DefaultProtocol` specialization:
>
>   ```c++
>   namespace bsp::proto { template<> struct DefaultProtocol<Point> { using type = proto::Schema; }; }
>   ```
>
> 4. Split macros: Separate Schema registration from explicit DefaultProtocol registration

### 7.3 Custom Protocol Fields

```c++
BSP_FIELD_WITH(Point, x, bsp::proto::Varint);
```

* Avoid using `PVal<T, Protocol>` directly in struct fields to reduce implicit conversion issues

### 7.4 Serialize Struct

```c++
Point pt{1,2};
bsp::write(w, pt);
bsp::read(r, pt);
```

* Serialized in field order
* Default protocol determined by `DefaultProtocol` (becomes `Schema` after registration)

---

## 8. Advanced Customization

### 8.1 Custom DefaultProtocol

```c++
namespace bsp::proto {
    template<>
    struct DefaultProtocol<MyType> {
        using type = Fixed<4>; // Default to Fixed<4>
    };
}
```

* Override default protocol
* Still can explicitly specify protocol during calls:

```c++
bsp::write<Fixed<8>>(w, my_obj);
```

### 8.2 Custom Serializer

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

* Must provide `write` and `read`
* Supports any type T
* Can be used with `DefaultProtocol` or directly specify protocol

---

## 9. I/O Interface

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

bsp::write(w, value);                   // Uses DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value); // Specify protocol

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

* Default strict mode: throws exception on any error

---

## 11. Common Issues

* **Container element protocol**: Use `PVal` to explicitly bind element protocol, avoid DefaultProtocol ambiguity
* **PVal implicit conversion**: See warning in 6.3
* **ByteView memory management**: read performs new allocation, caller must delete[]
* **clangd false errors**: See 7.2
* **Fixed container size assertion**: Mismatch throws `LengthOverflow`
* **Version compatibility**: Schema is order-sensitive, lacks field IDs, field order changes affect compatibility, be careful with cross-language usage