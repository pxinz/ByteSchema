# BSP (Byte Schema Protocol)

> [English](README.md)  
> [使用手册](usage-zh.md)

**一个基于协议标签驱动的 C++20 二进制序列化库。**

BSP 是一个**单头文件**、**零依赖**（仅 C++ 标准库）的二进制序列化库。其核心设计理念是：**类型即协议，而协议不侵入类型**。

---

## 特性一览

| 特性               | 说明                                                                |
|:-----------------|:------------------------------------------------------------------|
| **协议标签驱动**       | 通过 `Serializer<T, P>` 偏特化，在类型外部定义序列化行为，零侵入                        |
| **多编码协议**        | `Fixed`（定长）、`Varint`（LEB128+ZigZag）、`Trivial`（memcpy）             |
| **STL 全面支持**     | `vector`、`string`、`map`、`set`、`optional`、`variant`、`unique_ptr` 等 |
| **Schema 版本管理**  | 编译期版本号验证 + 运行时版本选择，支持向前兼容                                         |
| **多层安全防御**       | 递归深度限制、容器大小限制、三级错误策略（STRICT/MEDIUM/IGNORE）                        |
| **Traceback 诊断** | 异常发生时自动记录序列化调用栈，精确定位错误位置                                          |
| **零开销抽象**        | 端序转换、traceback 等均在编译期裁剪，无运行时分支                                    |
| **自定义扩展**        | 自定义协议、自定义序列化器、自定义 I/O，一切可扩展                                       |

---

## 快速上手

### 配置要求

| 需求项        | 最低要求                                  |
|:-----------|:--------------------------------------|
| **C++ 标准** | C++20 或更高                             |
| **编译器**    | GCC 11+ / Clang 14+ / MSVC 2022 17.0+ |
| **依赖项**    | 仅 C++ 标准库                             |

### 安装

将 `bsp.hpp` 复制到项目中并 `#include` 即可，无需任何构建配置。

### 示例

```c++
#include "bsp.hpp"
#include <sstream>
#include <cassert>

// 1. 定义数据结构
struct Player {
    uint64_t id;
    std::string name;
    float score;
};

// 2. 注册 Schema（字段顺序决定存储顺序）
BSP_SCHEMA_SET(Player,
    BSP_SCHEMA(
        BSP_FIELD(id),
        BSP_FIELD(name),
        BSP_FIELD(score)
    )
)

int main() {
    Player player{ 1001, "Alice", 98.5f };

    // 3. 序列化
    std::stringstream buffer;
    bsp::io::StreamWriter writer(buffer);
    bsp::write(writer, player);

    // 4. 反序列化
    buffer.seekg(0);
    bsp::io::StreamReader reader(buffer);
    auto loaded = bsp::read<Player>(reader);

    assert(loaded.id == 1001);
    assert(loaded.name == "Alice");
    assert(loaded.score == 98.5f);
}
```

---

## 核心概念

### 协议标签（Protocol Tag）

协议标签是 BSP 的核心机制，位于 `bsp::proto` 命名空间。它们**不侵入数据类型**，而是通过 `Serializer<T, P>` 特化在外部定义序列化行为。

**编码方式类协议**（指定值/容器如何编码）：

| 标签          | 作用                                      |
|:------------|:----------------------------------------|
| `Fixed<N>`  | 固定长度编码。`N=0` 时由类型自身大小决定                 |
| `Varint`    | 变长编码。整数为 LEB128 + ZigZag，容器为 Varint 长度头 |
| `Trivial`   | 直接内存拷贝。仅限平凡可复制类型，**不做端序转换**             |
| `Schema<V>` | 编译期 Schema 版本，`V` 为版本号                  |
| `DynSchema` | 运行时 Schema 版本选择                         |
| `Custom`    | 默认协议，用于自定义序列化                           |

**修饰器类协议**（包装其他协议）：

| 标签                    | 作用                   |
|:----------------------|:---------------------|
| `Default`             | 映射到类型的默认协议           |
| `Limited<Len, Inner>` | 限制读写长度，超出则抛异常        |
| `Forced<Len, Inner>`  | 强制读写长度，超出抛异常，不足补零/跳过 |

### T-P 对与序列化器

`(T, P)` 是 BSP 最小的可序列化单元，唯一对应一个 `Serializer<T, P>` 特化：

```c++
// 接口签名
Serializer<T, P>::write(io::Writer auto &w, const T &v, context &ctx);
Serializer<T, P>::read(io::Reader auto &r, T &out, context &ctx);
```

便捷 API：

```c++
bsp::write(writer, value);                    // 使用默认协议
auto v = bsp::read<T>(reader);

bsp::write<proto::Varint>(writer, value);     // 指定协议
auto v = bsp::read<T, proto::Varint>(reader);
```

### 上下文（Context）

`context` 是序列化过程中唯一的运行时环境输入：

```c++
struct context {
    safety sf;   // 防御性配置：最大深度、容器大小、字符串大小、错误策略
    option opt;  // 功能性配置：目标 Schema 版本（DynSchema 使用）
    status st;   // 调用级状态：当前递归深度（自动管理）
};
```

---

## 支持的编码格式

### 基本类型

| 类型         | 默认协议      | 格式                  |
|:-----------|:----------|:--------------------|
| `bool`     | `Fixed<>` | 单字节 `[0x00 / 0x01]` |
| 整数         | `Fixed<>` | 定长字节，大端序            |
| 整数（Varint） | —         | LEB128 + ZigZag     |
| 浮点         | `Fixed<>` | IEEE 754 字节表示，大端序   |

### 容器类型

| 类型                | 默认协议      | 格式                                |
|:------------------|:----------|:----------------------------------|
| `std::string`     | `Varint`  | `[LEB128 长度头][数据]`                |
| `types::bytes`    | `Varint`  | 同上（高性能字节拷贝）                       |
| `std::vector<T>`  | `Varint`  | `[LEB128 长度头][元素序列]`              |
| `std::map<K,V>`   | `Varint`  | `[LEB128 长度头][K1][V1][K2][V2]...` |
| `std::set<T>`     | `Varint`  | `[LEB128 长度头][元素序列]`              |
| `std::array<T,N>` | `Fixed<>` | `[元素序列]`（无长度头）                    |
| `std::bitset<N>`  | `Fixed<>` | `[ceil(N/8) 字节]`（位压缩）             |

### 结构化与可变类型

| 类型                     | 默认协议      | 格式                  |
|:-----------------------|:----------|:--------------------|
| `std::pair<T1,T2>`     | `Fixed<>` | `[First][Second]`   |
| `std::tuple<Ts...>`    | `Fixed<>` | `[Elem1][Elem2]...` |
| `std::optional<T>`     | `Varint`  | `[存在标识][T（若存在）]`    |
| `std::variant<Ts...>`  | `Varint`  | `[Varint 序号][T]`    |
| `T*` / `unique_ptr<T>` | `Varint`  | `[存在标识][T（若非空）]`    |

### Schema 结构体

通过 `BSP_SCHEMA_SET` 注册的结构体，默认协议为 `Schema<SIZE_MAX>`（匹配最大版本）：

```
[Field 1][Field 2]...   ← 字段顺序与注册顺序一致
```

---

## Schema 版本管理

### 注册多版本

```c++
BSP_SCHEMA_SET(Message,
    BSP_SCHEMA_V(1,
        BSP_FIELD(content),
        BSP_FIELD(sender_id)
    ),
    BSP_SCHEMA_V(2,
        BSP_FIELD(content),
        BSP_FIELD(sender_id),
        BSP_FIELD(timestamp)    // v2 新增字段
    )
)
```

- 版本号必须**单调递增**（编译期 `consteval` 验证）
- 版本匹配规则：选择**小于等于目标版本的最大版本号**
- 字段协议覆盖：`BSP_FIELD_P(field_name, proto::Varint)`

### 编译期指定版本

```c++
bsp::write<proto::Schema<1>>(writer, msg);
auto v1 = bsp::read<Message, proto::Schema<1>>(reader);
```

### 运行时选择版本

```c++
context ctx;
ctx.opt.target_schema_version = 2;
bsp::write<proto::DynSchema>(writer, msg, ctx);
```

---

## I/O 接口

BSP 通过 C++20 Concept 定义 I/O 接口，任何满足以下 concept 的类型均可作为序列化目标：

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

### 内置实现

| 类型                                | 说明                                 |
|:----------------------------------|:-----------------------------------|
| `StreamWriter` / `StreamReader`   | 包装 `std::ostream` / `std::istream` |
| `BufferWriter` / `BufferReader`   | 基于 `std::vector<uint8_t>` 的内存 I/O  |
| `BytesReader`                     | 基于裸内存指针的只读 I/O                     |
| `LimitedWriter` / `LimitedReader` | 限制读写字节数                            |
| `AnyWriter` / `AnyReader`         | 类型擦除（虚函数），用于多态序列化                  |

---

## 安全与调试

### 三级错误策略

| 策略       | 行为                 |
|:---------|:-------------------|
| `STRICT` | 严格模式：任何异常都抛出       |
| `MEDIUM` | 中等模式：容忍部分可恢复异常（默认） |
| `IGNORE` | 忽略模式：跳过异常，静默处理     |

### Traceback 调用栈

启用 `enable_traceback = true` 后，异常发生时自动记录序列化路径：

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

## 版本分支

| 版本                | 说明                             |
|:------------------|:-------------------------------|
| `bsp_lite.hpp`    | 核心功能，编译快，运行时开销小，不支持运行时 options |
| `bsp.hpp`         | 完整版，支持全部稳定功能，推荐默认使用            |
| `bsp_nightly.hpp` | 实验版，提供宏控制的功能开关与实验性功能           |

---

## 自定义扩展

### 自定义序列化器

```c++
struct MyCustomType { /* ... */ };
struct MyCustomProto {};

template<>
struct bsp::serialize::Serializer<MyCustomType, MyCustomProto> {
    static void write(io::Writer auto& w, const MyCustomType& v, context &ctx) {
        // 实现写入逻辑
    }
    static void read(io::Reader auto& r, MyCustomType& out, context &ctx) {
        // 实现读取逻辑
    }
};
```

### 自定义默认协议

```c++
BSP_DEFAULT_PROTO(int32_t, bsp::proto::Varint);
```

### 类内注册

```c++
struct MyStruct {
    using default_protocol = proto::Varint;

    static void write(io::Writer auto &w, const MyStruct &v, context &ctx, proto::Varint);
    static void read(io::Reader auto &r, MyStruct &out, context &ctx, proto::Varint);
};
```

---

## 与 JSON 对比

| 对比项            | JSON              | BSP                    |
|:---------------|:------------------|:-----------------------|
| 整数 `123456789` | 9 字节              | 4 字节（Fixed）或更少（Varint） |
| 浮点精度           | 字符串转换可能丢失精度       | 字节精确拷贝                 |
| 二进制数据          | 须 Base64 编码，体积膨胀  | 原始字节直接写入               |
| 解析开销           | 需处理转义、Unicode、空白符 | 固定布局或长度头，无歧义           |
| 类型安全           | 需运行时判空、类型检查       | 编译期确定                  |

### 场景推荐

| 场景     | 推荐             | 理由            |
|:-------|:---------------|:--------------|
| 网络通信   | BSP            | 跨平台、防攻击、版本兼容  |
| 跨平台存储  | BSP / Protobuf | 高密度、端序统一      |
| 公众 API | JSON           | 兼容性好          |
| 配置文件   | JSON / TOML    | 人类可读性优先       |
| 本地缓存   | BSP Trivial    | memcpy 优化，高性能 |

---

## FAQ

**Q：为什么 `std::unordered_map` 反序列化后元素顺序变了？**

A：`unordered_map` 本质是基于哈希表的无序容器，迭代顺序由哈希函数和桶分布决定。序列化时按迭代顺序写入，反序列化时按写入顺序重新插入。如需保留顺序，请使用
`std::vector<std::pair<K, V>>`。

**Q：如何安全地序列化包含指针的图结构（如树、链表）？**

A：BSP 对指针的默认处理是"按值深拷贝"，**不能处理循环引用**。建议将对象池化，序列化时写入对象 ID 而非原始指针，反序列化时重建引用关系。

**Q：如何实现跨语言兼容？**

A：BSP 高度依赖 C++ 特性，未直接提供跨语言支持。可自行实现 `Fixed<>`、`Varint`、`Schema<>` 等无语言依赖的协议，或编写 IDL
编译器。若跨语言是核心需求，建议直接选用 Protobuf 或 Cap'n Proto。

**Q：`types::bytes` 和 `std::vector<char>` 有什么区别？**

A：`types::bytes` 是 `std::vector<uint8_t>` 的别名。在 `char` 实现为有符号的平台上，`types::bytes` 在 `Varint`、`Fixed<N>`、
`Trivial` 协议下均为高性能字节拷贝实现；而 `std::vector<char>` 仅在 `Trivial` 协议下为字节拷贝实现。

---

## 许可证

MIT License
