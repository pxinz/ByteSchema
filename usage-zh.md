# BSP (ByteSchema) 使用文档

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

## 2. 基础概念

### 2.1 协议标签（Protocol Tag）

`bsp::proto` 提供内置协议标签：

| 标签 | 说明 | 默认应用类型 |
|------|------|--------------|
| `Fixed<N>` | 固定宽度类型，仅当 T 为容器时 `N` 有意义 | 整数、浮点、布尔、tuple |
| `Varint` | 变长类型，适用于整数、容器、字符串 | vector、map、string、ByteArray |
| `Schema` | 用户自定义结构体 | 已注册结构体 |
| `CVal` | 运行时多态类 | CVal |
| `Default` | 默认协议，占位符 | 所有未指定类型 |

> 容器的 `Fixed<0>` 表示空容器，在默认 Serializer 中表现为不读出、不写入。

### 2.2 默认协议映射

`proto::DefaultProtocol_t<T>` 提供类型到协议的默认映射：

| 类型 | 默认协议 |
|------|----------|
| bool | Fixed<> |
| 整数（有符号/无符号） | Fixed<> |
| 浮点数 | Fixed<> |
| std::string | Varint |
| bsp::types::ByteArray | Varint |
| std::vector\<T> | Varint |
| std::map<K,V> | Varint |
| std::tuple<Ts...> | Fixed<> |
| std::variant<Ts...> | Varint |
| bsp::types::Option\<T> | Varint |
| bsp::types::PVal<T, P> | P |
| struct T（已注册 Schema） | Schema |
| CVal | CVal |
| 其它 | Default |

> Default 最终由 `DefaultProtocol_t<T>` 映射到具体协议，也可自定义 Default 协议下的 Serializer 实现。

### 2.3 `Serializer<T, Protocol>`

`bsp::serialize::Serializer<T, Protocol>` 定义了类型 T 在协议 Protocol 下的读写：

```c++
Serializer<T, P>::write(io::Writer &w, const T &v);
Serializer<T, P>::read(io::Reader &r, T &out);
```

我们提供了更多的简便用法：

```c++
// 函数式编程，若不指定 P，则采用 DefaultProtocol_t<T>
bsp::write<P>(io::Writer &w, const T &v);
// 面向对象编程
w.write<P>(const T &v);
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
* 安全约束：超长容器或字符串会抛出 `LengthOverflow`

```c++
bsp::GlobalOptions::instance().max_depth = 128;
bsp::GlobalOptions::instance().max_container_size = 1 << 16;
```

## 3. 原生类型序列化

### 3.1 布尔值

```c++
bool b = true;
bsp::io::Writer w(os);
w.write(b);  // 写入单字节

bsp::io::Reader r(is);
bool read_b;
r.read(read_b);  // 读取布尔值
```

* 结构：`[单字节 0x00 / 0x01]`
* 仅支持 `Fixed<>`

### 3.2 整数类型

```c++
uint32_t u = 123456;
int32_t s = -42;

// Fixed 编码
bsp::write<bsp::proto::Fixed<>>(w, u);
bsp::write<bsp::proto::Fixed<>>(w, s);

// Varint 编码
bsp::write<bsp::proto::Varint>(w, u);
bsp::write<bsp::proto::Varint>(w, s);
```

* 支持有符号/无符号整数
* 支持 `Fixed<>` 和 `Varint`（对有符号使用 ZigZag 编码，对无符号使用 LEB128）

### 3.3 浮点类型

```c++
float f = 3.14f;
bsp::write<bsp::proto::Fixed<>>(w, f);
```

* 写入按字节拷贝 IEEE754
* 仅支持 `Fixed<>`

## 4. 容器类型序列化

此处的"容器类型"指存在子元素的类型：

* 子元素会采用对应类的默认协议  
  如有需要指定子元素的协议，请见 `PVal` 的用法
* 继承类会采用对应的父类的编码方式（如 `unordered_map<K, V>` 使用 `map<K, V>` 的编码方式）

### 4.1 vector

```c++
std::vector<int> v = {1, 2, 3};

// 变长序列化
bsp::write<bsp::proto::Varint>(w, v);
bsp::read<bsp::proto::Varint>(r, v);

// 固定长度
bsp::write<bsp::proto::Fixed<3>>(w, v);
```

* `Varint` 结构：`[LEB128 长度头][元素 1][元素 2]...`
* `Fixed<N>` 结构：`[元素 1][元素 2]...[元素 N]`

### 4.2 map

```c++
std::map<std::string, int> m = {{"a",1}, {"b",2}};
bsp::write<bsp::proto::Varint>(w, m);
bsp::read<bsp::proto::Varint>(r, m);
```

* `Varint` 结构：`[LEB128 长度头][键 1][值 1][键 2][值 2]...`
* `Fixed<N>` 结构：`[键 1][值 1][键 2][值 2]...[键 N][值 N]`

### 4.3 string / ByteArray

* 变长：前置 varint 长度
* 固定：写入 N 字节，不足填充

```c++
std::string s = "hello";
w.write(s);  // 使用默认协议（Varint）

bsp::types::ByteArray ba = {1,2,3};
w.write(ba);  // 使用默认协议（Varint）
```

* `Varint` 结构：`[LEB128 长度头][对应长度数据]`
* `Fixed<N>` 结构：`[长为 N 的数据]`

### 4.4 变体类型 `std::variant<Ts...>`

```c++
std::variant<int, std::string> var = "hi";
w.write(var);  // 写入变体
r.read(var);   // 读取变体
```

* 仅支持 `Varint`
* 结构：`[LEB128 索引][对应类型的值]`
* 会采用 `Ts...` 中的第索引值个类型
* 索引越界抛出 `VariantOutOfRange`

### 4.5 可选类型 `Option<T>`

该类型由 `bsp::types` 提供：

```c++
bsp::types::Option<int> opt{42};
if (opt.has_value) std::cout << opt.value;  // 检查并访问值

w.write(opt);  // 写入可选类型
r.read(opt);   // 读取可选类型
```

* 仅支持 `Varint`
* 结构：`[Bool Flag][若 flag=1，则为对应类型的值]`

### 4.6 零拷贝视图 `ByteView`

该类型由 `bsp::types` 提供：

```c++
bsp::types::ByteView view;
std::cout << view.size;  // 查看大小

r.read(view);  // 读取到视图
```

* 支持 `Varint` 和 `Fixed<N>`
* 结构与 `ByteArray` 相同
* 非拥有者视图：data 指向 read 时分配的缓冲区
* 调用者负责释放（`delete[]`）
* 若想避免分配，可用 ByteArray 或自定义缓冲

## 5. 能控制协议的类型

### 5.1 携带 Protocol 的值类型 `PVal<T, Protocol>`

该类型由 `bsp::types` 提供：

```c++
using Layer3 = types::PVal<int, proto::Varint>;
using Layer2 = types::PVal<std::vector<Layer3>, proto::Fixed<4>>;
using Layer1 = types::PVal<std::vector<Layer2>, proto::Varint>;

Layer1 arr;  // 三维数组示例
bsp::read(r, arr);

// 可直接访问原类型
std::cout << arr[0][1][2];
// 更安全的用法
std::cout << arr.value[0].value[1].value[2];

// 也可以使用嵌套写法
using 3DArray = types::PVal<
                    types::PVal<
                        types::PVal<std::vector<int>, proto::Varint>,
                        proto::Fixed<4>
                    >,
                    proto::Varint
                >;
```

* 默认协议由模板参数 `Protocol` 决定  
  `Serializer<PVal<T, ProtocolT>>` 对应 `Serializer<T, Protocol=ProtocolT>`
* 可直接当作 `T` 类型来进行访问，但使用 `instance.value` 更加安全

> ⚠ 直接使用 `PVal<T, Protocol> instance` 作为 `T` 类型实例可能发生的隐式转换错误：
>
> ```c++
> void f(int&);
> void f(types::PVal<int, proto::Fixed<16>>&);
>
> types::PVal<int, Fixed<16>> x;
> f(x);  // 可能调用 f(int&) 而非 f(PVal<...>&)
> ```

### 5.2 运行时多态值类型 `CVal`

`CVal` 是基于虚类的 **运行时多态值类型**，替代 `Any` 用于存储不同类型的值，同时支持动态协议序列化。

#### 5.2.1 定义与继承

```c++
struct CVal {
    virtual ~CVal() = default;

    // 写入序列化数据，protocol 可指定不同协议
    virtual void write(io::Writer &w, const std::type_info &protocol) const = 0;

    // 读取序列化数据，protocol 可指定不同协议
    virtual void read(io::Reader &r, const std::type_info &protocol) = 0;
};
```

* 必须在子类中实现 `write` 与 `read`
* 默认 protocol 为 `proto::CVal`，也可以指定其他类型

#### 5.2.2 示例：整型 `CVal` 子类

`IntCVal` 子类负责自身类型的序列化：

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

#### 5.2.3 序列化

```c++
// 单个值
std::stringstream ss;
io::Writer w(ss);
io::Reader r(ss);

IntCVal writeVal(12345);
writeVal.write(w, typeid(proto::CVal));  // 使用CVal本身的写入
w.write(writeVal);                       // 使用接口读取

IntCVal readVal;
readVal.read(r, typeid(proto::CVal));    // 使用CVal本身的读取
r.read(readVal);                         // 使用接口读取

// 容器嵌套
std::vector<IntCVal> writeVec = {IntCVal(111), IntCVal(-222), IntCVal(333)};
w.write(writeVec);

std::vector<IntCVal> readVec;
r.read(readVec);
```

#### 5.2.4 Protocol 控制

你可以使用 `protocol` 参数来控制协议：

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

> ⚠ `protocol` 参数只能保留 `type_info`，如 `Fixed<N>` 中的 `N` 会被擦除。

> 💡 **使用建议**
> * `CVal` 是 `Any` 的替代品，无需模板就能存储多种类型
> * 虚类会带来额外的开销，建议仅在强需求运行时多态，或结构极为复杂时使用 `CVal`
> * `CVal` 本身不管理内存。需要自行管理或用 `unique_ptr` / `shared_ptr`
> * 序列化实现中，可复用其它的 `Serializer<T, Protocol>` 实现

## 6. 自定义结构体 Schema

### 6.1 定义结构体

```c++
struct Point { int x; int y; };
struct Rect { Point p1; Point p2; };
```

### 6.2 注册结构体

```c++
BSP_REGISTER_STRUCT(Point,
    BSP_FIELD(Point, x),
    BSP_FIELD(Point, y)
);

BSP_REGISTER_STRUCT(Rect,
    BSP_FIELD(Rect, p1),
    BSP_FIELD(Rect, p2)
);

// 使用以下宏可以自定义字段采用的协议
// BSP_FIELD_WITH(Point, x, bsp::proto::Varint);
```

* `BSP_FIELD` 默认使用 `DefaultProtocol_t<T>`
* `BSP_FIELD_WITH` 可自定义字段采用的协议

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
     >   Clangd 在分析模板时，发现 `DefaultProtocol<T>` 仍是 `bsp::proto::Default`，触发了 `static_assert`。  
     >   实际上**编译器本身不会报错**，只是 Clangd 的模板解析器无法在 IDE 中正确推导出宏生成的偏特化。
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

### 6.3 序列化结构体

```c++
Point pt{1,2};
w.write(pt);  // 写入结构体
r.read(pt);   // 读取结构体
```

* 结构：`[注册字段1][注册字段2]...`
* 字段顺序由注册顺序决定
* 默认协议由 `DefaultProtocol` 决定（注册后默认为 `Schema`）

## 7. 高级自定义

### 7.1 自定义 DefaultProtocol

在 `bsp::proto` 命名空间下定义，即可处处使用：

```c++
namespace bsp::proto {
    template<>
    struct DefaultProtocol<MyType> {
        using type = Fixed<4>;  // 默认序列化为 Fixed<4>
    };
}
```

### 7.2 自定义 Serializer

在 `bsp::serializer` 命名空间下定义，即可处处使用：

```c++
struct Encrypt {};

namespace bsp::serialize {
    template<>
    struct Serializer<int, Encrypt> {
        static void write(io::Writer &w, const int &s) {
            int encrypted = s ^ 0x55AA;  // 简单加密
            utils::write_uleb128(w, encrypted);  // 使用变长编码写入
        }
        static void read(io::Reader &r, int &out) {
            int encrypted = static_cast<int>(utils::read_uleb128(r));  // 读取变长编码
            out = encrypted ^ 0x55AA;  // 解密
        }
    };
}
```

## 8. I/O 接口

```c++
bsp::io::Writer w(os);
bsp::io::Reader r(is);

// 字节读写
uint8_t data[16];
w.writeBytes(data, sizeof(data));  // 写入字节数组
r.readBytes(data, 16);             // 读取字节数组

uint8_t b;
w.writeByte(0xFF);  // 写入单个字节
r.readByte(b);      // 读取单个字节

// 结构读写
// 函数式编程
bsp::write(w, value);                      // 使用 DefaultProtocol_t<T>
bsp::write<bsp::proto::Varint>(w, value);  // 指定协议

// 面向对象的等价写法
w.write(value);
w.write<bsp::proto::Varint>(value);

// 函数式编程
bsp::read(r, value);
bsp::read<bsp::proto::Fixed<4>>(r, value);

// 面向对象的等价写法
r.read(value);
r.read<bsp::proto::Fixed<4>>(r, value);
```

## 9. 错误处理

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

## 10. 常见问题

* **容器元素协议**：使用 `PVal` 显式绑定元素协议，避免 DefaultProtocol 歧义
* **PVal 隐式转换**：见 5.1 节
* **ByteView 内存管理**：read 会 new 分配，调用方需 delete[]
* **clangd 报红**：见 6.2 节
* **Fixed 容器大小断言**：不匹配抛出 `LengthOverflow`
* **版本兼容**：Schema 是顺序敏感，无字段 ID，字段顺序变动会影响兼容性

## 11. 示例使用指引

ByteSchema 提供示例覆盖常用和高级用法：

```
examples/
├── 01_basic.cpp             // 原生类型序列化
├── 02_vector_map.cpp        // vector / map 序列化
├── 03_option_variant.cpp    // Option / variant
├── 04_pval.cpp              // PVal 实现多维数组
├── 05_schema.cpp            // 自定义 Schema
├── 06_custom_serializer.cpp // 自定义 Serializer & Protocol 实现加密数字
├── 07_cval.cpp              // CVal
```

编译示例：

```bash
cd examples
g++ -std=c++20 -I../include 01_basic.cpp -o 01_basic
./01_basic
```

> 所有示例中 `bsp.hpp` 路径均为 `../include/bsp.hpp`。