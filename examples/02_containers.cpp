#include "../include/bsp.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <map>

int main() {
    std::stringstream ss;
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    // vector
    std::vector vec = {1, 2, 3};
    bsp::write(w, vec);

    std::vector<int> read_vec;
    bsp::read(r, read_vec);
    std::cout << "Vector: ";
    for (auto x: read_vec) std::cout << x << " ";
    std::cout << "\n";

    // map
    std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
    bsp::write(w, m);

    std::map<std::string, int> read_m;
    bsp::read(r, read_m);
    std::cout << "Map: ";
    for (auto &[k,v]: read_m) std::cout << k << "=" << v << " ";
    std::cout << "\n";

    return 0;
}
