/// @file bsp.hpp
/// @brief byteschema 库头文件
///
/// 该头文件实现了一个轻量的、基于模板的字节序列化/反序列化框架，支持
/// - 固定宽度类型（Fixed）
/// - 变长整数/变长容器（Varint）
/// - 用户自定义结构体 Schema（Schema）

// TODO: 运行时Any
// TODO: max_depth实现

#pragma once

#include <cstdint>
#include <concepts>
#include <vector>
#include <tuple>
#include <string>
#include <map>
#include <type_traits>
#include <cstring>
#include <memory>
#include <utility>
#include <istream>
#include <ostream>
#include <limits>
#include <variant>
#include <stdexcept>

namespace bsp {
    /// @brief 错误处理策略枚举
    ///
    /// 用于控制在遇到协议不一致或异常数据时的库行为。
    enum ErrorPolicy {
        /// 严格模式：遇到任何协议错误抛异常
        STRICT = 1,
        /// 中等：尝试容忍某些非致命错误
        MEDIUM = 2,
        /// 忽略：尽量忽略错误，继续处理（谨慎使用）
        IGNORE = 3
    };

    /// @brief 全局选项与限制
    ///
    /// 提供对字节序、最大深度、容器/字符串大小等的全局配置。
    struct GlobalOptions {
        /// @brief 字节序设置，默认大端
        std::endian endian = std::endian::big;

        /// @brief 最大递归深度（防止恶意构造导致栈/递归耗尽），暂无实现
        size_t max_depth = 64;
        /// @brief 容器（vector/map 等）允许的最大元素数量
        size_t max_container_size = 1 << 20;
        /// @brief 字符串/字节数组允许的最大字节数
        size_t max_string_size = 1 << 20;

        /// @brief 是否在读取流末尾时报错（严格 EOF 检查）
        bool strict_eof = false;
        /// @brief 错误处理策略
        ErrorPolicy error_policy = STRICT;

        /// @brief 单例访问
        ///
        /// 使用函数静态局部变量实现线程安全延迟初始化。
        static GlobalOptions &instance() {
            static GlobalOptions opt;
            return opt;
        }
    };

    namespace error {
        /// @brief 协议相关的基类异常
        struct ProtocolError : std::runtime_error {
            using runtime_error::runtime_error;
        };

        /// @brief 非预期 EOF
        struct UnexpectedEOF final : ProtocolError {
            using ProtocolError::ProtocolError;
        };

        /// @brief 无效的变长整数编码
        struct InvalidVarint final : ProtocolError {
            using ProtocolError::ProtocolError;
        };

        /// @brief 长度溢出（超过配置的最大长度）
        struct LengthOverflow final : ProtocolError {
            using ProtocolError::ProtocolError;
        };

        /// @brief Variant 索引越界
        struct VariantOutOfRange final : ProtocolError {
            using ProtocolError::ProtocolError;
        };

        /// @brief ABI / 不兼容错误
        struct ABIError final : ProtocolError {
            using ProtocolError::ProtocolError;
        };
    }

    namespace types {
        /// @brief 可选值模板
        ///
        /// 类似于 std::optional，但为了避免依赖及控制二进制布局而提供简化版本。
        template<typename T>
        struct Option {
            /// 是否有值
            bool has_value = false;
            /// 值（未初始化时不可读取）
            T value;

            Option() = default;

            explicit Option(const T &v) : has_value(true), value(v) {
            }
        };

        /// 字节数组类型别名
        using ByteArray = std::vector<uint8_t>;

        /// 非拥有者字节视图（注意：读取时本实现会分配内存）
        struct ByteView {
            const uint8_t *data = nullptr;
            size_t size = 0;
        };

        /// @brief 携带 Protocol 的值类型
        /// @tparam T 实际值类型
        /// @tparam Protocol 序列化策略
        template<typename T, typename Protocol>
        struct PVal {
            T value;

            PVal() = default;

            explicit PVal(const T &v) : value(v) {
            }

            explicit PVal(T &&v) : value(std::move(v)) {
            }

            // 隐式转换，方便访问 value
            explicit operator T &() { return value; }
            explicit operator const T &() const { return value; }

            T &get() { return value; }
            const T &get() const { return value; }
        };
    }

    /// 抽象接口定义
    /// @{

    // 协议标签定义
    namespace proto {
        /// @brief 固定宽度协议标签
        /// @tparam N 可选的字节宽度（0 表示使用 Varint/动态处理）
        template<size_t N = 0>
        struct Fixed {
        };

        /// @brief 变长协议标签（用于变长整数、字符串、容器等）
        struct Varint {
        };

        /// @brief 表示该类型由用户提供 Schema 描述（结构体序列化）
        struct Schema {
        };

        /// @brief 默认占位符协议类型
        struct Default {
        };

        /// @brief 默认协议类型映射（不可覆盖时退回 Default）
        template<typename T>
        struct DefaultProtocol {
            using type = Default;
        };

        /// @brief 工具别名，获取类型的默认协议
        template<typename T>
        using DefaultProtocol_t = DefaultProtocol<T>::type;
    }

    // 序列化器定义
    namespace serialize {
        /// @brief Serializer 模板：针对不同类型和协议提供特化
        template<typename T, typename Protocol>
        struct Serializer;
    }

    // 模式定义
    namespace schema {
        /// @brief 结构体字段描述模板
        /// @tparam Struct 所属结构体类型
        /// @tparam MemberType 成员字段类型
        /// @tparam Protocol 成员字段使用的协议标签（默认由 DefaultProtocol 决定）
        ///
        /// 用法示例：
        /// @code
        /// struct Point { int x; int y; };
        /// template<> struct Schema<Point> {
        ///   static constexpr auto fields() {
        ///     return std::make_tuple(
        ///       Field<Point, int>("x", &Point::x),
        ///       Field<Point, int>("y", &Point::y)
        ///     );
        ///   }
        /// };
        /// @endcode
        template<typename Struct, typename MemberType, typename Protocol = proto::DefaultProtocol_t<MemberType> >
        struct Field {
            using member_type = MemberType;
            using protocol_type = Protocol;
            const char *name;
            MemberType Struct::*ptr;

            constexpr Field(const char *n, MemberType Struct::*p) : name(n), ptr(p) {
            }
        };

        /// @brief 用户必须为自定义结构体特化该模板以提供 fields() 函数
        template<typename T>
        struct Schema; // 用户必须特化

        /// @brief trait：判断类型是否定义了 Schema<T>::fields()
        template<typename T, typename = void>
        struct has_schema : std::false_type {
        };

        template<typename T>
        struct has_schema<T, std::void_t<decltype(Schema<T>::fields())> > : std::true_type {
        };

        /// @brief 概念：满足 SchemaType 意味着可以进行结构体序列化
        template<typename T>
        concept SchemaType = requires
        {
            { Schema<T>::fields() };
        };
    }

    /// @}

    // I/O 包装器
    namespace io {
        /// @brief 写入器
        ///
        /// 封装 std::ostream，并提供按字节写入的便捷接口。
        struct Writer {
            std::ostream &os;

            explicit Writer(std::ostream &o) : os(o) {
            }

            /// @brief 写入字节数组
            /// @param data 指向源字节
            /// @param n 写入字节数
            void writeBytes(const uint8_t *data, size_t n) {
                os.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(n));
            }

            /// @brief 写入单个字节
            void writeByte(uint8_t b) { writeBytes(&b, 1); }

            /// @brief 写入数据体，相当于write(w, v)
            template<typename Protocol, typename T>
            inline void write(const T &v) {
                serialize::Serializer<T, Protocol>::write(this, v);
            }

            /// @brief 写入数据体，相当于write(w, v)
            template<typename T>
            inline void write(const T &v) {
                serialize::Serializer<T, proto::DefaultProtocol_t<T> >::write(this, v);
            }
        };

        /// @brief 读取器
        ///
        /// 封装 std::istream，并在发生 EOF/IO 错误时抛出异常。
        struct Reader {
            std::istream &is;

            explicit Reader(std::istream &i) : is(i) {
            }

            /// @brief 从流中读取指定数量的字节到 buffer
            /// @throws error::UnexpectedEOF 当读取失败或到达流尾
            void readBytes(uint8_t *data, size_t n) {
                is.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(n));
                if (!is) throw error::UnexpectedEOF("unexpected EOF while reading");
            }

            /// @brief 读取单字节
            uint8_t readByte() {
                uint8_t b;
                readBytes(&b, 1);
                return b;
            }

            /// @brief 读出数据体，相当于read(r, out)
            template<typename Protocol, typename T>
            inline void read(T &out) {
                serialize::Serializer<T, Protocol>::read(this, out);
            }

            /// @brief 读出数据体，相当于read(r, out)
            template<typename T>
            inline void read(T &out) {
                serialize::Serializer<T, proto::DefaultProtocol_t<T> >::read(this, out);
            }
        };
    }

    /// 具体实现
    /// @{

    // 工具函数
    namespace utils {
        /// @brief ZigZag 编码（将有符号整数映射为无符号以便变长编码）
        inline uint64_t zigzag_encode(int64_t x) {
            return (static_cast<uint64_t>(x) << 1) ^ static_cast<uint64_t>(x >> 63);
        }

        /// @brief ZigZag 解码
        inline int64_t zigzag_decode(uint64_t v) {
            return static_cast<int64_t>((v >> 1) ^ (~(v & 1) + 1));
        }

        /// @brief 以 ULEB128 格式写入无符号变长整数
        inline void write_uleb128(io::Writer &w, uint64_t v) {
            while (v > 0x7F) {
                auto b = static_cast<uint8_t>((v & 0x7F) | 0x80);
                w.writeByte(b);
                v >>= 7;
            }
            w.writeByte(static_cast<uint8_t>(v));
        }

        /// @brief 从流中读取 ULEB128 编码的无符号变长整数
        /// @throws error::InvalidVarint 当 varint 过大
        inline uint64_t read_uleb128(io::Reader &r) {
            uint64_t res = 0;
            int shift = 0;
            while (true) {
                const uint8_t b = r.readByte();
                res |= static_cast<uint64_t>(b & 0x7F) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
                if (shift >= 64) {
                    throw error::InvalidVarint("varint too large");
                }
            }
            return res;
        }
    }

    // 默认协议标签指定
    namespace proto {
        // 针对常见类型的默认协议映射
        template<>
        struct DefaultProtocol<bool> {
            using type = Fixed<>;
        };

        template<std::unsigned_integral T>
        struct DefaultProtocol<T> {
            using type = Fixed<>;
        };

        template<std::signed_integral T>
        struct DefaultProtocol<T> {
            using type = Fixed<>;
        };

        template<std::floating_point T>
        struct DefaultProtocol<T> {
            using type = Fixed<>;
        };

        template<>
        struct DefaultProtocol<std::string> {
            using type = Varint;
        };

        template<>
        struct DefaultProtocol<types::ByteArray> {
            using type = Varint;
        };

        template<typename T>
        struct DefaultProtocol<std::vector<T> > {
            using type = Varint;
        };

        template<typename K, typename V>
        struct DefaultProtocol<std::map<K, V> > {
            using type = Varint;
        };

        template<typename... Ts>
        struct DefaultProtocol<std::tuple<Ts...> > {
            using type = Fixed<>;
        };

        template<typename... Ts>
        struct DefaultProtocol<std::variant<Ts...> > {
            using type = Varint;
        };

        template<typename T>
        struct DefaultProtocol<types::Option<T> > {
            using type = Varint;
        };

        template<typename T, typename Protocol>
        struct DefaultProtocol<types::PVal<T, Protocol> > {
            using type = Protocol;
        };

        /// @brief 对于满足 schema::SchemaType 的类型，默认协议为 proto::Schema
        template<schema::SchemaType T>
        struct DefaultProtocol<T> {
            using type = Schema;
        };
    }

    // 具体序列化器
    namespace serialize {
        /// @name 基础类型序列化
        /// @{

        // bool 类型
        template<>
        struct Serializer<bool, proto::Fixed<> > {
            static void write(io::Writer &w, bool v) {
                w.writeByte(v);
            }

            static void read(io::Reader &r, bool &v) {
                v = r.readByte();
            }
        };

        // 无符号整数（固定宽度）
        template<std::unsigned_integral T>
        struct Serializer<T, proto::Fixed<> > {
            static void write(io::Writer &w, const T &v) {
                constexpr size_t size = sizeof(T);
                if (GlobalOptions::instance().endian == std::endian::little) {
                    for (size_t i = 0; i < size; ++i) {
                        auto b = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
                        w.writeByte(b);
                    }
                } else {
                    for (size_t i = 0; i < size; ++i) {
                        auto b = static_cast<uint8_t>((v >> (8 * (size - 1 - i))) & 0xFF);
                        w.writeByte(b);
                    }
                }
            }

            static void read(io::Reader &r, T &out) {
                constexpr size_t size = sizeof(T);
                T x = 0;

                if (GlobalOptions::instance().endian == std::endian::little) {
                    for (size_t i = 0; i < size; ++i) {
                        uint8_t b = r.readByte();
                        x |= T(b) << (8 * i);
                    }
                } else {
                    for (size_t i = 0; i < size; ++i) {
                        uint8_t b = r.readByte();
                        x |= T(b) << (8 * (size - 1 - i));
                    }
                }

                out = x;
            }
        };

        // 有符号整数（固定宽度）
        template<std::signed_integral T>
        struct Serializer<T, proto::Fixed<> > {
            static void write(io::Writer &w, const T &v) {
                using U = std::make_unsigned_t<T>;
                U u = static_cast<U>(v);
                Serializer<U, proto::Fixed<> >::write(w, u);
            }

            static void read(io::Reader &r, T &out) {
                using U = std::make_unsigned_t<T>;
                U u = 0;
                Serializer<U, proto::Fixed<> >::read(r, u);
                out = static_cast<T>(u);
            }
        };

        // 浮点数（以字节直接拷贝的方式处理）
        template<std::floating_point T>
        struct Serializer<T, proto::Fixed<> > {
            static void write(io::Writer &w, const T &v) {
                static_assert(std::numeric_limits<T>::is_iec559, "requires IEEE float");
                uint8_t tmp[sizeof(T)];
                std::memcpy(tmp, &v, sizeof(T));
                w.writeBytes(tmp, sizeof(T));
            }

            static void read(io::Reader &r, T &out) {
                uint8_t tmp[sizeof(T)];
                r.readBytes(tmp, sizeof(T));
                std::memcpy(&out, tmp, sizeof(T));
            }
        };

        /// @}


        /// @name 变长整数序列化
        /// @{

        // 无符号变长整数（Varint）
        template<std::unsigned_integral T>
        struct Serializer<T, proto::Varint> {
            static void write(io::Writer &w, const T &v) {
                utils::write_uleb128(w, static_cast<uint64_t>(v));
            }

            static void read(io::Reader &r, T &out) {
                uint64_t v = utils::read_uleb128(r);
                out = static_cast<T>(v);
            }
        };

        // 有符号变长整数（利用 ZigZag 编码）
        template<std::signed_integral T>
        struct Serializer<T, proto::Varint> {
            static void write(io::Writer &w, const T &v) {
                uint64_t z = utils::zigzag_encode(static_cast<int64_t>(v));
                utils::write_uleb128(w, z);
            }

            static void read(io::Reader &r, T &out) {
                uint64_t z = utils::read_uleb128(r);
                out = static_cast<T>(utils::zigzag_decode(z));
            }
        };

        /// @}


        /// @name 容器序列化
        /// @{

        // 字符串（变长）
        template<>
        struct Serializer<std::string, proto::Varint> {
            static void write(io::Writer &w, const std::string &s) {
                utils::write_uleb128(w, s.size());
                if (!s.empty()) w.writeBytes(reinterpret_cast<const uint8_t *>(s.data()), s.size());
            }

            static void read(io::Reader &r, std::string &out) {
                uint64_t len = utils::read_uleb128(r);
                if (len > GlobalOptions::instance().max_string_size) {
                    throw error::LengthOverflow("string too large");
                }
                out.resize(static_cast<size_t>(len));
                if (len) r.readBytes(reinterpret_cast<uint8_t *>(out.data()), static_cast<size_t>(len));
            }
        };

        // 字符串（固定宽度）
        template<size_t N>
        struct Serializer<std::string, proto::Fixed<N> > {
            static void write(io::Writer &w, const std::string &s) {
                size_t to_write = std::min(s.size(), N);
                if (to_write) w.writeBytes(reinterpret_cast<const uint8_t *>(s.data()), to_write);
                if (N > to_write) {
                    types::ByteArray pad(N - to_write);
                    w.writeBytes(pad.data(), pad.size());
                }
            }

            static void read(io::Reader &r, std::string &out) {
                out.resize(N);
                if (N) r.readBytes(reinterpret_cast<uint8_t *>(out.data()), N);
            }
        };

        // 字节数组（变长）
        template<>
        struct Serializer<types::ByteArray, proto::Varint> {
            static void write(io::Writer &w, const types::ByteArray &v) {
                utils::write_uleb128(w, v.size());
                if (!v.empty()) w.writeBytes(v.data(), v.size());
            }

            static void read(io::Reader &r, types::ByteArray &out) {
                uint64_t len = utils::read_uleb128(r);
                if (len > GlobalOptions::instance().max_string_size) {
                    throw error::LengthOverflow("bytearray too large");
                }
                out.resize(static_cast<size_t>(len));
                if (len) r.readBytes(out.data(), static_cast<size_t>(len));
            }
        };

        // 字节数组（固定宽度）
        template<size_t N>
        struct Serializer<types::ByteArray, proto::Fixed<N> > {
            static void write(io::Writer &w, const types::ByteArray &v) {
                if (v.size() != N) {
                    throw error::LengthOverflow("bytearray size mismatch");
                }
                if (N) w.writeBytes(v.data(), N);
            }

            static void read(io::Reader &r, types::ByteArray &out) {
                out.resize(N);
                if (N) r.readBytes(out.data(), N);
            }
        };

        // 字节视图（注意：read 会分配内存并将指针指向新分配的缓冲区）
        template<>
        struct Serializer<types::ByteView, proto::Varint> {
            static void write(io::Writer &w, const types::ByteView &v) {
                utils::write_uleb128(w, v.size);
                w.writeBytes(v.data, v.size);
            }

            static void read(io::Reader &r, types::ByteView &v) {
                uint64_t len = utils::read_uleb128(r);
                if (len > GlobalOptions::instance().max_string_size) {
                    throw error::LengthOverflow("byteview too large");
                }

                auto *buf = new uint8_t[len];
                r.readBytes(buf, len);

                v.data = buf;
                v.size = len;
            }
        };

        // 元组（按元素依次序列化）
        template<typename... Ts>
        struct Serializer<std::tuple<Ts...>, proto::Fixed<> > {
            static void write(io::Writer &w, const std::tuple<Ts...> &t) {
                std::apply([&](auto const &... elems) {
                    ((Serializer<
                        std::remove_cvref_t<decltype(elems)>,
                        proto::DefaultProtocol_t<std::remove_cvref_t<decltype(elems)> >
                    >::write(w, elems)), ...);
                }, t);
            }

            static void read(io::Reader &r, std::tuple<Ts...> &t) {
                std::apply([&](auto &... elems) {
                    ((Serializer<
                        std::remove_cvref_t<decltype(elems)>,
                        proto::DefaultProtocol_t<std::remove_cvref_t<decltype(elems)> >
                    >::read(r, elems)), ...);
                }, t);
            }
        };

        // 映射（map）
        template<typename K, typename V>
        struct Serializer<std::map<K, V>, proto::Varint> {
            static void write(io::Writer &w, const std::map<K, V> &m) {
                utils::write_uleb128(w, m.size());
                for (auto const &p: m) {
                    Serializer<K, proto::DefaultProtocol_t<K> >::write(w, p.first);
                    Serializer<V, proto::DefaultProtocol_t<V> >::write(w, p.second);
                }
            }

            static void read(io::Reader &r, std::map<K, V> &out) {
                uint64_t len = utils::read_uleb128(r);
                if (len > GlobalOptions::instance().max_container_size) {
                    throw error::LengthOverflow("map too large");
                }
                out.clear();
                for (uint64_t i = 0; i < len; ++i) {
                    K k;
                    V v;
                    Serializer<K, proto::DefaultProtocol_t<K> >::read(r, k);
                    Serializer<V, proto::DefaultProtocol_t<V> >::read(r, v);
                    out.emplace(std::move(k), std::move(v));
                }
            }
        };

        template<typename K, typename V, size_t N>
        struct Serializer<std::map<K, V>, proto::Fixed<N> > {
            static void write(io::Writer &w, const std::map<K, V> &m) {
                if (m.size() != N) {
                    throw error::LengthOverflow("map size mismatch");
                }
                for (auto const &p: m) {
                    Serializer<K, proto::DefaultProtocol_t<K> >::write(w, p.first);
                    Serializer<V, proto::DefaultProtocol_t<V> >::write(w, p.second);
                }
            }

            static void read(io::Reader &r, std::map<K, V> &out) {
                out.clear();
                for (size_t i = 0; i < N; ++i) {
                    K k;
                    V v;
                    Serializer<K, proto::DefaultProtocol_t<K> >::read(r, k);
                    Serializer<V, proto::DefaultProtocol_t<V> >::read(r, v);
                    out.emplace(std::move(k), std::move(v));
                }
            }
        };

        // 向量（vector）
        template<typename Elem>
        struct Serializer<std::vector<Elem>, proto::Varint> {
            static void write(io::Writer &w, const std::vector<Elem> &vec) {
                utils::write_uleb128(w, vec.size());
                for (auto const &e: vec) {
                    Serializer<Elem, proto::DefaultProtocol_t<Elem> >::write(w, e);
                }
            }

            static void read(io::Reader &r, std::vector<Elem> &out) {
                uint64_t len = utils::read_uleb128(r);
                if (len > GlobalOptions::instance().max_container_size) {
                    throw error::LengthOverflow("vector too large");
                }
                out.clear();
                out.reserve(static_cast<size_t>(len));
                for (uint64_t i = 0; i < len; ++i) {
                    Elem e;
                    Serializer<Elem, proto::DefaultProtocol_t<Elem> >::read(r, e);
                    out.push_back(std::move(e));
                }
            }
        };

        template<typename Elem, size_t N>
        struct Serializer<std::vector<Elem>, proto::Fixed<N> > {
            static void write(io::Writer &w, const std::vector<Elem> &vec) {
                if (vec.size() != N) {
                    throw error::LengthOverflow("vector size mismatch");
                }
                for (auto const &e: vec) Serializer<Elem, proto::DefaultProtocol_t<Elem> >::write(w, e);
            }

            static void read(io::Reader &r, std::vector<Elem> &out) {
                out.clear();
                out.resize(N);
                for (size_t i = 0; i < N; ++i)
                    Serializer<Elem, proto::DefaultProtocol_t<Elem> >::read(r, out[i]);
            }
        };

        /// @}

        // 结构体模式（用户需特化 schema::Schema<T>）
        template<typename T>
        struct Serializer<T, proto::Schema> {
            static void write(io::Writer &w, const T &obj) {
                static_assert(schema::has_schema<T>::value, "Schema<T> must be defined for struct serialization");
                constexpr auto fields = schema::Schema<T>::fields();
                std::apply([&](auto const &... fld) {
                    ((Serializer<
                        typename std::remove_cvref_t<decltype(fld)>::member_type,
                        typename std::remove_cvref_t<decltype(fld)>::protocol_type
                    >::write(w, obj.*(fld.ptr))), ...);
                }, fields);
            }

            static void read(io::Reader &r, T &out) {
                static_assert(schema::has_schema<T>::value, "Schema<T> must be defined for struct deserialization");
                constexpr auto fields = schema::Schema<T>::fields();
                std::apply([&](auto &... fld) {
                    ((Serializer<
                        typename std::remove_cvref_t<decltype(fld)>::member_type,
                        typename std::remove_cvref_t<decltype(fld)>::protocol_type
                    >::read(r, out.*(fld.ptr))), ...);
                }, fields);
            }
        };

        // 默认回退实现：将 Default 代理到 DefaultProtocol_t
        template<typename T>
        struct Serializer<T, proto::Default> {
            static_assert(
                !std::is_same_v<proto::DefaultProtocol_t<T>, proto::Default>,
                "No concrete DefaultProtocol for this type"
            );

            static void write(io::Writer &w, const T &v) {
                Serializer<T, proto::DefaultProtocol_t<T> >::write(w, v);
            }

            static void read(io::Reader &r, T &out) {
                Serializer<T, proto::DefaultProtocol_t<T> >::read(r, out);
            }
        };

        // 指定Protocol类型的序列化
        template<typename T, typename ProtocolT, typename Protocol>
        struct Serializer<types::PVal<T, ProtocolT>, Protocol> {
            static void write(io::Writer &w, const types::PVal<T, ProtocolT> &v) {
                Serializer<T, Protocol>::write(w, v.value);
            }

            static void read(io::Reader &r, types::PVal<T, ProtocolT> &out) {
                Serializer<T, Protocol>::read(r, out.value);
            }
        };

        /// @name 可变值序列化
        /// @{
        // 可选值
        template<typename T>
        struct Serializer<types::Option<T>, proto::Varint> {
            static void write(io::Writer &w, const types::Option<T> &opt) {
                utils::write_uleb128(w, opt.has_value ? 1 : 0);
                if (opt.has_value) {
                    Serializer<T, proto::DefaultProtocol_t<T> >::write(w, opt.value);
                }
            }

            static void read(io::Reader &r, types::Option<T> &out) {
                uint64_t flag = utils::read_uleb128(r);
                if (flag == 0) {
                    out.has_value = false;
                } else {
                    out.has_value = true;
                    Serializer<T, proto::DefaultProtocol_t<T> >::read(r, out.value);
                }
            }
        };

        // 可变类型值
        template<typename... Ts>
        struct Serializer<std::variant<Ts...>, proto::Varint> {
            static void write(io::Writer &w, const std::variant<Ts...> &v) {
                utils::write_uleb128(w, v.index());
                std::visit([&](auto &&val) {
                    using T = std::decay_t<decltype(val)>;
                    Serializer<T, proto::DefaultProtocol_t<T> >::write(w, val);
                }, v);
            }

            static void read(io::Reader &r, std::variant<Ts...> &out) {
                uint64_t idx = utils::read_uleb128(r);
                if (idx >= sizeof...(Ts)) throw error::VariantOutOfRange("variant index out of range");

                // 使用 index_sequence 生成 tuple，访问对应类型
                bool assigned = false;
                std::size_t current = 0;
                ([&] {
                    if (current++ == idx) {
                        using T = Ts;
                        T value;
                        Serializer<T, proto::DefaultProtocol_t<T> >::read(r, value);
                        out = std::move(value);
                        assigned = true;
                    }
                }(), ...);
                if (!assigned) throw error::VariantOutOfRange("variant index out of range");
            }
        };

        /// @}
    }

    /// @}

    // 模式宏
#define BSP_FIELD(TYPE, MEMBER) \
    bsp::schema::Field<TYPE, decltype(TYPE::MEMBER), bsp::proto::DefaultProtocol_t<decltype(TYPE::MEMBER)>>(#MEMBER, &TYPE::MEMBER)

#define BSP_FIELD_WITH(TYPE, MEMBER, PROTO) \
    bsp::schema::Field<TYPE, decltype(TYPE::MEMBER), PROTO>(#MEMBER, &TYPE::MEMBER)

#define BSP_REGISTER_STRUCT(TYPE, ...)              \
template<>                                          \
struct bsp::schema::Schema<TYPE> {                  \
    using Self = TYPE;                              \
    inline static constexpr auto fields_value =     \
        std::make_tuple(__VA_ARGS__);               \
    static constexpr const auto& fields() {         \
        return fields_value;                        \
    }                                               \
}


    /// @brief 写入接口
    template<typename Protocol, typename T>
    inline void write(io::Writer &w, const T &v) {
        serialize::Serializer<T, Protocol>::write(w, v);
    }

    /// @brief 写入接口
    template<typename T>
    inline void write(io::Writer &w, const T &v) {
        serialize::Serializer<T, proto::DefaultProtocol_t<T> >::write(w, v);
    }

    /// @brief 读出接口
    template<typename Protocol, typename T>
    inline void read(io::Reader &r, T &out) {
        serialize::Serializer<T, Protocol>::read(r, out);
    }

    /// @brief 读出接口
    template<typename T>
    inline void read(io::Reader &r, T &out) {
        serialize::Serializer<T, proto::DefaultProtocol_t<T> >::read(r, out);
    }
}
