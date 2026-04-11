// bsp_test_standalone.cpp
// 编译: g++ -std=c++20 bsp_test_standalone.cpp -o bsp_test
// 运行: ./bsp_test

#include "../include/bsp.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <tuple>
#include <optional>
#include <variant>
#include <bitset>
#include <memory>
#include <cmath>

using namespace bsp;
using namespace std::string_literals;

// 简单的测试计数器
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed: " #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            ++tests_failed; \
        } else { \
            ++tests_passed; \
        } \
    } while (0)

#define TEST_ASSERT_THROWS(expr, exception_type) \
    do { \
        bool caught = false; \
        try { expr; } catch (const exception_type&) { caught = true; } catch (...) {} \
        if (!caught) { \
            std::cerr << "Expected exception " #exception_type << " not thrown at " << __FILE__ << ":" << __LINE__ << std::endl; \
            ++tests_failed; \
        } else { \
            ++tests_passed; \
        } \
    } while (0)

// 辅助函数：重置全局选项
void reset_options_to_default() {
    options::reset();
}

// 测试用结构体
struct Point {
    int x;
    int y;
    std::string name;
};

BSP_DEFAULT_SCHEMA(Point,
    BSP_FIELD(x),
    BSP_FIELD(y),
    BSP_FIELD(name)
)

struct Player {
    uint32_t id;
    std::string nickname;
    float score;
};

BSP_DEFAULT_SCHEMA(Player,
    BSP_FIELD_P(id, proto::Varint),
    BSP_FIELD(nickname),
    BSP_FIELD(score)
)

class Animal : public types::CVal {
public:
    std::string species;
    int age;

    Animal() = default;
    Animal(std::string s, int a) : species(std::move(s)), age(a) {}

    void write(io::AnyWriter& w) const override {
        bsp::write<proto::Default>(w, species);
        bsp::write<proto::Default>(w, age);
    }

    void read(io::AnyReader& r) override {
        bsp::read<proto::Default>(r, species);
        bsp::read<proto::Default>(r, age);
    }

    bool operator==(const Animal& other) const {
        return species == other.species && age == other.age;
    }
};

struct TrivialStruct {
    uint32_t a;
    uint16_t b;
    uint8_t  c;
    bool operator==(const TrivialStruct& o) const { return a==o.a && b==o.b && c==o.c; }
};

// 通用 roundtrip 函数
template<typename T>
T roundtrip(const T& value) {
    std::stringstream ss;
    io::StreamWriter writer(ss);
    write(writer, value);
    io::StreamReader reader(ss);
    return read<T>(reader);
}

template<typename Proto, typename T>
T roundtrip_with_proto(const T& value) {
    std::stringstream ss;
    io::StreamWriter writer(ss);
    write<Proto>(writer, value);
    io::StreamReader reader(ss);
    return read<Proto, T>(reader);
}

void test_basic_integrals() {
    std::cout << "Testing basic integrals..." << std::endl;

    // bool
    TEST_ASSERT(roundtrip(true) == true);
    TEST_ASSERT(roundtrip(false) == false);

    // 非法 bool (STRICT)
    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        writer.write_byte(2);
        io::StreamReader reader(ss);
        options::push(options{.error_policy = ErrorPolicy::STRICT});
        bool out;
        TEST_ASSERT_THROWS(read(reader, out), errors::InvalidBool);
        options::pop();
    }

    // uint32_t
    TEST_ASSERT(roundtrip<uint32_t>(0) == 0);
    TEST_ASSERT(roundtrip<uint32_t>(42) == 42);
    TEST_ASSERT(roundtrip<uint32_t>(0xDEADBEEF) == 0xDEADBEEF);

    // int32_t
    TEST_ASSERT(roundtrip<int32_t>(-12345) == -12345);
    TEST_ASSERT(roundtrip<int32_t>(0) == 0);
    TEST_ASSERT(roundtrip<int32_t>(12345) == 12345);

    // uint64_t
    TEST_ASSERT(roundtrip<uint64_t>(0x123456789ABCDEF0ULL) == 0x123456789ABCDEF0ULL);

    // int64_t
    TEST_ASSERT(roundtrip<int64_t>(-0x123456789ABCDEF0LL) == -0x123456789ABCDEF0LL);

    // float
    float f1 = 3.14159f;
    float f2 = roundtrip(f1);
    TEST_ASSERT(std::abs(f1 - f2) < 0.0001f);
    float fneg = -0.0f;
    TEST_ASSERT(std::signbit(roundtrip(fneg)));
    TEST_ASSERT(std::isnan(roundtrip<float>(NAN)));

    // double
    double d1 = 2.718281828;
    double d2 = roundtrip(d1);
    TEST_ASSERT(std::abs(d1 - d2) < 0.00000001);
}

void test_varint() {
    std::cout << "Testing varint..." << std::endl;
    using Proto = proto::Varint;

    // unsigned
    TEST_ASSERT(roundtrip_with_proto<Proto>(0u) == 0u);
    TEST_ASSERT(roundtrip_with_proto<Proto>(127u) == 127u);
    TEST_ASSERT(roundtrip_with_proto<Proto>(128u) == 128u);
    TEST_ASSERT(roundtrip_with_proto<Proto>(0xFFFFFFFFu) == 0xFFFFFFFFu);

    // signed
    TEST_ASSERT(roundtrip_with_proto<Proto>(0) == 0);
    TEST_ASSERT(roundtrip_with_proto<Proto>(-1) == -1);
    TEST_ASSERT(roundtrip_with_proto<Proto>(1) == 1);
    TEST_ASSERT(roundtrip_with_proto<Proto>(-64) == -64);
    TEST_ASSERT(roundtrip_with_proto<Proto>(64) == 64);
    TEST_ASSERT(roundtrip_with_proto<Proto>(INT32_MAX) == INT32_MAX);
    TEST_ASSERT(roundtrip_with_proto<Proto>(INT32_MIN) == INT32_MIN);

    // overflow
    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        detail::write_varint(writer, 0xFFFFFFFFFFULL);
        io::StreamReader reader(ss);
        options::push(options{.error_policy = ErrorPolicy::STRICT});
        uint32_t out;
        TEST_ASSERT_THROWS(read<Proto>(reader, out), errors::VarintOverflow);
        options::pop();
    }
}

void test_strings_and_bytes() {
    std::cout << "Testing strings and bytes..." << std::endl;

    // std::string
    TEST_ASSERT(roundtrip("hello"s) == "hello");
    TEST_ASSERT(roundtrip(""s) == "");
    std::string long_str(1000, 'A');
    TEST_ASSERT(roundtrip(long_str) == long_str);

    // types::bytes
    types::bytes data = {0x00, 0x01, 0x02, 0xFF, 0xFE};
    TEST_ASSERT(roundtrip(data) == data);
    types::bytes empty;
    TEST_ASSERT(roundtrip(empty).empty());

    // fixed length string
    std::string str = "ABCD";
    auto result = roundtrip_with_proto<proto::Fixed<4>>(str);
    TEST_ASSERT(result == "ABCD");

    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        TEST_ASSERT_THROWS(write<proto::Fixed<5>>(writer, str), errors::FixedSizeMismatch);
    }

    // string size limit
    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        std::string huge(100, 'X');
        write(writer, huge);
        io::StreamReader reader(ss);
        options::push(options{.max_string_size = 50});
        std::string out;
        TEST_ASSERT_THROWS(read(reader, out), errors::StringTooLarge);
        options::pop();
    }
}

void test_containers() {
    std::cout << "Testing containers..." << std::endl;

    // vector<int>
    std::vector<int> v = {1,2,3,4,5};
    TEST_ASSERT(roundtrip(v) == v);
    std::vector<int> empty;
    TEST_ASSERT(roundtrip(empty).empty());

    // vector<bool>
    std::vector<bool> vb = {true, false, true, true};
    TEST_ASSERT(roundtrip(vb) == vb);

    // vector of trivial
    std::vector<TrivialStruct> vt = {{1,2,3}, {4,5,6}};
    TEST_ASSERT(roundtrip(vt) == vt);

    // map
    std::map<std::string, int> m = {{"a",1}, {"b",2}};
    TEST_ASSERT(roundtrip(m) == m);

    // set
    std::set<int> s = {5,2,8,1};
    TEST_ASSERT(roundtrip(s) == s);

    // array
    std::array<int, 4> arr = {10,20,30,40};
    TEST_ASSERT(roundtrip(arr) == arr);

    // tuple
    auto t = std::make_tuple(42, 3.14, "hello"s);
    TEST_ASSERT(roundtrip(t) == t);

    // pair
    auto p = std::make_pair(100, 200);
    TEST_ASSERT(roundtrip(p) == p);

    // bitset
    std::bitset<16> bits;
    bits.set(3);
    bits.set(12);
    TEST_ASSERT(roundtrip(bits) == bits);

    // container size limit
    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        std::vector<int> big(100, 42);
        write(writer, big);
        io::StreamReader reader(ss);
        options::push(options{.max_container_size = 50});
        std::vector<int> out;
        TEST_ASSERT_THROWS(read(reader, out), errors::ContainerTooLarge);
        options::pop();
    }
}

void test_depth_protection() {
    std::cout << "Testing depth protection..." << std::endl;

    using Nested = std::vector<std::vector<int>>;
    Nested nested;
    nested.push_back({1,2});
    nested.push_back({3,4});
    TEST_ASSERT(roundtrip(nested) == nested);

    options::push(options{.max_depth = 1});
    std::stringstream ss;
    io::StreamWriter writer(ss);
    TEST_ASSERT_THROWS(write(writer, nested), errors::DepthExceeded);

    ss.clear();
    ss.seekp(0);
    write<proto::optmod::WithOptions<proto::Default, proto::optmod::MaxDepth<proto::optmod::ValueModifier<0, 1, 256>>>>(writer, nested);
    io::StreamReader reader(ss);
    Nested out;
    TEST_ASSERT_THROWS(read(reader, out), errors::DepthExceeded);
    options::pop();
}

void test_optional_and_variant() {
    std::cout << "Testing optional and variant..." << std::endl;

    // optional
    std::optional<int> opt1 = 42;
    TEST_ASSERT(roundtrip(opt1) == opt1);
    std::optional<int> opt2 = std::nullopt;
    TEST_ASSERT(roundtrip(opt2) == std::nullopt);

    // variant
    std::variant<int, std::string, double> var;
    var = 42;
    TEST_ASSERT(roundtrip(var) == var);
    var = "hello"s;
    TEST_ASSERT(roundtrip(var) == var);
    var = 3.14;
    TEST_ASSERT(roundtrip(var) == var);

    // invalid index
    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        detail::write_varint(writer, 99u);
        io::StreamReader reader(ss);
        std::variant<int, std::string> v;
        TEST_ASSERT_THROWS(read(reader, v), errors::InvalidVariantIndex);
    }
}

void test_pointers() {
    std::cout << "Testing pointers..." << std::endl;

    // raw pointer
    int* ptr = new int(123);
    auto result = roundtrip(ptr);
    TEST_ASSERT(result != nullptr);
    TEST_ASSERT(*result == 123);
    delete result;
    delete ptr;

    int* nullp = nullptr;
    auto result2 = roundtrip(nullp);
    TEST_ASSERT(result2 == nullptr);

    // unique_ptr
    auto uptr = std::make_unique<std::string>("hello");
    auto uresult = roundtrip(uptr);
    TEST_ASSERT(uresult != nullptr);
    TEST_ASSERT(*uresult == "hello");

    std::unique_ptr<int> nullup;
    auto uresult2 = roundtrip(nullup);
    TEST_ASSERT(uresult2 == nullptr);
}

void test_schema() {
    std::cout << "Testing schema macro..." << std::endl;

    Point p1{10, 20, "origin"};
    Point p2 = roundtrip(p1);
    TEST_ASSERT(p2.x == 10);
    TEST_ASSERT(p2.y == 20);
    TEST_ASSERT(p2.name == "origin");

    Player player{12345, "Alice", 99.5f};
    Player player2 = roundtrip(player);
    TEST_ASSERT(player2.id == 12345);
    TEST_ASSERT(player2.nickname == "Alice");
    TEST_ASSERT(std::abs(player2.score - 99.5f) < 0.001f);
}

void test_pval() {
    std::cout << "Testing PVal wrapper..." << std::endl;

    int value = 0x1234;
    PVal<int, proto::Varint> wrapped{value};
    std::stringstream ss;
    io::StreamWriter writer(ss);
    write(writer, wrapped);
    io::StreamReader reader(ss);
    PVal<int, proto::Varint> out;
    read(reader, out);
    TEST_ASSERT(out.value == value);
}

void test_cval() {
    std::cout << "Testing CVal polymorphic..." << std::endl;

    Animal cat{"cat", 3};
    std::stringstream ss;
    io::StreamWriter writer(ss);
    write(writer, cat);
    io::StreamReader reader(ss);
    Animal dog;
    read(reader, dog);
    TEST_ASSERT(dog.species == "cat");
    TEST_ASSERT(dog.age == 3);
}

void test_limited_forced() {
    std::cout << "Testing Limited and Forced protocols..." << std::endl;

    // Limited with Varint length
    {
        std::vector<int> data = {1,2,3,4,5};
        using LimitedVec = PVal<std::vector<int>, proto::Limited<proto::Varint, proto::Varint>>;
        LimitedVec wrapped{data};
        auto result = roundtrip(wrapped).value;
        TEST_ASSERT(result == data);
    }

    // Forced with fixed length
    {
        using ForcedStr = PVal<std::string, proto::Forced<proto::Fixed<10>, proto::Default>>;
        ForcedStr wrapped{"Hello"s};
        std::stringstream ss;
        io::StreamWriter writer(ss);
        write(writer, wrapped);
        io::StreamReader reader(ss);
        ForcedStr out;
        read(reader, out);
        TEST_ASSERT(out.value == "Hello");

        ss.seekg(0, std::ios::end);
        TEST_ASSERT(ss.tellg() == 10);
    }
}

void test_options_modifiers() {
    std::cout << "Testing options modifiers..." << std::endl;

    using UnlimitedString = proto::optmod::WithOptions<
        proto::Default,
        proto::optmod::MaxStringSize<proto::optmod::Unlimited>
    >;
    std::string huge(5000, 'A');
    PVal<std::string, UnlimitedString> wrapped{huge};

    options::push(options{.max_string_size = 100});
    std::stringstream ss;
    io::StreamWriter writer(ss);
    write(writer, wrapped);
    io::StreamReader reader(ss);
    PVal<std::string, UnlimitedString> out;
    read(reader, out);
    TEST_ASSERT(out.value == huge);
    options::pop();
}

void test_endianness() {
    std::cout << "Testing endianness..." << std::endl;

    uint32_t val = 0x12345678;
    std::stringstream ss;
    io::StreamWriter writer(ss);
    write(writer, val);

    ss.seekg(0);
    char buf[4];
    ss.read(buf, 4);
    TEST_ASSERT(static_cast<uint8_t>(buf[0]) == 0x12);
    TEST_ASSERT(static_cast<uint8_t>(buf[1]) == 0x34);
    TEST_ASSERT(static_cast<uint8_t>(buf[2]) == 0x56);
    TEST_ASSERT(static_cast<uint8_t>(buf[3]) == 0x78);
}

void test_errors() {
    std::cout << "Testing error handling..." << std::endl;

    // EOF
    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        write(writer, uint32_t(42));
        std::string data = ss.str();
        data.pop_back();
        std::stringstream ss2(data);
        io::StreamReader reader(ss2);
        uint32_t out;
        TEST_ASSERT_THROWS(read(reader, out), errors::EOFError);
    }

    // Fixed size mismatch
    {
        std::vector<int> v = {1,2,3};
        std::stringstream ss;
        io::StreamWriter writer(ss);
        TEST_ASSERT_THROWS((write<proto::Fixed<2>>(writer, v)), errors::FixedSizeMismatch);
    }

    // Invalid variant index
    {
        std::stringstream ss;
        io::StreamWriter writer(ss);
        detail::write_varint(writer, 5u);
        io::StreamReader reader(ss);
        std::variant<int, std::string> var;
        TEST_ASSERT_THROWS(read(reader, var), errors::InvalidVariantIndex);
    }
}

void test_trivial() {
    std::cout << "Testing trivial protocol..." << std::endl;

    TrivialStruct s{0x12345678, 0xABCD, 0xEF};
    auto s2 = roundtrip(s);
    TEST_ASSERT(s2.a == s.a);
    TEST_ASSERT(s2.b == s.b);
    TEST_ASSERT(s2.c == s.c);

    std::stringstream ss;
    io::StreamWriter writer(ss);
    write(writer, s);
    std::string bytes = ss.str();
    TEST_ASSERT(bytes.size() == sizeof(TrivialStruct));
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&s);
    for (size_t i = 0; i < 7; ++i) {
        TEST_ASSERT(static_cast<uint8_t>(bytes[i]) == raw[i]);
    }
}

int main() {
    reset_options_to_default();

    test_basic_integrals();
    test_varint();
    test_strings_and_bytes();
    test_containers();
    test_depth_protection();
    test_optional_and_variant();
    test_pointers();
    test_schema();
    test_pval();
    test_cval();
    test_limited_forced();
    test_options_modifiers();
    test_endianness();
    test_errors();
    test_trivial();

    std::cout << "\n=== Test Results ===" << std::endl;
    std::cout << "Passed: " << tests_passed << std::endl;
    std::cout << "Failed: " << tests_failed << std::endl;

    return (tests_failed == 0) ? 0 : 1;
}