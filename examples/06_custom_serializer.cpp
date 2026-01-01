#include "../include/bsp.hpp"
#include <iostream>
#include <sstream>

struct Encrypt {};

template<>
struct bsp::serialize::Serializer<int, Encrypt> {
    static void write(io::Writer &w, const int &s) {
        int enc = s ^ 0x55AA;
        utils::write_uleb128(w, enc);
    }
    static void read(io::Reader &r, int &out) {
        int enc = static_cast<int>(utils::read_uleb128(r));
        out = enc ^ 0x55AA;
    }
};

int main() {
    std::stringstream ss;
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    int s1 = 12345;
    bsp::write<Encrypt>(w, s1);

    int s2;
    bsp::read<Encrypt>(r, s2);

    std::cout << "Encrypted int roundtrip: " << s2 << "\n";
    return 0;
}
