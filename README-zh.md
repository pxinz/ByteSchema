# ByteSchema

> **C++ 高性能二进制编码 + Schema 库**
> 轻量、零依赖、强类型
> 使用 C++ 类型直接定义 Schema
> [English Version](./README.md)

---

## 核心特性

* Header-only，无运行时反射
* 支持多协议 & 复杂数据结构
* 高频率通信 & 大规模数据优化
* 支持 C++20 及以上
* 类型安全，IDE 完全支持补全

---

## 核心概念

更多用法请见 [`usage-zh.md`](./usage-zh.md)

### Protocol

ByteSchema 支持多种协议，每种协议定义了二进制编码规则：

| Protocol     | 描述                        | 适用场景        |
|--------------|---------------------------|-------------|
| `Varint`     | 变长整数编码，节省空间               | 小整数或可变长度容器  |
| `Fixed<N>`   | 固定长度编码                    | 定长数组、固定宽度结构 |
| 自定义 Protocol | 可自行实现 `read/write`，适配特殊需求 | 特殊协议或性能优化   |

> 支持 **嵌套组合**，可表达任意复杂数据结构。

---

### PVal<T, Protocol>

包装类型，将普通类型绑定到指定 Protocol：

```c++
bsp::PVal<int, bsp::proto::Varint> age{18}; // 单值
bsp::PVal<std::vector<int>, bsp::proto::Fixed<2>> scores{{90, 80, 70}}; // 单层 vector
```

嵌套组合示例：

```c++
bsp::PVal<
    std::vector<bsp::PVal<int, bsp::proto::Varint>>,
    bsp::proto::Fixed<2>
> nestedScores{{ {100, 90}, {80, 70} }};
```

> PVal 支持多维数组、协议绑定，避免容器元素协议不明确的问题。

---

### Serializer<T, Protocol>

模板静态生成读写逻辑：

```c++
template<typename T, typename Protocol>
struct Serializer {
    static void write(io::Writer&, const T&);
    static void read(io::Reader&, T&);
};
```

特性：

* **静态类型绑定**，无需运行时反射
* **高性能**，嵌套组合自然支持
* 支持任意类型组合，包括 `vector`, `PVal`, 自定义结构

---

### Any —— 动态类型

当类型在编译时未知，可使用 `Any`：

```c++
bsp::types::Any msg;
msg.read(reader);
msg.write(writer);
```

> 适合通用消息、插件式数据结构。

---

### Schema —— 类型内定义结构体

```c++
struct Player {
    bsp::PVal<int, bsp::proto::Varint> id;
    bsp::PVal<std::string, bsp::proto::Varint> name;
};
```

特点：

* 高效、无运行时解析
* 类型安全、IDE 支持补全
* 序列化行为完全由类型决定
* 可通过宏注册字段，支持自定义协议和默认协议映射

---

## 快速上手

### 1. 添加头文件

```c++
#include "../include/bsp.hpp"
```

### 2. 定义类型

```c++
struct Msg {
    int id;
    std::string content;
};

// 定义消息结构 Msg
BSP_SCHEMA(Msg,
    BSP_FIELD(id, int),
    BSP_FIELD(content, std::string)
);
```

### 3. 写入 / 读取示例

```c++
#include <iostream>
#include <sstream>

int main() {
    std::stringstream ss;

    // 写入
    bsp::io::Writer writer(ss);
    Msg msg{{1}, {"Hello World"}};
    bsp::write(writer, msg);

    // 读取
    bsp::io::Reader reader(ss);
    Msg msg2;
    bsp::read(reader, msg2);

    std::cout << "id=" << msg2.id.value
              << " content=" << msg2.content.value << "\n";
}
```

> 直接编译运行即可验证序列化和反序列化。

---

## Examples 使用指引

ByteSchema 提供丰富示例，覆盖常用和复杂用法：

```
examples/
├── 01_basic.cpp           // 基础类型序列化
├── 02_vector_map.cpp      // 容器序列化
├── 03_option_variant.cpp  // Option 和 Variant
├── 04_pval.cpp            // PVal / 多维数组
├── 05_schema.cpp          // 自定义 Schema
├── 06_custom_serializer.cpp // 自定义 Serializer 示例
```

编译示例：

```bash
cd examples
g++ -std=c++20 -I../include 01_basic.cpp -o 01_basic
./01_basic
```

或使用 `Makefile` / `CMakeLists.txt` 批量编译所有示例。

> 所有示例中 `bsp.hpp` 路径均为 `../include/bsp.hpp`。

---

## 设计理念

* 灵感来源 **Minecraft 通讯协议**
* 高性能 + 类型绑定
* 支持复杂数据结构与高频通信
* C++20 模板特性实现零运行时开销

---

## 贡献指南

欢迎提交 issue / PR：

1. 修复 bug
2. 增加 Protocol 或 Codec
3. 提供更多使用示例
4. 性能优化

**注意事项**：

* PR 前确保 `examples/*.cpp` 可编译
* 遵循 C++20 标准
* 遵循命名风格与类型约定

---

## 开源协议

MIT LICENSE

使用和修改请遵循 MIT 协议。
