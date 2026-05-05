#include "../include/bsp.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <variant>
#include <bitset>
#include <array>
#include <memory>

// ============================================================================
// 测试用的结构体，带 Schema 定义
// ============================================================================

struct Person {
    std::string name;
    int age;
    bool active;
    std::optional<std::string> email;
    std::vector<int> scores;
};

// 定义 Schema 字段
#define PERSON_FIELDS_V1 \
    BSP_FIELD(name),     \
    BSP_FIELD(age),      \
    BSP_FIELD(active)

#define PERSON_FIELDS_V2 \
    PERSON_FIELDS_V1,    \
    BSP_FIELD(email),    \
    BSP_FIELD(scores)

// 注册 SchemaSet
BSP_SCHEMA_SET(Person,
               BSP_SCHEMA_V(1, PERSON_FIELDS_V1),
               BSP_SCHEMA_V(2, PERSON_FIELDS_V2)
);

// ============================================================================
// 另一个测试用的结构体，使用 PVal 覆盖协议
// ============================================================================

struct Data {
    uint32_t id;
    std::vector<uint8_t> payload;
};

BSP_SCHEMA_SET(Data,
               BSP_SCHEMA(BSP_FIELD(id), BSP_FIELD(payload))
);

// ============================================================================
// 测试用的 CVal 派生类
// ============================================================================

class MyCVal : public bsp::types::CVal {
public:
    int x = 0;
    std::string s;

    void write(bsp::io::AnyWriter &w, bsp::context &ctx) const override {
        bsp::write(w, x, ctx);
        bsp::write(w, s, ctx);
    }

    void read(bsp::io::AnyReader &r, bsp::context &ctx) override {
        bsp::read(r, x, ctx);
        bsp::read(r, s, ctx);
    }
};

// ============================================================================
// 辅助函数：检查两个 vector 是否相等
// ============================================================================

template<typename T>
bool vec_eq(const std::vector<T> &a, const std::vector<T> &b) {
    return a == b;
}

// ============================================================================
// 主测试函数：编译性 + 基础功能验证
// ============================================================================

int main() {
    using namespace bsp;
    using namespace bsp::io;
    using namespace bsp::types;

    std::cout << "=== BSP Library Compilation Test ===\n";

    // ------------------------------------------------------------------------
    // 1. 基础类型测试 (bool, int, float)
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 1] Basic types\n";

        BufferWriter bw;

        bool b_val = true;
        int32_t i_val = -12345;
        uint64_t u_val = 9876543210ULL;
        float f_val = 3.14159f;
        double d_val = 2.718281828459045;

        write(bw, b_val);
        write(bw, i_val);
        write(bw, u_val);
        write(bw, f_val);
        write(bw, d_val);

        BytesReader br(bw.buf);

        bool b_out{};
        int32_t i_out{};
        uint64_t u_out{};
        float f_out{};
        double d_out{};

        read(br, b_out);
        read(br, i_out);
        read(br, u_out);
        read(br, f_out);
        read(br, d_out);

        assert(b_out == b_val);
        assert(i_out == i_val);
        assert(u_out == u_val);
        assert(f_out == f_val);
        assert(d_out == d_val);

        std::cout << "  Basic types passed\n";
    }

    // ------------------------------------------------------------------------
    // 2. 字符串与字节数组
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 2] String and bytes\n";

        BufferWriter bw;
        std::string str = "Hello, BSP!";
        bytes byte_arr = {0x01, 0x02, 0x03, 0xFF};

        write(bw, str);
        write(bw, byte_arr);

        BytesReader br(bw.buf);
        std::string str_out;
        bytes byte_out;

        read(br, str_out);
        read(br, byte_out);

        assert(str_out == str);
        assert(byte_out == byte_arr);

        std::cout << "  String and bytes passed\n";
    }

    // ------------------------------------------------------------------------
    // 3. 容器: vector, map, set, array, bitset
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 3] Containers\n";

        BufferWriter bw;

        std::vector<int> vec = {1, 2, 3, 4, 5};
        std::map<std::string, int> mp = {{"one", 1}, {"two", 2}};
        std::set<double> st = {1.1, 2.2, 3.3};
        std::array<float, 3> arr = {1.0f, 2.0f, 3.0f};
        std::bitset<8> bits = 0b10101010;

        write(bw, vec);
        write(bw, mp);
        write(bw, st);
        write(bw, arr);
        write(bw, bits);

        BytesReader br(bw.buf);

        std::vector<int> vec_out;
        std::map<std::string, int> mp_out;
        std::set<double> st_out;
        std::array<float, 3> arr_out{};
        std::bitset<8> bits_out;

        read(br, vec_out);
        read(br, mp_out);
        read(br, st_out);
        read(br, arr_out);
        read(br, bits_out);

        assert(vec_out == vec);
        assert(mp_out == mp);
        assert(st_out == st);
        assert(arr_out == arr);
        assert(bits_out == bits);

        std::cout << "  Containers passed\n";
    }

    // ------------------------------------------------------------------------
    // 4. 可选值与变体
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 4] Optional and Variant\n";

        BufferWriter bw;

        std::optional<int> opt1 = 42;
        std::optional<int> opt2 = std::nullopt;
        std::variant<int, double, std::string> var1 = 3.14;
        std::variant<int, double, std::string> var2 = std::string("hello");

        write(bw, opt1);
        write(bw, opt2);
        write(bw, var1);
        write(bw, var2);

        BytesReader br(bw.buf);

        std::optional<int> opt1_out;
        std::optional<int> opt2_out;
        std::variant<int, double, std::string> var1_out;
        std::variant<int, double, std::string> var2_out;

        read(br, opt1_out);
        read(br, opt2_out);
        read(br, var1_out);
        read(br, var2_out);

        assert(opt1_out.has_value() && opt1_out.value() == 42);
        assert(!opt2_out.has_value());
        assert(std::holds_alternative<double>(var1_out) && std::get<double>(var1_out) == 3.14);
        assert(std::holds_alternative<std::string>(var2_out) && std::get<std::string>(var2_out) == "hello");

        std::cout << "  Optional and Variant passed\n";
    }

    // ------------------------------------------------------------------------
    // 5. 指针类型 (原始指针和 unique_ptr)
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 5] Pointers\n";

        BufferWriter bw;

        int *raw_ptr = new int(999);
        std::unique_ptr<int> uptr = std::make_unique<int>(888);
        int *null_ptr = nullptr;

        write(bw, raw_ptr);
        write(bw, uptr);
        write(bw, null_ptr);

        BytesReader br(bw.buf);

        int *raw_out = nullptr;
        std::unique_ptr<int> uptr_out;
        int *null_out = nullptr;

        read(br, raw_out);
        read(br, uptr_out);
        read(br, null_out);

        assert(raw_out != nullptr && *raw_out == 999);
        assert(uptr_out != nullptr && *uptr_out == 888);
        assert(null_out == nullptr);

        delete raw_out; // read 内部分配了新内存
        delete raw_ptr; // 原指针也需要释放
        delete null_ptr; // nullptr delete 安全

        std::cout << "  Pointers passed\n";
    }

    // ------------------------------------------------------------------------
    // 6. Trivial 协议 (批量内存拷贝)
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 6] Trivial protocol\n";

        BufferWriter bw;

        std::vector<int> vec = {100, 200, 300, 400};
        using TrivialVec = PVal<std::vector<int>, proto::Trivial>;
        TrivialVec tv{vec};

        write(bw, tv);

        BytesReader br(bw.buf);
        TrivialVec tv_out;
        read(br, tv_out);

        assert(tv_out.value == vec);

        std::cout << "  Trivial protocol passed\n";
    }

    // ------------------------------------------------------------------------
    // 7. Schema 结构体 (Person, with versions)
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 7] Schema-based struct (Person)\n";

        Person p1;
        p1.name = "Alice";
        p1.age = 30;
        p1.active = true;
        p1.email = "alice@example.com";
        p1.scores = {95, 87, 92};

        // 写入时使用 Schema<2> 确保包含所有字段
        BufferWriter bw;
        write<proto::Schema<2> >(bw, p1);

        BytesReader br(bw.buf);

        Person p2;
        context ctx = context::get_default_context();
        ctx.opt.target_schema_version = 2;
        read<proto::DynSchema>(br, p2, ctx);

        assert(p2.name == p1.name);
        assert(p2.age == p1.age);
        assert(p2.active == p1.active);
        assert(p2.email == p1.email);
        assert(p2.scores == p1.scores);

        // 测试向后兼容：用旧版本 Schema 读取新版本数据（应该只读取部分字段）
        // 注意：这需要数据严格按照版本顺序布局，此处仅为验证编译性
        {
            BytesReader br2(bw.buf);
            Person p3;
            read<proto::Schema<1> >(br2, p3);
            std::cout << "  Schema version compatibility compiled OK\n";
        }

        std::cout << "  Schema-based struct passed\n";
    }

    // ------------------------------------------------------------------------
    // 8. CVal 运行时多态
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 8] CVal runtime polymorphic\n";

        BufferWriter bw;
        MyCVal cv;
        cv.x = 12345;
        cv.s = "runtime value";

        write(bw, cv); // 使用 proto::CVal

        BytesReader br(bw.buf);
        MyCVal cv_out;
        read(br, cv_out);

        assert(cv_out.x == cv.x);
        assert(cv_out.s == cv.s);

        std::cout << "  CVal passed\n";
    }

    // ------------------------------------------------------------------------
    // 9. 限定长度协议 (Limited / Forced)
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 9] Limited and Forced protocols\n";

        // Limited<Varint, ...>
        {
            BufferWriter bw;
            auto limited_val = PVal<std::string, proto::Limited<proto::Varint, proto::Default> >{"short"};
            write(bw, limited_val);
            BytesReader br(bw.buf);
            PVal<std::string, proto::Limited<proto::Varint, proto::Default> > out;
            read(br, out);
            assert(out.value == "short");
        }

        // Forced<Fixed<16>, ...>
        {
            BufferWriter bw;
            auto forced_val = PVal<std::string, proto::Forced<proto::Fixed<16>, proto::Default> >{"hi"};
            write(bw, forced_val);
            BytesReader br(bw.buf);
            PVal<std::string, proto::Forced<proto::Fixed<16>, proto::Default> > out;
            read(br, out);
            assert(out.value == "hi");
        }

        std::cout << "  Limited/Forced protocols passed\n";
    }

    // ------------------------------------------------------------------------
    // 10. 流式 I/O (std::istream / std::ostream)
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 10] Stream I/O\n";

        std::stringstream ss;
        StreamWriter sw(ss);
        StreamReader sr(ss);

        std::vector<int> data = {10, 20, 30, 40, 50};
        write(sw, data);

        std::vector<int> data_out;
        read(sr, data_out);

        assert(data_out == data);

        std::cout << "  Stream I/O passed\n";
    }

    // ------------------------------------------------------------------------
    // 11. 自定义错误策略与 Traceback
    // ------------------------------------------------------------------------
    {
        std::cout << "\n[Test 11] Error policy and traceback\n";

        context ctx;
        ctx.sf.policy = errors::error_policy::STRICT;
        ctx.sf.max_string_size = 10;

        BufferWriter bw;
        std::string long_str = "this is a very long string exceeding the limit";

        write(bw, long_str); // 写入没问题

        BytesReader br(bw.buf);
        std::string out_str;

        try {
            read(br, out_str, ctx); // 读取时会触发 string_too_large
            assert(false); // 不应该到达这里
        } catch (const errors::error &e) {
            std::cout << "  Caught expected error: " << e.what() << std::endl;
            std::cout << e.format_tb() << std::endl;
        }

        std::cout << "  Error policy and traceback passed\n";
    }

    std::cout << "\n=== All compilation tests passed successfully ===\n";
    return 0;
}
