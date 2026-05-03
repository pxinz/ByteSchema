#ifndef BSP_TESTS_HPP
#define BSP_TESTS_HPP

#include "bsp.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <source_location>
#include <functional>

/* ============================================================================
 * Test Framework for bsp
 * 测试框架
 * ============================================================================ */

namespace bsp::test {
    // === Test Result ========================================================
    // 测试结果
    enum class result {
        PASSED,
        FAILED,
        SKIPPED
    };

    inline std::string_view nameof(const result r) {
        switch (r) {
            case result::PASSED: return "PASSED";
            case result::FAILED: return "FAILED";
            case result::SKIPPED: return "SKIPPED";
        }
        return "";
    }

    // === Test Case ==========================================================
    // 单个测试用例
    struct test_case {
        std::string name;
        std::function<result()> fn;
        std::source_location loc;
    };

    // === Test Suite =========================================================
    // 测试套件
    class test_suite {
    public:
        test_suite() = default;

        // 注册测试用例
        void add(test_case tc) {
            cases.push_back(std::move(tc));
        }

        // 运行所有测试用例
        int run(bool verbose = false) {
            int passed = 0;
            int failed = 0;
            int skipped = 0;

            for (auto &tc: cases) {
                if (verbose) {
                    std::cout << "  [RUN]      " << tc.name
                            << " (" << tc.loc.file_name() << ":" << tc.loc.line() << ")"
                            << std::endl;
                }

                result r;
                try {
                    r = tc.fn();
                } catch (const std::exception &e) {
                    std::cout << "  [" << nameof(result::FAILED) << "] "
                            << tc.name << " - unhandled exception: " << e.what()
                            << std::endl;
                    ++failed;
                    continue;
                } catch (...) {
                    std::cout << "  [" << nameof(result::FAILED) << "] "
                            << tc.name << " - unknown unhandled exception"
                            << std::endl;
                    ++failed;
                    continue;
                }

                switch (r) {
                    case result::PASSED:
                        if (verbose)
                            std::cout << "  [" << nameof(r) << "] " << tc.name << std::endl;
                        ++passed;
                        break;
                    case result::FAILED:
                        std::cout << "  [" << nameof(r) << "] " << tc.name << std::endl;
                        ++failed;
                        break;
                    case result::SKIPPED:
                        if (verbose)
                            std::cout << "  [" << nameof(r) << "] " << tc.name << std::endl;
                        ++skipped;
                        break;
                }
            }

            // 汇总
            std::cout << "\n  Results: "
                    << passed << " passed, "
                    << failed << " failed, "
                    << skipped << " skipped"
                    << std::endl;

            return failed;
        }

    private:
        std::vector<test_case> cases;
    };

    // === Global Test Suite ==================================================
    // 全局单例
    inline test_suite &global_suite() {
        static test_suite suite;
        return suite;
    }

    // === Roundtrip ==========================================================
    inline std::pair<io::BufferWriter *, io::BufferReader *> roundtrip_io() {
        static io::BufferWriter writer;
        static io::BufferReader reader(writer.buf);
        writer.buf.clear();
        reader.pos = 0;
        return std::make_pair(&writer, &reader);
    }

    template<typename T>
    T roundtrip(const T &v, context ctx = context::get_default_context()) {
        using P = proto::DefaultProtocol_t<T>;
        auto [w, r] = roundtrip_io();
        write<P>(&w, v, ctx);
        return read<P>(&r, ctx);
    }

    template<typename P, typename T>
    T roundtrip(const T &v, context ctx = context::get_default_context()) {
        auto [w,r] = roundtrip_io();
        write<P>(&w, v, ctx);
        return read<P>(&r, ctx);
    }

    // === Test Registration Macro ============================================
    // 注册测试用例的宏
    namespace detail {
        struct test_registrar {
            test_registrar(
                test_suite &suite,
                std::string name,
                std::function<result()> fn,
                const std::source_location &loc = std::source_location::current()
            ) {
                suite.add({std::move(name), std::move(fn), loc});
            }
        };
    } // namespace detail

    // 注册测试：TEST(SuiteName, TestName) { ... }
    // 使用静态变量在程序启动时自动注册
    // 例如：
    //   TEST(BufferReader, read_byte) {
    //       ...
    //       return bsp::test::result::PASSED;
    //   }
} // namespace bsp::test

#define TEST_CONCAT_(a, b)  a##b
#define TEST_CONCAT(a, b)   TEST_CONCAT_(a, b)

#define TEST(SUITE, NAME)                                                      \
    static bsp::test::result TEST_CONCAT(__test_fn_, __LINE__)();              \
    static ::bsp::test::detail::test_registrar                                 \
        TEST_CONCAT(__test_reg_, __LINE__)(                                    \
            ::bsp::test::global_suite(),                                       \
            std::string(#SUITE) + "." + std::string(#NAME),                    \
            TEST_CONCAT(__test_fn_, __LINE__)                                  \
        );                                                                     \
    static bsp::test::result TEST_CONCAT(__test_fn_, __LINE__)()

// === Helper Macros ==========================================================
// 断言宏

#define TEST_ASSERT(cond)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::cerr << "    ASSERTION FAILED: " << #cond                      \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;    \
            return ::bsp::test::result::FAILED;                                 \
        }                                                                       \
    } while (false)

#define TEST_ASSERT_EQ(a, b)                                                    \
    do {                                                                        \
        auto _a = (a);                                                          \
        auto _b = (b);                                                          \
        if (_a != _b) {                                                         \
            std::cerr << "    ASSERTION FAILED: " << #a << " == " << #b         \
                      << " (" << _a << " != " << _b << ")"                      \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;    \
            return ::bsp::test::result::FAILED;                                 \
        }                                                                       \
    } while (false)

#define TEST_ASSERT_THROW(expr, exception_type)                                 \
    do {                                                                        \
        bool _caught = false;                                                   \
        try {                                                                   \
            (expr);                                                             \
        } catch (const exception_type &) {                                      \
            _caught = true;                                                     \
        } catch (...) {                                                         \
        }                                                                       \
        if (!_caught) {                                                         \
            std::cerr << "    ASSERTION FAILED: " << #expr                      \
                      << " did not throw " << #exception_type                   \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;    \
            return ::bsp::test::result::FAILED;                                 \
        }                                                                       \
    } while (false)

#define TEST_SKIP()                                                             \
    do {                                                                        \
        return ::bsp::test::result::SKIPPED;                                    \
    } while (false)

// === Main Function ===========================================================
// 用户只需在 main 中调用 RUN_ALL_TESTS()

#define RUN_ALL_TESTS()                                                         \
    int main(int argc, char *argv[]) {                                          \
        bool verbose = (argc > 1 && std::string(argv[1]) == "-v");              \
        std::cout << "Running all tests..." << std::endl;                       \
        int failed = ::bsp::test::global_suite().run(verbose);                  \
        if (failed == 0)                                                        \
            std::cout << "All tests passed!" << std::endl;                      \
        else                                                                    \
            std::cout << failed << " test(s) failed!" << std::endl;             \
        return failed;                                                          \
    }

#endif
