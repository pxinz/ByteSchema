#ifndef BSP_HPP
#define BSP_HPP


#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100)
#endif

/*
===============================================================================
BSP - Bytestream Schema Protocol
Single-header serialization library

Features
--------
? Strongly typed serializers
? Protocol tags (Fixed, Varint, Schema, CVal, Trivial...)
? Compile-time dispatch
? Container safety limits
? Limited / Forced wrappers
? Varint + ZigZag encoding
? Schema macros
? Depth protection against malicious packets
? Endian-safe numeric serialization

Design goals
------------
1. Safe against fuzzing and construction attacks
2. Mostly compile-time resolution
3. Minimal runtime overhead
4. Easy schema registration
===============================================================================
*/

#include <stack>
#include <limits>
#include <vector>
#include <string>
#include <map>
#include <tuple>
#include <variant>
#include <optional>
#include <type_traits>
#include <stdexcept>
#include <bit>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <bitset>
#include <set>
#include <unordered_map>
#include <unordered_set>

// TODOs now:
// High
// Medium
// - Examples
// - Better Compile-Time asserts
// Low
// - Better code style (long-term)
// - Better comments (long-term)

// Length of Dividers ---------------------------------------------------------

namespace bsp {
    /* ========================================================================
     * Global Options
     * 全局设置
     * ======================================================================== */

    // === Error Policy =======================================================
    // 错误处理策略
    enum class ErrorPolicy {
        STRICT = 1,
        MEDIUM = 2,
        IGNORE = 3
    };

    // === Options ============================================================
    // 配置器
    struct options {
        friend struct OptionsGuard;

        static constexpr std::endian endian = std::endian::big; // Fixed

        std::optional<size_t> max_depth;
        std::optional<size_t> max_container_size; // Length
        std::optional<size_t> max_string_size; // Byte Size

        std::optional<ErrorPolicy> error_policy;

        void inherit(const options &fa) {
            if (!max_depth.has_value()) max_depth = fa.max_depth;
            if (!max_container_size.has_value()) max_container_size = fa.max_container_size;
            if (!max_string_size.has_value()) max_string_size = fa.max_string_size;
            if (!error_policy.has_value()) error_policy = fa.error_policy;
        }

    private:
        static thread_local std::stack<options> option_stack;

    public:
        static const options default_options;

        static void push(options options) {
            options.inherit(option_stack.top());
            option_stack.push(options);
        }

        static void pop() {
            if (option_stack.size() > 1) {
                option_stack.pop();
            }
        }

        static const options &current() {
            return option_stack.top();
        }

        static void reset() {
            while (option_stack.size() > 1) option_stack.pop();
        }
    };

    inline const options options::default_options{
        .max_depth = 256,
        .max_container_size = 1 * 1024 * 1024,
        .max_string_size = 4 * 1024 * 1024,
        .error_policy = ErrorPolicy::MEDIUM
    };

    thread_local std::stack<options> options::option_stack = [] {
        std::stack<options> st;
        st.push(default_options);
        return st;
    }();

    struct OptionsGuard {
    private:
        size_t sz;

    public:
        explicit OptionsGuard(const options &opts) {
            sz = options::option_stack.size();
            options::push(opts);
        }

        ~OptionsGuard() {
            while (options::option_stack.size() > sz) options::pop();
        }

        OptionsGuard(const OptionsGuard &) = delete;

        OptionsGuard &operator=(const OptionsGuard &) = delete;
    };


    /* ========================================================================
     * Class Definitions
     * 类声明
     * ======================================================================== */

    // === Error Classes ======================================================
    // 错误类
    namespace errors {
        struct Error : std::runtime_error {
            using runtime_error::runtime_error;
        };

        struct EOFError final : Error {
            size_t expected;
            size_t actual;
            const char *context;

            explicit EOFError(const size_t exp, const size_t act = 0, const char *ctx = "")
                : Error(build_message(exp, act, ctx)),
                  expected(exp), actual(act), context(ctx) {
            }

        private:
            static std::string build_message(const size_t exp, const size_t act, const char *ctx) {
                std::string msg = "bsp: unexpected EOF";
                if (ctx && *ctx) {
                    msg += " while ";
                    msg += ctx;
                }
                msg += " (expected " + std::to_string(exp) + " bytes";
                if (act > 0) msg += ", got " + std::to_string(act);
                msg += ")";
                return msg;
            }
        };

        struct DepthExceeded final : Error {
            size_t current_depth;
            size_t max_allowed;

            DepthExceeded(const size_t cur, const size_t maxd)
                : Error("bsp: container recursion depth exceeded (current=" +
                        std::to_string(cur) + ", max=" + std::to_string(maxd) + ")"),
                  current_depth(cur), max_allowed(maxd) {
            }
        };

        struct ContainerTooLarge final : Error {
            size_t requested_size;
            size_t max_allowed;
            const char *container_type; // "vector", "map", etc.

            ContainerTooLarge(const size_t req, const size_t maxd, const char *type)
                : Error("bsp: container size exceeds limit (requested=" +
                        std::to_string(req) + ", max=" + std::to_string(maxd) +
                        ", type=" + type + ")"),
                  requested_size(req), max_allowed(maxd), container_type(type) {
            }
        };

        struct StringTooLarge final : Error {
            size_t requested_size;
            size_t max_allowed;

            StringTooLarge(const size_t req, const size_t maxd)
                : Error("bsp: string size exceeds limit (requested=" +
                        std::to_string(req) + ", max=" + std::to_string(maxd) + ")"),
                  requested_size(req), max_allowed(maxd) {
            }
        };

        struct VarintOverflow final : Error {
            int max_bits; // 例如 64
            const char *type_name; // "uint32_t"

            VarintOverflow(const int bits, const char *tname)
                : Error("bsp: varint overflow (max bits=" + std::to_string(bits) +
                        ", type=" + tname + ")"),
                  max_bits(bits), type_name(tname) {
            }
        };

        struct InvalidBool final : Error {
            uint8_t bad_value;

            explicit InvalidBool(const uint8_t val)
                : Error("bsp: invalid bool value " + std::to_string(val)),
                  bad_value(val) {
            }
        };

        struct InvalidVariantIndex final : Error {
            size_t index;
            size_t num_alternatives;

            InvalidVariantIndex(const size_t idx, const size_t num)
                : Error("bsp: invalid variant index (index=" + std::to_string(idx) +
                        ", alternatives=" + std::to_string(num) + ")"),
                  index(idx), num_alternatives(num) {
            }
        };

        struct FixedSizeMismatch final : Error {
            size_t expected_size;
            size_t actual_size;
            const char *context;

            FixedSizeMismatch(const size_t exp, const size_t act, const char *ctx = "")
                : Error("bsp: forced size mismatch (expected=" + std::to_string(exp) +
                        ", actual=" + std::to_string(act) +
                        (ctx && *ctx ? ", context=" + std::string(ctx) : "") + ")"),
                  expected_size(exp), actual_size(act), context(ctx) {
            }
        };

        struct InvalidEnumValue final : Error {
            int64_t value;
            const char *enum_name;

            InvalidEnumValue(const int64_t val, const char *name)
                : Error("bsp: invalid enum value " + std::to_string(val) +
                        " for enum " + name),
                  value(val), enum_name(name) {
            }
        };

        struct SchemaVersionMismatch final : Error {
            const char *expected_version;
            const char *actual_version;

            SchemaVersionMismatch(const char *exp, const char *act)
                : Error(std::string("bsp: schema version mismatch (expected=") +
                        exp + ", actual=" + act + ")"),
                  expected_version(exp), actual_version(act) {
            }
        };

        struct WriteError final : Error {
            const char *operation;

            explicit WriteError(const char *op = "write")
                : Error(std::string("bsp: write error during ") + op),
                  operation(op) {
            }
        };

        struct ReadError final : Error {
            const char *operation;

            explicit ReadError(const char *op = "read")
                : Error(std::string("bsp: read error during ") + op),
                  operation(op) {
            }
        };

        struct NullptrSerialization final : Error {
            NullptrSerialization()
                : Error("bsp: attempted to serialize a null pointer that is not allowed") {
            }
        };
    }

    // === I/O Classes ========================================================
    // I/O 类
    namespace io {
        template<typename R> concept Reader = requires(R r, uint8_t *buf, std::streamsize n)
        {
            { r.read_bytes(buf, n) } -> std::same_as<void>;
            { r.read_byte() } -> std::same_as<uint8_t>;
        };
        template<typename W> concept Writer = requires(W w, const uint8_t *buf, std::streamsize n, uint8_t b)
        {
            { w.write_bytes(buf, n) } -> std::same_as<void>;
            { w.write_byte(b) } -> std::same_as<void>;
        };


        // --- I/O Wrapping std::stream ---------------------------------------
        // 包装 std::stream 的 I/O 类

        struct StreamReader {
            std::istream &is;

            explicit StreamReader(std::istream &s) : is(s) {
            }

            void read_bytes(uint8_t *buf, const std::streamsize n) const {
                is.read(reinterpret_cast<char *>(buf), n);
                if (is.eof())
                    throw errors::EOFError(
                        static_cast<size_t>(n),
                        static_cast<size_t>(is.gcount()),
                        "reading from std::stream"
                    );
                if (is.fail()) throw errors::ReadError("stream read_bytes");
            }

            [[nodiscard]] uint8_t read_byte() const {
                char c;
                if (!is.get(c)) {
                    if (is.eof())
                        throw errors::EOFError(1, 0, "reading single byte from std::istream");
                    throw errors::ReadError("stream read_byte");
                }
                return static_cast<uint8_t>(c);
            }
        };

        struct StreamWriter {
            std::ostream &os;

            explicit StreamWriter(std::ostream &s) : os(s) {
            }

            void write_bytes(const uint8_t *buf, const std::streamsize n) const {
                os.write(reinterpret_cast<const char *>(buf), n);
                if (os.eof()) throw errors::EOFError(static_cast<size_t>(n), 0, "writing to std::ostream");
                if (os.fail()) throw errors::WriteError("stream write_bytes");
            }

            void write_byte(const uint8_t b) const {
                if (!os.put(static_cast<char>(b))) {
                    if (os.eof()) throw errors::EOFError(1, 0, "writing single byte to std::ostream");
                    throw errors::WriteError("stream write_byte");
                }
            }
        };


        // --- I/O Wrapping other Readers/Writers -----------------------------
        // 包装其它 I/O 类的 I/O 类

        template<Reader R>
        struct LimitedReader {
            R &base;
            size_t remaining;

            LimitedReader(R &r, const size_t n) : base(r), remaining(n) {
            }

            void read_bytes(uint8_t *buf, std::streamsize n) {
                if (n > remaining)
                    throw errors::EOFError(static_cast<size_t>(n), remaining, "limited reader");
                base.read_bytes(buf, n);
                remaining -= n;
            }

            [[nodiscard]] uint8_t read_byte() {
                if (remaining == 0)
                    throw errors::EOFError(1, 0, "limited reader byte read");
                --remaining;
                return base.read_byte();
            }

            void skip_remaining() {
                while (remaining) {
                    uint8_t tmp[256];
                    size_t k = std::min(remaining, static_cast<size_t>(256));
                    base.read_bytes(tmp, k);
                    remaining -= k;
                }
            }
        };

        template<Writer W>
        struct LimitedWriter {
            W &base;
            size_t remaining;

            LimitedWriter(W &w, const size_t n) : base(w), remaining(n) {
            }

            void write_bytes(const uint8_t *buf, std::streamsize n) {
                if (static_cast<size_t>(n) > remaining)
                    throw errors::FixedSizeMismatch(remaining, static_cast<size_t>(n), "limited writer");
                base.write_bytes(buf, n);
                remaining -= n;
            }

            void write_byte(uint8_t b) {
                if (remaining == 0)
                    throw errors::FixedSizeMismatch(0, 1, "limited writer byte write");
                --remaining;
                base.write_byte(b);
            }

            void pad_zero() {
                while (remaining) {
                    write_byte(0);
                }
            }
        };


        // --- I/O with Type Erasure ------------------------------------------
        // 类型擦除 I/O 类

        class AnyReader {
        public:
            template<Reader R>
            explicit AnyReader(R &reader)
                : impl_(std::make_unique<ReaderModel<R> >(reader)) {
            }

            // 移动语义
            AnyReader(AnyReader &&) noexcept = default;

            AnyReader &operator=(AnyReader &&) noexcept = default;

            // 禁止拷贝
            AnyReader(const AnyReader &) = delete;

            AnyReader &operator=(const AnyReader &) = delete;

            // 类型擦除接口
            void read_bytes(uint8_t *buf, const std::streamsize n) {
                impl_->read_bytes(buf, n);
            }

            [[nodiscard]] uint8_t read_byte() {
                return impl_->read_byte();
            }

            // 获取原始类型信息（可选，用于调试）
            [[nodiscard]] const std::type_info &reader_type() const noexcept {
                return impl_->reader_type();
            }

        private:
            // 内部概念：类型擦除的 Reader
            struct ReaderConcept {
                virtual ~ReaderConcept() = default;

                virtual void read_bytes(uint8_t *buf, std::streamsize n) = 0;

                [[nodiscard]] virtual uint8_t read_byte() = 0;

                [[nodiscard]] virtual const std::type_info &reader_type() const noexcept = 0;
            };

            // 模板实现
            template<Reader R>
            struct ReaderModel final : ReaderConcept {
                R &reader;

                explicit ReaderModel(R &r) : reader(r) {
                }

                void read_bytes(uint8_t *buf, std::streamsize n) override {
                    reader.read_bytes(buf, n);
                }

                [[nodiscard]] uint8_t read_byte() override {
                    return reader.read_byte();
                }

                [[nodiscard]] const std::type_info &reader_type() const noexcept override {
                    return typeid(R);
                }
            };

            std::unique_ptr<ReaderConcept> impl_;
        };

        class AnyWriter {
        public:
            template<Writer W>
            explicit AnyWriter(W &writer)
                : impl_(std::make_unique<WriterModel<W> >(writer)) {
            }

            AnyWriter(AnyWriter &&) noexcept = default;

            AnyWriter &operator=(AnyWriter &&) noexcept = default;

            AnyWriter(const AnyWriter &) = delete;

            AnyWriter &operator=(const AnyWriter &) = delete;

            void write_bytes(const uint8_t *buf, const std::streamsize n) {
                impl_->write_bytes(buf, n);
            }

            void write_byte(uint8_t b) {
                impl_->write_byte(b);
            }

            [[nodiscard]] const std::type_info &writer_type() const noexcept {
                return impl_->writer_type();
            }

        private:
            struct WriterConcept {
                virtual ~WriterConcept() = default;

                virtual void write_bytes(const uint8_t *buf, std::streamsize n) = 0;

                virtual void write_byte(uint8_t b) = 0;

                [[nodiscard]] virtual const std::type_info &writer_type() const noexcept = 0;
            };

            template<Writer W>
            struct WriterModel final : WriterConcept {
                W &writer;

                explicit WriterModel(W &w) : writer(w) {
                }

                void write_bytes(const uint8_t *buf, std::streamsize n) override {
                    writer.write_bytes(buf, n);
                }

                void write_byte(uint8_t b) override {
                    writer.write_byte(b);
                }

                [[nodiscard]] const std::type_info &writer_type() const noexcept override {
                    return typeid(W);
                }
            };

            std::unique_ptr<WriterConcept> impl_;
        };
    }

    // === Wrappers ===========================================================
    // 包装类
    namespace types {
        /*
            Protocol override wrapper.
            Forces a specific protocol for a value.
        */
        template<typename T, typename Proto>
        struct PVal {
            T value;

            operator T &() { return value; }
            operator const T &() const { return value; }

            operator T *() { return &value; }
            operator const T *() const { return &value; }

            T *operator->() { return &value; }
            const T *operator->() const { return &value; }

            T &operator*() { return value; }
            const T &operator*() const { return value; }
        };

        /*
            Runtime-polymorphic serialization interface.
            Derived classes implement protocol-based read/write.
        */
        struct CVal {
            virtual void write(io::AnyWriter &w) const =0;

            virtual void read(io::AnyReader &r) =0;

            virtual ~CVal() = default;
        };

        using bytes = std::vector<uint8_t>;

        template<typename T>
        concept trivially_serializable =
                std::is_trivially_copyable_v<T> &&
                !std::is_pointer_v<T> &&
                !std::is_member_pointer_v<T>;
    }


    /* ========================================================================
     * Forward Declarations
     * 前向声明
     * ======================================================================== */

    // === Protocol Tags ======================================================
    // 协议标签
    namespace proto {
        struct WrapperProto {
        };

        struct Default {
        };

        template<size_t N = 0>
        struct Fixed {
        };

        struct Varint {
        };

        template<typename Version=Default>
        struct Schema {
        };

        struct Trivial {
        };

        struct CVal {
        };

        /*
            Limit total bytes used for a value.
            Encoding:
            [length][payload]
        */
        template<typename LenProto, typename InnerProto>
        struct Limited : WrapperProto {
            using is_wrapper_proto = std::true_type;
        };

        /*
            Force fixed serialized length.

            Encoding:
            [length][payload padded with zero]
        */
        template<typename LenProto, typename InnerProto>
        struct Forced : WrapperProto {
        };

        template<typename T>
        struct DefaultProtocol {
            using type = Default;
        };

        template<typename T>
        using DefaultProtocol_t = DefaultProtocol<T>::type;


        // --- Option Modifier ------------------------------------------------
        // 配置修改器
        namespace optmod {
            // new_value = old_value * Multiplier / Divisor + Addend
            // Use Multiplier = 0 to set constant
            template<size_t Multiplier = 1, size_t Divisor = 1, size_t Addend = 0>
            struct ValueModifier {
                static_assert(Divisor != 0, "can't divide by zero");

                static constexpr size_t apply(const size_t old_value) {
                    if constexpr (Multiplier == 0) {
                        return Addend;
                    } else {
                        if (old_value != 0 && Multiplier > SIZE_MAX / old_value) return old_value;
                        const size_t temp = old_value * Multiplier / Divisor;
                        if (temp > SIZE_MAX - Addend) return SIZE_MAX;
                        return temp + Addend;
                    }
                }
            };

            using Unlimited = ValueModifier<0, 1, SIZE_MAX>;

            template<typename Policy>
            struct MaxDepth {
                using policy = Policy;
            };

            template<typename Policy>
            struct MaxContainerSize {
                using policy = Policy;
            };

            template<typename Policy>
            struct MaxStringSize {
                using policy = Policy;
            };

            template<ErrorPolicy Value>
            struct ErrorPolicyMode {
                static constexpr ErrorPolicy value = Value;
            };

            using StrictErrors = ErrorPolicyMode<ErrorPolicy::STRICT>;
            using MediumErrors = ErrorPolicyMode<ErrorPolicy::MEDIUM>;
            using IgnoreErrors = ErrorPolicyMode<ErrorPolicy::IGNORE>;

            template<typename InnerProto, typename... Modifiers>
            struct WithOptions {
                using Inner = InnerProto;
                using modifiers = std::tuple<Modifiers...>;
            };
        }
    }

    // === Serializer =========================================================
    // 序列化器
    namespace serialize {
        template<typename T, typename Proto>
        struct Serializer;

        template<typename T>
        using DefaultSerializer = Serializer<T, proto::DefaultProtocol_t<T> >;
    }

    // === Schema =============================================================
    // 模式串
    namespace schema {
        template<typename Class, typename FieldType, typename Proto = proto::Default>
        struct Field {
            using protocol = Proto;
            using field_type = FieldType;
            using class_type = Class;

            const char *name;
            FieldType Class::*ptr;
        };

        template<typename T, typename Version = proto::Default>
        struct Schema;
    }


    /* ========================================================================
     * Tools
     * 工具
     * ======================================================================== */

    // === Detail =============================================================
    // 实现细节与辅助器
    namespace detail {
        // --- Depth Guard ----------------------------------------------------
        // 递归深度限制器
        inline thread_local size_t current_depth = 0;

        struct DepthGuard {
            DepthGuard() {
                current_depth++;
                if (current_depth > options::current().max_depth)
                    throw errors::DepthExceeded(current_depth, options::current().max_depth.value());
            }

            ~DepthGuard() {
                current_depth--;
            }

            DepthGuard(const DepthGuard &) = delete;

            DepthGuard &operator=(const DepthGuard &) = delete;
        };


        // --- Varint Implementation ------------------------------------------
        // 变长整数实现
        template<std::unsigned_integral UInt>
        void write_varint(io::Writer auto &w, UInt v) {
            while (v >= 0x80) {
                w.write_byte((v & 0x7F) | 0x80);
                v >>= 7;
            }

            w.write_byte(v);
        }

        template<std::unsigned_integral UInt>
        [[nodiscard]] UInt read_varint(io::Reader auto &r) {
            UInt result = 0;
            int shift = 0;

            while (true) {
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (shift >= static_cast<int>(sizeof(UInt) * 8))
                        throw errors::VarintOverflow(static_cast<int>(sizeof(UInt) * 8), typeid(UInt).name());

                const uint8_t b = r.read_byte();
                result |= UInt(b & 0x7F) << shift;

                if (!(b & 0x80))
                    return result;

                shift += 7;
            }
        }

        template<typename S> requires std::is_signed_v<S>
        [[nodiscard]] std::make_unsigned_t<S> zigzag_encode(S v) {
            using U = std::make_unsigned_t<S>;
            return (static_cast<U>(v) << 1) ^ static_cast<U>(v >> (sizeof(S) * 8 - 1));
        }

        template<typename U> requires std::is_unsigned_v<U>
        [[nodiscard]] std::make_signed_t<U> zigzag_decode(U v) {
            using S = std::make_signed_t<U>;
            const U mask = ~(v & 1) + 1;
            return static_cast<S>((v >> 1) ^ mask);
        }


        // --- Endian Conversion ----------------------------------------------
        // 端序转换
        [[nodiscard]] inline uint16_t byteswap_impl(const uint16_t x) {
#if defined(__GNUC__) || defined(__clang__)
            return __builtin_bswap16(x);
#elif defined(_MSC_VER)
            return _byteswap_ushort(x);
#else
            return (x >> 8) | (x << 8);
#endif
        }

        [[nodiscard]] inline uint32_t byteswap_impl(const uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
            return __builtin_bswap32(x);
#elif defined(_MSC_VER)
            return _byteswap_ulong(x);
#else
            return ((x & 0x000000FFu) << 24) |
                   ((x & 0x0000FF00u) << 8) |
                   ((x & 0x00FF0000u) >> 8) |
                   ((x & 0xFF000000u) >> 24);
#endif
        }

        [[nodiscard]] inline uint64_t byteswap_impl(const uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
            return __builtin_bswap64(x);
#elif defined(_MSC_VER)
            return _byteswap_uint64(x);
#else
            return ((x & 0x00000000000000FFULL) << 56) |
                   ((x & 0x000000000000FF00ULL) << 40) |
                   ((x & 0x0000000000FF0000ULL) << 24) |
                   ((x & 0x00000000FF000000ULL) << 8) |
                   ((x & 0x000000FF00000000ULL) >> 8) |
                   ((x & 0x0000FF0000000000ULL) >> 24) |
                   ((x & 0x00FF000000000000ULL) >> 40) |
                   ((x & 0xFF00000000000000ULL) >> 56);
#endif
        }

        template<typename T> requires std::is_trivially_copyable_v<T>
        [[nodiscard]] T byteswap(T v) {
            if constexpr (sizeof(T) == 1) {
                return v;
            } else if constexpr (sizeof(T) == 2) {
                return byteswap_impl(std::bit_cast<uint16_t>(v));
            } else if constexpr (sizeof(T) == 4) {
                return byteswap_impl(std::bit_cast<uint32_t>(v));
            } else if constexpr (sizeof(T) == 8) {
                return byteswap_impl(std::bit_cast<uint64_t>(v));
            } else {
                T result{};
                auto *dst = reinterpret_cast<unsigned char *>(&result);
                auto *src = reinterpret_cast<const unsigned char *>(&v);
                for (size_t i = 0; i < sizeof(T); ++i)
                    dst[i] = src[sizeof(T) - 1 - i];
                return result;
            }
        }

        template<typename T>
        [[nodiscard]] T adapt_endian(T v) {
            if constexpr (options::endian == std::endian::native)
                return v;
            else
                return byteswap(v);
        }


        // --- Options Modifier -----------------------------------------------
        template<typename Policy>
        constexpr void apply_modifier(options &opt, proto::optmod::MaxDepth<Policy> *) {
            if (opt.max_depth.has_value()) {
                opt.max_depth = Policy::apply(opt.max_depth.value());
            }
        }

        template<typename Policy>
        constexpr void apply_modifier(options &opt, proto::optmod::MaxStringSize<Policy> *) {
            if (opt.max_string_size.has_value()) {
                opt.max_string_size = Policy::apply(opt.max_string_size.value());
            }
        }

        template<typename Policy>
        constexpr void apply_modifier(options &opt, proto::optmod::MaxContainerSize<Policy> *) {
            if (opt.max_container_size.has_value()) {
                opt.max_container_size = Policy::apply(opt.max_container_size.value());
            }
        }

        template<ErrorPolicy Value>
        constexpr void apply_modifier(options &opt, proto::optmod::ErrorPolicyMode<Value>) {
            opt.error_policy = Value;
        }

        template<typename... Modifiers>
        constexpr void apply_modifiers(options &opt) {
            (apply_modifier(opt, static_cast<Modifiers *>(nullptr)), ...);
        }
    }


    /* ========================================================================
     * Specializations
     * 特化实现
     * ======================================================================== */

    // === Default Protocol ===================================================
    // 默认协议指定
    namespace proto {
        // --- Value Types ----------------------------------------------------
        // 数据类型

        template<>
        struct DefaultProtocol<bool> {
            using type = Fixed<>;
        };

        template<std::unsigned_integral T> requires types::trivially_serializable<T>
        struct DefaultProtocol<T> {
            using type = Fixed<>;
        };

        template<std::signed_integral T> requires types::trivially_serializable<T>
        struct DefaultProtocol<T> {
            using type = Fixed<>;
        };

        template<std::floating_point T> requires types::trivially_serializable<T>
        struct DefaultProtocol<T> {
            using type = Fixed<>;
        };


        // --- Container Types ------------------------------------------------
        // 容器类型

        template<>
        struct DefaultProtocol<std::string> {
            using type = Varint;
        };

        template<>
        struct DefaultProtocol<types::bytes> {
            using type = Varint;
        };

        template<typename T>
        struct DefaultProtocol<std::vector<T> > {
            using type = Varint;
        };

        template<size_t N>
        struct DefaultProtocol<std::bitset<N> > {
            using type = Fixed<>;
        };

        template<typename K, typename V>
        struct DefaultProtocol<std::map<K, V> > {
            using type = Varint;
        };

        template<typename K, typename V>
        struct DefaultProtocol<std::unordered_map<K, V> > {
            using type = Varint;
        };

        template<typename T>
        struct DefaultProtocol<std::set<T> > {
            using type = Varint;
        };

        template<typename T>
        struct DefaultProtocol<std::unordered_set<T> > {
            using type = Varint;
        };

        template<typename T, size_t N>
        struct DefaultProtocol<std::array<T, N> > {
            using type = Fixed<>;
        };


        // --- Structured Types -----------------------------------------------
        // 结构化类型

        template<typename T1, typename T2>
        struct DefaultProtocol<std::pair<T1, T2> > {
            using type = Fixed<>;
        };

        template<typename... Ts>
        struct DefaultProtocol<std::tuple<Ts...> > {
            using type = Fixed<>;
        };

        // DefaultProtocol for Schemas need to be registered manually

        // --- Trivially-Serializable Types -----------------------------------
        // 平凡可复制类型

        template<types::trivially_serializable T>
        struct DefaultProtocol<T> {
            using type = Trivial;
        };

        // --- Variable Types -------------------------------------------------
        // 可变类型

        template<typename T>
        struct DefaultProtocol<std::optional<T> > {
            using type = Varint;
        };

        template<typename... Ts>
        struct DefaultProtocol<std::variant<Ts...> > {
            using type = Varint;
        };


        // --- Pointers -------------------------------------------------------
        // 指针

        template<typename T>
        struct DefaultProtocol<T *> {
            using type = Varint;
        };

        template<typename T>
        struct DefaultProtocol<std::unique_ptr<T> > {
            using type = Varint;
        };

        // --- Types with Specified Protocol ----------------------------------
        // 指定协议的类型

        template<typename T, typename Protocol>
        struct DefaultProtocol<types::PVal<T, Protocol> > {
            using type = Default;
        };

        template<typename T>
            requires std::is_base_of_v<types::CVal, T>
        struct DefaultProtocol<T> {
            using type = CVal;
        };
    }

    // === Serializers ========================================================
    // 序列化器特化实现
    namespace serialize {
        // ~~~ Serializers for Types ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // 为特定类型设计的序列化器

        // --- Serializers for Value Types  -----------------------------------
        // 数据类型的序列化器

        // bool
        // [0/1 Bool]
        template<>
        struct Serializer<bool, proto::Fixed<> > {
            static void write(io::Writer auto &w, const bool &v) {
                w.write_byte(v);
            }

            static void read(io::Reader auto &r, bool &out) {
                if (options::current().error_policy <= ErrorPolicy::STRICT) {
                    const uint8_t b = r.read_byte();
                    if (b > 1) throw errors::InvalidBool(b);
                    out = b;
                } else {
                    out = r.read_byte();
                }
            }
        };

        // Integral
        template<std::integral T>
        struct Serializer<T, proto::Fixed<> > {
            static void write(io::Writer auto &w, const T &v) {
                T x = detail::adapt_endian(v);
                w.write_bytes(reinterpret_cast<uint8_t *>(&x), sizeof(T));
            }

            static void read(io::Reader auto &r, T &out) {
                r.read_bytes(reinterpret_cast<uint8_t *>(&out), sizeof(T));
                out = detail::adapt_endian(out);
            }
        };

        // [LEB128 Varint]
        // Not affected by endian
        template<std::unsigned_integral T>
        struct Serializer<T, proto::Varint> {
            static void write(io::Writer auto &w, const T &v) {
                detail::write_varint(w, v);
            }

            static void read(io::Reader auto &r, T &out) {
                out = detail::read_varint<T>(r);
            }
        };

        // [ZigZag+LEB128 Varint]
        // Not affected by endian
        template<std::signed_integral T>
        struct Serializer<T, proto::Varint> {
            static void write(io::Writer auto &w, const T &v) {
                detail::write_varint(w, detail::zigzag_encode(v));
            }

            static void read(io::Reader auto &r, T &out) {
                out = detail::zigzag_decode(detail::read_varint<std::make_unsigned_t<T> >(r));
            }
        };

        // Floating
        template<std::floating_point T> requires std::numeric_limits<T>::is_iec559
        struct Serializer<T, proto::Fixed<> > {
            static void write(io::Writer auto &w, const T &v) {
                using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
                UInt x = std::bit_cast<UInt>(v);
                x = detail::adapt_endian(x);
                w.write_bytes(reinterpret_cast<const uint8_t *>(&x), sizeof(UInt));
            }

            static void read(io::Reader auto &r, T &out) {
                using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
                UInt x;
                r.read_bytes(reinterpret_cast<uint8_t *>(&x), sizeof(UInt));
                x = detail::adapt_endian(x);
                out = std::bit_cast<T>(x);
            }
        };


        // --- Serializers for Container Types --------------------------------
        // 容器类型的序列化器

        // std::string
        // [Varint length][String]
        template<>
        struct Serializer<std::string, proto::Varint> {
            static void write(io::Writer auto &w, const std::string &v) {
                detail::write_varint(w, v.size());
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), v.size());
            }

            static void read(io::Reader auto &r, std::string &out) {
                size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (size > options::current().max_string_size)
                        throw errors::StringTooLarge(size, options::current().max_string_size.value());

                out.resize(size);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), size);
            }
        };

        // [String]
        template<size_t N>
        struct Serializer<std::string, proto::Fixed<N> > {
            static void write(io::Writer auto &w, const std::string &v) {
                if (v.size() != N)
                    throw errors::FixedSizeMismatch(N, v.size(), "std::string (fixed length)");
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), N);
            }

            static void read(io::Reader auto &r, std::string &out) {
                out.resize(N);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), N);
            }
        };

        // types::bytes (std::vector<uint8_t>)
        // [Varint length][Bytearray]
        template<>
        struct Serializer<types::bytes, proto::Varint> {
            static void write(io::Writer auto &w, const types::bytes &v) {
                detail::write_varint(w, v.size());
                w.write_bytes(v.data(), v.size());
            }

            static void read(io::Reader auto &r, types::bytes &out) {
                size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (size > options::current().max_string_size)
                        throw errors::StringTooLarge(size, options::current().max_string_size.value());
                out.resize(size);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), size);
            }
        };

        // [Bytearray]
        template<size_t N>
        struct Serializer<types::bytes, proto::Fixed<N> > {
            static void write(io::Writer auto &w, const types::bytes &v) {
                if (v.size() != N)
                    throw errors::FixedSizeMismatch(N, v.size(), "bytes (fixed length)");
                w.write_bytes(v.data(), N);
            }

            static void read(io::Reader auto &r, types::bytes &out) {
                out.resize(N);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), N);
            }
        };

        // std::vector
        // [Varint length][Value 0][Value 1]...
        template<typename T>
        struct Serializer<std::vector<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::vector<T> &v) {
                detail::DepthGuard g;

                detail::write_varint(w, v.size());

                for (const T &item: v) {
                    DefaultSerializer<T>::write(w, item);
                }
            }

            static void read(io::Reader auto &r, std::vector<T> &out) {
                detail::DepthGuard g;

                size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (size > options::current().max_container_size)
                        throw errors::ContainerTooLarge(size, options::current().max_container_size.value(),
                                                        "std::vector");
                out.resize(size);
                for (size_t i = 0; i < size; i++) {
                    DefaultSerializer<T>::read(r, out[i]);
                }
            }
        };

        // [Value 0][Value 1]...
        template<typename T, size_t N>
        struct Serializer<std::vector<T>, proto::Fixed<N> > {
            static void write(io::Writer auto &w, const std::vector<T> &v) {
                detail::DepthGuard g;

                if (v.size() != N) throw errors::FixedSizeMismatch(N, v.size(), "std::vector (fixed length)");
                for (const T &item: v) {
                    DefaultSerializer<T>::write(w, item);
                }
            }

            static void read(io::Reader auto &r, std::vector<T> &out) {
                detail::DepthGuard g;

                out.resize(N);
                for (size_t i = 0; i < N; i++) {
                    DefaultSerializer<T>::read(r, out[i]);
                }
            }
        };

        // std::vector<bool>
        // Not bit-compressed
        // Use std::bitset if you want to enable bit-compression
        template<>
        struct Serializer<std::vector<bool>, proto::Varint> {
            static void write(io::Writer auto &w, const std::vector<bool> &v) {
                detail::write_varint(w, v.size());

                for (const bool b: v) {
                    w.write_byte(b);
                }
            }

            static void read(io::Reader auto &r, std::vector<bool> &out) {
                const size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (size > options::current().max_container_size)
                        throw errors::ContainerTooLarge(size, options::current().max_container_size.value(),
                                                        "std::vector<bool>");

                out.resize(size);
                for (size_t i = 0; i < size; ++i) {
                    const uint8_t b = r.read_byte();

                    if (options::current().error_policy == ErrorPolicy::STRICT)
                        if (b > 1) throw errors::InvalidBool(b);

                    out[i] = b != 0;
                }
            }
        };

        template<size_t N>
        struct Serializer<std::vector<bool>, proto::Fixed<N> > {
            static void write(io::Writer auto &w, const std::vector<bool> &v) {
                if (v.size() != N) throw errors::FixedSizeMismatch(N, v.size(), "std::vector<bool> (fixed)");

                for (const bool b: v) {
                    w.write_byte(b);
                }
            }

            static void read(io::Reader auto &r, std::vector<bool> &out) {
                out.resize(N);

                for (size_t i = 0; i < N; ++i) {
                    const uint8_t b = r.read_byte();

                    if (options::current().error_policy == ErrorPolicy::STRICT) {
                        if (b > 1) throw errors::InvalidBool(b);
                    }

                    out[i] = (b != 0);
                }
            }
        };

        // std::bitset
        template<size_t N>
        struct Serializer<std::bitset<N>, proto::Fixed<> > {
            static_assert(N > 0, "bitset size must be known at compile time");

            static constexpr size_t byte_count = (N + 7) / 8;

            static void write(io::Writer auto &w, const std::bitset<N> &v) {
                for (size_t i = 0; i < byte_count; ++i) {
                    uint8_t byte = 0;
                    for (size_t bit = 0; bit < 8 && (i * 8 + bit) < N; ++bit) {
                        if (v[i * 8 + bit]) {
                            byte |= (1 << bit);
                        }
                    }
                    w.write_byte(byte);
                }
            }

            static void read(io::Reader auto &r, std::bitset<N> &out) {
                out.reset();
                for (size_t i = 0; i < byte_count; ++i) {
                    uint8_t byte = r.read_byte();
                    for (size_t bit = 0; bit < 8 && (i * 8 + bit) < N; ++bit) {
                        if (byte & (1 << bit)) {
                            out.set(i * 8 + bit);
                        }
                    }
                }
            }
        };

        // std::map
        // [Varint length][Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V>
        struct Serializer<std::map<K, V>, proto::Varint> {
            static void write(io::Writer auto &w, const std::map<K, V> &v) {
                detail::DepthGuard g;

                detail::write_varint(w, v.size());
                for (const auto &item: v) {
                    DefaultSerializer<K>::write(w, item.first);
                    DefaultSerializer<V>::write(w, item.second);
                }
            }

            static void read(io::Reader auto &r, std::map<K, V> &out) {
                detail::DepthGuard g;

                const size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (size > options::current().max_container_size)
                        throw errors::ContainerTooLarge(size, options::current().max_container_size.value(),
                                                        "std::map");

                out.clear();
                for (size_t i = 0; i < size; i++) {
                    K key;
                    DefaultSerializer<K>::read(r, key);
                    V value;
                    DefaultSerializer<V>::read(r, value);
                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // [Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V, size_t N>
        struct Serializer<std::map<K, V>, proto::Fixed<N> > {
            static void write(io::Writer auto &w, const std::map<K, V> &v) {
                detail::DepthGuard g;

                if (v.size() != N) throw errors::FixedSizeMismatch(N, v.size(), "std::map (fixed)");
                for (const auto &item: v) {
                    DefaultSerializer<K>::write(w, item.first);
                    DefaultSerializer<V>::write(w, item.second);
                }
            }

            static void read(io::Reader auto &r, std::map<K, V> &out) {
                detail::DepthGuard g;

                out.clear();
                for (size_t i = 0; i < N; i++) {
                    K key;
                    DefaultSerializer<K>::read(r, key);
                    V value;
                    DefaultSerializer<V>::read(r, value);
                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // std::unordered_map
        // [Varint length][Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V>
        struct Serializer<std::unordered_map<K, V>, proto::Varint> {
            static void write(io::Writer auto &w, const std::unordered_map<K, V> &v) {
                detail::DepthGuard g;

                detail::write_varint(w, v.size());
                for (const auto &item: v) {
                    DefaultSerializer<K>::write(w, item.first);
                    DefaultSerializer<V>::write(w, item.second);
                }
            }

            static void read(io::Reader auto &r, std::map<K, V> &out) {
                detail::DepthGuard g;

                const size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (size > options::current().max_container_size)
                        throw errors::ContainerTooLarge(size, options::current().max_container_size.value(),
                                                        "std::unordered_map");

                out.clear();
                for (size_t i = 0; i < size; i++) {
                    K key;
                    DefaultSerializer<K>::read(r, key);
                    V value;
                    DefaultSerializer<V>::read(r, value);
                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // [Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V, size_t N>
        struct Serializer<std::unordered_map<K, V>, proto::Fixed<N> > {
            static void write(io::Writer auto &w, const std::unordered_map<K, V> &v) {
                detail::DepthGuard g;

                if (v.size() != N) throw errors::FixedSizeMismatch(N, v.size(), "std::unordered_map (fixed)");
                for (const auto &item: v) {
                    DefaultSerializer<K>::write(w, item.first);
                    DefaultSerializer<V>::write(w, item.second);
                }
            }

            static void read(io::Reader auto &r, std::map<K, V> &out) {
                detail::DepthGuard g;

                out.clear();
                for (size_t i = 0; i < N; i++) {
                    K key;
                    DefaultSerializer<K>::read(r, key);
                    V value;
                    DefaultSerializer<V>::read(r, value);
                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // std::set
        template<typename K>
        struct Serializer<std::set<K>, proto::Varint> {
            static void write(io::Writer auto &w, const std::set<K> &v) {
                detail::DepthGuard g;
                detail::write_varint(w, v.size());
                for (const auto &item: v) {
                    DefaultSerializer<K>::write(w, item);
                }
            }

            static void read(io::Reader auto &r, std::set<K> &out) {
                detail::DepthGuard g;
                size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM) {
                    if (size > options::current().max_container_size) {
                        throw errors::ContainerTooLarge(size, options::current().max_container_size.value(),
                                                        "std::set");
                    }
                }
                out.clear();
                for (size_t i = 0; i < size; ++i) {
                    K key;
                    DefaultSerializer<K>::read(r, key);
                    out.emplace(std::move(key));
                }
            }
        };

        // std::unordered_set
        template<typename K>
        struct Serializer<std::unordered_set<K>, proto::Varint> {
            static void write(io::Writer auto &w, const std::unordered_set<K> &v) {
                detail::DepthGuard g;
                detail::write_varint(w, v.size());
                for (const auto &item: v) {
                    DefaultSerializer<K>::write(w, item);
                }
            }

            static void read(io::Reader auto &r, std::unordered_set<K> &out) {
                detail::DepthGuard g;
                size_t size = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM) {
                    if (size > options::current().max_container_size) {
                        throw errors::ContainerTooLarge(size, options::current().max_container_size.value(),
                                                        "std::unordered_set");
                    }
                }
                out.clear();
                for (size_t i = 0; i < size; ++i) {
                    K key;
                    DefaultSerializer<K>::read(r, key);
                    out.emplace(std::move(key));
                }
            }
        };

        // std::array
        template<typename T, size_t N>
        struct Serializer<std::array<T, N>, proto::Fixed<> > {
            static void write(io::Writer auto &w, const std::array<T, N> &v) {
                detail::DepthGuard g;

                for (size_t i = 0; i < N; i++) {
                    DefaultSerializer<T>::write(w, v[i]);
                }
            }

            static void read(io::Reader auto &r, std::array<T, N> &out) {
                detail::DepthGuard g;

                for (size_t i = 0; i < N; i++) {
                    DefaultSerializer<T>::read(r, out[i]);
                }
            }
        };


        // --- Serializers for Structured Types -------------------------------
        // 结构化类型的序列化器

        // pair
        // [Field 1][Field 2]
        template<typename T1, typename T2>
        struct Serializer<std::pair<T1, T2>, proto::Fixed<> > {
            static void write(io::Writer auto &w, const std::pair<T1, T2> &v) {
                DefaultSerializer<T1>::write(w, v.first);
                DefaultSerializer<T2>::write(w, v.second);
            }

            static void read(io::Reader auto &r, std::pair<T1, T2> &out) {
                DefaultSerializer<T1>::read(r, out.first);
                DefaultSerializer<T2>::read(r, out.second);
            }
        };

        // tuple
        // [Field 1][Field 2]...
        template<class... Ts>
        struct Serializer<std::tuple<Ts...>, proto::Fixed<> > {
            static void write(io::Writer auto &w, const std::tuple<Ts...> &v) {
                detail::DepthGuard g;
                write_impl(w, v, std::index_sequence_for<Ts...>{});
            }

            static void read(io::Reader auto &r, std::tuple<Ts...> &out) {
                detail::DepthGuard g;
                read_impl(r, out, std::index_sequence_for<Ts...>{});
            }

        private:
            template<size_t... I>
            static void write_impl(io::Writer auto &w,
                                   const std::tuple<Ts...> &v,
                                   std::index_sequence<I...>) {
                (DefaultSerializer<std::tuple_element_t<I, std::tuple<Ts...> > >
                    ::write(w, std::get<I>(v)), ...);
            }

            template<size_t... I>
            static void read_impl(io::Reader auto &r,
                                  std::tuple<Ts...> &out,
                                  std::index_sequence<I...>) {
                (DefaultSerializer<std::tuple_element_t<I, std::tuple<Ts...> > >
                    ::read(r, std::get<I>(out)), ...);
            }
        };

        // schema
        // [Field 1][Field 2]...
        // The order of fields is the same as being registered
        template<typename T, typename Version>
        struct Serializer<T, proto::Schema<Version> > {
            static void write(io::Writer auto &w, const T &v) {
                [[maybe_unused]] constexpr auto &fields = schema::Schema<T, Version>::fields;
                constexpr size_t field_count = std::tuple_size_v<std::decay_t<decltype(fields)> >;
                write_impl(w, v, std::make_index_sequence<field_count>{});
            }

            static void read(io::Reader auto &r, T &out) {
                [[maybe_unused]] constexpr auto &fields = schema::Schema<T, Version>::fields;
                constexpr size_t field_count = std::tuple_size_v<std::decay_t<decltype(fields)> >;
                read_impl(r, out, std::make_index_sequence<field_count>{});
            }

        private:
            template<size_t... I>
            static void write_impl(io::Writer auto &w, const T &v, std::index_sequence<I...>) {
                constexpr auto &fields = schema::Schema<T, Version>::fields;
                (Serializer<
                    typename std::tuple_element_t<I, std::decay_t<decltype(fields)> >::field_type,
                    typename std::tuple_element_t<I, std::decay_t<decltype(fields)> >::protocol
                >::write(w, v.*(std::get<I>(fields).ptr)), ...);
            }

            template<size_t... I>
            static void read_impl(io::Reader auto &r, T &out, std::index_sequence<I...>) {
                constexpr auto &fields = schema::Schema<T, Version>::fields;
                (Serializer<
                    typename std::tuple_element_t<I, std::decay_t<decltype(fields)> >::field_type,
                    typename std::tuple_element_t<I, std::decay_t<decltype(fields)> >::protocol
                >::read(r, out.*(std::get<I>(fields).ptr)), ...);
            }
        };


        // --- Serializers for Variable Types ---------------------------------
        // 可变类型的序列化器

        // std::optional
        // [0/1 Bool](T if has value)
        template<typename T>
        struct Serializer<std::optional<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::optional<T> &v) {
                w.write_byte(v.has_value());
                if (v.has_value()) {
                    DefaultSerializer<T>::write(w, v.value());
                }
            }

            static void read(io::Reader auto &r, std::optional<T> &out) {
                const uint8_t has = r.read_byte();
                if (options::current().error_policy <= ErrorPolicy::STRICT) {
                    if (has > 1) throw errors::InvalidBool(has);
                }
                if (has) {
                    T val;
                    DefaultSerializer<T>::read(r, val);
                    out = std::move(val);
                } else {
                    out = std::nullopt;
                }
            }
        };

        // std::variant
        // [Varint length][Selected T]
        template<typename... Ts>
        struct Serializer<std::variant<Ts...>, proto::Varint> {
            static void write(io::Writer auto &w, const std::variant<Ts...> &v) {
                if (v.valueless_by_exception())
                    throw errors::InvalidVariantIndex(0, sizeof...(Ts));
                write_impl(w, v);
            }

            static void read(io::Reader auto &r, std::variant<Ts...> &out) {
                size_t idx = detail::read_varint<size_t>(r);
                if (idx >= sizeof...(Ts))
                    throw errors::InvalidVariantIndex(idx, sizeof...(Ts));
                read_impl(r, idx, out);
            }

        private:
            template<size_t I = 0>
            static void write_impl(io::Writer auto &w, const std::variant<Ts...> &v) {
                if (v.index() == I) {
                    detail::write_varint(w, I);
                    DefaultSerializer<std::variant_alternative_t<I, std::variant<Ts...> > >::write(w, std::get<I>(v));
                } else {
                    if constexpr (I + 1 < sizeof...(Ts)) write_impl<I + 1>(w, v);
                }
            }

            template<size_t I = 0>
            static void read_impl(io::Reader auto &r, size_t idx, std::variant<Ts...> &out) {
                if (idx == I) {
                    std::variant_alternative_t<I, std::variant<Ts...> > val;
                    DefaultSerializer<std::variant_alternative_t<I, std::variant<Ts...> > >::read(r, val);
                    out.template emplace<I>(std::move(val));
                } else {
                    if constexpr (I + 1 < sizeof...(Ts)) read_impl<I + 1>(r, idx, out);
                }
            }
        };


        // --- Serializers for Types with Specified Protocol ------------------
        // 指定协议的类型的序列化器

        // types::PVal
        // [T with prototag ProtocolT]
        template<typename T, typename ProtocolT, typename Protocol> requires (!std::is_base_of_v<proto::WrapperProto,
            Protocol>)
        struct Serializer<types::PVal<T, ProtocolT>, Protocol> {
            static void write(io::Writer auto &w, const types::PVal<T, ProtocolT> &v) {
                Serializer<T, ProtocolT>::write(w, v.value);
            }

            static void read(io::Reader auto &r, types::PVal<T, ProtocolT> &out) {
                Serializer<T, ProtocolT>::read(r, out.value);
            }
        };

        // types::CVal
        // [Unknown in compile-time, defined in CVal]
        template<typename T> requires std::is_base_of_v<types::CVal, T>
        struct Serializer<T, proto::CVal> {
            static void write(io::AnyWriter &w, const T &v) {
                v.write(w);
            }

            static void write(io::Writer auto &w, const T &v) {
                io::AnyWriter any_w(w);
                v.write(any_w);
            }

            static void read(io::AnyReader &r, T &out) {
                out.read(r);
            }

            static void read(io::Reader auto &r, T &out) {
                io::AnyReader any_r(r);
                out.read(any_r);
            }
        };

        // --- Serializers for Trivially-Serializable Types -------------------
        // 平凡可复制类型的序列化器

        template<types::trivially_serializable T>
        struct Serializer<T, proto::Trivial> {
            static void write(io::Writer auto &w, const T &v) {
                w.write_bytes(reinterpret_cast<const uint8_t *>(&v), sizeof(T));
            }

            static void read(io::Reader auto &r, T &out) {
                r.read_bytes(reinterpret_cast<uint8_t *>(&out), sizeof(T));
            }
        };

        template<types::trivially_serializable T>
        struct Serializer<std::vector<T>, proto::Trivial> {
            static void write(io::Writer auto &w, const std::vector<T> &v) {
                detail::DepthGuard g;
                size_t count = v.size();
                detail::write_varint(w, count);
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), count * sizeof(T));
            }

            static void read(io::Reader auto &r, std::vector<T> &out) {
                detail::DepthGuard g;
                size_t count = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM)
                    if (count > options::current().max_container_size)
                        throw errors::ContainerTooLarge(count, options::current().max_container_size.value(),
                                                        "std::vector<trivial>");
                out.resize(count);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), count * sizeof(T));
            }
        };

        template<>
        struct Serializer<std::vector<bool>, proto::Trivial> {
            static void write(io::Writer auto &w, const std::vector<bool> &v) {
                detail::DepthGuard g;

                const size_t count = v.size();
                detail::write_varint(w, count);

                const size_t byte_count = (count + 7) / 8;
                for (size_t i = 0; i < byte_count; ++i) {
                    uint8_t byte = 0;
                    const size_t base = i * 8;
                    for (size_t bit = 0; bit < 8 && (base + bit) < count; ++bit) {
                        if (v[base + bit]) {
                            byte |= static_cast<uint8_t>(1u << bit);
                        }
                    }
                    w.write_byte(byte);
                }
            }

            static void read(io::Reader auto &r, std::vector<bool> &out) {
                detail::DepthGuard g;

                const size_t count = detail::read_varint<size_t>(r);
                if (options::current().error_policy <= ErrorPolicy::MEDIUM) {
                    if (count > options::current().max_container_size) {
                        throw errors::ContainerTooLarge(count,
                                                        options::current().max_container_size.value(),
                                                        "std::vector<bool> (trivial)");
                    }
                }

                out.resize(count);
                const size_t byte_count = (count + 7) / 8;
                for (size_t i = 0; i < byte_count; ++i) {
                    const uint8_t byte = r.read_byte();
                    const size_t base = i * 8;
                    for (size_t bit = 0; bit < 8 && (base + bit) < count; ++bit) {
                        out[base + bit] = (byte & (1u << bit)) != 0;
                    }
                }
            }
        };

        template<types::trivially_serializable T, size_t N>
        struct Serializer<std::array<T, N>, proto::Trivial> {
            static void write(io::Writer auto &w, const std::array<T, N> &v) {
                detail::DepthGuard g;
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), N * sizeof(T));
            }

            static void read(io::Reader auto &r, std::array<T, N> &out) {
                detail::DepthGuard g;
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), N * sizeof(T));
            }
        };


        // --- Serializers for Pointers ---------------------------------------
        // 指针的序列化器

        template<typename T>
        struct Serializer<T *, proto::Varint> {
            static void write(io::Writer auto &w, T *const &ptr) {
                detail::DepthGuard g;

                if (ptr == nullptr) {
                    w.write_byte(0);
                } else {
                    w.write_byte(1);
                    DefaultSerializer<T>::write(w, *ptr);
                }
            }

            static void read(io::Reader auto &r, T *&out) {
                detail::DepthGuard g;

                const uint8_t has_value = r.read_byte();
                if (options::current().error_policy <= ErrorPolicy::STRICT) {
                    if (has_value > 1) throw errors::InvalidBool(has_value);
                }

                if (has_value) {
                    auto *obj = new T();
                    try {
                        DefaultSerializer<T>::read(r, *obj);
                        out = obj;
                    } catch (...) {
                        delete obj;
                        throw;
                    }
                } else {
                    out = nullptr;
                }
            }
        };

        template<typename T>
        struct Serializer<std::unique_ptr<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::unique_ptr<T> &ptr) {
                detail::DepthGuard g;

                if (!ptr) {
                    w.write_byte(0);
                } else {
                    w.write_byte(1);
                    DefaultSerializer<T>::write(w, *ptr);
                }
            }

            static void read(io::Reader auto &r, std::unique_ptr<T> &out) {
                detail::DepthGuard g;

                const uint8_t has_value = r.read_byte();
                if (options::current().error_policy <= ErrorPolicy::STRICT) {
                    if (has_value > 1) throw errors::InvalidBool(has_value);
                }

                if (has_value) {
                    auto obj = std::make_unique<T>();
                    DefaultSerializer<T>::read(r, *obj);
                    out = std::move(obj);
                } else {
                    out.reset();
                }
            }
        };


        // ~~~ Serializers for Protocols ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // 为特定协议设计的序列化器

        // --- Default Fallback -----------------------------------------------
        // 默认协议映射
        template<typename T> requires (!std::is_same_v<proto::DefaultProtocol_t<T>, proto::Default>)
        struct Serializer<T, proto::Default> {
            static void write(io::Writer auto &w, const T &v) {
                DefaultSerializer<T>::write(w, v);
            }

            static void read(io::Reader auto &r, T &out) {
                DefaultSerializer<T>::read(r, out);
            }
        };


        // --- Serializers for Length-Limited Protocols -----------------------
        // 限定长度的协议的序列化器

        // proto::Limited
        template<typename T, typename InnerProto>
        struct Serializer<T, proto::Limited<proto::Varint, InnerProto> > {
            static void write(io::Writer auto &w, const T &v) {
                std::ostringstream oss;
                io::StreamWriter tmp_writer(oss);
                Serializer<T, InnerProto>::write(tmp_writer, v);

                const std::string data = oss.str();
                size_t len = data.size();

                detail::write_varint(w, len);
                w.write_bytes(reinterpret_cast<const uint8_t *>(data.data()), len);
            }

            static void read(io::Reader auto &r, T &out) {
                size_t len = detail::read_varint<size_t>(r);
                io::LimitedReader limited(r, len);
                Serializer<T, InnerProto>::read(limited, out);
            }
        };

        template<typename T, size_t N, typename InnerProto>
        struct Serializer<T, proto::Limited<proto::Fixed<N>, InnerProto> > {
            static void write(io::Writer auto &w, const T &v) {
                io::LimitedWriter limited_w(w, N);
                Serializer<T, InnerProto>::write(limited_w, v);
            }

            static void read(io::Reader auto &r, T &out) {
                io::LimitedReader limited_r(r, N);
                Serializer<T, InnerProto>::read(limited_r, out);
            }
        };

        // proto::Forced
        template<typename T, typename InnerProto>
        struct Serializer<T, proto::Forced<proto::Varint, InnerProto> > {
            static void write(io::Writer auto &w, const T &v) {
                std::ostringstream oss;
                io::StreamWriter tmp_writer(oss);
                Serializer<T, InnerProto>::write(tmp_writer, v);

                const std::string data = oss.str();
                size_t len = data.size();

                detail::write_varint(w, len);
                w.write_bytes(reinterpret_cast<const uint8_t *>(data.data()), len);
            }

            static void read(io::Reader auto &r, T &out) {
                size_t len = detail::read_varint<size_t>(r);
                io::LimitedReader limited(r, len);
                Serializer<T, InnerProto>::read(limited, out);
                limited.skip_remaining();
            }
        };

        template<typename T, size_t N, typename InnerProto>
        struct Serializer<T, proto::Forced<proto::Fixed<N>, InnerProto> > {
            static void write(io::Writer auto &w, const T &v) {
                io::LimitedWriter limited_w(w, N);
                Serializer<T, InnerProto>::write(limited_w, v);
                limited_w.pad_zero();
            }

            static void read(io::Reader auto &r, T &out) {
                io::LimitedReader limited_r(r, N);
                Serializer<T, InnerProto>::read(limited_r, out);
                limited_r.skip_remaining();
            }
        };


        // --- Serializer for Option-Modifying Protocol -----------------------
        // 配置修改器的序列化器
        template<typename T, typename InnerProto, typename... Modifiers>
        struct Serializer<T, proto::optmod::WithOptions<InnerProto, Modifiers...> > {
            static void write(io::Writer auto &w, const T &v) {
                options new_options = options::current();
                detail::apply_modifiers<Modifiers...>(new_options);
                OptionsGuard g(new_options);

                Serializer<T, InnerProto>::write(w, v);
            }

            static void read(io::Reader auto &r, T &out) {
                options new_options = options::current();
                detail::apply_modifiers<Modifiers...>(new_options);
                OptionsGuard g(new_options);

                Serializer<T, InnerProto>::read(r, out);
            }
        };
    }


    /* ========================================================================
     * Public API
     * 开放使用的 API
     * ======================================================================== */

    template<typename Proto, typename T>
    void write(io::Writer auto &w, const T &v) {
        serialize::Serializer<T, Proto>::write(w, v);
    }

    template<typename T>
    void write(io::Writer auto &w, const T &v) {
        write<proto::DefaultProtocol_t<T> >(w, v);
    }

    template<typename Proto, typename T>
    void read(io::Reader auto &r, T &out) {
        serialize::Serializer<T, Proto>::read(r, out);
    }

    template<typename T>
    void read(io::Reader auto &r, T &out) {
        read<proto::DefaultProtocol_t<T> >(r, out);
    }

    template<typename Proto, typename T>
    [[nodiscard]] T read(io::Reader auto &r) {
        T out{};
        serialize::Serializer<T, Proto>::read(r, out);
        return out;
    }

    template<typename T>
    [[nodiscard]] T read(io::Reader auto &r) {
        return read<proto::DefaultProtocol_t<T>, T>(r);
    }

    template<typename T, typename P>
    using PVal = types::PVal<T, P>;
} // namespace bsp


/* ============================================================================
 * Macros
 * 宏
 * ============================================================================ */

// Set the Default Protocol of T
// 设置类型的默认协议
#define BSP_DEFAULT_PROTO(T, P)         \
    namespace bsp {                     \
        namespace proto {               \
            template<>                  \
            struct DefaultProtocol<T> { \
                using type = P;         \
            };                          \
        }                               \
    }
// Register Field with Protocol
// 指定协议的字段注册
#define BSP_FIELD_P(F, P) \
    ::bsp::schema::Field<Type, decltype(Type::F), P>{#F, &Type::F}

// Register Field with Default Protocol
// 默认协议的字段注册
#define BSP_FIELD(F) \
    BSP_FIELD_P(F, ::bsp::proto::Default)

// Register Schema with Version
// 指定版本的模式串注册
#define BSP_SCHEMA_V(T, Version, ...)                                               \
    namespace bsp {                                                                 \
        namespace schema {                                                          \
            template<>                                                              \
            struct Schema<T, Version> {                                             \
                using Type = T;                                                     \
                static constexpr inline auto fields = std::make_tuple(__VA_ARGS__); \
            };                                                                      \
        }                                                                           \
    }

#define BSP_DEFAULT_SCHEMA_V(T, Version, ...) \
    BSP_SCHEMA_V(T, Version, __VA_ARGS__) \
    BSP_DEFAULT_PROTO(T, ::bsp::proto::Schema<Version>)

// Register Schema with Default Version
// 默认版本的模式串注册
#define BSP_SCHEMA(T, ...) \
    BSP_SCHEMA_V(T, ::bsp::proto::Default, __VA_ARGS__)

#define BSP_DEFAULT_SCHEMA(T, ...) \
    BSP_SCHEMA(T, __VA_ARGS__) \
    BSP_DEFAULT_PROTO(T, ::bsp::proto::Schema<>)

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
