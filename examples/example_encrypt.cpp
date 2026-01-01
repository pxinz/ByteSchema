#include <iostream>
#include <sstream>
#include "../include/bsp.hpp"

struct Encrypt {};

namespace bsp::serialize {
    template<>
    struct Serializer<int, Encrypt> {
        static void write(io::Writer &w, const int &s) {
            int encrypted = s ^ 0x55AA; // 异或加密
            utils::write_uleb128(w, encrypted);
        }

        static void read(io::Reader &r, int &out) {
            int encrypted = static_cast<int>(utils::read_uleb128(r));
            out = encrypted ^ 0x55AA;
        }
    };
}

int main() {
    std::stringstream ss;
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    int s1 = 12345;
    bsp::write<Encrypt>(w, s1);

    int s2;
    bsp::read<Encrypt>(r, s2);
    std::cout << s2 << "\n"; // 输出 12345
}