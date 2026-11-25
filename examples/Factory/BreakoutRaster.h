#pragma once
#include <algorithm>
#include <cstdint>

namespace FactoryGraphics {
constexpr int Width = 480, Height = 222;
// Fixed internal-RAM storage: no heap allocation and no dependency on PSRAM.
class StripBuffer {
public:
    static constexpr int RowsPerStrip = 8;
    static constexpr int Count = (Height + RowsPerStrip - 1) / RowsPerStrip;
    uint16_t pixels[Width * RowsPerStrip] = {};
    int width = Width, height = Height;
    int top = 0, rows = RowsPerStrip;

    void resize(bool portrait) {
        width = portrait ? Height : Width;
        height = portrait ? Width : Height;
    }

    void select(int y) {
        top = y;
        rows = height - y < RowsPerStrip ? height - y : RowsPerStrip;
    }
    void pixel(int x, int y, uint16_t color) {
        if (x >= 0 && x < width && y >= top && y < top + rows)
            pixels[(y - top) * width + x] = color;
    }
    void rect(int x, int y, int width, int height, uint16_t color) {
        if (width < 0) { x += width + 1; width = -width; }
        if (height < 0) { y += height + 1; height = -height; }
        const int left = std::max(x, 0), right = std::min(x + width, this->width);
        const int first = std::max(y, top), end = std::min(y + height, top + rows);
        if (left >= right || first >= end) return;
        for (int line = first; line < end; ++line)
            std::fill(pixels + (line - top) * this->width + left,
                      pixels + (line - top) * this->width + right, color);
    }
    uint32_t fingerprint() const {
        uint32_t value = 2166136261u;
        for (int i = 0; i < width * rows; ++i) value = (value ^ pixels[i]) * 16777619u;
        return value;
    }
};
} // namespace FactoryGraphics

namespace Breakout { using StripBuffer = FactoryGraphics::StripBuffer; }
