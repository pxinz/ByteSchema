# BSP 使用手册

> [English](usage.md)

## 0. 概述

### 0.1 配置要求

BSP 是一个现代 C++ 单头文件库，对编译环境有以下基本要求：

| 需求项        | 最低要求                                    |
|:-----------|:----------------------------------------|
| **C++ 标准** | C++20 或更高版本                             |
| **编译器**    | GCC 11+ / Clang 14+ / MSVC 2022 17.0+   |
| **构建系统**   | 无特殊要求，将 `bsp.hpp` 复制到项目中并 `#include` 即可 |
| **依赖项**    | 仅依赖 C++ 标准库                             |

库的核心实现依赖于以下 C++20 特性：

- **Concepts**：用于定义 `io::Reader` 和 `io::Writer` 接口约束，以及实现编译期类型检查。
- **`std::endian`**：用于检测系统端序，结合 `constexpr` 分支在编译时裁剪无用代码。
- **`std::bit_cast`**：用于在浮点数与整数表示之间安全转换。
- **`consteval`**：用于在编译期验证 Schema 版本号单调递增。

> **注意**：若目标平台不支持 C++20，代码将无法编译。

---

### 0.2 适用范围

BSP 是二进制序列化协议，与 JSON 等文本格式相比：

| 对比项            | JSON              | BSP                      |
|:---------------|:------------------|:-------------------------|
| 整数 `123456789` | 占 9 字节            | 占 4 字节（Fixed）或更少（Varint） |
| 浮点精度           | 字符串转换可能丢失精度       | 字节精确拷贝                   |
| 二进制数据          | 须 Base64 编码，体积膨胀  | 原始字节直接写入                 |
| 解析开销           | 需处理转义、Unicode、空白符 | 固定布局或长度头，无歧义             |
| 类型安全           | 需运行时判空、类型检查       | 编译期确定                    |

| 场景    | 推荐                   | 理由            |
|:------|:---------------------|:--------------|
| 网络通信  | BSP                  | 跨平台、防攻击、版本兼容  |
| 跨平台存储 | BSP / Protobuf (多平台) | 高密度、端序统一      |
| 公众API | JSON                 | 兼容性好          |
| 配置文件  | JSON/TOML            | 以人类可读性优先      |
| 本地缓存  | BSP Trivial          | memcpy 优化，高性能 |

---

### 0.3 快速上手

以下示例演示了最基础的序列化与反序列化流程。假设您需要将一个包含整数和字符串的结构体保存到文件或内存中。

**第一步：包含头文件并定义数据结构**

```c++
#include "bsp.hpp"
#include <sstream>

struct Player {
    uint64_t id;
    std::string name;
    float score;
};

// 注册 Schema（字段顺序决定了存储顺序）
BSP_SCHEMA_SET(Player,
    BSP_SCHEMA(
        BSP_FIELD(id),
        BSP_FIELD(name),
        BSP_FIELD(score)
    )
)

// 注意：BSP_SCHEMA_SET 已将 Player 的默认协议注册为 proto::Schema<>，无需额外 BSP_DEFAULT_PROTO
```

**第二步：序列化（写入）**

```c++
int main() {
    Player player{ 1001, "Alice", 98.5f };

    // 使用标准库字符串流作为输出
    std::stringstream buffer;
    bsp::io::StreamWriter writer(buffer);

    // 一行代码写入（使用默认上下文）
    bsp::write(writer, player);

    // buffer 现在包含序列化后的二进制数据
    return 0;
}
```

**第三步：反序列化（读取）**

```c++
    // 接上例，从相同的 buffer 读取
    buffer.seekg(0);
    bsp::io::StreamReader reader(buffer);

    auto loaded_player = bsp::read<Player>(reader);

    // loaded_player 的内容将与 player 完全一致
    assert(loaded_player.id == 1001);
    assert(loaded_player.name == "Alice");
    assert(loaded_player.score == 98.5f);
```

现在您已经了解了 BSP 的基本用法。后续章节将展开讲解各种协议标签、I/O 接口、Schema 版本管理以及高级自定义功能。

---

### 0.4 版本间差异 (lite/bsp/nightly)

`bsp_lite.hpp`：仅核心部分，不支持根据运行时 options 进行调整。编译快，运行时开销小。基本没有迭代。  
`bsp.hpp`：完整版，支持全部稳定功能，有时更新新功能，最实用，作为默认选择。  
`bsp_nightly.hpp`：实验版，提供由宏控制的功能开关与实验性功能，不保证稳定性。

以下是不同版本间的能力差异

| 章节    | 功能               | lite | bsp | nightly |
|:------|:-----------------|:-----|:----|:--------|
| -     | 基础序列化            | ✅    | ✅   | ✅       |
| 1.3.1 | safety           | ✅    | ✅   | ✅       |
| 1.3.2 | options          | ❌    | ✅   | ✅+      |
| 3.3   | Limited I/O      | ❌    | ✅   | ✅       |
| 3.4   | Any I/O          | ❌    | ✅   | ✅       |
| 4.2   | CVal             | ❌    | ✅   | ✅       |
| 5.3.2 | DynSchema        | ❌    | ✅   | ✅       |
| 6.2   | Limited & Forced | ❌    | ✅   | ✅       |
| 8.1   | traceback        | ❌    | ✅   | ✅       |
| 9.2   | 实验性功能            | ❌    | ❌   | ✅       |

---

## 1. 概念

**编译时概念：**

### 1.1 协议标签 / Protocol Tag

BSP 的核心机制，用于指定数据的序列化方式，存放在 `bsp::proto` 命名空间内。  
BSP 主张**类型即协议**，同时也主张**协议不侵入类型**，因此引入了**协议标签**这一核心机制。

主要分为两类：

**编码方式类协议：**

| 标签          | 作用                                      |
|:------------|:----------------------------------------|
| `Custom`    | 默认协议。用于自定义序列化                           |
| `Fixed<N>`  | 固定长度编码。`N=0` 时由类型自身大小决定                 |
| `Varint`    | 变长编码。整数为 LEB128 + ZigZag，容器为 Varint 长度头 |
| `Trivial`   | 直接内存拷贝。仅限平凡可复制类型，**不做端序转换**，参见 6.1      |
| `Schema<V>` | 编译期 Schema，`V` 为版本号，参见 5.3.1            |
| `DynSchema` | 运行时 Schema 版本选择，参见 5.3.2 **[非 lite]**   |

对于**值类型**，此类协议指定了它本身该被如何编码。  
对于**容器类型**，此类协议指定了它应该如何容纳子元素，而不指定子元素的编码方式。子元素会使用默认的协议进行编码。

**修饰器类协议：**

| 标签                    | 作用                                           |
|:----------------------|:---------------------------------------------|
| `Default`             | 映射到类的默认协议。                                   |
| `Limited<Len, Inner>` | 限制读写长度，超出则抛异常，参见 6.2.1 **[非 lite]**          |
| `Forced<Len, Inner>`  | 强制读写长度，超出则抛异常，不足则补零/跳过，参见 6.2.2 **[非 lite]** |

任何修饰器类都只应创建形如 `template<typename T> struct Serializer<T, Wrapper>` 的序列化器，不应了解 `T` 的具体类型。

修饰器类均继承自 `proto::WrapperProto`。

---

### 1.2 T-P 对与序列化器 / T-P Pair & Serializer

`T-P` 是 BSP 最小的可序列化单元，可以唯一对应一个 `serialize::Serializer<T, P>` 特化。  
`serialize::Serializer<T, P>` 定义了 `T-P` 对的读写实现方式：

```c++
// 接口签名（concept 约束）
Serializer<T, P>::write(io::Writer auto &w, const T &v, context &ctx);
Serializer<T, P>::read(io::Reader auto &r, T &out, context &ctx);
```

便捷 API 封装了 Serializer 的调用：

```c++
// 使用默认协议
bsp::write(writer, value);        // 等效于 Serializer<T, DefaultProtocol<T>>::write
auto v = bsp::read<T>(reader);    // 等效于 Serializer<T, DefaultProtocol<T>>::read

// 指定协议
bsp::write<proto::Varint>(writer, value);
auto v = bsp::read<T, proto::Varint>(reader);
```

每个 `T` 对应一个 `proto::DefaultProto<T>`，表示为该类型选择的默认协议：

```c++
namespace proto {
    template<typename T>
    struct DefaultProtocol {
        using type = Custom;  // 默认：需要用户特化
    };
}
```

| 分类          | 类型                                       | 协议            |
|:------------|:-----------------------------------------|:--------------|
| **数据类型**    | `bool`                                   | `Fixed<0>`    |
|             | 整数与浮点数                                   | `Fixed<0>`    |
| **容器类型**    | `std::string`                            | `Varint`      |
|             | `types::bytes`（`std::vector<uint8_t>`）   | `Varint`      |
|             | `std::vector<T>`                         | `Varint`      |
|             | `std::bitset<N>`                         | `Fixed<0>`    |
|             | `std::map<K, V>` & `unordered_map<K, V>` | `Varint`      |
|             | `std::set<T>` & `unordered_set<T>`       | `Varint`      |
|             | `std::array<T, N>`                       | `Fixed<0>`    |
| **结构化类型**   | `std::pair<T1, T2>`                      | `Fixed<0>`    |
|             | `std::tuple<Ts...>`                      | `Fixed<0>`    |
|             | 通过 `BSP_DEFAULT_SCHEMA_SET()` 注册的结构      | `Schema<MAX>` |
| **平凡可复制类型** | 平凡可复制的非指针单字节类型                           | `Trivial`     |
| **可变类型**    | `std::optional<T>`                       | `Varint`      |
|             | `std::variant<Ts...>`                    | `Varint`      |
| **指针**      | `T*`                                     | `Varint`      |
|             | `std::unique_ptr<T>`                     | `Varint`      |
| **指定协议的类型** | `PVal`                                   | `Custom`      |
|             | `CVal`                                   | `Custom`      |
| **其它**      | 不符合以上任何一项的类型                             | `Custom`      |

> 只支持 `Fixed<0>` 的类型，其中的0并不表示不写入，而是表示长度由类本身指定。  
> `Default` 能自动映射到 `DefaultProtocol_t<T>` 协议。

---

### 1.3 编译时配置

端序与 `traceback` 的启用由 `constexpr` 变量控制。

```c++
static constexpr inline auto endian = std::endian::big;
static constexpr inline bool enable_traceback = true;   // [非 lite]
```

---

**运行时概念：**

### 1.4 上下文 / context

`context` 是 BSP 中唯一的运行时环境输入。它包含了序列化过程中需要的所有配置和状态信息。

```c++
struct context {
   safety sf;  // 防御性配置
   option opt; // 功能性配置 [非 lite]
   status st;  // 调用级状态
};
```

函数 `context::get_default_context()` 可以构造一个默认行为下的 `context` 对象。  
**不同线程不能共享同一个 context。**

#### 1.4.1 防御性配置 / safety

`safety` 控制序列化过程中的**安全检查**：

```c++
struct safety {
   size_t max_depth;            // 最大递归深度，默认 256
   size_t max_container_size;   // 最大容器元素数，默认 1M
   size_t max_string_size;      // 最大字符串字节数，默认 4MB
   errors::error_policy policy; // 错误策略
};
```

错误策略有三种：

| 策略       | 行为                 |
|:---------|:-------------------|
| `STRICT` | 严格模式：任何异常都抛出       |
| `MEDIUM` | 中等模式：容忍部分可恢复异常（默认） |
| `IGNORE` | 忽略模式：跳过异常，静默处理     |

静态变量 `safety::default_safety` 指定了默认的防御性配置，你可以在运行时进行修改。

#### 1.4.2 功能性配置 / option [非 lite]

`option` 控制序列化过程中的**功能性选择**：

```c++
struct option {
    size_t target_schema_version; // 运行时目标 Schema 版本，默认 SIZE_MAX
};
```

`target_schema_version` 用于 `DynSchema` 协议，在运行时决定使用哪个版本的 Schema。具体参见 5.3.2。

静态变量 `option::default_option` 指定了默认的功能性配置，你可以在运行时进行修改。

#### 1.4.3 调用级状态 / status

status 记录**单次**序列化调用过程中的状态：

```c++
struct status {
    size_t depth; // 当前递归深度
};
```

`depth` 由 `scope_guard` 自动管理，用于检测递归深度是否超过 `safety::max_depth`。

---

## 2. 序列化 —— 原生与 STL 类型

本节的内容是序列化的具体行为。有关序列化器的概念，参见 1.2；有关自定义序列化器，参见 7.1。

> 带有子元素的类型，子元素一贯使用默认协议读取。  
> 表中加粗的协议表示默认协议。  
> 如需指定协议，可以使用 `types::PVal`，具体参考章节4.1。

> 如无特殊声明，序列化时均不会计入递归深度。

---

### 2.1 基本数据类型

#### 2.1.1 bool

| 协议           | 结构                 | 端序 | 备注 |
|:-------------|:-------------------|:---|:---|
| **Fixed\<>** | 单字节`[0x00 / 0x01]` | 无关 |    |

> 在 `STRICT` 策略下，读取到0/1以外的值会抛出 `invalid_bool`

#### 2.1.2 整数

| 协议           | 结构             | 端序 | 备注                   |
|:-------------|:---------------|:---|:---------------------|
| **Fixed\<>** | 对应字节数`[Int]`   | 有关 |                      |
| Varint       | `[LEB128编码结果]` | 无关 | 对于有符号整数，采用 ZigZag 编码 |

> **Varint协议**：在非 `IGNORE` 策略下，长度过长会抛出 `varint_overflow`

#### 2.1.3 浮点

**Fixed<>**

| 协议           | 结构             | 端序 | 备注                   |
|:-------------|:---------------|:---|:---------------------|
| **Fixed\<>** | 对应字节数`[Float]` | 有关 | 仅支持 `IEEE754` 协议的浮点数 |

---

### 2.2 容器类型

#### 2.2.1 std::string

**Varint**

| 协议         | 结构                   | 备注 |
|:-----------|:---------------------|:---|
| **Varint** | `[LEB128长度头][对应字节数]` |    |
| Fixed\<N>  | `[N字节]`              |    |

> **Varint协议**：在非 `IGNORE` 策略下，读取时长度超出 `max_string_size` 会抛出 `string_too_large`  
> **Fixed\<N>协议**：写入时，长度与N不符会抛出 `fixed_size_mismatch`

#### 2.2.2 types::bytes（std::vector<uint_8>）

行为与 `std::string` 完全相同。

#### 2.2.3 std::vector\<T>

计入递归深度。

| 协议         | 结构                             | 备注         |
|:-----------|:-------------------------------|:-----------|
| **Varint** | `[LEB128长度头][Elem1][Elem2]...` |            |
| Fixed\<N>  | `[Elem1][Elem2]...`            |            |
| Trivial    | `[LEB128长度头][对应长度]`            | 详见章节 6.1.1 |

> **Varint协议**：在非 `IGNORE` 策略下，读出时长度超出 `max_container_size` 会抛出 `container_too_large`  
> **Fixed\<N>协议**：写入时，长度与N不符会抛出 `fixed_size_mismatch`

#### 2.2.4 std::vector\<bool>

**该实现并不启用位压缩。  
如果需要，请使用 `std::bitset<N>`，或 `Trivial` 协议的 `std::vector<bool>`（详见章节6.1.1）。**

每个bool占1字节，其它行为与 `std::vector<T>` 相同。

> 在 `STRICT` 策略下，bool读取到0/1以外的值会抛出 `invalid_bool`

#### 2.2.5 std::bitset\<N>

**该实现启用位压缩。**

| 协议          | 结构              | 备注                |
|:------------|:----------------|:------------------|
| **Fixed<>** | `[ceil(N/8)字节]` | 小端序，协议为`Fixed<0>` |

#### 2.2.6 std::map<K, V> & unordered_map<K, V>

计入递归深度。

| 协议         | 结构                               | 备注 |
|:-----------|:---------------------------------|:---|
| **Varint** | `[LEB128长度头][K1][V1][K2][V2]...` |    |
| Fixed\<N>  | `[K1][V1][K2][V2]...`            |    |

> **Varint协议**：在非 `IGNORE` 策略下，读出时长度超出 `max_container_size` 会抛出 `container_too_large`  
> **Fixed\<N>协议**：写入时，长度与N不符会抛出 `fixed_size_mismatch`

#### 2.2.7 std::set\<T> & unordered_set\<T>

计入递归深度。

| 协议         | 结构                             | 备注 |
|:-----------|:-------------------------------|:---|
| **Varint** | `[LEB128长度头][Elem1][Elem2]...` |    |

> 在非 `IGNORE` 策略下，长度超出 `max_container_size` 会抛出 `container_too_large`

#### 2.2.8 std::array<T, N>

计入递归深度。

| 协议          | 结构                    | 备注            |
|:------------|:----------------------|:--------------|
| **Fixed<>** | `[Elem 1][Elem 2]...` | 协议为`Fixed<0>` |
| Trivial     | `[对应长度]`              | 详见章节 6.1.1    |

---

### 2.3 结构化类型

#### 2.3.1 std::pair<T1, T2>

计入递归深度。

| 协议          | 结构                | 备注 |
|:------------|:------------------|:---|
| **Fixed<>** | `[First][Second]` |    |

#### 2.3.2 std::tuple<Ts...>

计入递归深度。

| 协议          | 结构                  | 备注            |
|:------------|:--------------------|:--------------|
| **Fixed<>** | `[Elem1][Elem2]...` | 协议为`Fixed<0>` |

---

### 2.4 可变类型

#### 2.4.1 std::optional\<T>

| 协议         | 结构                    | 备注 |
|:-----------|:----------------------|:---|
| **Varint** | `[Bool存在标识]([T，若存在])` |    |

> 在 `STRICT` 策略下，存在标识读取到0/1以外的值会抛出 `invalid_bool`

#### 2.4.2 std::variant<Ts...>

| 协议         | 结构              | 备注 |
|:-----------|:----------------|:---|
| **Varint** | `[Varint序号][T]` |    |

> 写入时，若值为空会抛出 `invalid_index`  
> 读取时，若序号无法匹配会抛出 `invalid_index`

---

### 2.5 指针

计入递归深度。  
**拷贝指针指向的值**，在禁用递归深度限制时，请确保结构不存在循环，否则会陷入死循环。

| 协议         | 结构              | 备注              |
|:-----------|:----------------|:----------------|
| **Varint** | `[Bool存在标识][T]` | 存在标识为0x00时，为空指针 |

> 在 `STRICT` 策略下，存在标识读取到0/1以外的值会抛出 `invalid_bool`

#### 2.5.1 T*

需要自行管理内存。

```c++
MyStruct* s = read<>(reader);

delete s;
```

#### 2.5.2 std::unique_ptr\<T>

由智能指针自动管理内存。

---

## 3. I/O 接口

### 3.1 Reader & Writer 概念

BSP 通过 C++20 Concept 定义了 I/O 接口。任何满足以下 concept 的类型都可以作为序列化的目标或来源：

```c++
template<typename R>
concept Reader = requires(R r, uint8_t* buf, std::streamsize n) {
    { r.read_bytes(buf, n) } -> std::same_as<void>;  // 读取 n 字节到 buf
    { r.read_byte() }        -> std::same_as<uint8_t>; // 读取并返回 1 字节
};

template<typename W>
concept Writer = requires(W w, const uint8_t* buf, std::streamsize n, uint8_t b) {
    { w.write_bytes(buf, n) } -> std::same_as<void>;  // 写入 n 字节
    { w.write_byte(b) }       -> std::same_as<void>;  // 写入 1 字节
};
```

---

### 3.2 通用 I/O 接口

BSP 提供了三种内置 I/O 实现：

#### StreamReader / StreamWriter

包装 `std::istream` / `std::ostream`，适用于文件、stringstream 等：

```c++
std::stringstream ss;
io::StreamWriter writer(ss);
io::StreamReader reader(ss);

// 写入文件
std::ofstream file("data.bin", std::ios::binary);
io::StreamWriter file_writer(file);
```

#### BufferReader / BufferWriter

基于 `std::vector<uint8_t>` 的内存 I/O：

```c++
io::BufferWriter writer;
write(writer, value);  // 数据写入 writer.buf

io::BufferReader reader(writer.buf);
auto v = read<T>(reader);
```

`BufferReader` 内存储的是 `buf` 的引用，因此上述代码可以构建 pipeline。

`BufferWriter` 的 `buf` 是公开成员，可以直接访问或移动：

```c++
std::vector<uint8_t> data = std::move(writer.buf);
```

#### BytesReader

基于裸内存指针的只读 I/O，适用于从网络缓冲区或预分配内存中读取：

```c++
uint8_t buffer[1024];
// ... 填充 buffer ...
io::BytesReader reader(buffer, sizeof(buffer));
auto v = read<T>(reader);
```

---

### 3.3 限制字节数：Limited I/O [非 lite]

`LimitedReader` 和 `LimitedWriter` 对读写操作施加字节数限制：

```c++
io::BufferWriter bw;
io::LimitedWriter lw(bw, 100);  // 最多写入 100 字节
// ... 序列化操作 ...
lw.pad_zero();  // 未用完的字节补 0x00

io::BufferReader br(bw.buf);
io::LimitedReader lr(br, 100);  // 最多读取 100 字节
// ... 反序列化操作 ...
lr.skip_remaining();  // 跳过未读取的字节
```

> **读取**：长度超出限制会抛出 `unexpected_eof`  
> **写入**：长度超出限制会抛出 `fixed_size_mismatch`（仅 `Fixed<N>`）

> **参见：** 6.2 节介绍了协议层的 `Limited` 和 `Forced`，它们基于本节介绍的 I/O 层 `LimitedReader`/`LimitedWriter` 实现。

---

### 3.4 类型擦除：Any I/O [非 lite]

`AnyReader` 和 `AnyWriter` 通过虚函数实现类型擦除，将任意满足 concept 的 Reader/Writer 统一为同一类型：

```c++
io::StreamReader sr(ss);
io::LimitedReader lr(sr, 100);

io::AnyReader any_r1(sr);   // StreamReader 擦除
io::AnyReader any_r2(lr);   // LimitedReader 擦除——类型与 any_r1 相同
```

`AnyReader` 提供了 `reader_type()` 方法，返回原始类型的枚举标识，可用于运行时类型判断。

> **注意：** 虚函数调用有运行时开销。通常环境推荐使用 `concept auto` 而非 `AnyReader`。`AnyReader`/`AnyWriter` 主要用于配合
`CVal`（4.2 节）实现多态序列化。

---

## 4. 覆写协议的类型

本章节关于 `bsp::types` 提供的类型，有关在类型声明中注册序列化方法，参见7.3。

### 4.1 协议覆盖 / PVal

`types::PVal<T, P>` 提供了临时覆盖类型 `T` 的默认协议为 `P` 的能力。

**修饰器类协议** 会直接作用于 `PVal` 上，效果与作用在内部元素上无异。  
**编码方式类协议** 不会对 `PVal` 产生任何影响。

```c++
int value = 0x1234;
types::PVal<int, proto::Varint> wrapped{value};   // 强制使用 Varint

// 序列化 wrapped 即可用 Varint 编码该 int
std::stringstream ss;
io::StreamWriter writer(ss);
write(writer, wrapped);
```

`PVal` 支持隐式转换为 T& 和 T*，可像普通值一样使用：

```c++
int x = *wrapped;   // 解引用
wrapped->...        // 若 T 为类类型，可调用成员
```

> 隐式转换可能会产生问题，建议使用 `pval.value` 访问值。

---

### 4.2 运行时多态 / CVal [非 lite]

`types::CVal` 是一个抽象基类，用于实现运行时多态序列化。派生类需实现两个纯虚函数：

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

`io::AnyReader & AnyWriter` 实现了类型擦除，使得 `types::CVal` 能够接受任意满足 `Reader/Writer` 概念的对象。具体参见 2.4
节。

> 虚函数调用会带来较大
> 运行时开销，建议非必要不使用 `types::CVal`

---

## 5. Schema 与版本管理

### 5.1 注册

使用 `BSP_SCHEMA_SET` 宏注册结构体的序列化 Schema：

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

`BSP_SCHEMA_SET` 会：

1. 在 `bsp::schema` 命名空间中生成 `SchemaSet<Player>` 特化
2. `Player` 由于满足 `types::schema_serializable` Concept，默认协议变为 `proto::Schema<SIZE_MAX>`

之后即可直接序列化：

```c++
Player player{1001, "Alice", 98.5f};

bsp::io::BufferWriter writer;
bsp::write(writer, player);  // 使用默认协议（Schema<SIZE_MAX>）

bsp::io::BufferReader reader(writer.buf);
auto loaded = bsp::read<Player>(reader);
```

**字段协议覆盖：** 可以为单个字段指定协议：

```c++
BSP_SCHEMA_SET(Player,
    BSP_SCHEMA(
        BSP_FIELD_P(id, bsp::proto::Varint),  // id 用 Varint 编码
        BSP_FIELD(name),                       // 使用字段默认协议
        BSP_FIELD(score)
    )
)
```

> **注意：** `BSP_SCHEMA_SET` 必须放在全局命名空间内。

---

### 5.2 版本化

通过 `BSP_SCHEMA_V` 注册不同版本的 Schema：

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

**版本号规则：** 版本号必须单调递增，否则编译器报错。

**版本匹配规则：** 选择**小于等于目标版本的最大版本号**。

| 目标版本 | 匹配的 Schema 版本        |
|:-----|:---------------------|
| 0    | 无（报错）                |
| 1    | V1                   |
| 2    | V2                   |
| 3    | V2（因为 V2 是 ≤3 的最大版本） |

这个规则意味着**仅同一版本区间兼容**：写入方和读取方使用的协议版本都应为匹配到同一个模式。

---

### 5.3 序列化

**结构**：`[Field 1][Field 2]...`  
其中 **字段顺序** 与 **注册顺序** 完全相同。

#### 5.3.1 Schema<V>

编译期指定版本：

```c++
// 用特定版本写入
bsp::write<proto::Schema<1>>(writer, msg);

// 用特定版本读取
auto msg_v1 = bsp::read<Message, proto::Schema<1>>(reader);
```

默认 `V` 值为 `SIZE_MAX`，表示匹配版本号最大的版本。

#### 5.3.2 DynSchema [非 lite]

运行时选择版本：

```c++
context ctx;
ctx.opt.target_schema_version = 2;  // 运行时决定版本

bsp::write<proto::DynSchema>(writer, msg, ctx);

ctx.opt.target_schema_version = 1;
auto older = bsp::read<Message, proto::DynSchema>(reader, ctx);
```

`DynSchema` 的行为与 `Schema<V>` 完全相同，区别仅在于版本号来自 `ctx.opt.target_schema_version` 而非编译期模板参数。

---

## 6. 高级序列化

本节的内容是 `bsp::proto` 提供的高级协议。

### 6.1 Trivial / 平凡可复制类型

对于满足 `types::trivially_serializable`（平凡可复制+非指针）的类型，本库提供了高效率的直接内存拷贝实现。

```c++
struct MyStruct {
    int32_t a;
    int16_t b;
    int8_t c;
} s;

bsp::write<bsp::proto::Trivial>(writer, s); // 无需注册模式
```

> 需要注意的是，`Trivial` 协议**并不会**考虑端序、内存布局、内存对齐等具体实现问题。  
> 也就是说，在不同平台、不同编译器上，该协议都可能出现不同的行为。  
> 建议只在本地存储时启用 `Trivial`。

#### 6.1.1 平凡可复制容器

对于子元素类型满足 `types::trivially_serializable` 的容器，本库同样提供了直接内存拷贝实现。

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
// 写入：Varint(2) + 24 字节内存块

std::array<Vec3, 2> arr = {{
    {1.0f, 2.0f, 3.0f},
    {4.0f, 5.0f, 6.0f}
}};

write<proto::Trivial>(writer, arr);
// 写入：24 字节内存块，无长度头
```

**std::vector\<T>** 在该协议下，长度策略采用 `Varint` 式。  
特别的，对于 `std::vector<bool>`，在 `Trivial` 协议下，会**启用位压缩**。该位压缩由库内部实现，**是跨平台安全的**。  
**std::array<T, N>** 在该协议下，长度策略采用 `Fixed<>` 式。

本库 **只会对单字节的平凡可复制类型** 启用这项功能，请手动指定协议为 `Trivial`。  
启用后，元素类型的特化编码方式将被忽略。

---

### 6.2 Limited & Forced / 限制长度

以便于协议向前兼容，本库提供了 `proto::Limited<Len, Inner> & Forced<Len, Inner>` 以限制读写长度。  
这些协议的实现基于 `io::LimitedReader & LimitedWriter` ，具体参考2.3章节。

模板参数 `Len` 规定该协议的行为，`Inner` 规定内层的序列化方式。

#### 6.2.1 Limited<Len, Inner>

该协议实行读取/写入限制，但并不实行强制大小。

**Len: Varint**

- 结构：`[Varint长度头][具体内容]`

> 写入时，会先写入一个 `BufferWriter` 缓冲区中，然后根据写入的内容长度写入 `Varint长度头`，再拷贝内容。  
> 读出时，会限制读取的长度，若超出长度限制，会抛出 `unexpected_eof`。

**Len: Fixed\<N>**

- 结构：`[具体内容]`

> 写入时，会限制写入的长度，若超出长度限制，会抛出 `fixed_size_mismatch`。  
> 读出时，会限制读取的长度，错误同上。

#### 6.2.2 Forced<Len, Inner>

该协议实行强制写入/读取大小。

若写入大小未达到期望，会在写入内容之后填补一段全部由0x00组成的字节。  
若读出大小未达到期望，会略过剩余的内容。  
**即使序列化过程中报错，若流可用，依旧会进行补0/略过。**  
其它行为与 `Limited<Len, Inner>` 相同。

---

## 7. 自定义

### 7.1 自定义序列化器

若默认生成的行为不满足需求，你可以为自定义类型和自定义协议特化 Serializer。

```c++
struct MyCustomType { /* ... */ };
struct MyCustomProto {}; // 当然，你也可以使用bsp的内置协议

template<>
struct bsp::serialize::Serializer<MyCustomType, MyCustomProto> {
    static void write(io::Writer auto& w, const MyCustomType& v, context &ctx) {
        // 实现你的写入逻辑
    }
    static void read(io::Reader auto& r, MyCustomType& out, context &ctx) {
        // 实现你的读取逻辑
    }
};
```

请在命名空间 `bsp::serialize` 下进行特化。  
在 Serializer 实现中，务必记得使用 `scope_guard`（参见下一节）。

---

### 7.1.1 作用域护卫 / scope_guard

`scope_guard` 是 BSP 提供的一个 RAII 工具，用于管理序列化过程中的**递归深度**、**配置生命周期**和**调用栈追踪**。  
在序列化嵌套结构时，它辅助处理三个问题：

1. **递归深度控制**：进入容器或结构体时深度 +1，离开时深度 -1。超过 `safety::max_depth` 时抛出异常。
2. **配置临时修改**：某些序列化操作可能需要临时修改 `safety` 或 `option` 配置，离开时自动恢复。
3. **调用栈追踪**：发生异常时，自动记录当前正在序列化的类型和协议信息，形成调用链。

```c++
auto g = ctx.guard<GetDeeper, RollbackSafety, RollbackOpts>(frame_fn);
```

#### 模板参数

| 参数                          | 类型     | 含义                | 何时为 `true`      |
|:----------------------------|:-------|:------------------|:----------------|
| `GetDeeper`                 | `bool` | 是否计入递归深度          | 容器、结构体等包含子元素的类型 |
| `RollbackSafety`            | `bool` | 析构时是否回滚 `ctx.sf`  | 临时修改了安全配置时      |
| `RollbackOpts` **[非 lite]** | `bool` | 析构时是否回滚 `ctx.opt` | 临时修改了功能配置时      |

#### 参数：traceback 帧生成器 [非 lite]

第四个参数是一个返回 `traceback_frame` 的可调用对象。**当且仅当序列化过程中发生异常时**它被调用，在正常路径下零开销：

```c++
auto g = ctx.guard<true, false, false>([] {
    return errors::value_frame{"Player", "Schema<0>"};
});
```

帧生成器支持两种帧类型：

| 帧类型             | 用途              | 参数                                                |
|:----------------|:----------------|:--------------------------------------------------|
| `value_frame`   | 记录类型名、协议名、子元素信息 | `type`, `proto`, `child_label`（可选）, `details`（可选） |
| `wrapper_frame` | 记录包装器信息         | `wrapper_info`                                    |

如果帧生成器本身抛出异常，guard 会捕获该异常并生成一个 `wrapper_frame{"[!!] error when generating traceback info"}`，确保
traceback 的完整性不受影响。

#### 使用示例

**容器/结构体：**

```c++
template<typename T> requires types::default_serializable<T>
struct Serializer<std::vector<T>, proto::Varint> {
    static void write(io::Writer auto &w, const std::vector<T> &v, context &ctx) {
        size_t index = 0;                            // 先声明需要捕获的子元素信息
        auto g = ctx.guard<true, false, false>([&] { // GetDeeper 设置为 true
            return errors::value_frame{
                "std::vector", "Varint", 
                detail::concat("Elem ", index),      // lambda 捕获当前下标
                detail::concat("length=", v.size())  // 附带长度信息
            };
        });
        detail::write_varint(w, v.size());
 
        for (; index < v.size(); ++index) {
            DefaultSerializer<T>::write(w, v[index], ctx);
        }
    }
}
```

**基本类型：**

```c++
template<>
struct Serializer<uint_8, proto::Fixed<>> {
    static void write(Writer auto& w, int v, context& ctx) {
        auto g = ctx.guard<false, false, false>([] {
            return errors::value_frame{"uint_8", "Fixed<>"};
        });
        // ...
    }
};
```

**临时修改配置：**

```c++
static void write(Writer auto& w, const T& v, context& ctx) {
    auto g = ctx.guard<false, true, false>([] {         // 回滚防御性配置
        return errors::value_frame{"MyType", "Custom"};
    });
    
    // 临时放宽长度限制
    ctx.sf.max_container_size = 4096;
    
    // ... 序列化逻辑 ...
    
    // 离开作用域时，ctx.sf 自动恢复为进入前的值，而 ctx.opt 不会
}
```

#### 工作原理

细节请参考库代码。

`scope_guard` 对象在创建时，会根据需求存储目前的信息，并将深度 +1。  
离开作用域时，对象析构，自定义的析构函数被调用，根据需求还原存储的信息，将深度 -1；若启用了 `traceback` 功能且有未捕获的错误，则会调用传入的
lambda 生成调用帧存入 `traceback` 中。

---

### 7.2 自定义默认协议

你可以通过宏全局覆盖：

```c++
// 这会让库内所有不显式指定协议的 int32_t 序列化都采用 Varint。
BSP_DEFAULT_PROTO(int32_t, bsp::proto::Varint);
```

一定要将注册宏放置在全局命名空间内。

对于模板类等复杂类型，可以在 `bsp::proto` 命名空间下进行特化：

```c++
template<typename T>
struct bsp::proto::DefaultProtocol<std::vector<T> > {
    using type = Varint;
};
```

---

### 7.3 语法糖：类内注册

退出到全局命名空间的实现较不优雅，所以我们也允许直接在类型内部，通过标签分派进行序列化注册：

```c++
struct MyStruct {
    using default_protocol = proto::Varint;                                                // 默认协议
    
    static void write(io::Writer auto &w, const MyStruct &v, context &ctx, proto::Varint); // 写入逻辑
    
    static void read(io::Reader auto &r, MyStruct &out, context &ctx, proto::Varint);      // 读取逻辑
}
```

> 由于实现限制，如此方法注册的特化是 **偏特化** 而不是 **全特化**。  
> 因此，如果你同时进行了类内注册和类外注册，编译期 **不会** 触发 ODR，且优先选择类外的全特化注册。

---

## 8. 调试与安全

### 8.1 异常类 / errors

BSP 使用运行时异常，而不是传递错误码。  
`errors::error` 是 BSP 的异常对象。  
其存在异常码与类别，分别对应枚举类 `code` 与 `kind`。

---

### 8.2 调用栈 / Traceback [非 lite]

`errors::traceback` 是 BSP 提供的运行时错误日志打印工具。  
它以 `std::shared_ptr` 的形式存在于 `context` 与 `error` 类中，仅在有异常时被初始化，并在回溯过程中，通过 `scope_guard`
记录调用栈信息。

使用 `traceback::format()` 或 `error::format_tb()` 可以获取调用栈信息。

典型的 Traceback 信息如下所示：

```text
[bsp::string_too_large] string size 46 larget than limit=10 bytes // what
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

#### 对象帧 / value_frame

```text
  - [label] | [type], [proto]
    (details)
```

**label**  
`label` 由上一个对象帧指定，表示“该对象属于上一个对象的什么”。若为调用的根，则为 `[ROOT]`，若没有被指定，则为 `UNKNOWN`。  
若将整个调用栈信息的 `label` 连接起来，可以得到元素的逻辑路径：

```text
[ROOT] -> Elem 2 -> Field "username"
```

**details**  
`details` 是可选字段，用于附加信息，以更加精确地确定错误的原因。

#### 包装器帧 / wrapper_frame

```text
  @ [info]
```

包装器的作用范围是：栈内，自包装器帧之后的所有帧。  
通常用于 `WrapperProto` 与包装器类。

---

## 9. 附录

### 9.1 提供的 concepts

`bsp::types` 提供了一些 Concepts，用于判断序列化能力。

```c++
// 能通过 Trivial 协议序列化
template<typename T>
concept trivial_serializable;

// 在类内注册了序列化方法
template<typename T, typename Proto>
concept internal_serializable;

// 在类内注册了默认协议
template<typename T>
concept internal_default_protocol;

// 定义了对应的 SchemaSet
template<typename T>
concept schema_serializable;

// 能被序列化的 T-P 对，基于对 Serializer 特化的检测
template<typename T, typename Proto>
concept serializable;

// 能以默认协议被序列化的类型
template<typename T>
concept default_serializable = serializable<T, proto::DefaultProtocol_t<T> >;

// Ts 中所有类型都能被以默认协议被序列化
template<typename... Ts>
concept all_serializable = (default_serializable<Ts> && ...);
```

---

### 9.2 bsp_nightly 功能简述

暂无。

---

### 9.3 Examples 使用指南

暂无。

---

### 9.4 技巧

#### 9.4.1 用法速查

| 目标              | 代码                                                                 |
|:----------------|:-------------------------------------------------------------------|
| 序列化一个 int       | `bsp::write(writer, 42)`                                           |
| 序列化一个结构体        | `bsp::write(writer, player)`                                       |
| 用 `Varint` 编码整数 | `bsp::write<bsp::proto::Varint>(writer, 42)`                       |
| 用 `Trivial` 编码  | `bsp::write<bsp::proto::Trivial>(writer, vec)`                     |
| 指定 `Schema` 版本  | `bsp::write<bsp::proto::Schema<2>>(writer, msg)`                   |
| 运行时选择版本         | `bsp::write<bsp::proto::DynSchema>(writer, msg, ctx)` **[非 lite]** |
| 限制序列化长度         | `bsp::write<bsp::proto::Limited<...>>(writer, data)` **[非 lite]**  |
| 强制序列化长度         | `bsp::write<bsp::proto::Forced<...>>(writer, data)` **[非 lite]**   |
| 自定义类型序列化        | 特化 `bsp::serialize::Serializer<T, P>`                              |
| 自定义 I/O         | 实现 `bsp::io::Writer` / `bsp::io::Reader` concept                   |

#### 9.4.2 协议版本兼容

只要保证高版本 `Schema` 只在末端添加字段，你可以使用低版本 `Schema` 读取高版本 `Schema`，只需使用 `proto::Forced`：

```c++
io::BufferWriter w;
io::BufferReader r(w.buf);

MyStruct item = ...;

write<proto::Forced<proto::Varint, proto::Schema<2> >(w, item);
read<proto::Forced<proto::Varint, proto::Schema<1> >(r, item); // 仍能正常读取对应字段，且不会导致流错位
```

#### 9.4.4 更换序列化库

BSP 基本不对类型进行侵入，因此可以非常方便地迁移至其它序列化库。  
如果 BSP 无法满足你的需求，以下是删除残留的 BSP 代码的指南：

1. 删除 Schema 注册
2. 删除 Serializer & DefaultProto 注册
3. 删除 `PVal` 使用：建议手动替换，若手动替换较为麻烦，也可定义：
   ```c++
   template<typename T, typename P>
   using bsp::types::PVal = T;
   ```

---

## 10. FAQ

以下是一些常见问题，供参考。

**Q：为什么要固定 `endian` 与 `enable_traceback` 为 `constexpr`？**  
A：  
基础类型（如整数、浮点数）的序列化非常频繁，若每次读写都进行运行时端序判断会引入不必要的分支开销。将 `endian` 定义为
`static constexpr` 后，编译器可在编译期确定当前平台的端序，并完全剪除无关的转换分支，实现零开销端序适配。大端序是多数网络协议的约定，因此库默认采用大端序。若你的应用场景必须使用小端存储，只需修改头文件中的
`endian` 定义即可（MIT 协议允许此类修改）。  
`enable_traceback` 同理，该功能基本在所有 `Serializer` 中出现，消耗性能更大，且通常不需要运行时开关。

---

**Q：为什么不提供 `std::span` / `std::string_view` 的支持？**  
A：  
这些容器都是只读视图容器，无法进行反序列化，`read` 的逻辑很可能不符合预期。

---

**Q：为什么 `std::vector<bool>` 默认不启用位压缩？**  
A：  
`std::vector<bool>` 是标准库中一个特化的“伪容器”，其元素并非真正的 `bool`
对象，而是返回代理引用的位域。若在默认协议下启用位压缩，库内部需要额外处理这种代理语义，导致复杂度上升，且可能与用户预期的逐字节行为不符。此外，很多业务场景中
`std::vector<bool>` 用于标志位集合，更推荐使用 `std::bitset<N>`（编译期固定大小）或显式指定 `proto::Trivial`
协议来获得位压缩能力。这样设计保持了默认行为的简洁与可预测性。

---

**Q：Varint 读取溢出检查的具体阈值是多少？**  
A：  
溢出检查在两种情况下触发：

1. **长度过大导致无法放入目标类型**：例如将超过 32 位的 LEB128 序列读取到 `uint32_t` 中。阈值由目标类型的位宽决定（如 32
   位类型阈值为 32 位）。
2. **容器元素数量或字符串长度超过 `safety` 中的限制**：通过 `max_container_size` 和 `max_string_size` 控制，默认为 1 MiB
   个元素和 4 MiB 字节。在 `MEDIUM` 或 `STRICT` 错误策略下，超出即抛出异常。

---

**Q：`types::bytes` 和 `std::vector<char>` 有什么区别？**  
A：  
`types::bytes` 是 `std::vector<uint8_t>` 的别名；`char` 实际为 `unsigned char` 还是 `signed char`
是编译器实现定义的行为。  
在 `char` 实现为**无符号**的平台上，二者没有区别。

在 `char` 实现为**有符号**的平台上，二者区别如下：
`types::bytes` 在 `Varint`、`Fixed<N>`、`Trivial` 协议下，均为高性能的字节拷贝实现；  
`std::vector<char>` 仅在 `Trivial` 协议下为字节拷贝实现。

---

**Q：为什么我反序列化 `std::unordered_map` 后元素顺序变了？**  
A：  
`std::unordered_map`
本质是基于哈希表的无序容器，其迭代顺序由哈希函数和桶分布决定，本身不保证任何特定顺序。序列化时按迭代顺序写入，反序列化时按写入顺序重新插入，因此最终顺序取决于插入顺序和哈希表的内部状态。  
由于标准库并不提供固定顺序的字典容器，如果需要保留顺序，请使用 `std::vector<std::pair<K, V>>` 替代。

---

**Q：如何安全地序列化包含指针的图结构（如树、链表）？**  
A：  
BSP 对指针的默认处理是“按值深拷贝”，即递归序列化指针所指向的对象。这种方式**不能处理循环引用**
（会导致无限递归或栈溢出），也不支持共享所有权（同一对象被多次序列化）。  
对于复杂图结构，推荐以下方案：

- 将对象池化，序列化时写入对象 ID 而非原始指针，反序列化时重建引用关系。
- 若图结构无环，可临时增大 `safety::max_depth`。

---

**Q：为什么我的自定义结构体使用 `BSP_SCHEMA_SET` 注册后仍然编译错误？**  
A：  
常见原因：

1. **宏未放在全局命名空间**：`BSP_SCHEMA_SET` 必须在全局作用域展开，不能在函数或命名空间内使用。
2. **字段类型不完整**：确保结构体定义在宏之前已完成。

---

**Q：如何实现跨语言的兼容性（如与 Python/Java 交互）？**  
A：  
由于 BSP 的实现高度依赖 C++ 语言特性，并未直接提供跨语言支持。但你可通过以下方式实现互操作：

- **自行实现序列化**：实现 `Fixed<>`、`Varint`、`Schema<>` 等无语言依赖的协议，并避免 `Trivial`。
- **编写 IDL 编译器**：根据 BSP 的 Schema 宏生成其他语言的解析代码。

通常，若跨语言是核心需求，建议直接选用 Protobuf 或 Cap'n Proto。
