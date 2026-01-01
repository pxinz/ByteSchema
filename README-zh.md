# BSP (ByteSchema) 使用文档

[English Version](./README.md)

## 1. 简介

`bsp` 是一个轻量级、模板化的 **字节序列化/反序列化框架**，提供对 C++ 原生类型、容器以及自定义结构体的高效序列化支持。主要特点：

* **固定宽度类型**（`Fixed<>`）支持整数、浮点数、布尔值的字节级表示
* **变长类型**（`Varint`）支持可变长度整数、字符串、字节数组、容器
* **结构化类型**（`Schema`）支持用户自定义结构体
* 支持 `Option<T>` 和 `std::variant<Ts...>`
* 全局配置可控（字节序、最大容器大小、递归深度、安全策略）
* 错误处理可选（严格、中等、忽略）
* `bsp.hpp` 需要 `C++20` 及以上

目标是**跨平台、可控、安全、灵活**的二进制协议框架。

---

## 2. 基础概念

### 2.1 协议标签（Protocol Tag）

| 标签         | 说明                                  | 默认应用类型                      |
|------------|-------------------------------------|-----------------------------|
| `Fixed<N>` | 固定宽度类型，仅当T为容器时 `N` 有意义              | 整数、浮点、布尔、tuple              |
| `Varint`   | 变长类型，适用于整数、容器、字符串                   | vector、map、string、ByteArray |
| `Schema`   | 用户自定义结构体                            | 用户注册的结构体                    |
| `Default`  | 默认协议，占位符，映射到 `DefaultProtocol_t<T>` | 所有未指定类型                     |

> 注：`T` 是容器时，`Fixed<0>` 表示容器内是空的，在所有默认容器 `Serializer` 实现中都表现为不读出，不写入。

### 2.2 默认协议映射

`proto::DefaultProtocol_t<T>` 提供类型到协议的默认映射：

| 类型                  | 默认协议    |
|---------------------|---------|
| bool                | Fixed<> |
| 整数（有符号/无符号）         | Fixed<> |
| 浮点数                 | Fixed<> |
| std::string         | Varint  |
| ByteArray           | Varint  |
| std::vector<T>      | Varint  |
| std::map<K,V>       | Varint  |
| std::tuple<Ts...>   | Fixed<> |
| std::variant<Ts...> | Varint  |
| Option<T>           | Varint  |
| 用户结构体 T（已注册 Schema） | Schema  |
| 其它                  | Default |

> 注：`Default` 是占位符，最终由 `DefaultProtocol_t<T>` 映射到具体协议。  
> 当然，你可以自定义 `Default` 协议下 `Serializer` 的实现，那么不会再进行映射。

---

### 2.3 全局选项 `GlobalOptions`

用于全局控制序列化行为、安全限制与 ABI：

```c++
struct GlobalOptions {
    std::endian endian = std::endian::big; // 字节序
    size_t max_depth = 64;                 // 最大递归深度
    size_t max_container_size = 1 << 20;   // 容器最大元素数
    size_t max_string_size = 1 << 20;      // 字符串/ByteArray最大长度
    bool strict_eof = false;               // 读完对象后要求 EOF
    ErrorPolicy error_policy = STRICT;     // 错误策略
    static GlobalOptions &instance();
};
```

* **错误策略**：`STRICT` / `MEDIUM` / `IGNORE`
* **安全约束**：超长容器或字符串会抛 `LengthOverflow`

你可以按以下方法自定义选项：

```c++
bsp::GlobalOptions::instance().max_depth = 128;
bsp::GlobalOptions::instance().max_container_size = 1<<16;
```

---

## 3. 基础类型序列化

### 3.1 布尔值

```c++
bool b = true;
bsp::io::Writer w(os);
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::write(w, b);

bsp::io::Reader r(is);
bool read_b;
bsp::serialize::Serializer<bool, bsp::proto::Fixed<>>::read(r, read_b);
```

* 写入为 **单字节**
* 仅支持 `Fixed<>`

### 3.2 整数类型

* **无符号/有符号整数**
* 支持 `Fixed<>` 与 `Varint`（变长编码）
* 有符号整数在 Varint 下使用 **ZigZag 编码**

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

## 4. 容器类型

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

## 5. 可选类型 `Option<T>`

```c++
bsp::types::Option<int> opt{42};
bsp::write(w, opt);  // 前置 flag 0/1 + 可选值
```

* Flag = 0 表示无值
* Flag = 1 表示有值

---

## 6. 变体类型 `std::variant<Ts...>`

* 写入前置索引（varint）
* 然后写入对应类型的值

```c++
std::variant<int, std::string> var = "hi";
bsp::write(w, var);
bsp::read(r, var);
```

* 索引越界会抛 `VariantOutOfRange`

---

## 7. 自定义结构体 `Schema`

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

* `BSP_FIELD` 会自动使用 `DefaultProtocol_t<T>`
* `BSP_FIELD_WITH` 可自定义协议

### 7.3 序列化结构体

```c++
Point pt{1,2};
bsp::write(w, pt);
bsp::read(r, pt);
```

* 自动按字段顺序序列化
* 默认协议由 `DefaultProtocol` 决定，注册后默认变成 `Schema`

### 7.4 自定义协议字段

有时结构体的子内容需要使用特殊协议：

```c++
BSP_FIELD_WITH(Point, x, bsp::proto::Varint)
```

---

## 8. 自定义 DefaultProtocol

有时你希望修改某个类型默认协议：

```c++
namespace bsp::proto {
    template<>
    struct DefaultProtocol<MyType> {
        using type = Fixed<4>; // 将 MyType 默认序列化为 Fixed<4>
    };
}
```

* 不必修改 Serializer 代码
* 仍可在特定调用时显式指定协议

```c++
bsp::write<Fixed<8>>(w, my_obj);  // 显式协议覆盖默认
```

---

## 9. 自定义 Serializer

如果默认协议不足，可自定义 Serializer，实现 write 和 read。

### 9.1 示例：加密整数

```c++
#include <iostream>
#include <sstream>
#include "bsp.hpp"

struct Encrypt {};

namespace bsp::serialize {
    template<>
    struct Serializer<int, Encrypt> {
        static void write(io::Writer &w, const int &s) {
            int encrypted = s ^ 0x55AA; // 异或加密
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
    std::cout << s2 << "\n"; // 输出 12345
}
```

### 9.2 特性

* 必须提供 `write(io::Writer&, const T&)` 和 `read(io::Reader&, T&)`
* 支持任意类型 `T`
* 可以和上一条一起使用覆盖默认协议（`DefaultProtocol`），或直接在 `Protocol` 中指定协议

---

## 10. I/O 接口

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

bsp::write(w, value);          // 自动使用 DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value); // 指定协议

bsp::read(r, value);
bsp::read<bsp::proto::Fixed<4>>(r, value);
```

---

## 11. 错误处理

* `bsp::error::ProtocolError` 基类

* 常见派生：

    * `UnexpectedEOF`
    * `InvalidVarint`
    * `LengthOverflow`
    * `VariantOutOfRange`
    * `ABIError`

* 全局错误策略：

```c++
bsp::GlobalOptions::instance().error_policy = bsp::MEDIUM;
```

* 默认严格模式（遇错则抛异常）

---

## 12. 小例子

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

* 演示了结构体序列化、vector 序列化
* 自动使用 `DefaultProtocol`
* 安全检查（超长容器/字符串）
