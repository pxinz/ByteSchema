#include "../include/bsp.hpp"
#include <iostream>
#include <sstream>

int main() {
    std::stringstream ss;

    // Writer / Reader
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    // 布尔
    bool b = true;
    bsp::write<bsp::proto::Fixed<> >(w, b);

    bool read_b;
    bsp::read<bsp::proto::Fixed<> >(r, read_b);
    std::cout << "Bool: " << read_b << "\n";

    // 整数
    int32_t s = -42;
    bsp::write<bsp::proto::Varint>(w, s);

    int32_t read_s;
    bsp::read<bsp::proto::Varint>(r, read_s);
    std::cout << "Int32: " << read_s << "\n";

    // 浮点
    float f = 3.14f;
    bsp::write<bsp::proto::Fixed<> >(w, f);

    float read_f;
    bsp::read<bsp::proto::Fixed<> >(r, read_f);
    std::cout << "Float: " << read_f << "\n";

    return 0;
}
