#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include "../include/bsp.hpp"

using namespace bsp;

struct IntCVal : types::CVal {
    int value = 0;

    IntCVal() = default;
    explicit IntCVal(int v) : value(v) {}

    void write(io::Writer &w, const std::type_info &protocol) const override {
        serialize::Serializer<int, proto::Varint>::write(w, value);
    }

    void read(io::Reader &r, const std::type_info &protocol) override {
        serialize::Serializer<int, proto::Varint>::read(r, value);
    }
};

int main() {
    std::stringstream ss;
    io::Writer w(ss);
    io::Reader r(ss);

    // 单个 IntCVal 写入/读取
    IntCVal writeVal(12345);
    writeVal.write(w, typeid(proto::CVal));

    IntCVal readVal;
    readVal.read(r, typeid(proto::CVal));
    std::cout << "Read single value: " << readVal.value << std::endl;

    // vector<IntCVal> 写入/读取
    std::vector<IntCVal> writeVec = {IntCVal(111), IntCVal(-222), IntCVal(333)};
    w.write(writeVec);

    std::vector<IntCVal> readVec;
    r.read(readVec);

    std::cout << "Read vector values: ";
    for (const auto &v : readVec) {
        std::cout << v.value << " ";
    }
    std::cout << std::endl;

    return 0;
}
