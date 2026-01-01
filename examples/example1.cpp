#include "../include/bsp.hpp"
#include <sstream>
#include <iostream>

struct Point {
    int x;
    int y;
};

BSP_REGISTER_STRUCT(Point,
                    BSP_FIELD(Point, x),
                    BSP_FIELD(Point, y)
);

int main() {
    std::stringstream ss;
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    Point pt1{10, 20};
    bsp::write(w, pt1);

    Point pt2{};
    bsp::read(r, pt2);

    std::cout << "Point: " << pt2.x << ", " << pt2.y << "\n";

    std::vector<int> vec{1, 2, 3};
    bsp::write(w, vec);

    std::vector<int> vec2;
    bsp::read(r, vec2);

    for (const auto v: vec2) std::cout << v << " "; // 1 2 3
}
