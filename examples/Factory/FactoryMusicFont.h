#pragma once
#include "FactoryMusicFontData.h"
#include "FactorySurface.h"

namespace FactoryMusicFont {
inline int index(uint32_t code) {
    for (const auto &range : ranges) if (code >= range.first && code <= range.last) return range.offset + code - range.first;
    return '?' - 32;
}
inline uint32_t next(const char *&value) {
    const uint8_t first = static_cast<uint8_t>(*value++);
    if (first < 128) return first;
    const int count = first < 224 ? 1 : first < 240 ? 2 : 3;
    uint32_t code = first & (count == 1 ? 31 : count == 2 ? 15 : 7);
    for (int i = 0; i < count; ++i) {
        if ((static_cast<uint8_t>(*value) & 0xc0) != 0x80) return '?';
        code = (code << 6) | (static_cast<uint8_t>(*value++) & 63);
    }
    return code;
}
inline void glyph(FactorySurface &canvas, int n, int x, int y, uint16_t foreground, uint16_t background) {
    uint16_t colors[4] = {background, 0, 0, foreground};
    for (int a = 1; a < 3; ++a) {
        const int r = (((foreground >> 11) & 31) * a + ((background >> 11) & 31) * (3-a)) / 3;
        const int g = (((foreground >> 5) & 63) * a + ((background >> 5) & 63) * (3-a)) / 3;
        const int b = ((foreground & 31) * a + (background & 31) * (3-a)) / 3;
        colors[a] = (r << 11) | (g << 5) | b;
    }
    // Draw only this strip's rows; avoid decoding the whole font every strip.
    for (int row = max(0, canvas.clipTop() - y); row < min(16, canvas.clipBottom() - y); ++row)
        for (int col = 0; col < 16; ++col) {
            const uint8_t alpha = (glyphs[n][row * 4 + col / 4] >> (6 - (col % 4) * 2)) & 3;
            if (alpha) canvas.drawPixel(x + col, y + row, colors[alpha]);
        }
}
inline void draw(FactorySurface &canvas, const char *value, int x, int y, int width, uint16_t foreground, uint16_t background) {
    const int right = x + width;
    while (*value) {
        const int n = index(next(value));
        if (x + max(16, int(advances[n])) > right - 14) {
            for (int i = 0; i < 3; ++i) { glyph(canvas, index('.'), x, y, foreground, background); x += advances[index('.')]; }
            break;
        }
        glyph(canvas, n, x, y, foreground, background);
        x += advances[n];
    }
}
}
