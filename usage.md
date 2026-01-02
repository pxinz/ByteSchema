# BSP (ByteSchema) Usage Documentation

## 1. Introduction

`bsp` is a lightweight, template-based **byte serialization/deserialization framework** for efficient binary serialization of C++ native types, containers, and custom structures.

Main features:

* **Fixed-width types** (`Fixed<>`) support byte-level representation of integers, floating-point numbers, and booleans
* **Variable-length types** (`Varint`) support variable-length integers, strings, byte arrays, and containers
* **Structured types** (`Schema`) support user-defined structure registration
* Support for `Option<T>`, `std::variant<Ts...>`
* Global configuration control (endianness, maximum container size, recursion depth, safety policies)
* Selectable error handling policies: strict, medium, ignore
* Requires C++20 or higher

Goal: A **cross-platform, controllable, safe, flexible** binary protocol framework.

## 2. Basic Concepts

### 2.1 Protocol Tags

`bsp::proto` provides built-in protocol tags:

| Tag | Description | Default Applied Types |
|-----|-------------|-----------------------|
| `Fixed<N>` | Fixed-width type, `N` only meaningful when T is a container | integers, floats, booleans, tuples |
| `Varint` | Variable-length type, suitable for integers, containers, strings | vector, map, string, ByteArray |
| `Schema` | User-defined structures | registered structures |
| `CVal` | Runtime polymorphic class | CVal |
| `Default` | Default protocol, placeholder | all unspecified types |

> For containers, `Fixed<0>` represents an empty container, which behaves as no read/no write in the default Serializer.

### 2.2 Default Protocol Mapping

`proto::DefaultProtocol_t<T>` provides default type-to-protocol mapping:

| Type | Default Protocol |
|------|------------------|
| bool | Fixed<> |
| integers (signed/unsigned) | Fixed<> |
| floating-point numbers | Fixed<> |
| std::string | Varint |
| bsp::types::ByteArray | Varint |
| std::vector\<T> | Varint |
| std::map<K,V> | Varint |
| std::tuple<Ts...> | Fixed<> |
| std::variant<Ts...> | Varint |
| bsp::types::Option\<T> | Varint |
| bsp::types::PVal<T, P> | P |
| struct T (registered Schema) | Schema |
| CVal | CVal |
| Others | Default |

> Default is eventually mapped to a concrete protocol by `DefaultProtocol_t<T>`. Custom Serializer implementations under the Default protocol can also be defined.

### 2.3 `Serializer<T, Protocol>`

`bsp::serialize::Serializer<T, Protocol>` defines read/write operations for type T under protocol Protocol:

```c++
Serializer<T, P>::write(io::Writer &w, const T &v);
Serializer<T, P>::read(io::Reader &r, T &out);
```

We provide more convenient usage:

```c++
// Functional programming, if P is not specified, use DefaultProtocol_t<T>
bsp::write<P>(io::Writer &w, const T &v);
// Object-oriented programming
w.write<P>(const T &v);
```

> ⚠ Note: Protocol must be explicit or mapped via DefaultProtocol.

### 2.4 Global Options `GlobalOptions`

Controls serialization behavior, safety limits, and ABI:

```c++
struct GlobalOptions {
    std::endian endian = std::endian::big;  // byte order
    size_t max_depth = 64;                  // maximum recursion depth
    size_t max_container_size = 1 << 20;    // maximum container elements
    size_t max_string_size = 1 << 20;       // maximum string/ByteArray length
    bool strict_eof = false;                // require EOF after reading object
    bsp::ErrorPolicy error_policy = STRICT; // error policy

    static GlobalOptions &instance();
};
```

* Error policies: `STRICT` / `MEDIUM` / `IGNORE`
* Safety constraints: overly large containers or strings throw `LengthOverflow`

```c++
bsp::GlobalOptions::instance().max_depth = 128;
bsp::GlobalOptions::instance().max_container_size = 1 << 16;
```

## 3. Native Type Serialization

### 3.1 Boolean

```c++
bool b = true;
bsp::io::Writer w(os);
w.write(b);  // write single byte

bsp::io::Reader r(is);
bool read_b;
r.read(read_b);  // read boolean
```

* Structure: `[single byte 0x00 / 0x01]`
* Only supports `Fixed<>`

### 3.2 Integer Types

```c++
uint32_t u = 123456;
int32_t s = -42;

// Fixed encoding
bsp::write<bsp::proto::Fixed<>>(w, u);
bsp::write<bsp::proto::Fixed<>>(w, s);

// Varint encoding
bsp::write<bsp::proto::Varint>(w, u);
bsp::write<bsp::proto::Varint>(w, s);
```

* Supports signed/unsigned integers
* Supports `Fixed<>` and `Varint` (ZigZag encoding for signed, LEB128 for unsigned)

### 3.3 Floating-Point Types

```c++
float f = 3.14f;
bsp::write<bsp::proto::Fixed<>>(w, f);
```

* Writes IEEE754 byte-by-byte copy
* Only supports `Fixed<>`

## 4. Container Type Serialization

"Container types" here refer to types with sub-elements:

* Sub-elements use the default protocol of their corresponding class  
  To specify sub-element protocols, see `PVal` usage
* Inherited classes use encoding methods of their parent classes (e.g., `unordered_map<K, V>` uses `map<K, V>` encoding)

### 4.1 vector

```c++
std::vector<int> v = {1, 2, 3};

// Variable-length serialization
bsp::write<bsp::proto::Varint>(w, v);
bsp::read<bsp::proto::Varint>(r, v);

// Fixed-length
bsp::write<bsp::proto::Fixed<3>>(w, v);
```

* `Varint` structure: `[LEB128 length header][element 1][element 2]...`
* `Fixed<N>` structure: `[element 1][element 2]...[element N]`

### 4.2 map

```c++
std::map<std::string, int> m = {{"a",1}, {"b",2}};
bsp::write<bsp::proto::Varint>(w, m);
bsp::read<bsp::proto::Varint>(r, m);
```

* `Varint` structure: `[LEB128 length header][key 1][value 1][key 2][value 2]...`
* `Fixed<N>` structure: `[key 1][value 1][key 2][value 2]...[key N][value N]`

### 4.3 string / ByteArray

* Variable-length: prefixed with varint length
* Fixed: writes N bytes, padded if insufficient

```c++
std::string s = "hello";
w.write(s);  // use default protocol (Varint)

bsp::types::ByteArray ba = {1,2,3};
w.write(ba);  // use default protocol (Varint)
```

* `Varint` structure: `[LEB128 length header][data of corresponding length]`
* `Fixed<N>` structure: `[data of length N]`

### 4.4 Variant Type `std::variant<Ts...>`

```c++
std::variant<int, std::string> var = "hi";
w.write(var);  // write variant
r.read(var);   // read variant
```

* Only supports `Varint`
* Structure: `[LEB128 index][value of corresponding type]`
* Uses the type at the index position in `Ts...`
* Out-of-range index throws `VariantOutOfRange`

### 4.5 Optional Type `Option<T>`

This type is provided by `bsp::types`:

```c++
bsp::types::Option<int> opt{42};
if (opt.has_value) std::cout << opt.value;  // check and access value

w.write(opt);  // write optional type
r.read(opt);   // read optional type
```

* Only supports `Varint`
* Structure: `[Bool Flag][if flag=1, value of corresponding type]`

### 4.6 Zero-Copy View `ByteView`

This type is provided by `bsp::types`:

```c++
bsp::types::ByteView view;
std::cout << view.size;  // view size

r.read(view);  // read into view
```

* Supports `Varint` and `Fixed<N>`
* Same structure as `ByteArray`
* Non-owning view: data points to buffer allocated during read
* Caller responsible for deallocation (`delete[]`)
* To avoid allocation, use ByteArray or custom buffer

## 5. Types That Can Control Protocol

### 5.1 Protocol-Bound Value Type `PVal<T, Protocol>`

This type is provided by `bsp::types`:

```c++
using Layer3 = types::PVal<int, proto::Varint>;
using Layer2 = types::PVal<std::vector<Layer3>, proto::Fixed<4>>;
using Layer1 = types::PVal<std::vector<Layer2>, proto::Varint>;

Layer1 arr;  // 3D array example
bsp::read(r, arr);

// Direct access to underlying type
std::cout << arr[0][1][2];
// Safer usage
std::cout << arr.value[0].value[1].value[2];

// Can also use nested notation
using 3DArray = types::PVal<
                    types::PVal<
                        types::PVal<std::vector<int>, proto::Varint>,
                        proto::Fixed<4>
                    >,
                    proto::Varint
                >;
```

* Default protocol determined by template parameter `Protocol`  
  `Serializer<PVal<T, ProtocolT>>` corresponds to `Serializer<T, Protocol=ProtocolT>`
* Can be accessed as type `T`, but using `instance.value` is safer

> ⚠ Implicit conversion errors when using `PVal<T, Protocol> instance` as `T` type instance:
>
> ```c++
> void f(int&);
> void f(types::PVal<int, proto::Fixed<16>>&);
>
> types::PVal<int, Fixed<16>> x;
> f(x);  // might call f(int&) instead of f(PVal<...>&)
> ```

### 5.2 Runtime Polymorphic Value Type `CVal`

`CVal` is a **runtime polymorphic value type** based on virtual classes, replacing `Any` for storing values of different types, while supporting dynamic protocol serialization.

#### 5.2.1 Definition and Inheritance

```c++
struct CVal {
    virtual ~CVal() = default;

    // Write serialized data, protocol can specify different protocols
    virtual void write(io::Writer &w, const std::type_info &protocol) const = 0;

    // Read serialized data, protocol can specify different protocols
    virtual void read(io::Reader &r, const std::type_info &protocol) = 0;
};
```

* Must implement `write` and `read` in subclasses
* Default protocol is `proto::CVal`, can also specify other types

#### 5.2.2 Example: Integer `CVal` Subclass

`IntCVal` subclass responsible for its own type serialization:

```c++
struct IntCVal : types::CVal {
    int value = 0;

    IntCVal() = default;
    explicit IntCVal(int v) : value(v) {}

    void write(io::Writer &w, const std::type_info &protocol) const override {
        serialize::Serializer<int, proto::Varint>::write(w, value);
    }

    void read(io::Reader &r, const std::type_info &protocol) override {
        serialize::Serializer<int, proto::Varint>::read(r, value);
    }
};
```

#### 5.2.3 Serialization

```c++
// Single value
std::stringstream ss;
io::Writer w(ss);
io::Reader r(ss);

IntCVal writeVal(12345);
writeVal.write(w, typeid(proto::CVal));  // use CVal's own write
w.write(writeVal);                       // use interface read

IntCVal readVal;
readVal.read(r, typeid(proto::CVal));    // use CVal's own read
r.read(readVal);                         // use interface read

// Container nesting
std::vector<IntCVal> writeVec = {IntCVal(111), IntCVal(-222), IntCVal(333)};
w.write(writeVec);

std::vector<IntCVal> readVec;
r.read(readVec);
```

#### 5.2.4 Protocol Control

You can use the `protocol` parameter to control protocol:

```c++
struct IntCVal : types::CVal {
    int value = 0;

    IntCVal() = default;
    explicit IntCVal(int v) : value(v) {}

    void write(io::Writer &w, const std::type_info &protocol) const override {
        if (protocol == typeid(proto::Fixed<>)){
            serialize::Serializer<int, proto::Fixed<>>::write(w, value);
        } else {
            serialize::Serializer<int, proto::Varint>::write(w, value);
        }
    }

    void read(io::Reader &r, const std::type_info &protocol) override {
        if (protocol == typeid(proto::Fixed<>)){
            serialize::Serializer<int, proto::Fixed<>>::read(r, value);
        } else {
            serialize::Serializer<int, proto::Varint>::read(r, value);
        }
    }
};
```

> ⚠ The `protocol` parameter can only preserve `type_info`, e.g., `N` in `Fixed<N>` is erased.

> 💡 **Usage Recommendations**
> * `CVal` is a replacement for `Any`, can store multiple types without templates
> * Virtual classes bring additional overhead, recommend using `CVal` only when strong runtime polymorphism is needed, or structures are extremely complex
> * `CVal` itself doesn't manage memory. Need to manage manually or use `unique_ptr` / `shared_ptr`
> * In serialization implementation, can reuse other `Serializer<T, Protocol>` implementations

## 6. Custom Structure Schema

### 6.1 Define Structure

```c++
struct Point { int x; int y; };
struct Rect { Point p1; Point p2; };
```

### 6.2 Register Structure

```c++
BSP_REGISTER_STRUCT(Point,
    BSP_FIELD(Point, x),
    BSP_FIELD(Point, y)
);

BSP_REGISTER_STRUCT(Rect,
    BSP_FIELD(Rect, p1),
    BSP_FIELD(Rect, p2)
);

// Use the following macro to customize field protocol
// BSP_FIELD_WITH(Point, x, bsp::proto::Varint);
```

* `BSP_FIELD` defaults to `DefaultProtocol_t<T>`
* `BSP_FIELD_WITH` can customize field protocol

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
     >   When analyzing templates, Clangd finds `DefaultProtocol<T>` is still `bsp::proto::Default`, triggering `static_assert`.  
     >   Actually **the compiler itself won't error**, just Clangd's template parser can't correctly deduce macro-generated partial specializations in the IDE.
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

### 6.3 Serializing Structures

```c++
Point pt{1,2};
w.write(pt);  // write structure
r.read(pt);   // read structure
```

* Structure: `[registered field 1][registered field 2]...`
* Field order determined by registration order
* Default protocol determined by `DefaultProtocol` (defaults to `Schema` after registration)

## 7. Advanced Customization

### 7.1 Custom DefaultProtocol

Define in the `bsp::proto` namespace for global use:

```c++
namespace bsp::proto {
    template<>
    struct DefaultProtocol<MyType> {
        using type = Fixed<4>;  // default serialize as Fixed<4>
    };
}
```

### 7.2 Custom Serializer

Define in the `bsp::serializer` namespace for global use:

```c++
struct Encrypt {};

namespace bsp::serialize {
    template<>
    struct Serializer<int, Encrypt> {
        static void write(io::Writer &w, const int &s) {
            int encrypted = s ^ 0x55AA;  // simple encryption
            utils::write_uleb128(w, encrypted);  // write with variable-length encoding
        }
        static void read(io::Reader &r, int &out) {
            int encrypted = static_cast<int>(utils::read_uleb128(r));  // read variable-length encoding
            out = encrypted ^ 0x55AA;  // decrypt
        }
    };
}
```

## 8. I/O Interface

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

// Byte read/write
uint8_t data[16];
w.writeBytes(data, sizeof(data));  // write byte array
r.readBytes(data, 16);             // read byte array

uint8_t b;
w.writeByte(0xFF);  // write single byte
r.readByte(b);      // read single byte

// Structure read/write
// Functional programming
bsp::write(w, value);                      // use DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value);  // specify protocol

// Object-oriented equivalent
w.write(value);
w.write<bsp::proto::Varint>(value);

// Functional programming
bsp::read(r, value);
bsp::read<bsp::proto::Fixed<4>>(r, value);

// Object-oriented equivalent
r.read(value);
r.read<bsp::proto::Fixed<4>>(r, value);
```

## 9. Error Handling

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

## 10. Common Issues

* **Container element protocol**: Use `PVal` to explicitly bind element protocol, avoiding DefaultProtocol ambiguity
* **PVal implicit conversion**: Refer to warning in section 5.1
* **ByteView memory management**: read allocates with new, caller must delete[]
* **clangd red errors**: See section 6.2
* **Fixed container size assertion**: Mismatch throws `LengthOverflow`
* **Version compatibility**: Schema is order-sensitive, no field IDs, field order changes affect compatibility

## 11. Examples Guide

ByteSchema provides examples covering common and advanced usage:

```
examples/
├── 01_basic.cpp             // native type serialization
├── 02_vector_map.cpp        // vector / map serialization
├── 03_option_variant.cpp    // Option / variant
├── 04_pval.cpp              // PVal implements multi-dimensional arrays
├── 05_schema.cpp            // custom Schema
├── 06_custom_serializer.cpp // custom Serializer & Protocol implements encrypted numbers
├── 07_cval.cpp              // CVal
```

Compile examples:

```bash
cd examples
g++ -std=c++20 -I../include 01_basic.cpp -o 01_basic
./01_basic
```

> In all examples, the path to `bsp.hpp` is `../include/bsp.hpp`.