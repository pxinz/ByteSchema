#include "../include/bsp.hpp"
#include <iostream>
#include <sstream>
#include <variant>

int main() {
    std::stringstream ss;
    bsp::io::Writer w(ss);
    bsp::io::Reader r(ss);

    // Option
    bsp::types::Option<int> opt{42};
    bsp::write(w, opt);

    bsp::types::Option<int> read_opt;
    bsp::read(r, read_opt);
    std::cout << "Option: " << (read_opt.has_value ? std::to_string(read_opt.value) : "None") << "\n";

    // Variant
    std::variant<int,std::string> var = "hello";
    bsp::write(w, var);

    std::variant<int,std::string> read_var;
    bsp::read(r, read_var);
    std::visit([](auto&& v){ std::cout << "Variant: " << v << "\n"; }, read_var);

    return 0;
}
