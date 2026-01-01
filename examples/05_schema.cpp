#include "../include/bsp.hpp"
#include <iostream>
#include <sstream>

// Define types and schemas
struct Point {
    float x;
    float y;
};

struct Scene {
    std::vector<Point> dynamicPath;
    std::vector<int> fixedTriple; // override protocol to Fixed<3>
    std::string name;
};

struct Stage {
    std::vector<Scene> scenes;
};

BSP_REGISTER_STRUCT(Point,
                    BSP_FIELD(Point, x),
                    BSP_FIELD(Point, y)
);

BSP_REGISTER_STRUCT(Scene,
                    BSP_FIELD(Scene, dynamicPath),
                    BSP_FIELD_WITH(Scene, fixedTriple, proto::Fixed<3>),
                    BSP_FIELD(Scene, name)
);

BSP_REGISTER_STRUCT(Stage,
                    BSP_FIELD(Stage, scenes)
);

int main() {
    Stage st;
    Scene s1;
    s1.name = "First";
    s1.fixedTriple = {1, 2, 3};
    s1.dynamicPath = {{0.0f, 0.0f}, {1.0f, 1.0f}};

    Scene s2;
    s2.name = "Second";
    s2.fixedTriple = {4, 5, 6};
    s2.dynamicPath = {{2.0f, 3.0f}};

    st.scenes = {s1, s2};

    std::ostringstream os(std::ios::binary);
    bsp::io::Writer w(os);
    bsp::write(w, st);

    std::string bytes = os.str();
    std::istringstream is(bytes, std::ios::binary);
    bsp::io::Reader r(is);
    Stage out;
    bsp::read(r, out);

    std::cout << "Deserialized Stage: scenes = " << out.scenes.size() << "\n";
    for (size_t i = 0; i < out.scenes.size(); ++i) {
        const auto &sc = out.scenes[i];
        std::cout << " Scene[" << i << "].name = " << sc.name << ", fixedTriple: ";
        for (int x: sc.fixedTriple) std::cout << x << ' ';
        std::cout << ", dynamicPath size = " << sc.dynamicPath.size() << "\n";
    }
    return 0;
}
