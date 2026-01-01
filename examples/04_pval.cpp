#include "../include/bsp.hpp"
#include <iostream>
#include <sstream>
#include <vector>

int main() {
    std::stringstream ss;
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    using Layer3 = bsp::types::PVal<int, bsp::proto::Varint>;
    using Layer2 = bsp::types::PVal<std::vector<Layer3>, bsp::proto::Fixed<2>>;
    using Layer1 = bsp::types::PVal<std::vector<Layer2>, bsp::proto::Varint>;

    Layer1 arr;
    arr.get().push_back(Layer2{ { Layer3{1}, Layer3{2} } });
    arr.get().push_back(Layer2{ { Layer3{3}, Layer3{4} } });

    bsp::write(w, arr);

    Layer1 read_arr;
    bsp::read(r, read_arr);

    std::cout << "PVal 3D array: ";
    for(auto &l2 : read_arr.get()) {
        for(auto &l3 : l2.get()) {
            std::cout << l3.get() << " ";
        }
    }
    std::cout << "\n";

    return 0;
}
