#include "map_generator.hpp"
#include "load_save_png.hpp"
#include "read_write_chunk.hpp"

#include <iostream>
#include <fstream>

int main() {
    glm::uvec2 size;
    std::vector< glm::u8vec4 > data;
    load_png("../map.png", &size, &data, UpperLeftOrigin);

    std::vector<uint8_t> level;
    level.resize(data.size(), TILE_NONE);

    for (unsigned int x = 0; x < size.x; x++) {
        for (unsigned int y = 0; y < size.y; y++) {
            glm::u8vec4 color = data[y * size.x + x];

            if (color == glm::u8vec4(0, 0, 0, 255)) {
                level[y * size.x + x] = TILE_WALL;
            } else if (color == glm::u8vec4(0, 0, 255, 255)) {
                level[y * size.x + x] = TILE_SPAWN;
            } else if (color == glm::u8vec4(255, 0, 0, 255)) {
                level[y * size.x + x] = TILE_OUTOFBOUNDS;
            } else if (color == glm::u8vec4(0, 255, 0, 255)) {
                level[y * size.x + x] = TILE_PORTAL;
            } else if (color == glm::u8vec4(128, 128, 128, 255)) {
                level[y * size.x + x] = TILE_INNER;
            }
        }
    }

    std::ofstream level_file;
    level_file.open("../../dist/level_data", std::ios::binary);

    std::vector<unsigned int> level_width_chunk;
    level_width_chunk.push_back(size.x);

    write_chunk<unsigned int>("widt", level_width_chunk, &level_file);
    write_chunk<uint8_t>("lev0", level, &level_file);

    level_file.close();
}