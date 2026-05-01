#ifndef BSP_HPP
#define BSP_HPP

#include <vector>
#include <string>
#include <type_traits>
#include <stdexcept>
#include <bit>
#include <memory>
#include <bitset>
#include <cmath>
#include <concepts>
#include <functional>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>
#include <variant>

// Length of Dividers ---------------------------------------------------------

namespace bsp {
    /* ========================================================================
     * Helpers
     * 工具
     * ======================================================================== */
    namespace tools {
        // --- Compile-Time String Helpers ------------------------------------
        // 编译期字符串工具
        template<std::unsigned_integral T>
        constexpr std::string to_string_ct(T n) {
            if (n == 0) return "0";

            constexpr size_t max_digits = static_cast<size_t>(8 * sizeof(T) * 0.30103) + 2;

            std::array<char, max_digits> buf{};
            int pos = max_digits - 1;

            while (n > 0) {
                buf[--pos] = static_cast<char>('0' + (n % 10));
                n /= 10;
            }

            return std::string(buf.data() + pos);
        }

        template<std::signed_integral T>
        constexpr std::string to_string_ct(T n) {
            using UInt = std::make_unsigned_t<T>;

            if (n == 0) return "0";

            if (n < 0) {
                UInt abs_value;
                if (n == std::numeric_limits<T>::min()) {
                    abs_value = static_cast<UInt>(n);
                } else {
                    abs_value = static_cast<UInt>(-n);
                }
                return "-" + to_string_ct(abs_value);
            } else {
                return to_string_ct(static_cast<UInt>(n));
            }
        }

        // constexpr string concatenation helper
        constexpr std::string concat() {
            return "";
        }

        template<typename First, typename... Rest>
        constexpr std::string concat(First &&first, Rest &&... rest) {
            if constexpr (std::is_integral_v<std::decay_t<First> >) {
                return to_string_ct(first).append(concat(std::forward<Rest>(rest)...));
            } else {
                return std::string(std::forward<First>(first)).append(concat(std::forward<Rest>(rest)...));
            }
        }
    }


    /* ========================================================================
     * Safety & Options
     * 安全措施与配置项
     * ======================================================================== */

    // === Traceback -=========================================================
    // 调用栈记录
    namespace errors {
        // --- Error Policy ---------------------------------------------------
        // 错误处理策略
        enum class error_policy {
            STRICT = 1,
            MEDIUM = 2,
            IGNORE = 3
        };

        struct wrapper_frame {
            std::string wrapper_info;
        };

        struct value_frame {
            const char *type;
            const char *proto;

            const std::optional<std::string> child_label;
            const std::optional<std::string> details;
        };

        using traceback_frame = std::variant<wrapper_frame, value_frame>;

        template<typename Fn>
        concept trace_frame_generator = requires(Fn fn)
        {
            { fn() } -> std::convertible_to<traceback_frame>;
        };

        struct traceback {
            std::vector<traceback_frame> frames;

            [[nodiscard]] std::string format() const {
                std::string result = "Traceback:\n";

                bool newline = false;
                std::string label = "[ROOT]";

                for (size_t i = frames.size(); i > 0; --i) {
                    if (newline) {
                        result.push_back('\n');
                        newline = false;
                    }

                    const auto &frame = frames[i - 1];
                    if (frame.index() == 0) {
                        const auto &wrapper = std::get<0>(frame);

                        result.append(tools::concat("  @", wrapper.wrapper_info, "\n"));
                    } else {
                        const auto &value = std::get<1>(frame);

                        result.append(tools::concat("  -", label, " | ", value.type, ", ", value.proto, "\n"));
                        if (value.child_label.has_value()) {
                            label = value.child_label.value();
                        } else {
                            label = "[UNKNOWN]";
                        }

                        if (value.details.has_value()) {
                            result.append(tools::concat("  (", value.details.value(), ")\n"));
                        }

                        newline = true;
                    }
                }
                result.append("  ^ Error Here");
                return result;
            }
        };

        constexpr auto value_frame_gen(const char *type, const char *proto) -> trace_frame_generator auto {
            return [=] { return value_frame{type, proto}; };
        }

        constexpr auto wrapper_frame_gen(const char *info) -> trace_frame_generator auto {
            return [=] { return wrapper_frame{info}; };
        }
    }


    // === Context ============================================================
    // 配置与上下文

    // We use MIT License, so you can modify these constexpr options in this file:

    static constexpr auto endian = std::endian::big;
    static constexpr bool enable_traceback = true;

    // --- Session Level Options ----------------------------------------------
    // 会话级配置
    struct options {
        size_t max_depth;
        size_t max_container_size; // Length
        size_t max_string_size; // Byte Size
        errors::error_policy policy;
        size_t target_schema_version = SIZE_MAX;

        static options default_options;
    };

    inline options options::default_options{
        .max_depth = 256,
        .max_container_size = 1 * 1024 * 1024,
        .max_string_size = 4 * 1024 * 1024,
        .policy = errors::error_policy::MEDIUM,
        .target_schema_version = 0
    };

    // --- Process Level Status -----------------------------------------------
    // 单次调用级状态
    struct status {
        size_t current_depth = 0;
    };

    // --- Context ------------------------------------------------------------
    // 上下文
    struct context {
        options opt;
        status st;
        std::shared_ptr<errors::traceback> traceback;

        errors::traceback &get_traceback() {
            if (traceback == nullptr) {
                traceback = std::make_shared<errors::traceback>();
            }
            return *traceback;
        }

        template<bool GetDeeper, bool RollbackOpts, errors::trace_frame_generator FrameFn>
        struct scope_guard;

        template<bool GetDeeper, bool RollbackOpts, errors::trace_frame_generator FrameFn>
        static scope_guard<GetDeeper, RollbackOpts, FrameFn> guard(context &ctx, FrameFn &&frame_fn);

        template<bool GetDeeper, bool RollbackOpts, errors::trace_frame_generator FrameFn>
        scope_guard<GetDeeper, RollbackOpts, FrameFn> guard(FrameFn &&frame_fn);

        static context get_default_context();
    };

    inline context context::get_default_context() {
        return context{
            .opt = options::default_options,
            .st = status{},
            .traceback = nullptr
        };
    }


    // === Error Class ========================================================
    // 错误类
    namespace errors {
        // --- Error Codes ----------------------------------------------------
        // 错误码
        enum class code : uint32_t {
            // IO / Stream
            unexpected_eof,

            // Schema / Protocol
            invalid_index,
            fixed_size_mismatch,
            duplicate_key,

            // Safety limits
            depth_limit_exceeded,
            container_too_large,
            string_too_large,
            varint_overflow,

            // Value validation
            invalid_bool,

            // Runtime / logic
            not_implemented,
            runtime_error
        };

        // --- Error Kinds ----------------------------------------------------
        // 错误种类
        enum class kind : uint8_t {
            fatal,
            safety,
            io,
            schema,
            logic
        };

        [[nodiscard]] constexpr kind classify(const code c) {
            switch (c) {
                case code::unexpected_eof:
                    return kind::io;

                case code::invalid_index:
                case code::fixed_size_mismatch:
                    return kind::schema;

                case code::duplicate_key:
                    return kind::schema;

                case code::depth_limit_exceeded:
                case code::container_too_large:
                case code::string_too_large:
                case code::varint_overflow:
                case code::invalid_bool:
                    return kind::safety;

                case code::not_implemented:
                case code::runtime_error:
                    return kind::fatal;

                default:
                    return kind::logic;
            }
        }

        [[nodiscard]] constexpr const char *nameof(const code c) {
            switch (c) {
                case code::unexpected_eof: return "unexpected_eof";
                case code::invalid_index: return "invalid_index";
                case code::fixed_size_mismatch: return "fixed_size_mismatch";
                case code::duplicate_key: return "duplicate_key";
                case code::depth_limit_exceeded: return "depth_limit_exceeded";
                case code::container_too_large: return "container_too_large";
                case code::string_too_large: return "string_too_large";
                case code::varint_overflow: return "varint_overflow";
                case code::invalid_bool: return "invalid_bool";
                case code::not_implemented: return "not_implemented";
                case code::runtime_error: return "runtime_error";
            }
            return "unknown";
        }

        // --- Error Object ---------------------------------------------------
        // 错误对象
        struct error final : std::runtime_error {
            code c;
            kind k;

            std::string message;
            std::shared_ptr<traceback> tb;

            constexpr static std::string build_what(const code c_, std::string const &msg) {
                return std::move(tools::concat("[bsp::", nameof(c_), "] ", msg));
            }

            error(const code c_, std::string msg, std::shared_ptr<traceback> tb = nullptr) : runtime_error(
                    build_what(c_, msg)),
                c(c_), k(classify(c_)),
                message(std::move(msg)), tb(std::move(tb)) {
            }
        };

        inline error make(
            const code c,
            context &ctx,
            std::string msg = {}
        ) {
            ctx.get_traceback();
            return error(c, std::move(msg), ctx.traceback);
        }

        inline error make(
            const code c,
            std::string msg = {}
        ) {
            return error(c, std::move(msg), nullptr);
        }

        inline error unexpected_eof(const size_t expected, const size_t actual, const char *stream_type) {
            return make(
                code::unexpected_eof,
                tools::concat("unexpected EOF (expected ", expected, ", got", actual, ") when reading", stream_type));
        }

        inline error invalid_bool(const uint8_t actual, context &ctx) {
            return make(
                code::invalid_bool, ctx,
                tools::concat("invalid bool value ", actual));
        }

        inline error not_implemented(context &ctx) {
            return make(code::not_implemented, ctx,
                        "feature not implemented");
        }

        inline error fixed_size_mismatch(const size_t expected, const size_t actual, context &ctx) {
            return make(
                code::fixed_size_mismatch, ctx,
                tools::concat("container size ", expected, " mismatches fixed size ", actual));
        }

        inline error depth_limit_exceeded(const size_t limit, context &ctx) {
            return make(
                code::depth_limit_exceeded, ctx,
                tools::concat("depth limit", limit, "exceeded")
            );
        }

        inline error container_too_large(const size_t actual, context &ctx) {
            return make(
                code::container_too_large, ctx,
                tools::concat("container size ", actual,
                              " larger than limit=", ctx.opt.max_container_size, " children"));
        }

        inline error string_too_large(const size_t actual, context &ctx) {
            return make(
                code::string_too_large, ctx,
                tools::concat("string size ", actual,
                              "larget than limit=", ctx.opt.max_string_size, " bytes"));
        }
    }


    // === RAII Guards ========================================================
    // RAII 限制工具
    template<bool GetDeeper, bool RollbackOpts, errors::trace_frame_generator FrameFn>
    struct context::scope_guard {
        // 该字段应该永远被传入，而不是自行创建
        const std::reference_wrapper<context> ctx;
        [[no_unique_address, maybe_unused]] const FrameFn traceback_frame_fn;

    private:
        [[maybe_unused]] options origin_opt;
        [[maybe_unused]] size_t origin_depth;
        [[maybe_unused]] int uncaught_exceptions;

    public:
        explicit scope_guard(context &ctx,
                             const FrameFn &&frame_fn) : ctx(ctx),
                                                         traceback_frame_fn(std::forward<FrameFn>(frame_fn)) {
            if constexpr (GetDeeper) {
                if (ctx.st.current_depth + 1 > ctx.opt.max_depth)
                    throw errors::depth_limit_exceeded(ctx.opt.max_depth, ctx);
                origin_depth = ctx.st.current_depth;
                ctx.st.current_depth++;
            }

            if constexpr (RollbackOpts) {
                origin_opt = ctx.opt;
            }

            if constexpr (enable_traceback) {
                uncaught_exceptions = std::uncaught_exceptions();
            }
        }

        ~scope_guard() {
            if constexpr (GetDeeper) ctx.get().st.current_depth = origin_depth;
            if constexpr (RollbackOpts) ctx.get().opt = origin_opt;
            if constexpr (enable_traceback)
                if (std::uncaught_exceptions() > uncaught_exceptions) {
                    try {
                        ctx.get().get_traceback().frames.push_back(traceback_frame_fn());
                    } catch (...) {
                        ctx.get().get_traceback().frames.push_back(errors::wrapper_frame{
                            "[!!] error when generating traceback info"
                        });
                    }
                }
        }

        scope_guard(const scope_guard &) = delete;

        scope_guard &operator=(const scope_guard &) = delete;
    };

    // Usage:
    // auto g = context::guard<G, R>(ctx, [&] { return traceback_frame; });
    // Let G true when the serializer is a container
    // Let R true when the serializer may modify the options
    //
    // The compiler will optimize it so the lambda func won't cause runtime cost.
    // So enable O2 optimize plz
    template<bool GetDeeper, bool RollbackOpts, errors::trace_frame_generator FrameFn>
    context::scope_guard<GetDeeper, RollbackOpts, FrameFn> context::guard(context &ctx, FrameFn &&frame_fn) {
        return scope_guard<GetDeeper, RollbackOpts, std::decay_t<FrameFn> >(ctx, std::forward<FrameFn>(frame_fn));
    }

    // Usage:
    // auto g = ctx.guard<G, R>([&] { return traceback_frame; });
    // Let G true when the serializer is a container
    // Let R true when the serializer may modify the options
    //
    // The compiler will optimize it so the lambda func won't cause runtime cost.
    // So enable O2 optimize plz
    template<bool GetDeeper, bool RollbackOpts, errors::trace_frame_generator FrameFn>
    context::scope_guard<GetDeeper, RollbackOpts, FrameFn> context::guard(FrameFn &&frame_fn) {
        return scope_guard<GetDeeper, RollbackOpts, std::decay_t<FrameFn> >(*this, std::forward<FrameFn>(frame_fn));
    }


    /* ========================================================================
     * Class Definitions
     * 类定义
     * ======================================================================== */

    // === I/O Classes ========================================================
    // I/O 类
    namespace io {
        template<typename R> concept Reader = requires(R r, uint8_t *buf, const std::streamsize n)
        {
            { r.read_bytes(buf, n) } -> std::same_as<void>;
            { r.read_byte() } -> std::same_as<uint8_t>;
        };
        template<typename W> concept Writer = requires(W w, const uint8_t *buf, const std::streamsize n, uint8_t b)
        {
            { w.write_bytes(buf, n) } -> std::same_as<void>;
            { w.write_byte(b) } -> std::same_as<void>;
        };

        // --- I/O Wrapping std::stream -----------------------------------------------
        // 包装 std::stream 的 I/O 类

        struct StreamReader {
            std::istream &is;

            explicit StreamReader(std::istream &s) : is(s) {
            }

            void read_bytes(uint8_t *buf, const std::streamsize n) const {
                is.read(reinterpret_cast<char *>(buf), n);
                if (is.eof())
                    throw errors::unexpected_eof(
                        static_cast<size_t>(n),
                        static_cast<size_t>(is.gcount()),
                        "std::istream"
                    );
                if (is.fail())
                    throw errors::error(errors::code::runtime_error, "error when reading std::istream");
            }

            [[nodiscard]] uint8_t read_byte() const {
                char c;
                if (!is.get(c)) {
                    if (is.eof())
                        throw errors::unexpected_eof(1, 0, "std::istream");
                    throw errors::error(errors::code::runtime_error, "error when reading std::istream");
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
                if (os.eof())
                    throw errors::unexpected_eof(
                        static_cast<size_t>(n),
                        0,
                        "std::ostream"
                    );
                if (os.fail())
                    throw errors::error(errors::code::runtime_error, "error when writing to std::ostream");
            }

            void write_byte(const uint8_t b) const {
                if (!os.put(static_cast<char>(b))) {
                    if (os.eof())
                        throw errors::unexpected_eof(1, 0, "std::ostream");
                    throw errors::error(errors::code::runtime_error, "error when writing to std::ostream");
                }
            }
        };


        // --- I/O Wrapping std::vector<uint8_t> --------------------------------------
        // 包装字节数组的 I/O 类

        struct BufferReader {
            const uint8_t *data;
            size_t size;
            size_t pos;

            explicit BufferReader(const std::vector<uint8_t> &buf)
                : data(buf.data()), size(buf.size()), pos(0) {
            }

            BufferReader(const uint8_t *data_, const size_t size_)
                : data(data_), size(size_), pos(0) {
            }

            void read_bytes(uint8_t *buf, const std::streamsize n) {
                if (pos + static_cast<size_t>(n) > size)
                    throw errors::unexpected_eof(
                        static_cast<size_t>(n),
                        size - pos,
                        "BufferReader"
                    );
                memcpy(buf, data + pos, static_cast<size_t>(n));
                pos += static_cast<size_t>(n);
            }

            [[nodiscard]] uint8_t read_byte() {
                if (pos >= size)
                    throw errors::unexpected_eof(1, 0, "BufferReader");
                return data[pos++];
            }
        };

        struct BufferWriter {
            std::vector<uint8_t> buf;

            void write_bytes(const uint8_t *p, const std::streamsize n) {
                buf.insert(buf.end(), p, p + n);
            }

            void write_byte(const uint8_t b) {
                buf.push_back(b);
            }
        };


        // --- I/O Wrapping other Readers/Writers -------------------------------------
        // 包装其它 I/O 类的 I/O 类

        template<Reader R>
        struct LimitedReader {
            R &base;
            size_t remaining;

            LimitedReader(R &r, const size_t n) : base(r), remaining(n) {
            }

            void read_bytes(uint8_t *buf, const std::streamsize n) {
                if (static_cast<size_t>(n) > remaining)
                    throw errors::unexpected_eof(
                        static_cast<size_t>(n),
                        remaining,
                        "LimitedReader"
                    );
                base.read_bytes(buf, n);
                remaining -= static_cast<size_t>(n);
            }

            [[nodiscard]] uint8_t read_byte() {
                if (remaining == 0)
                    throw errors::unexpected_eof(1, 0, "LimitedReader");
                --remaining;
                return base.read_byte();
            }

            void skip_remaining() {
                uint8_t tmp[256];
                while (remaining) {
                    const size_t k = std::min(remaining, static_cast<size_t>(256));
                    base.read_bytes(tmp, static_cast<std::streamsize>(k));
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

            void write_bytes(const uint8_t *buf, const std::streamsize n) {
                if (static_cast<size_t>(n) > remaining)
                    throw errors::make(
                        errors::code::fixed_size_mismatch,
                        tools::concat("writing ", n, " bytes to a LimitWriter remaining ", remaining, "bytes")
                    );
                base.write_bytes(buf, n);
                remaining -= static_cast<size_t>(n);
            }

            void write_byte(const uint8_t b) {
                if (remaining == 0)
                    throw errors::make(
                        errors::code::fixed_size_mismatch,
                        "writing 1 byte to a LimitedWriter remaining 0 byte"
                    );
                --remaining;
                base.write_byte(b);
            }

            void pad_zero() {
                while (remaining) {
                    write_byte(0);
                }
            }
        };


        // --- I/O with Type Erasure --------------------------------------------------
        // 类型擦除 I/O 类

        class AnyReader {
        public:
            template<Reader R>
            explicit AnyReader(R &reader)
                : impl_(std::make_unique<ReaderModel<R> >(reader)) {
            }

            AnyReader(AnyReader &&) noexcept = default;

            AnyReader &operator=(AnyReader &&) noexcept = default;

            AnyReader(const AnyReader &) = delete;

            AnyReader &operator=(const AnyReader &) = delete;

            void read_bytes(uint8_t *buf, const std::streamsize n) const {
                impl_->read_bytes(buf, n);
            }

            [[nodiscard]] uint8_t read_byte() const {
                return impl_->read_byte();
            }

            [[nodiscard]] const std::type_info &reader_type() const noexcept {
                return impl_->reader_type();
            }

        private:
            struct ReaderConcept {
                virtual ~ReaderConcept() = default;

                virtual void read_bytes(uint8_t *buf, std::streamsize n) = 0;

                [[nodiscard]] virtual uint8_t read_byte() = 0;

                [[nodiscard]] virtual const std::type_info &reader_type() const noexcept = 0;
            };

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

            void write_bytes(const uint8_t *buf, const std::streamsize n) const {
                impl_->write_bytes(buf, n);
            }

            void write_byte(const uint8_t b) const {
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
            virtual void write(io::AnyWriter &w, context &ctx) const =0;

            virtual void read(io::AnyReader &r, context &ctx) =0;

            virtual ~CVal() = default;
        };

        using bytes = std::vector<uint8_t>;
    }


    /* ========================================================================
     * Template Declarations
     * 模板声明
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

        template<size_t Version = SIZE_MAX>
        struct Schema {
        };

        struct DynSchema {
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
        template<typename Len, typename Inner>
        struct Limited : WrapperProto {
        };

        /*
            Force fixed serialized length.

            Encoding:
            [length][payload padded with zero]
        */
        template<typename Len, typename Inner>
        struct Forced : WrapperProto {
        };

        template<typename T>
        struct DefaultProtocol {
            using type = Default;
        };

        template<typename T>
        using DefaultProtocol_t = DefaultProtocol<T>::type;
    }

    // === Serializer =========================================================
    // 序列化器
    namespace serialize {
        template<typename T, typename Proto>
        struct Serializer {
            // Interface:
            // static void write(io::Writer auto &w, const T &v, context &ctx);
            // static void read(io::Reader auto &r, T &out, context &ctx);
        };

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
            field_type Class::*ptr;
        };

        template<size_t Version, typename... Fields>
        struct SchemaEntry {
            static constexpr size_t version = Version;
            std::tuple<Fields...> fields;

            explicit constexpr SchemaEntry(Fields... fields) : fields(fields...) {
            }
        };

        template<size_t Version, typename... Fields>
        static constexpr auto make_schema_entry(Fields... fields) {
            return SchemaEntry<Version, Fields...>{fields...};
        }

        template<typename T>
        struct SchemaSet {
        };

        // --- Schema Version Match -------------------------------------------
        // 模式查找
        template<typename T>
        constexpr size_t match_schema_index(const size_t target) {
            constexpr auto &schemas = SchemaSet<T>::schemas;
            constexpr size_t count = std::tuple_size_v<std::decay_t<decltype(schemas)> >;
            size_t result = SIZE_MAX;

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                (
                    (
                        result == SIZE_MAX && std::get<count - 1 - Is>(schemas).version <= target
                            ? result = count - 1 - Is
                            : void()
                    ),
                    ...
                );
            }(std::make_index_sequence<count>{});

            return result;
        }


        template<typename T, size_t target>
        constexpr size_t match_schema_index() {
            return match_schema_index<T>(target);
        }

        template<typename T>
        consteval bool validate_schemas(const T &t) {
            constexpr size_t N = std::tuple_size_v<T>;

            size_t prev = std::get<0>(t).version;

            for (size_t i = 1; i < N; ++i) {
                const size_t cur = std::get<i>(t).version;
                if (cur <= prev) return false;
                prev = cur;
            }
            return true;
        }
    }

    // === Concepts ===========================================================
    // 概念
    namespace types {
        // Types which can be serialized by copying memory
        template<typename T>
        concept trivially_serializable =
                std::is_trivially_copyable_v<T> &&
                !std::is_pointer_v<T> &&
                !std::is_member_pointer_v<T>;

        // T-P Pairs which can be serialized
        template<typename T, typename Proto>
        concept serializable = requires(io::AnyWriter &w, io::AnyReader &r, const T &cv, T &v, context &ctx)
        {
            // Q: Why requires AnyIO?
            // A:
            // CPP doesn't allow "requires(concept auto &param)", so we need a specified typename.
            // AnyIO is the IO interface which exposes the least inner details (for StreamIO is std::stream, for BufferIO is std::vector...)
            // So if one supports AnyIO, it must support any type of IO.
            { serialize::Serializer<T, Proto>::write(w, cv, ctx) } -> std::same_as<void>;
            { serialize::Serializer<T, Proto>::read(r, v, ctx) } -> std::same_as<void>;
        };

        // Types which can be serialized through default protocol
        template<typename T>
        concept default_serializable = serializable<T, proto::DefaultProtocol_t<T> >;

        // All the types can be serialized through default protocol
        template<typename... Ts>
        concept all_serializable = (default_serializable<Ts> && ...);

        // Types with defined schema
        template<typename T>
        concept has_schema = std::is_same_v<typename schema::SchemaSet<T>::Type, T>;
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

        // DefaultProtocol for Schemas is registered in BSP_SCHEMA_SET macro.
        // Proto Trivial need to be used explicitly.
    }

    // === Serializers ========================================================
    // 序列化器特化实现
    namespace serialize {
        namespace detail {
            // --- Varint Implementation --------------------------------------
            // 变长整数实现
            template<std::unsigned_integral UInt>
            void write_varint(io::Writer auto &w, UInt v) {
                while (v >= 0x80) {
                    w.write_byte(v & 0x7F | 0x80);
                    v >>= 7;
                }

                w.write_byte(v);
            }

            template<std::unsigned_integral UInt>
            [[nodiscard]] UInt read_varint(io::Reader auto &r, const bool overflow_error) {
                UInt result = 0;
                int shift = 0;

                while (true) {
                    if (overflow_error)
                        if (shift >= static_cast<int>(sizeof(UInt) * 8))
                            throw errors::make(errors::code::varint_overflow,
                                               tools::concat("varint overflow (max bits=",
                                                             static_cast<uint8_t>(std::ceil(sizeof(UInt) * 8.0 / 7)),
                                                             ")"));

                    const uint8_t b = r.read_byte();
                    result |= UInt(b & 0x7F) << shift;

                    if (!(b & 0x80))
                        return result;

                    shift += 7;
                }
            }

            template<std::signed_integral S>
            [[nodiscard]] std::make_unsigned_t<S> zigzag_encode(S v) {
                using U = std::make_unsigned_t<S>;
                return (static_cast<U>(v) << 1) ^ static_cast<U>(v >> (sizeof(S) * 8 - 1));
            }

            template<std::unsigned_integral U>
            [[nodiscard]] std::make_signed_t<U> zigzag_decode(U v) {
                using S = std::make_signed_t<U>;
                const U mask = ~(v & 1) + 1;
                return static_cast<S>((v >> 1) ^ mask);
            }


            // --- Endian Conversion ------------------------------------------
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
            [[nodiscard]] constexpr T adapt_endian(T v) {
                if constexpr (endian == std::endian::native)
                    return v;
                else
                    return byteswap(v);
            }


            // --- Compile-Time Tools -----------------------------------------
            // 编译时工具

            template<typename T>
            constexpr const char *literal_name() {
                if constexpr (std::is_same_v<T, bool>) return "bool";
                else if constexpr (std::is_signed_v<T>) {
                    switch (sizeof(T)) {
                        case 1: return "int8_t";
                        case 2: return "int16_t";
                        case 4: return "int32_t";
                        case 8: return "int64_t";
                        case 16: return "int128_t";
                        default: return "int?_t";
                    }
                } else if constexpr (std::is_unsigned_v<T>) {
                    switch (sizeof(T)) {
                        case 1: return "uint8_t";
                        case 2: return "uint16_t";
                        case 4: return "uint32_t";
                        case 8: return "uint64_t";
                        case 16: return "uint128_t";
                        default: return "uint?_t";
                    }
                } else if constexpr (std::is_floating_point_v<T>) {
                    switch (sizeof(T)) {
                        case 4: return "float";
                        case 8: return "double";
                        case 10:
                        case 16:
                            return "long double";
                        default: return "float?_t";
                    }
                } else {
                    return "unknown";
                }
            }
        }


        // ~~~ Serializers for Types ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // 为特定类型设计的序列化器

        // --- Serializers for Value Types  -----------------------------------
        // 数据类型的序列化器

        // bool
        // [0/1 Bool]
        template<>
        struct Serializer<bool, proto::Fixed<> > {
            static void write(io::Writer auto &w, const bool &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen("bool", "Fixed<>"));
                w.write_byte(v);
            }

            static void read(io::Reader auto &r, bool &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen("bool", "Fixed<>"));
                if (ctx.opt.policy <= errors::error_policy::STRICT) {
                    const uint8_t b = r.read_byte();
                    if (b > 1)
                        throw errors::invalid_bool(b, ctx);
                    out = b;
                } else {
                    out = r.read_byte();
                }
            }
        };

        // Integral
        template<std::integral T>
        struct Serializer<T, proto::Fixed<> > {
            static constexpr const char *t_str = detail::literal_name<T>();

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Fixed<>"));
                const auto x = detail::adapt_endian(v);
                w.write_bytes(reinterpret_cast<const uint8_t *>(&x), sizeof(T));
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Fixed<>"));
                T x;
                r.read_bytes(reinterpret_cast<uint8_t *>(&x), sizeof(T));
                out = detail::adapt_endian(x);
            }
        };

        // [LEB128 Varint]
        // Not affected by endian
        template<std::unsigned_integral T>
        struct Serializer<T, proto::Varint> {
            static constexpr const char *t_str = detail::literal_name<T>();

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Varint"));
                detail::write_varint(w, v);
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Varint"));
                out = detail::read_varint<T>(r, ctx.opt.policy <= errors::error_policy::MEDIUM);
            }
        };

        // [ZigZag+LEB128 Varint]
        // Not affected by endian
        template<std::signed_integral T>
        struct Serializer<T, proto::Varint> {
            static constexpr const char *t_str = detail::literal_name<T>();

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Varint"));
                detail::write_varint(w, detail::zigzag_encode(v));
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Varint"));
                out = detail::zigzag_decode(
                    detail::read_varint<std::make_unsigned_t<T> >(r, ctx.opt.policy <= errors::error_policy::MEDIUM));
            }
        };

        // Floating
        template<std::floating_point T> requires std::numeric_limits<T>::is_iec559
        struct Serializer<T, proto::Fixed<> > {
            using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
            static constexpr const char *t_str = detail::literal_name<T>();

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Fixed<>"));
                const UInt x = detail::adapt_endian(std::bit_cast<UInt>(v));
                w.write_bytes(reinterpret_cast<const uint8_t *>(&x), sizeof(T));
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Fixed<>"));
                UInt x;
                r.read_bytes(reinterpret_cast<uint8_t *>(&x), sizeof(T));
                out = std::bit_cast<T>(detail::adapt_endian(x));
            }
        };


        // --- Serializers for Container Types --------------------------------
        // 容器类型的序列化器

        // std::string
        // [Varint length][String]
        template<>
        struct Serializer<std::string, proto::Varint> {
            static void write(io::Writer auto &w, const std::string &v, context &ctx) {
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "std::string", "Varint", std::nullopt,
                        tools::concat("length=", v.size())
                    };
                });
                detail::write_varint(w, v.size());
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), v.size());
            }

            static void read(io::Reader auto &r, std::string &out, context &ctx) {
                size_t size = 0;
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "std::string", "Varint", std::nullopt,
                        tools::concat("length=", size)
                    };
                });
                size = detail::read_varint<size_t>(r, ctx.opt.policy <= errors::error_policy::MEDIUM);

                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_string_size)
                        throw errors::string_too_large(size, ctx);

                out.resize(size);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), size);
            }
        };

        // [String]
        template<size_t N>
        struct Serializer<std::string, proto::Fixed<N> > {
            static constexpr std::string p_str = tools::concat("Fixed<", N, ">");

            static void write(io::Writer auto &w, const std::string &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen("std::string", p_str.c_str()));
                if (v.size() != N) throw errors::fixed_size_mismatch(N, v.size(), ctx);

                detail::write_varint(w, N);
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), v.size());
            }

            static void read(io::Reader auto &r, std::string &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen("std::string", p_str.c_str()));

                out.resize(N);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), N);
            }
        };

        // types::bytes (std::vector<uint8_t>)
        // [Varint length][Bytearray]
        template<>
        struct Serializer<types::bytes, proto::Varint> {
            static void write(io::Writer auto &w, const types::bytes &v, context &ctx) {
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "types::bytes", "Varint", std::nullopt,
                        tools::concat("length=", v.size())
                    };
                });
                detail::write_varint(w, v.size());
                w.write_bytes(v.data(), v.size());
            }

            static void read(io::Reader auto &r, types::bytes &out, context &ctx) {
                size_t size = 0;
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "types::bytes", "Varint", std::nullopt,
                        tools::concat("length=", size)
                    };
                });
                size = detail::read_varint<size_t>(r, ctx.opt.policy <= errors::error_policy::MEDIUM);

                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_string_size)
                        throw errors::string_too_large(size, ctx);

                out.resize(size);
                r.read_bytes(out.data(), size);
            }
        };

        // [Bytearray]
        template<size_t N>
        struct Serializer<types::bytes, proto::Fixed<N> > {
            static constexpr std::string p_str = tools::concat("Fixed<", N, ">");

            static void write(io::Writer auto &w, const types::bytes &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen("types::bytes", p_str.c_str()));
                if (v.size() != N) throw errors::fixed_size_mismatch(N, v.size(), ctx);
                w.write_bytes(v.data(), v.size());
            }

            static void read(io::Reader auto &r, types::bytes &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen("types::bytes", p_str.c_str()));
                out.resize(N);
                r.read_bytes(out.data(), N);
            }
        };

        // std::vector
        // [Varint length][Value 0][Value 1]...
        template<typename T> requires types::default_serializable<T>
        struct Serializer<std::vector<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::vector<T> &v, context &ctx) {
                size_t index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::vector", "Varint", tools::concat("Elem ", index),
                        tools::concat("length=", v.size())
                    };
                });
                detail::write_varint(w, v.size());

                for (; index < v.size(); ++index) {
                    DefaultSerializer<T>::write(w, v[index], ctx);
                }
            }

            static void read(io::Reader auto &r, std::vector<T> &out, context &ctx) {
                size_t index = 0;
                size_t size = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::vector", "Varint", tools::concat("Elem ", index),
                        tools::concat("length=", size)
                    };
                });

                size = detail::read_varint<size_t>(r);
                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_container_size) throw errors::container_too_large(size, ctx);

                out.resize(size);
                for (; index < size; ++index) {
                    DefaultSerializer<T>::read(r, out[index], ctx);
                }
            }
        };

        // [Value 0][Value 1]...
        template<typename T, size_t N> requires types::default_serializable<T>
        struct Serializer<std::vector<T>, proto::Fixed<N> > {
            static constexpr std::string p_str = tools::concat("Fixed<", N, ">");

            static void write(io::Writer auto &w, const std::vector<T> &v, context &ctx) {
                size_t index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::vector", p_str.c_str(), tools::concat("Elem ", index)
                    };
                });
                if (v.size() != N) throw errors::fixed_size_mismatch(N, v.size(), ctx);

                for (; index < N; ++index) {
                    DefaultSerializer<T>::write(w, v[index], ctx);
                }
            }

            static void read(io::Reader auto &r, std::vector<T> &out, context &ctx) {
                size_t index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::vector", p_str.c_str(), tools::concat("Elem ", index)
                    };
                });

                out.resize(N);
                for (; index < N; ++index) {
                    DefaultSerializer<T>::read(r, out[index], ctx);
                }
            }
        };

        // Note: std::vector<bool> is not bit-compressed
        // Use std::bitset or vector<bool> + Trivial if you want to enable bit-compression

        // std::bitset
        // Bit-compressed with Little-endian style
        template<size_t N>
        struct Serializer<std::bitset<N>, proto::Fixed<> > {
            static constexpr size_t byte_count = (N + 7) / 8;
            static constexpr std::string t_str = tools::concat("std::bitset<", N, ">");

            static void write(io::Writer auto &w, const std::bitset<N> &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str.c_str(), "Fixed<>"));

                // Serialize as little-endian bytes
                for (size_t i = 0; i < byte_count; ++i) {
                    uint8_t byte = 0;
                    for (size_t bit = 0; bit < 8 && (i * 8 + bit) < N; ++bit) {
                        if (v[i * 8 + bit])
                            byte |= (1u << bit);
                    }
                    w.write_byte(byte);
                }
            }

            static void read(io::Reader auto &r, std::bitset<N> &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str.c_str(), "Fixed<>"));

                out.reset();
                for (size_t i = 0; i < byte_count; ++i) {
                    const uint8_t byte = r.read_byte();
                    for (size_t bit = 0; bit < 8 && (i * 8 + bit) < N; ++bit) {
                        if (byte & (1u << bit))
                            out.set(i * 8 + bit);
                    }
                }
            }
        };

        // std::map
        // [Varint length][Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V> requires (types::default_serializable<K> &&
                                                   types::default_serializable<V>)
        struct Serializer<std::map<K, V>, proto::Varint> {
            static void write(io::Writer auto &w, const std::map<K, V> &v, context &ctx) {
                size_t index = 0;
                bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::vector", "Varint", tools::concat(is_value ? "Value " : "Key ", index),
                        tools::concat("length=", v.size())
                    };
                });

                detail::write_varint(w, v.size());
                for (const auto &[key, value]: v) {
                    is_value = false;
                    DefaultSerializer<K>::write(w, key, ctx);
                    is_value = true;
                    DefaultSerializer<V>::write(w, value, ctx);
                    ++index;
                }
            }

            static void read(io::Reader auto &r, std::map<K, V> &out, context &ctx) {
                size_t index = 0;
                size_t size = 0;
                [[maybe_unused]] bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::map", "Varint", tools::concat(is_value ? "Value " : "Key ", index),
                        tools::concat("length=", size)
                    };
                });

                size = detail::read_varint<size_t>(r);
                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_container_size) throw errors::container_too_large(size, ctx);

                out.clear();
                for (; index < size; index++) {
                    is_value = false;
                    K key;
                    DefaultSerializer<K>::read(r, key, ctx);
                    is_value = true;
                    V value;
                    DefaultSerializer<V>::read(r, value, ctx);

                    if (ctx.opt.policy <= errors::error_policy::STRICT)
                        if (out.contains(key))
                            throw errors::make(errors::code::duplicate_key, ctx,
                                               std::string("duplicate key in std::map"));

                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // [Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V, size_t N> requires (types::default_serializable<K> &&
                                                             types::default_serializable<V>)
        struct Serializer<std::map<K, V>, proto::Fixed<N> > {
            static constexpr std::string p_str = tools::concat("Fixed<", N, ">");

            static void write(io::Writer auto &w, const std::map<K, V> &v, context &ctx) {
                size_t index = 0;
                bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::map", p_str.c_str(), tools::concat(is_value ? "Value " : "Key ", index)
                    };
                });
                if (v.size() != N) throw errors::fixed_size_mismatch(N, v.size(), ctx);

                for (const auto &[key, value]: v) {
                    is_value = false;
                    DefaultSerializer<K>::write(w, key, ctx);
                    is_value = true;
                    DefaultSerializer<V>::write(w, value, ctx);
                    ++index;
                }
            }

            static void read(io::Reader auto &r, std::map<K, V> &out, context &ctx) {
                size_t index = 0;
                [[maybe_unused]] bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::map", p_str.c_str(), tools::concat(is_value ? "Value " : "Key ", index)
                    };
                });

                out.clear();
                for (; index < N; ++index) {
                    is_value = false;
                    K key;
                    DefaultSerializer<K>::read(r, key, ctx);
                    is_value = true;
                    V value;
                    DefaultSerializer<V>::read(r, value, ctx);

                    if (ctx.opt.policy <= errors::error_policy::STRICT)
                        if (out.contains(key))
                            throw errors::make(errors::code::duplicate_key, ctx,
                                               std::string("duplicate key in std::map"));

                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // std::unordered_map
        // [Varint length][Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V> requires (types::default_serializable<K> &&
                                                   types::default_serializable<V>)
        struct Serializer<std::unordered_map<K, V>, proto::Varint> {
            static void write(io::Writer auto &w, const std::unordered_map<K, V> &v, context &ctx) {
                size_t index = 0;
                bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::unordered_map", "Varint", tools::concat(is_value ? "Value " : "Key ", index),
                        tools::concat("length=", v.size())
                    };
                });

                detail::write_varint(w, v.size());
                for (const auto &[key, value]: v) {
                    is_value = false;
                    DefaultSerializer<K>::write(w, key, ctx);
                    is_value = true;
                    DefaultSerializer<V>::write(w, value, ctx);
                    ++index;
                }
            }

            static void read(io::Reader auto &r, std::unordered_map<K, V> &out, context &ctx) {
                size_t index = 0;
                size_t size = 0;
                [[maybe_unused]] bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::unordered_map", "Varint", tools::concat(is_value ? "Value " : "Key ", index),
                        tools::concat("length=", size)
                    };
                });

                size = detail::read_varint<size_t>(r);
                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_container_size) throw errors::container_too_large(size, ctx);

                out.clear();
                for (; index < size; ++index) {
                    is_value = false;
                    K key;
                    DefaultSerializer<K>::read(r, key, ctx);
                    is_value = true;
                    V value;
                    DefaultSerializer<V>::read(r, value, ctx);

                    if (ctx.opt.policy <= errors::error_policy::STRICT)
                        if (out.contains(key))
                            throw errors::make(errors::code::duplicate_key, ctx,
                                               std::string("duplicate key in std::unordered_map"));

                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // [Key 1][Value 1][Key 2][Value 2]...
        template<typename K, typename V, size_t N> requires (types::default_serializable<K> &&
                                                             types::default_serializable<V>)
        struct Serializer<std::unordered_map<K, V>, proto::Fixed<N> > {
            static constexpr std::string p_str = tools::concat("Fixed<", N, ">");

            static void write(io::Writer auto &w, const std::unordered_map<K, V> &v, context &ctx) {
                size_t index = 0;
                bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::unordered_map", p_str.c_str(), tools::concat(is_value ? "Value " : "Key ", index)
                    };
                });
                if (v.size() != N) throw errors::fixed_size_mismatch(N, v.size(), ctx);

                for (const auto &[key, value]: v) {
                    is_value = false;
                    DefaultSerializer<K>::write(w, key, ctx);
                    is_value = true;
                    DefaultSerializer<V>::write(w, value, ctx);
                    ++index;
                }
            }

            static void read(io::Reader auto &r, std::unordered_map<K, V> &out, context &ctx) {
                size_t index = 0;
                [[maybe_unused]] bool is_value = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::unordered_map", p_str.c_str(), tools::concat(is_value ? "Value " : "Key ", index)
                    };
                });

                out.clear();
                for (; index < N; ++index) {
                    is_value = false;
                    K key;
                    DefaultSerializer<K>::read(r, key, ctx);
                    is_value = true;
                    V value;
                    DefaultSerializer<V>::read(r, value, ctx);

                    if (ctx.opt.policy <= errors::error_policy::STRICT)
                        if (out.contains(key))
                            throw errors::make(errors::code::duplicate_key, ctx,
                                               std::string("duplicate key in std::unordered_map"));

                    out.emplace(std::move(key), std::move(value));
                }
            }
        };

        // std::set
        // [Varint length][Elem 0][Elem 1]...
        template<typename T> requires types::default_serializable<T>
        struct Serializer<std::set<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::set<T> &v, context &ctx) {
                size_t index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::set", "Varint", tools::concat("Elem ", index),
                        tools::concat("length=", v.size())
                    };
                });

                detail::write_varint(w, v.size());
                for (const auto &elem: v) {
                    DefaultSerializer<T>::write(w, elem, ctx);
                    ++index;
                }
            }

            static void read(io::Reader auto &r, std::set<T> &out, context &ctx) {
                size_t index = 0;
                size_t size = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::set", "Varint", tools::concat("Elem ", index),
                        tools::concat("length=", size)
                    };
                });

                size = detail::read_varint<size_t>(r);
                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_container_size) throw errors::container_too_large(size, ctx);

                out.clear();
                for (; index < size; ++index) {
                    T elem;
                    DefaultSerializer<T>::read(r, elem, ctx);
                    out.emplace(std::move(elem));
                }
            }
        };

        // std::unordered_set
        // [Varint length][Elem 0][Elem 1]...
        template<typename T> requires types::default_serializable<T>
        struct Serializer<std::unordered_set<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::unordered_set<T> &v, context &ctx) {
                size_t index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::unordered_set", "Varint", tools::concat("Elem ", index),
                        tools::concat("length=", v.size())
                    };
                });

                detail::write_varint(w, v.size());
                for (const auto &elem: v) {
                    DefaultSerializer<T>::write(w, elem, ctx);
                    ++index;
                }
            }

            static void read(io::Reader auto &r, std::unordered_set<T> &out, context &ctx) {
                size_t index = 0;
                size_t size = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::unordered_set", "Varint", tools::concat("Elem ", index),
                        tools::concat("length=", size)
                    };
                });

                size = detail::read_varint<size_t>(r);
                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_container_size) throw errors::container_too_large(size, ctx);

                out.clear();
                for (; index < size; ++index) {
                    T elem;
                    DefaultSerializer<T>::read(r, elem, ctx);
                    out.emplace(std::move(elem));
                }
            }
        };

        // std::array
        // [Elem 0][Elem 1]...
        template<typename T, size_t N> requires types::default_serializable<T>
        struct Serializer<std::array<T, N>, proto::Fixed<> > {
            static constexpr std::string t_str = tools::concat("std::array<", N, ">");

            static void write(io::Writer auto &w, const std::array<T, N> &v, context &ctx) {
                size_t index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        t_str.c_str(), "Fixed<>", tools::concat("Elem ", index)
                    };
                });

                for (; index < N; ++index) {
                    DefaultSerializer<T>::write(w, v[index], ctx);
                }
            }

            static void read(io::Reader auto &r, std::array<T, N> &out, context &ctx) {
                size_t index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        t_str.c_str(), "Fixed<>", tools::concat("Elem ", index)
                    };
                });

                for (; index < N; ++index) {
                    DefaultSerializer<T>::read(r, out[index], ctx);
                }
            }
        };


        // --- Serializers for Structured Types -------------------------------
        // 结构化类型的序列化器

        // pair
        // [Field 1][Field 2]
        template<typename T1, typename T2> requires (types::default_serializable<T1> &&
                                                     types::default_serializable<T2>)
        struct Serializer<std::pair<T1, T2>, proto::Fixed<> > {
            static void write(io::Writer auto &w, const std::pair<T1, T2> &v, context &ctx) {
                [[maybe_unused]] bool is_second = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{"std::pair", "Fixed<>", (is_second ? "Second" : "First")};
                });

                DefaultSerializer<T1>::write(w, v.first, ctx);
                is_second = true;
                DefaultSerializer<T2>::write(w, v.second, ctx);
            }

            static void read(io::Reader auto &r, std::pair<T1, T2> &out, context &ctx) {
                [[maybe_unused]] bool is_second = false;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{"std::pair", "Fixed<>", (is_second ? "Second" : "First")};
                });

                DefaultSerializer<T1>::read(r, out.first, ctx);
                is_second = true;
                DefaultSerializer<T2>::read(r, out.second, ctx);
            }
        };

        // tuple
        // [Field 1][Field 2]...
        template<typename... Ts> requires types::all_serializable<Ts...>
        struct Serializer<std::tuple<Ts...>, proto::Fixed<> > {
            static void write(io::Writer auto &w, const std::tuple<Ts...> &v, context &ctx) {
                [[maybe_unused]] size_t field_index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::tuple", "Fixed<>", tools::concat("Field ", field_index)
                    };
                });

                std::apply([&](const auto &... field) {
                    (
                        (
                            field_index = (&field - &std::get<0>(v)),
                            DefaultSerializer<std::decay_t<decltype(field)> >::write(w, field, ctx)
                        ),
                        ...
                    );
                }, v);
            }

            static void read(io::Reader auto &r, std::tuple<Ts...> &out, context &ctx) {
                [[maybe_unused]] size_t field_index = 0;
                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        "std::tuple", "Fixed<>", tools::concat("Field ", field_index)
                    };
                });

                std::apply([&](auto &... field) {
                    (
                        (
                            field_index = (&field - &std::get<0>(out)),
                            DefaultSerializer<std::decay_t<decltype(field)> >::read(r, field, ctx)
                        ),
                        ...
                    );
                }, out);
            }
        };


        // --- Serializers for Schemas ----------------------------------------
        // 模式串序列化器

        // Field access helper
        namespace detail {
            template<typename T, typename Entry>
            static void write_fields(io::Writer auto &w, const T &v, context &ctx, const Entry &entry,
                                     const char *type_name, const char *proto_name) {
                [[maybe_unused]] const char *current_field = nullptr;

                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        .type = type_name,
                        .proto = proto_name,
                        .child_label = current_field
                                           ? std::optional(tools::concat("Field \"", current_field, "\""))
                                           : std::nullopt,
                        .details = tools::concat("exact version ", entry.version)
                    };
                });

                std::apply([&](const auto &... field) {
                    (
                        (
                            current_field = field.name,
                            Serializer<
                                typename std::decay_t<decltype(field)>::field_type,
                                typename std::decay_t<decltype(field)>::protocol
                            >::write(w, v.*(field.ptr), ctx)
                        ),
                        ...
                    );
                }, entry.fields);
            }

            template<typename T, typename Entry>
            static void read_fields(io::Reader auto &r, T &out, context &ctx, const Entry &entry,
                                    const char *type_name, const char *proto_name) {
                [[maybe_unused]] const char *current_field = nullptr;

                auto g = ctx.guard<true, false>([&] {
                    return errors::value_frame{
                        .type = type_name,
                        .proto = proto_name,
                        .child_label = current_field
                                           ? std::optional(tools::concat("Field \"", current_field, "\""))
                                           : std::nullopt,
                        .details = tools::concat("exact version ", entry.version)
                    };
                });

                std::apply([&](const auto &... field) {
                    (
                        (
                            current_field = field.name,
                            Serializer<
                                typename std::decay_t<decltype(field)>::field_type,
                                typename std::decay_t<decltype(field)>::protocol
                            >::read(r, out.*(field.ptr), ctx)
                        ),
                        ...
                    );
                }, entry.fields);
            }
        }

        // Compile-Time specified
        template<typename T, size_t V> requires types::has_schema<T>
        struct Serializer<T, proto::Schema<V> > {
            static constexpr size_t exact_index = schema::match_schema_index<T, V>();
            static_assert(exact_index != SIZE_MAX, "bsp: no such schema under version V");

            static constexpr const auto &entry = std::get<exact_index>(schema::SchemaSet<T>::schemas);
            static constexpr std::string p_str =
                    tools::concat("Schema<", V == SIZE_MAX ? "MAX" : std::to_string(V), ">");

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                detail::write_fields<T>(w, v, ctx, entry,
                                        schema::SchemaSet<T>::Typename, p_str.c_str());
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                detail::read_fields<T>(r, out, ctx, entry,
                                       schema::SchemaSet<T>::Typename, p_str.c_str());
            }
        };

        // Runtime dispatch
        template<typename T> requires types::has_schema<T>
        struct Serializer<T, proto::DynSchema> {
            static void write(io::Writer auto &w, const T &v, context &ctx) {
                const size_t exact_index = schema::match_schema_index<T>(ctx.opt.target_schema_version);

                if (exact_index == SIZE_MAX)
                    throw errors::make(errors::code::invalid_index, ctx,
                                       tools::concat("no such schema under version", ctx.opt.target_schema_version));

                const auto &entry = std::get<exact_index>(schema::SchemaSet<T>::schemas);
                detail::write_fields<T>(w, v, ctx, entry,
                                        schema::SchemaSet<T>::Typename, "DynSchema");
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                const size_t exact_index = schema::match_schema_index<T>(ctx.opt.target_schema_version);

                if (exact_index == SIZE_MAX)
                    throw errors::make(errors::code::invalid_index, ctx,
                                       tools::concat("no such schema under version", ctx.opt.target_schema_version));

                const auto &entry = std::get<exact_index>(schema::SchemaSet<T>::schemas);
                detail::read_fields<T>(r, out, ctx, entry,
                                       schema::SchemaSet<T>::Typename, "DynSchema");
            }
        };

        // --- Serializers for Variable Types ---------------------------------
        // 可变类型的序列化器

        // std::optional
        // [0/1 Bool](T if having value)
        template<typename T> requires types::default_serializable<T>
        struct Serializer<std::optional<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::optional<T> &v, context &ctx) {
                const bool has = v.has_value();
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("std::optional"));

                w.write_byte(static_cast<uint8_t>(has));
                if (has) {
                    DefaultSerializer<T>::write(w, v.value(), ctx);
                }
            }

            static void read(io::Reader auto &r, std::optional<T> &out, context &ctx) {
                bool has = false;
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("std::optional"));

                const uint8_t has_byte = r.read_byte();
                if (ctx.opt.policy <= errors::error_policy::STRICT && has_byte > 1)
                    throw errors::invalid_bool(has_byte, ctx);
                has = static_cast<bool>(has_byte);

                if (has) {
                    T value{};
                    DefaultSerializer<T>::read(r, value, ctx);
                    out = std::move(value);
                } else {
                    out = std::nullopt;
                }
            }
        };

        // std::variant
        // [Varint index][Selected T]
        template<typename... Ts> requires types::all_serializable<Ts...>
        struct Serializer<std::variant<Ts...>, proto::Varint> {
            static void write(io::Writer auto &w, const std::variant<Ts...> &v, context &ctx) {
                const size_t which = v.index();
                auto g = ctx.guard<false, false>([&] {
                    return errors::wrapper_frame{
                        tools::concat("std::variant (index=", which, ")")
                    };
                });

                detail::write_varint(w, which);

                std::visit([&](const auto &value) {
                    using ValueType = std::decay_t<decltype(value)>;
                    DefaultSerializer<ValueType>::write(w, value, ctx);
                }, v);
            }

            static void read(io::Reader auto &r, std::variant<Ts...> &out, context &ctx) {
                size_t which = SIZE_MAX;

                auto g = ctx.guard<false, false>([&] {
                    return errors::wrapper_frame{
                        tools::concat("std::variant (index=", which, ")")
                    };
                });

                which = detail::read_varint<size_t>(r);

                if (which >= sizeof...(Ts))
                    throw errors::make(errors::code::invalid_index, ctx,
                                       tools::concat("variant index ", which, " out of range"));

                [&]<size_t... Is>(std::index_sequence<Is...>) {
                    return (
                        (
                            which == Is
                                ? [&] {
                                    std::decay_t<decltype(std::get<Is>(std::declval<std::variant<Ts...> >()))> value{};
                                    DefaultSerializer<
                                        std::decay_t<decltype(std::get<Is>(std::declval<std::variant<Ts...> >()))>
                                    >::read(r, value, ctx);
                                    out.template emplace<Is>(std::move(value));
                                }()
                                : void()
                        ),
                        ...
                    );
                }(std::make_index_sequence<sizeof...(Ts)>{});
            }
        };


        // --- Serializers for Types with Specified Protocol ------------------
        // 指定协议的类型的序列化器

        // types::PVal
        // [T with prototag ProtocolT]
        template<typename T, typename ProtocolT, typename Protocol>
            requires (!std::is_base_of_v<proto::WrapperProto, Protocol> &&
                      types::serializable<T, ProtocolT>)
        struct Serializer<types::PVal<T, ProtocolT>, Protocol> {
            static void write(io::Writer auto &w, const types::PVal<T, ProtocolT> &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("PVal"));
                Serializer<T, ProtocolT>::write(w, v.value, ctx);
            }

            static void read(io::Reader auto &r, types::PVal<T, ProtocolT> &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("PVal"));
                Serializer<T, ProtocolT>::read(r, v.value, ctx);
            }
        };

        // types::CVal
        // [Unknown in compile-time, defined in CVal]
        template<typename T> requires std::is_base_of_v<types::CVal, T>
        struct Serializer<T, proto::CVal> {
            static void write(io::AnyWriter &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("Inside CVal:"));
                v.write(w, ctx);
            }

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("Inside CVal:"));
                io::AnyWriter any_w(w);
                v.write(any_w, ctx);
            }

            static void read(io::AnyReader &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("Inside CVal:"));
                out.read(r, ctx);
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("Inside CVal:"));
                io::AnyReader any_r(r);
                out.read(any_r, ctx);
            }
        };


        // --- Serializers for Trivially-Serializable Types -------------------
        // 平凡可复制类型的序列化器

        template<typename T> requires types::trivially_serializable<T>
        struct Serializer<T, proto::Trivial> {
            static constexpr const char *t_str = detail::literal_name<T>();

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Trivial"));
                w.write_bytes(reinterpret_cast<const uint8_t *>(&v), sizeof(T));
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str, "Trivial"));
                r.read_bytes(reinterpret_cast<uint8_t *>(&out), sizeof(T));
            }
        };

        template<typename T> requires types::trivially_serializable<T>
        struct Serializer<std::vector<T>, proto::Trivial> {
            static void write(io::Writer auto &w, const std::vector<T> &v, context &ctx) {
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "std::vector", "Trivial", std::nullopt,
                        tools::concat("length=", v.size())
                    };
                });
                detail::write_varint(w, v.size());
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), v.size() * sizeof(T));
            }

            static void read(io::Reader auto &r, std::vector<T> &out, context &ctx) {
                size_t size = 0;
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "std::vector", "Trivial", std::nullopt,
                        tools::concat("length=", size)
                    };
                });
                size = detail::read_varint<size_t>(r);
                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (size > ctx.opt.max_container_size) throw errors::container_too_large(size, ctx);

                out.resize(size);
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), size * sizeof(T));
            }
        };

        // Bit-compressed with Little-endian style
        // Have the same behaviour on different platforms.
        template<>
        struct Serializer<std::vector<bool>, proto::Trivial> {
            static void write(io::Writer auto &w, const std::vector<bool> &v, context &ctx) {
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "std::vector<bool>", "Trivial", std::nullopt,
                        tools::concat("bit count=", v.size())
                    };
                });
                detail::write_varint(w, v.size());

                const size_t byte_count = (v.size() + 7) / 8;
                for (size_t i = 0; i < byte_count; ++i) {
                    uint8_t byte = 0;
                    for (size_t bit = 0; bit < 8 && (i * 8 + bit) < v.size(); ++bit) {
                        if (v[i * 8 + bit])
                            byte |= (1u << bit);
                    }
                    w.write_byte(byte);
                }
            }

            static void read(io::Reader auto &r, std::vector<bool> &out, context &ctx) {
                size_t bit_size = 0;
                auto g = ctx.guard<false, false>([&] {
                    return errors::value_frame{
                        "std::vector<bool>", "Trivial", std::nullopt,
                        tools::concat("bit count=", bit_size)
                    };
                });
                bit_size = detail::read_varint<size_t>(r);
                if (ctx.opt.policy <= errors::error_policy::MEDIUM)
                    if (bit_size > ctx.opt.max_container_size) throw errors::container_too_large(bit_size, ctx);

                out.resize(bit_size);
                const size_t byte_count = (bit_size + 7) / 8;
                for (size_t i = 0; i < byte_count; ++i) {
                    const uint8_t byte = r.read_byte();
                    for (size_t bit = 0; bit < 8 && (i * 8 + bit) < bit_size; ++bit) {
                        out[i * 8 + bit] = (byte & (1u << bit)) != 0;
                    }
                }
            }
        };

        template<typename T, size_t N> requires types::trivially_serializable<T>
        struct Serializer<std::array<T, N>, proto::Trivial> {
            static constexpr std::string t_str = tools::concat("std::array<", N, ">");

            static void write(io::Writer auto &w, const std::array<T, N> &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str.c_str(), "Trivial"));
                w.write_bytes(reinterpret_cast<const uint8_t *>(v.data()), N * sizeof(T));
            }

            static void read(io::Reader auto &r, std::array<T, N> &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::value_frame_gen(t_str.c_str(), "Trivial"));
                r.read_bytes(reinterpret_cast<uint8_t *>(out.data()), N * sizeof(T));
            }
        };


        // --- Serializers for Pointers ---------------------------------------
        // 指针的序列化器

        template<typename T> requires types::default_serializable<T>
        struct Serializer<T *, proto::Varint> {
            static void write(io::Writer auto &w, const T *const &v, context &ctx) {
                auto g = ctx.guard<true, false>(errors::wrapper_frame_gen("ptr"));

                const bool non_null = v != nullptr;
                w.write_byte(static_cast<uint8_t>(non_null));

                if (non_null) {
                    DefaultSerializer<T>::write(w, *v, ctx);
                }
            }

            static void read(io::Reader auto &r, T *&out, context &ctx) {
                auto g = ctx.guard<true, false>(errors::wrapper_frame_gen("ptr"));

                const uint8_t non_null_byte = r.read_byte();
                if (ctx.opt.policy <= errors::error_policy::STRICT && non_null_byte > 1)
                    throw errors::invalid_bool(non_null_byte, ctx);
                const bool non_null = static_cast<bool>(non_null_byte);

                if (non_null) {
                    out = new T{};
                    DefaultSerializer<T>::read(r, *out, ctx);
                } else {
                    delete out;
                    out = nullptr;
                }
            }
        };

        template<typename T> requires types::default_serializable<T>
        struct Serializer<std::unique_ptr<T>, proto::Varint> {
            static void write(io::Writer auto &w, const std::unique_ptr<T> &v, context &ctx) {
                auto g = ctx.guard<true, false>(errors::wrapper_frame_gen("std::unique_ptr"));

                const bool non_null = v != nullptr;
                w.write_byte(static_cast<uint8_t>(non_null));

                if (non_null) {
                    DefaultSerializer<T>::write(w, *v, ctx);
                }
            }

            static void read(io::Reader auto &r, std::unique_ptr<T> &out, context &ctx) {
                auto g = ctx.guard<true, false>(errors::wrapper_frame_gen("std::unique_ptr"));

                const uint8_t non_null_byte = r.read_byte();
                if (ctx.opt.policy <= errors::error_policy::STRICT && non_null_byte > 1)
                    throw errors::invalid_bool(non_null_byte, ctx);
                const bool non_null = static_cast<bool>(non_null_byte);

                if (non_null) {
                    out = std::make_unique<T>();
                    DefaultSerializer<T>::read(r, *out, ctx);
                } else {
                    out.reset();
                }
            }
        };


        // ~~~ Serializers for Protocols ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // 为特定协议设计的序列化器

        // --- Default Fallback -----------------------------------------------
        // 默认协议映射
        template<typename T> requires (!std::is_same_v<proto::DefaultProtocol_t<T>, proto::Default> &&
                                       types::default_serializable<T>)
        struct Serializer<T, proto::Default> {
            static void write(io::Writer auto &w, const T &v, context &ctx) {
                DefaultSerializer<T>::write(w, v, ctx);
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                DefaultSerializer<T>::read(r, out, ctx);
            }
        };

        // --- Serializers for Length-Limited Protocols -----------------------
        // 限定长度的协议的序列化器

        // proto::Limited

        // [Varint length][Inner payload]
        template<typename T, typename Inner> requires types::serializable<T, Inner>
        struct Serializer<T, proto::Limited<proto::Varint, Inner> > {
            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("Limited<Varint>"));

                // Write to temporary buffer to know the size
                io::BufferWriter tmp;
                Serializer<T, Inner>::write(tmp, v, ctx);

                detail::write_varint(w, tmp.buf.size());
                w.write_bytes(tmp.buf.data(), tmp.buf.size());
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                size_t len = 0;
                auto g = ctx.guard<false, false>([&] {
                    return errors::wrapper_frame{tools::concat("Limited<Varint> size=", len)};
                });

                len = detail::read_varint<size_t>(r);
                io::LimitedReader limited_r(r, len);
                Serializer<T, Inner>::read(limited_r, out, ctx);
            }
        };

        // [Inner payload]
        template<typename T, size_t N, typename Inner> requires types::serializable<T, Inner>
        struct Serializer<T, proto::Limited<proto::Fixed<N>, Inner> > {
            static constexpr std::string p_str = tools::concat("Limited<Fixed<", N, ">>");

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen(p_str.c_str()));

                io::BufferWriter tmp;
                Serializer<T, Inner>::write(tmp, v, ctx);

                if (tmp.buf.size() > N)
                    throw errors::fixed_size_mismatch(N, tmp.buf.size(), ctx);

                detail::write_varint(w, tmp.buf.size());
                w.write_bytes(tmp.buf.data(), tmp.buf.size());
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen(p_str.c_str()));

                const size_t len = detail::read_varint<size_t>(r);
                if (len > N)
                    throw errors::fixed_size_mismatch(N, len, ctx);

                io::LimitedReader limited_r(r, len);
                Serializer<T, Inner>::read(limited_r, out, ctx);
            }
        };

        // proto::Forced

        // [Varint length][Inner payload padded with zero]
        template<typename T, typename Inner> requires types::serializable<T, Inner>
        struct Serializer<T, proto::Forced<proto::Varint, Inner> > {
            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen("Forced<Varint>"));

                // Write to temp buffer, then declare the length
                io::BufferWriter tmp;
                Serializer<T, Inner>::write(tmp, v, ctx);

                detail::write_varint(w, tmp.buf.size());
                w.write_bytes(tmp.buf.data(), tmp.buf.size());
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                size_t len = 0;
                auto g = ctx.guard<false, false>([&] {
                    return errors::wrapper_frame{tools::concat("Limited<Varint> size=", len)};
                });

                len = detail::read_varint<size_t>(r);

                io::LimitedReader limited_r(r, len);
                Serializer<T, Inner>::read(limited_r, out, ctx);
                limited_r.skip_remaining();
            }
        };

        // [Inner payload padded with zero]
        template<typename T, size_t N, typename Inner> requires types::serializable<T, Inner>
        struct Serializer<T, proto::Forced<proto::Fixed<N>, Inner> > {
            static constexpr std::string p_str = tools::concat("Forced<Fixed<",N,">>");

            static void write(io::Writer auto &w, const T &v, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen(p_str.c_str()));

                io::LimitedWriter limited_w(w, N);
                Serializer<T, Inner>::write(limited_w, v, ctx);
                limited_w.pad_zero();
            }

            static void read(io::Reader auto &r, T &out, context &ctx) {
                auto g = ctx.guard<false, false>(errors::wrapper_frame_gen(p_str.c_str()));

                io::LimitedReader limited_r(r, N);
                Serializer<T, Inner>::read(limited_r, out, ctx);
                limited_r.skip_remaining();
            }
        };
    }


    /* ========================================================================
     * Public API
     * 开放使用的 API
     * ======================================================================== */

    // === Serialize Functions ================================================
    // 序列化函数

    // These functions are backward compatible and more convenient.
    // But they SHOULD NOT be used in serializers!

    template<typename Proto, typename T> requires types::serializable<T, Proto>
    void write(io::Writer auto &w, const T &v, context &ctx) {
        serialize::Serializer<T, Proto>::write(w, v, ctx);
    }

    template<typename T> requires types::default_serializable<T>
    void write(io::Writer auto &w, const T &v, context &ctx) {
        serialize::DefaultSerializer<T>::write(w, v, ctx);
    }

    template<typename Proto, typename T> requires types::serializable<T, Proto>
    void write(io::Writer auto &w, const T &v) {
        auto ctx = context::get_default_context();
        serialize::Serializer<T, Proto>::write(w, v, ctx);
    }

    template<typename T> requires types::default_serializable<T>
    void write(io::Writer auto &w, const T &v) {
        auto ctx = context::get_default_context();
        serialize::DefaultSerializer<T>::write(w, v, ctx);
    }


    template<typename Proto, typename T> requires types::serializable<T, Proto>
    void read(io::Reader auto &r, T &out, context &ctx) {
        serialize::Serializer<T, Proto>::read(r, out, ctx);
    }

    template<typename T> requires types::default_serializable<T>
    void read(io::Reader auto &r, T &out, context &ctx) {
        serialize::DefaultSerializer<T>::read(r, out, ctx);
    }

    template<typename Proto, typename T> requires types::serializable<T, Proto>
    void read(io::Reader auto &r, T &out) {
        auto ctx = context::get_default_context();
        serialize::Serializer<T, Proto>::read(r, out, ctx);
    }

    template<typename T> requires types::default_serializable<T>
    void read(io::Reader auto &r, T &out) {
        auto ctx = context::get_default_context();
        serialize::DefaultSerializer<T>::read(r, out, ctx);
    }

    template<typename Proto, typename T> requires types::serializable<T, Proto>
    [[nodiscard]] T read(io::Reader auto &r) {
        T out{};
        auto ctx = context::get_default_context();
        serialize::Serializer<T, Proto>::read(r, out, ctx);
        return out;
    }

    template<typename T> requires types::default_serializable<T>
    [[nodiscard]] T read(io::Reader auto &r) {
        T out{};
        auto ctx = context::get_default_context();
        serialize::DefaultSerializer<T>::read(r, out, ctx);
        return out;
    }

    // === Aliases ============================================================
    // 类型别名

    template<typename T, typename P>
    using PVal = types::PVal<T, P>;
} // namespace bsp


/* ============================================================================
 * Macros
 * 宏
 * ============================================================================ */

#define BSP_DEFAULT_PROTO(T, P)         \
    namespace bsp {                     \
        namespace proto {               \
            template<>                  \
            struct DefaultProtocol<T> { \
                using type = P;         \
            };                          \
        }                               \
    }

#define BSP_FIELD_P(F, P) \
    ::bsp::schema::Field<Type, decltype(std::declval<Type>().F), P>{#F, &Type::F}

#define BSP_FIELD(F) \
    BSP_FIELD_P(F, ::bsp::proto::Default)

#define BSP_SCHEMA_V(V, ...) \
    ::bsp::schema::make_schema_entry<V>(__VA_ARGS__)

#define BSP_SCHEMA(...) \
    BSP_SCHEMA_V(0, __VA_ARGS__)

#define BSP_SCHEMA_SET(T, ...)                                                                          \
    namespace bsp {                                                                                     \
        namespace schema {                                                                              \
            template<>                                                                                  \
            struct SchemaSet<T> {                                                                       \
                using Type = T;                                                                         \
                static constexpr const char* Typename = #T;                                             \
                                                                                                        \
                static constexpr auto schemas = std::make_tuple(__VA_ARGS__);                           \
                static constexpr inline size_t schema_count = std::tuple_size_v<decltype(schemas)>;     \
                static constexpr auto versions = []<size_t... Is>(std::index_sequence<Is...>) {         \
                    return std::integer_sequence<size_t, std::get<Is>(schemas).version...>{};           \
                }(std::make_index_sequence<schema_count>{});                                            \
                                                                                                        \
                static_assert(schema_count != 0, "there must be at least 1 schema in SchemaSet");       \
                static_assert(::bsp::schema::validate_schemas(schemas), "schema versions must ascend"); \
            };                                                                                          \
        }                                                                                               \
        namespace proto {                                                                               \
            template<>                                                                                  \
            struct DefaultProtocol<T> {                                                                 \
                using type = ::bsp::proto::Schema<SIZE_MAX>;                                            \
            }                                                                                           \
        }                                                                                               \
    }

#endif
