# BSP 使用手册

> [English Version](usage.md)

## 0. 概述

### 0.1 配置要求

BSP 是一个现代 C++ 单头文件库，对编译环境有以下基本要求：

| 需求项        | 最低要求                                    |
|:-----------|:----------------------------------------|
| **C++ 标准** | C++20 或更高版本                             |
| **编译器**    | GCC 11+ / Clang 14+ / MSVC 2022 17.0+   |
| **构建系统**   | 无特殊要求，将 `bsp.hpp` 复制到项目中并 `#include` 即可 |
| **依赖项**    | 仅依赖 C++ 标准库                             |
| **线程环境**   | 支持 `thread_local` 存储期（用于递归深度保护和选项栈）     |

库的核心实现依赖于以下 C++20 特性：

- **Concepts（概念）**：用于定义 `io::Reader` 和 `io::Writer` 接口约束，实现编译期类型检查。
- **`std::endian`**：用于检测系统端序，结合 `constexpr` 分支在编译时裁剪无用代码。
- **`std::bit_cast`**：用于在浮点数与整数表示之间安全转换。
- **`std::make_unsigned_t` / `std::type_identity` 等**：增强模板元编程能力。

> **注意**：若目标平台不支持 C++20，代码将无法编译。在之后，开发组可能会推出 C++17 的版本，但并不考虑 C++14 及以下。

---

### 0.2 适用范围

为什么用选用二进制流存储替代 JSON？

JSON 在许多领域存在显著缺陷：

- **空间膨胀**：整数 `123456789` 在 JSON 中占 9 字节，在 BSP 中仅需 4 字节。
- **浮点精度损失**：JSON 将二进制浮点转为十进制字符串，往返转换可能丢失精度。
- **二进制数据**：JSON 需要 Base64 编码，体积膨胀越 33%。
- **解析开销**：文本解析器需要处理转义、Unicode、空白符，带来 CPU 开销。
- **低确定性**：JSON 不具有格式确定性，在业务逻辑实现中经常需要判空保证安全。

BSP 作为二进制协议，更适合作为强即时性通信的通信协议，以及本地缓存的存储格式。

| 场景    | 推荐          | 理由           |
|:------|:------------|:-------------|
| 网络通信  | BSP         | 跨平台、防攻击、版本兼容 |
| 跨平台存储 | BSP         | 高密度、跨平台      |
| 配置文件  | JSON/TOML   | 以人类可读性优先     |
| 本地缓存  | BSP Trivial | 高压缩率、性能敏感    |
| 游戏存档  | BSP Trivial | 同上，且二进制难以纂改  |

---

### 0.3 快速上手

以下示例演示了最基础的序列化与反序列化流程。假设您需要将一个包含整数和字符串的结构体保存到文件或内存中。

**第一步：包含头文件并定义数据结构**

```cpp
#include "bsp.hpp"

struct Player {
    uint64_t id;
    std::string name;
    float score;
};

// 注册 Schema（字段顺序决定了存储顺序）
BSP_SCHEMA(Player,
    BSP_FIELD(id),
    BSP_FIELD(name),
    BSP_FIELD(score)
)

// 将 Player 的默认序列化协议设置为 Schema
BSP_DEFAULT_PROTO(Player, bsp::proto::Schema<>)
```

**第二步：序列化（写入）**

```cpp
#include <sstream>

int main() {
    Player player{ 1001, "Alice", 98.5f };
    
    // 使用标准库字符串流作为输出
    std::stringstream buffer;
    bsp::io::StreamWriter writer(buffer);
    
    // 一行代码写入
    bsp::write(writer, player);
    
    // buffer.str() 现在包含序列化后的二进制数据
    return 0;
}
```

**第三步：反序列化（读取）**

```cpp
// 接上例，从相同的 buffer 读取
buffer.seekg(0); // 重置读取位置
bsp::io::StreamReader reader(buffer);

Player loaded_player;
bsp::read(reader, loaded_player);

// loaded_player 的内容将与 player 完全一致
assert(loaded_player.id == 1001);
assert(loaded_player.name == "Alice");
assert(loaded_player.score == 98.5f);
```

现在您已经了解了 BSP 的基本用法。后续章节将展开讲解各种协议标签、容器处理、错误策略以及高级自定义功能。

## 1. 概念

BSP 主张**类型即协议**，同时也主张**协议不侵入类型**，因此引入了**协议标签**这一概念。

### 1.1 Protocol Tags / 协议标签

本库的核心机制，用于指定数据的（反）序列化方式，存放在 `bsp::proto` 命名空间内。  
能够分为两类：  
**编码方式类：**  
此类标签包括 `Fixed<N>`、`Varint`、`Schema<V>`、`Trivial`、`CVal`。

- 当修饰普通类型时，它规定该数据应该如何表示。  
  例如：`int32` 在 `Fixed<>` 下为固定4字节，在 `Varint` 下为 ZigZag + LEB128 编码实现。
- 当修饰容器/包装器类型时，它规定容器本身如何包含子元素，而不规定子元素的编码方式。  
  例如：`vector<int>` 在 `Fixed<4>` 下固定包含4个元素，在 `Varint` 下使用 LEB128 长度头。

**修饰器类：**  
此类标签包括 `Limited<Len, Inner>`、`Forced<Len, Inner>`、`optmod::WithOptions<Inner, Modifiers...>`。  
此类标签的最大特点是：它并不指定编码方式，而是在编码过程中添加修饰/限制，并允许指定内部协议。必须继承
`proto::WrapperProto`。  
例如：使用 `Limited<Fixed<256>, Varint>` 读取 `vector<int>` 时，会使用与 `Varint` 相同的方式读取，同时限制最多只能读取 256
个字节。

### 1.2 默认协议映射

`proto::DefaultProtocol_t<T>` 提供类型到协议的默认映射：

| 分类          | 类型                                   | 协议         |
|:------------|:-------------------------------------|:-----------|
| **数据类型**    | bool                                 | Fixed<0>   |
|             | 整数与浮点数                               | Fixed<0>   |
| **容器类型**    | std::string                          | Varint     |
|             | bytes（std::vector<uint8_t>）          | Varint     |
|             | std::vector\<T>                      | Varint     |
|             | std::bitset\<N>                      | Fixed<0>   |
|             | std::map<K, V> & unordered_map<K, V> | Varint     |
|             | std::set\<T> & unordered_set\<T>     | Varint     |
|             | std::array<T, N>                     | Varint     |
| **结构化类型**   | std::pair<T1, T2>                    | Fixed<0>   |
|             | std::tuple<Ts...>                    | Fixed<0>   |
|             | 通过 BSP_DEFAULT_SCHEMA(\_V) 注册的结构     | Schema\<V> |
| **平凡可复制类型** | 满足 types::trivial_serializable 的类型   | Trivial    |
| **可变类型**    | std::optional\<T>                    | Varint     |
|             | std::variant<Ts...>                  | Varint     |
| **指针**      | T*                                   | Varint     |
|             | std::unique_ptr\<T>                  | Varint     |
| **指定协议的类型** | PVal                                 | Default    |
|             | CVal                                 | CVal       |
| **其它**      | 不符合以上任何一项的类型                         | Default    |

> 只支持 `Fixed<0>` 的类型，其中的0并不表示不写入，而是表示长度由类本身指定。  
> `Default` 在没有特化定义时，能自动映射到 `DefaultProtocol_t<T>` 协议。

### 1.3 Serializer / 序列化器

`serialize::Serializer<T, Protocol>` 定义了类型 T 在协议 Protocol 下的读写方式：

```c++
Serializer<T, P>::write(io::Writer &w, const T &v);
Serializer<T, P>::read(io::Reader &r, T &out);
```

我们也提供了简便用法：

```c++
bsp::read<P>(io::Reader &r, T &out);
bsp::read<T, P>(io::Reader &r); // -> T
// 若不指定P，则使用默认协议
```

### 1.4 Options / 选项

`bsp::options` 提供了配置选项。

```c++
struct options {
    static constexpr std::endian endian = std::endian::big; // 为了编译时裁剪，固定不变
    std::optional<size_t> max_depth; // 最大递归深度
    std::optional<size_t> max_container_size; // 最大容器长度(单位：元素数)
    std::optional<size_t> max_string_size; // 最大字符串/字节数组长度(单位：Byte)
  
    std::optional<ErrorPolicy> error_policy; // 错误处理策略，分为STRICT、MEDIUM、IGNORE
};
```

你可以通过选项栈管理：

```c++
options::push(options); // 若部分选项为空，则继承自上一层选项
options::pop();
options::current(); // -> const options&
options::reset();
```

也可以，且更推荐通过 RAII `OptionsGuard` 自动管理：

```c++
OptionsGuard g(options);
```

这会修改本线程的全局 options，并在作用域结束时将选项栈回退到原层数。

## 2. I/O 接口

`bsp` 对字节流进行操作，且提供了一些便于操作的接口。

### 2.1 Reader & Writer 概念

`io::Reader` 和 `io::Writer` 均为C++20概念，提供了对流的操作能力：

```c++
template<typename R> concept Reader = requires(R r, uint8_t *buf, std::streamsize n)
{
    { r.read_bytes(buf, n) } -> std::same_as<void>; // 读取n字节并存储到*buf内
    { r.read_byte() } -> std::same_as<uint8_t>;     // 读取并返回1字节
};
template<typename W> concept Writer = requires(W w, const uint8_t *buf, std::streamsize n, uint8_t b)
{
    { w.write_bytes(buf, n) } -> std::same_as<void>; // 写入*buf开始的n字节
    { w.write_byte(b) } -> std::same_as<void>;       // 写入1字节
};
```

只要提供这些接口，你可以支持对任何流的操作。

### 2.2 StreamReader & Writer / 操作STL流

`io::StreamReader` 和 `io::StreamWriter` 提供了对STL流的操作能力：

```c++
std::stringstream ss;

io::StreamReader sr(ss);
io::StreamWriter sw(ss);
```

> 当遇到EOF时，会抛出 `errors::EOFError`。

### 2.3 LimitedReader & Writer / 限制字节数

`io::LimitedReader` 和 `io::LimitedWriter` 提供了流的有限视图：

```c++
io::Writer reader;
io::LimitedReader lr(reader, 16); // 只允许读取16字节
lr.skip_remaining();              // 跳过未读完的字节

io::Reader writer;
io::LimitedWriter lw(writer, 16); // 只允许写入16字节
lw.pad_zero();                    // 未写完的部分填补0x00
```

> 当超出限定的读取/写入量时，会抛出 `errors::EOFError`。

### 2.4 AnyReader & Writer / 类型擦除

`io::AnyReader` 和 `io::AnyWriter` 提供对流的类型擦除，通常配合 `types::CVal` 使用。

```c++
io::StreamReader sr;
io::LimitedReader lr(sr);

io::AnyReader* any_r = new io::AnyReader(sr); 
any_r = new io::AnyReader(lr); // 二者变为了同一类型
```

有关 `types::CVal`，请参考章节4.2。

> 属于运行时多态，会带来运行时开销。  
> 推荐在通常环境下使用 `concept auto io::Reader`，而不是 `io::AnyReader`。

## 3. 序列化 - 原生与STL

本节的内容是序列化的具体行为。有关序列化器的概念，请参考章节1.3；有关自定义序列化器，请参考章节7.1。

> 带有子元素的类型，子元素一贯使用默认协议读取。  
> 如需指定协议，可以使用 `types::PVal`，具体参考章节4.1。

> 如无特殊声明，序列化时均不会计入递归深度。

### 3.1 数据类型

#### 3.1.1 bool

| 协议           | 结构                 | 端序 | 备注 |
|:-------------|:-------------------|:---|:---|
| **Fixed\<>** | 单字节`[0x00 / 0x01]` | 无关 |    |

> 在 `STRICT` 策略下，读取到0/1以外的值会抛出 `InvalidBool`

#### 3.1.2 整数

| 协议           | 结构             | 端序 | 备注                   |
|:-------------|:---------------|:---|:---------------------|
| **Fixed\<>** | 对应字节数`[Int]`   | 有关 |                      |
| Varint       | `[LEB128编码结果]` | 无关 | 对于有符号整数，采用 ZigZag 编码 |

> **Varint协议**：在非 `IGNORE` 策略下，长度过长会抛出 `VarintOverflow`

#### 3.1.3 浮点

**Fixed<>**

| 协议           | 结构             | 端序 | 备注                   |
|:-------------|:---------------|:---|:---------------------|
| **Fixed\<>** | 对应字节数`[Float]` | 有关 | 仅支持 `IEEE754` 协议的浮点数 |

### 3.2 容器类型

#### 3.2.1 std::string

**Varint**

| 协议         | 结构                   | 备注 |
|:-----------|:---------------------|:---|
| **Varint** | `[LEB128长度头][对应字节数]` |    |
| Fixed\<N>  | `[N字节]`              |    |

> **Varint协议**：在非 `IGNORE` 策略下，读取时长度超出 `max_string_size` 会抛出 `StringTooLarge`  
> **Fixed\<N>协议**：写入时，长度与N不符会抛出 `FixedSizeMismatch`

#### 3.2.2 types::bytes（std::vector<uint_8>）

行为与 `std::string` 完全相同。

#### 3.2.3 std::vector\<T>

计入递归深度。

| 协议         | 结构                             | 备注         |
|:-----------|:-------------------------------|:-----------|
| **Varint** | `[LEB128长度头][Elem1][Elem2]...` |            |
| Fixed\<N>  | `[Elem1][Elem2]...`            |            |
| Trivial    | `[LEB128长度头][对应长度]`            | 详见章节 6.1.1 |

> **Varint协议**：在非 `IGNORE` 策略下，读出时长度超出 `max_container_size` 会抛出 `ContainerTooLarge`  
> **Fixed\<N>协议**：写入时，长度与N不符会抛出 `FixedSizeMismatch`

#### 3.2.4 std::vector\<bool>

**该实现并不启用位压缩。  
如果需要，请使用 `std::bitset<N>`，或 `Trivial` 协议的 `std::vector<bool>`（详见章节6.1.1）。**

每个bool占1字节，其它行为与 `std::vector<T>` 相同。

> 在 `STRICT` 策略下，bool读取到0/1以外的值会抛出 `InvalidBool`

#### 3.2.5 std::bitset\<N>

**该实现启用位压缩。**

| 协议          | 结构              | 备注                |
|:------------|:----------------|:------------------|
| **Fixed<>** | `[ceil(N/8)字节]` | 小端序，协议为`Fixed<0>` |

#### 3.2.6 std::map<K, V> & unordered_map<K, V>

计入递归深度。

| 协议         | 结构                               | 备注 |
|:-----------|:---------------------------------|:---|
| **Varint** | `[LEB128长度头][K1][V1][K2][V2]...` |    |
| Fixed\<N>  | `[K1][V1][K2][V2]...`            |    |

> **Varint协议**：在非 `IGNORE` 策略下，读出时长度超出 `max_container_size` 会抛出 `ContainerTooLarge`  
> **Fixed\<N>协议**：写入时，长度与N不符会抛出 `FixedSizeMismatch`

#### 3.2.7 std::set\<T> & unordered_set\<T>

计入递归深度。

| 协议         | 结构                             | 备注 |
|:-----------|:-------------------------------|:---|
| **Varint** | `[LEB128长度头][Elem1][Elem2]...` |    |

> 在非 `IGNORE` 策略下，长度超出 `max_container_size` 会抛出 `ContainerTooLarge`

#### 3.2.8 std::array<T, N>

计入递归深度。

| 协议          | 结构                    | 备注            |
|:------------|:----------------------|:--------------|
| **Fixed<>** | `[Elem 1][Elem 2]...` | 协议为`Fixed<0>` |
| Trivial     | `[对应长度]`              | 详见章节 6.1.1    |

### 3.3 结构化类型

#### 3.3.1 std::pair<T1, T2>

| 协议          | 结构                | 备注 |
|:------------|:------------------|:---|
| **Fixed<>** | `[First][Second]` |    |

#### 3.3.2 std::tuple<Ts...>

计入递归深度。

| 协议          | 结构                  | 备注            |
|:------------|:--------------------|:--------------|
| **Fixed<>** | `[Elem1][Elem2]...` | 协议为`Fixed<0>` |

### 3.4 可变类型

#### 3.4.1 std::optional\<T>

| 协议         | 结构                    | 备注 |
|:-----------|:----------------------|:---|
| **Varint** | `[Bool存在标识]([T，若存在])` |    |

> 在 `STRICT` 策略下，存在标识读取到0/1以外的值会抛出 `InvalidBool`

#### 3.4.2 std::variant<Ts...>

| 协议         | 结构              | 备注 |
|:-----------|:----------------|:---|
| **Varint** | `[Varint序号][T]` |    |

> 写入时，若值为空会抛出 `InvalidVariantIndex`  
> 读取时，若序号无法匹配会抛出 `InvalidVariantIndex`

### 3.5 指针

计入递归深度。  
**拷贝指针指向的值**，在禁用递归深度限制时，请确保结构不存在循环，否则会陷入死循环。

| 协议         | 结构              | 备注              |
|:-----------|:----------------|:----------------|
| **Varint** | `[Bool存在标识][T]` | 存在标识为0x00时，为空指针 |

> 在 `STRICT` 策略下，存在标识读取到0/1以外的值会抛出 `InvalidBool`

#### 3.5.1 T*

需要自行管理内存。

```c++
MyStruct* s = read<>(reader);

delete s;
```

#### 3.5.2 std::unique_ptr\<T>

由智能指针自动管理内存。

## 4. 指定协议的类型

默认协议无法满足需求时，可能需要让类型携带协议标记。  
`bsp` 提供了 `PVal` 和 `CVal`，分别作为编译时和运行时协议侵入类型的接口。

### 4.1 types::PVal<T, P>

`types::PVal` 是一个轻量级包装器，用于**强制覆盖**类型 T 的序列化协议，而无需修改全局默认映射。

**修饰器类协议** 会直接作用于 `PVal` 上，效果与作用在内部元素上无异。  
**编码方式类协议** 不会对 `PVal` 产生任何影响。

```c++
int value = 0x1234;
bsp::PVal<int, bsp::proto::Varint> wrapped{value};   // 强制使用 Varint

// 序列化 wrapped 即可用 Varint 编码该 int
std::stringstream ss;
bsp::io::StreamWriter writer(ss);
bsp::write(writer, wrapped);
```

PVal 支持隐式转换为 T& 和 T*，可像普通值一样使用：

```c++
int x = *wrapped;   // 解引用
wrapped->...        // 若 T 为类类型，可调用成员
```

> 隐式转换可能会产生问题，更建议使用 `pval.value` 访问值

### 4.2 types::CVal

`types::CVal` 是一个抽象基类，用于实现运行时多态序列化。派生类需实现两个纯虚函数：

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

之后，通过基类指针或引用即可序列化/反序列化：

```c++
Animal cat{"cat", 3};
std::stringstream ss;
bsp::io::StreamWriter writer(ss);
bsp::write(writer, cat); // 自动调用多态 write

bsp::io::StreamReader reader(ss);
Animal dog;
bsp::read(reader, dog); // 自动调用多态 read
```

`io::AnyReader & AnyWriter` 实现了类型擦除，使得 `types::CVal` 能够接受任意满足 `Reader/Writer` 概念的对象。具体参考章节2.4。

> 虚函数调用会带来运行时开销，建议非必要不使用 `types::CVal`

## 5. Schema / 模式

结构体是C++里存储数据的重要方式，对此，`bsp`引入了模式`schema`进行序列化。  
对于用户自定义结构体，推荐通过 **模式宏** 进行注册，库将自动生成 Schema<Version> 协议的序列化器。

- 结构：`[Field 1][Field 2]...`
- **字段的序列化顺序即为注册顺序。**

### 5.1 基本注册

示例：

```c++
struct Message {
    uint64_t timestamp;
    std::string content;
    uint64_t sender_id;
};

// 注册默认版本的模式
BSP_SCHEMA(Message,
    BSP_FIELD_P(timestamp, bsp::proto::Varint), // 使用变长编码
    BSP_FIELD(content),                         // 字符串默认协议 Varint
    BSP_FIELD(sender_id)                        // 整形默认 Fixed<>
);

// 指定结构体的默认协议为 Schema
BSP_DEFAULT_PROTO(Point, proto::Schema<>);
```

> `BSP_FIELD(Field)` 使用该字段类型的默认协议。  
> `BSP_FIELD_P(Field, Protocol)` 可为字段指定特定协议。  
> `BSP_SCHEMA(Type, ...)` 注册为 `proto::Schema<proto::Default>`  
> `BSP_SCHEMA(Type, Version, ...)` 注册为 `proto::Schema<V>`

### 5.2 版本化模式

> 更多控制方法开发中

通过 `BSP_SCHEMA_V(Type, Version, ...)` 可注册带版本标签的模式，便于协议演进：

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

// 使用时指定版本
bsp::write<bsp::proto::Schema<V1>>(writer, {0, content, sender});
```

一定要将注册宏放置在全局命名空间内。

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

```

**std::vector\<T>** 在该协议下，长度策略采用 `Varint` 式。  
特别的，对于 `std::vector<bool>`，在 `Trivial` 协议下，会**启用位压缩**。该位压缩由库内部实现，**是跨平台安全的**。  
**std::array<T, N>** 在该协议下，长度策略采用 `Fixed<>` 式。

本库并不会默认启用这项功能，请手动指定协议为 `Trivial`。  
启用后，元素类型的特化编码方式将被忽略。

### 6.2 Limited & Forced / 限制长度

以便于协议向前兼容，本库提供了 `proto::Limited<Len, Inner> & Forced<Len, Inner>` 以限制读写长度。  
这些协议的实现基于 `io::LimitedReader & LimitedWriter` ，具体参考2.3章节。

模板参数 `Len` 规定该协议的行为，`Inner` 规定内层的序列化方式。

#### 6.2.1 Limited<Len, Inner>

该协议实行读取/写入限制，但并不实行强制大小。

**Len: Varint**

- 结构：`[Varint长度头][具体内容]`

> 写入时，会先写入一个 `StreamWriter` 缓冲区中，然后根据写入的内容长度写入 `Varint长度头`，再拷贝内容。  
> 读出时，会限制读取的长度，若超出长度限制，会抛出 `errors::EOFError`。

**Len: Fixed\<N>**

- 结构：`[具体内容]`

> 写入时，会限制写入的长度，若超出长度限制，会抛出 `errors::EOFError`。  
> 读出时，会限制读取的长度，错误同上。

#### 6.2.2 Forced<Len, Inner>

该协议实行强制写入/读取大小。

若写入大小未达到期望，会在写入内容之后填补一段全部由0x00组成的字节。  
若读出大小未达到期望，会略过剩余的内容。  
其它行为与 `Limited<Len, Inner>` 相同。

### 6.3 WithOptions / 临时修改配置

`bsp::proto::optmod` 提供了临时修改配置的协议标签。  
用于在序列化特定字段时临时覆盖全局选项，以便于序列化结构性数据（如树），以及大文件（如图片）。

**定义：**

```c++
template<typename InnerProto, typename... Modifiers>
struct WithOptions {};
```

有多个修改器时，从左到右依次执行。

**修改选项：**

- `MaxDepth<Policy>`：修改 `max_depth`。
- `MaxContainerSize<Policy>`：修改 `max_container_size`。
- `MaxStringSize<Policy>`：修改 `max_string_size`。
- `ErrorPolicyMod<Value>`：修改错误策略。

其中，`Policy` 应使用 `ValueModifier` 表示数值更改。

**策略计算：**

```c++
template<size_t Mul, size_t Div, size_t Add>
struct ValueModifier<Mul, Div, Add> {}; // new = old * Mul / Div + Add
```

`ValueModifier` 的实现自带防止溢出。  
`Unlimited` 是 `ValueModifier<0, 1, SIZE_MAX>` 的别名。

## 7. 自定义

### 7.1 自定义序列化器

若默认生成的行为不满足需求，你可以为自定义类型和自定义协议特化 Serializer。

```c++
struct MyCustomType { /* ... */ };
struct MyCustomProto {}; // 当然，你也可以使用bsp的内置协议

template<>
struct bsp::serialize::Serializer<MyCustomType, MyCustomProto> {
    static void write(bsp::io::Writer auto& w, const MyCustomType& v) {
        // 实现你的写入逻辑
    }
    static void read(bsp::io::Reader auto& r, MyCustomType& out) {
        // 实现你的读取逻辑
    }
};
```

#### 7.1.1 添加递归深度保护

如果你定义的类型属于容器，务必在序列化器读取/写入的开头加入 RAII 递归深度保护器。

```c++
bsp::detail::DepthGuard guard;
```

这会自动递增 thread_local 计数器并在析构时递减。

> 若深度超过 options::current().max_depth，构造函数会抛出 errors::DepthExceeded。

### 7.2 自定义默认协议

你可以通过宏全局覆盖：

```c++
// 这会让库内所有不显式指定协议的 int32_t 序列化都采用 Varint。
BSP_DEFAULT_PROTO(int32_t, bsp::proto::Varint);
```

一定要将注册宏放置在全局命名空间内。

## 8. FAQ

以下是一些常见问题，供参考。

**Q：为什么要固定 `options::endian` 为 `constexpr`？**  
A：  
基础类型（如整数、浮点数）的序列化非常频繁，若每次读写都进行运行时端序判断会引入不必要的分支开销。将 `endian` 定义为
`static constexpr` 后，编译器可在编译期确定当前平台的端序，并完全剪除无关的转换分支，实现零开销端序适配。大端序是多数网络协议的约定，因此库默认采用大端序。若你的应用场景必须使用小端存储，只需修改头文件中的
`endian` 定义即可（MIT 协议允许此类修改）。

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
2. **容器元素数量或字符串长度超过 `options` 中的限制**：通过 `max_container_size` 和 `max_string_size` 控制，默认为 1 MiB
   个元素和 4 MiB 字节。在 `MEDIUM` 或 `STRICT` 错误策略下，超出即抛出异常。

---

**Q：`types::bytes` 和 `std::vector<char>` 有什么区别？**  
A：  
`types::bytes` 是 `std::vector<uint8_t>` 的别名，其默认协议为 `Varint`（长度头 + 原始字节）。  
`std::vector<char>` 没有特化默认协议，会 fallback 到 `std::vector<T>` 的 `Varint` 协议，该协议会逐个元素调用默认序列化器。对于
`char` 类型，默认协议为 `Fixed<>`，因此每个字符会被当作单字节整数独立处理，导致序列化结果与 `types::bytes` 相同。

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
- 若图结构无环，可临时增大 `options::max_depth`。

---

**Q：为什么我的自定义结构体使用 `BSP_SCHEMA` 注册后仍然编译错误？**  
A：  
常见原因：

1. **宏未放在全局命名空间**：`BSP_SCHEMA` 必须在全局作用域展开，不能在函数或命名空间内使用。
2. **字段类型不完整**：确保结构体定义在宏之前已完成。
3. **未指定默认协议**：注册 Schema 后，还需通过 `BSP_DEFAULT_PROTO(MyStruct, bsp::proto::Schema<>);` 告知库使用该 Schema
   作为默认协议，或在序列化时显式指定 `bsp::proto::Schema<>`。

---

**Q：如何实现跨语言的兼容性（如与 Python/Java 交互）？**  
A：  
由于 BSP 的实现高度依赖 C++ 语言特性，并未直接提供跨语言支持。但你可通过以下方式实现互操作：

- **自行实现序列化**：实现 `Fixed<>`、`Varint`、`Schema<>` 等无语言依赖的协议，并避免 `Trivial`。
- **编写 IDL 编译器**：根据 BSP 的 Schema 宏生成其他语言的解析代码。

通常，若跨语言是核心需求，建议直接选用 Protobuf 或 Cap'n Proto。  
~~也许某一天开发组会因为自己需要而编写其他语言的实现。~~

## 9. 技巧

以下是一些在开发过程中想到的，具有实用价值的技巧。

### 9.1 安全地处理未知字段，以实现向前兼容

你可以使用 `proto::Forced` 与 多版本Schema 共同作用：

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

// 使用时指定版本
using MsgPackage = bsp::types::PVal<
    std::variant<
        bsp::types::PVal<Message, bsp::proto::Schema<V1> >,
        bsp::types::PVal<Message, bsp::proto::Schema<V2> >,
    >,
    bsp::proto::Forced<bsp::proto::Varint, bsp::proto::Varint>
>;

bsp::write<>(writer, MsgPackage{0, content, sender});
```

以后会推出更智能的 Schema 匹配以及 Multi 协议以简化写法。