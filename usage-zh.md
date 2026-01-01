# BSP (ByteSchema) 使用文档

[English Version](./usage.md)

---

## 1. 简介

`bsp` 是一个轻量级、模板化的 **字节序列化/反序列化框架**，用于 C++ 原生类型、容器以及自定义结构体的高效二进制序列化。

主要特点：

* **固定宽度类型**（`Fixed<>`）支持整数、浮点数、布尔值的字节级表示
* **变长类型**（`Varint`）支持可变长度整数、字符串、字节数组及容器
* **结构化类型**（`Schema`）支持用户自定义结构体注册
* 支持 `Option<T>`、`std::variant<Ts...>`
* 全局配置可控（字节序、最大容器大小、递归深度、安全策略）
* 错误处理策略可选：严格、中等、忽略
* 仅依赖 C++20 及以上

目标是 **跨平台、可控、安全、灵活** 的二进制协议框架。

---

## 2. 基础概念

### 2.1 协议标签（Protocol Tag）

`bsp::proto` 提供内置协议标签：

| 标签         | 说明                       | 默认应用类型                      |
|------------|--------------------------|-----------------------------|
| `Fixed<N>` | 固定宽度类型，仅当 T 为容器时 `N` 有意义 | 整数、浮点、布尔、tuple              |
| `Varint`   | 变长类型，适用于整数、容器、字符串        | vector、map、string、ByteArray |
| `Schema`   | 用户自定义结构体                 | 已注册结构体                      |
| `Default`  | 默认协议，占位符                 | 所有未指定类型                     |

> 容器的 `Fixed<0>` 表示空容器，在默认 Serializer 中表现为不读出、不写入。

### 2.2 默认协议映射

`proto::DefaultProtocol_t<T>` 提供类型到协议的默认映射：

| 类型                     | 默认协议    |
|------------------------|---------|
| bool                   | Fixed<> |
| 整数（有符号/无符号）            | Fixed<> |
| 浮点数                    | Fixed<> |
| std::string            | Varint  |
| bsp::types::ByteArray  | Varint  |
| std::vector\<T>        | Varint  |
| std::map<K,V>          | Varint  |
| std::tuple<Ts...>      | Fixed<> |
| std::variant<Ts...>    | Varint  |
| bsp::types::Option\<T> | Varint  |
| bsp::types::PVal<T, P> | P       |
| struct T（已注册 Schema）   | Schema  |
| 其它                     | Default |

> Default 最终由 `DefaultProtocol_t<T>` 映射到具体协议，也可自定义 Default 协议下的 Serializer 实现。

### 2.3 `Serializer<T, Protocol>`

`bsp::serialize::Serializer<T, Protocol>` 定义了类型 T 在协议 Protocol 下的读写：

```c++
Serializer<T, P>::write(io::Writer &w, const T &v);
Serializer<T, P>::read(io::Reader &r, T &v);
```

> ⚠ 注意：协议必须明确，或通过 DefaultProtocol 映射。

### 2.4 全局选项 `GlobalOptions`

控制序列化行为、安全限制和 ABI：

```c++
struct GlobalOptions {
    std::endian endian = std::endian::big;  // 字节序
    size_t max_depth = 64;                  // 最大递归深度
    size_t max_container_size = 1 << 20;    // 容器最大元素数
    size_t max_string_size = 1 << 20;       // 字符串/ByteArray最大长度
    bool strict_eof = false;                // 读完对象后要求 EOF
    bsp::ErrorPolicy error_policy = STRICT; // 错误策略

    static GlobalOptions &instance();
};
```

* 错误策略：`STRICT` / `MEDIUM` / `IGNORE`
* 安全约束：超长容器或字符串会抛 `LengthOverflow`

```c++
bsp::GlobalOptions::instance().max_depth = 128;
bsp::GlobalOptions::instance().max_container_size = 1 << 16;
```

---

## 3. 原生类型序列化

### 3.1 布尔值

```c++
bool b = true;
bsp::io::Writer w(os);
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::write(w, b);

bsp::io::Reader r(is);
bool read_b;
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::read(r, read_b);
```

* 写入单字节
* 仅支持 `Fixed<>`

### 3.2 整数类型

* 支持有符号/无符号整数
* 协议支持 `Fixed<>` 和 `Varint`（ZigZag 编码对有符号，LEB128 对无符号）

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

### 3.3 浮点类型

* 写入按字节拷贝 IEEE754
* 仅支持 `Fixed<>`

```c++
float f = 3.14f;
bsp::write<bsp::proto::Fixed<>>(w, f);
```

---

## 4. 容器类型序列化

### 4.1 vector

```c++
std::vector<int> v = {1, 2, 3};

// 变长序列化
bsp::write(bsp::proto::Varint, w, v);
bsp::read(bsp::proto::Varint, r, v);

// 固定长度
bsp::write<bsp::proto::Fixed<3>>(w, v);
```

### 4.2 map

```c++
std::map<std::string, int> m = {{"a",1}, {"b",2}};
bsp::write(bsp::proto::Varint, w, m);
bsp::read(bsp::proto::Varint, r, m);
```

### 4.3 string / ByteArray

* 变长：前置 varint 长度
* 固定：写入 N 字节，不足填充

```c++
std::string s = "hello";
bsp::write(bsp::proto::Varint, w, s);

bsp::types::ByteArray ba = {1,2,3};
bsp::write(bsp::proto::Varint, w, ba);
```

---

## 5. 变体类型 `std::variant<Ts...>` 序列化

* 写入前置索引（varint）
* 再写入对应类型的值

```c++
std::variant<int, std::string> var = "hi";
bsp::write(w, var);
bsp::read(r, var);
```

* 索引越界抛 `VariantOutOfRange`

---

## 6. bsp::types 提供的类型

### 6.1 可选类型 `Option<T>`

```c++
bsp::types::Option<int> opt{42};
if (opt.has_value) std::cout << opt.value;

bsp::write(w, opt);  // 前置 flag 0/1 + 可选值
```

* Flag=0 表示无值
* Flag=1 表示有值

### 6.2 零拷贝视图 `ByteView`

```c++
bsp::types::ByteView view;
std::cout << view.size;

bsp::read(r, view);
```

* 非拥有者视图：data 指向 read 时分配的缓冲区
* 调用者负责释放（`delete[]`）
* 若想避免分配，可用 ByteArray 或自定义缓冲

### 6.3 携带 Protocol 的值类型 `PVal<T, Protocol>`

```c++
using Layer3 = types::PVal<int, proto::Varint>;
using Layer2 = types::PVal<std::vector<Layer3>, proto::Fixed<4>>;
using Layer1 = types::PVal<std::vector<Layer2>, proto::Varint>;

Layer1 arr; // 三维数组示例
bsp::read(r, arr);
std::cout << arr[0][1][2]; // 可直接访问原类型
std::cout << arr.value[0].value[1].value[2]; // 更安全的用法
```

* 默认协议由模板参数 `Protocol` 决定
* `Serializer<PVal<T, ProtocolT>>` 对应 `Serializer<T, Protocol=ProtocolT>`
* 可直接当作 `T` 类型来进行访问，但使用 `instance.value` 更加安全

> ⚠ 直接使用 `PVal<T, Protocol> instance` 作为 `T` 类型实例可能发生的隐式转换错误：
>
> ```c++
> void f(int&);
> void f(types::PVal<int, proto::Fixed<16>>&);
>
> types::PVal<int, Fixed<16>> x;
> f(x); // 可能调用 f(int&) 而非 f(PVal<...>&)
> ```

---

## 7. 自定义结构体 Schema

### 7.1 定义结构体

```c++
struct Point { int x; int y; };
struct Rect { Point p1; Point p2; };
```

### 7.2 注册结构体

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

* `BSP_FIELD` 默认使用 `DefaultProtocol_t<T>`
* `BSP_FIELD_WITH` 可自定义协议

> ⚠ 在使用宏注册 Schema 时，Clangd 可能会报如下红色提示：
> ```
> Clangd: In template: static assertion failed due to requirement '!std::is_same_v<bsp::proto::Default, bsp::proto::Default>': No concrete DefaultProtocol for this type
> ```
> 该提示**并不会**影响正常编译运行。
>
> **原因：**
> 1. **宏展开生成特化**：  
     `BSP_REGISTER_STRUCT(T, ...)` 会生成 `Serializer` 和 `DefaultProtocol` 偏特化，用于编译期绑定协议和序列化逻辑。
> 2. **Clangd 静态分析误判**：
     Clangd 在分析模板时，发现 `DefaultProtocol<T>` 仍是 `bsp::proto::Default`，触发了模板约束检查（concept 或 `static_assert`）。实际上**编译器本身不会报错**，只是 Clangd 的模板解析器无法在 IDE 中正确推导出宏生成的偏特化。
>
> **解决方法：**
> 1. **编译成功后重新加载 IDE**：编译器（GCC/Clang/MSVC）可以正常通过编译时，现代 IDE（如CLion）通常会更新静态检查结果
> 2. **忽略 IDE 提示**：可能会导致意料之外的后果
> 3. **显式提供 DefaultProtocol 特化**
     >   ```c++
     >   struct MyStruct { int a; int b; };
     >
     >   BSP_REGISTER_STRUCT(MyStruct,
     >       BSP_FIELD(MyStruct, a),
     >       BSP_FIELD(MyStruct, b)
     >   );
     >
     >   // 显式指定默认协议为 Schema
     >   namespace bsp::proto {
     >       template<>
     >       struct DefaultProtocol<MyStruct> {
     >           using type = proto::Schema;
     >       };
     >   }
     >   ```
> 4. **更新 Clangd / IDE**：Clangd 16+ 对模板宏分析更稳定


### 7.3 自定义协议字段

```c++
BSP_FIELD_WITH(Point, x, bsp::proto::Varint);
```

### 7.4 序列化结构体

```c++
Point pt{1,2};
bsp::write(w, pt);
bsp::read(r, pt);
```

* 按字段顺序序列化
* 默认协议由 `DefaultProtocol` 决定（注册后默认 `Schema`）

---

## 8. 高级自定义

### 8.1 自定义 DefaultProtocol

```c++
namespace bsp::proto {
    template<>
    struct DefaultProtocol<MyType> {
        using type = Fixed<4>; // 默认序列化为 Fixed<4>
    };
}
```

### 8.2 自定义 Serializer

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

## 9. I/O 接口

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

bsp::write(w, value);                   // 使用 DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value); // 指定协议

bsp::read(r, value);
bsp::read<bsp::proto::Fixed<4>>(r, value);
```

---

## 10. 错误处理

* 基类：`bsp::error::ProtocolError`

* 派生异常：

    * `UnexpectedEOF`
    * `InvalidVarint`
    * `LengthOverflow`
    * `VariantOutOfRange`
    * `ABIError`

* 全局策略：

```c++
bsp::GlobalOptions::instance().error_policy = bsp::MEDIUM;
```

* 默认为严格模式：遇任何错误，都抛出异常

---

## 11. 常见问题

* **容器元素协议**：使用 `PVal` 显式绑定元素协议，避免 DefaultProtocol 歧义
* **PVal 隐式转换**：参考 6.3 警告
* **ByteView 内存管理**：read 会 new 分配，调用方需 delete[]
* **clangd 报红**：见 7.2
* **Fixed 容器大小断言**：不匹配抛 `LengthOverflow`
* **版本兼容**：Schema 是顺序敏感，无字段 ID，字段顺序变动会影响兼容性

---

## 12. Examples 使用指引

ByteSchema 提供示例覆盖常用和高级用法：

```
examples/
├── 01_basic.cpp           // 原生类型序列化
├── 02_vector_map.cpp      // vector / map 序列化
├── 03_option_variant.cpp  // Option / variant
├── 04_pval.cpp            // PVal / 多维数组
├── 05_schema.cpp          // 自定义 Schema
├── 06_custom_serializer.cpp // 自定义 Serializer
```

编译示例：

```bash
cd examples
g++ -std=c++20 -I../include 01_basic.cpp -o 01_basic
./01_basic
```

> 所有示例中 `bsp.hpp` 路径均为 `../include/bsp.hpp`。
