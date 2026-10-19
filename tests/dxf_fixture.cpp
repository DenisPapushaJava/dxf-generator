#include "dxfgen/dxf_writer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: dxfgen_fixture OUTPUT_DIRECTORY\n";
        return 1;
    }
    std::filesystem::create_directories(argv[1]);
    const dxfgen::Part parts[] = {{"rectangle", dxfgen::Rectangle{120.5, 80.25}, 3, {}},
                                  {"circle", dxfgen::Circle{80.5}, 2, {}},
                                  {"washer", dxfgen::Washer{100.5, 50.25}, 8, 6.0}};
    for (const auto& part : parts) {
        std::ofstream file(std::filesystem::path(argv[1]) / (part.name + ".dxf"), std::ios::binary);
        file << dxfgen::write_dxf(part);
        if (!file)
            return 1;
    }
}
