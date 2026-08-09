// PNG writing, isolated so two host targets can link stb_image_write once each.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

void write_png_flipped(const char* path, int w, int h, const uint8_t* rgba);
