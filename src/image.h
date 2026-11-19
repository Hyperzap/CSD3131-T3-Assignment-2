#pragma once
// Minimal image type for the seam carving core.
//
// The core has no dependencies, so it compiles both natively (seambench: MSVC,
// GCC, Clang) and to WebAssembly (Emscripten). Pixels are 8-bit RGBA, row-major,
// which is exactly the layout of a browser canvas ImageData, so the web build
// can copy pixels in and out without conversion. Alpha is carried along with
// the pixels but ignored by the energy function.

#include <cstddef>
#include <cstdint>
#include <vector>

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> px; // width * height * 4 bytes: R, G, B, A

    Image() = default;
    Image(int w, int h) : width(w), height(h), px(static_cast<size_t>(w) * h * 4, 255) {}

    bool empty() const { return width <= 0 || height <= 0; }
    std::uint8_t* row(int i) { return px.data() + static_cast<size_t>(i) * width * 4; }
    const std::uint8_t* row(int i) const { return px.data() + static_cast<size_t>(i) * width * 4; }
    std::uint8_t* at(int i, int j) { return row(i) + static_cast<size_t>(j) * 4; }
    const std::uint8_t* at(int i, int j) const { return row(i) + static_cast<size_t>(j) * 4; }
};

// Swaps rows and columns (used for horizontal seams).
Image transposeImage(const Image& img);
