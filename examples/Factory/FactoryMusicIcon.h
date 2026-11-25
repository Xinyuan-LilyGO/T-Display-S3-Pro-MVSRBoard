#pragma once
#include <cstdint>
#include <cmath>

// Supersampled vector icon: pastel backdrop, violet/blue headphones and a
// three-bar equalizer. Returning RGB565 also lets host tools preview the exact
// artwork without creating a bitmap dependency for the firmware.
namespace FactoryMusicIcon {
inline bool rounded(float x, float y, float cx, float cy, float hw, float hh, float r) {
    const float dx = std::fmax(std::fabs(x - cx) - (hw - r), 0.0f);
    const float dy = std::fmax(std::fabs(y - cy) - (hh - r), 0.0f);
    return dx * dx + dy * dy <= r * r;
}
inline uint16_t pixel(int x, int y) {
    int red = 0, green = 0, blue = 0;
    for (int sy = 0; sy < 3; ++sy) for (int sx = 0; sx < 3; ++sx) {
        const float px = x + (sx + 0.5f) / 3.0f - 0.5f;
        const float py = y + (sy + 0.5f) / 3.0f - 0.5f;
        const float radius = px * px + py * py;
        int r = 255, g = 255, b = 255;
        if (radius <= 26 * 26) {
            const float t = (py + 26) / 52;
            r = 247 - 9 * t; g = 245 - 7 * t; b = 255;
        }
        const float band = px * px + (py + 2) * (py + 2);
        const bool arch = py < 4 && band <= 20 * 20 && band >= 14.5f * 14.5f;
        const bool cups = rounded(px, py, -16.5f, 7, 5.2f, 9, 4) || rounded(px, py, 16.5f, 7, 5.2f, 9, 4);
        if (arch || cups) {
            const float t = std::fmax(0.0f, std::fmin(1.0f, (px + py + 37) / 74));
            r = 137 - 91 * t; g = 67 + 72 * t; b = 222 + 17 * t;
        }
        // Small inner highlights give the earpads depth without a dark disk.
        if (rounded(px, py, -15, 6, 1.3f, 5, 1.2f) || rounded(px, py, 15, 6, 1.3f, 5, 1.2f)) {
            r = 192; g = 184; b = 250;
        }
        if (rounded(px, py, -6, 5, 1.6f, 4, 1.5f)) { r = 202; g = 105; b = 196; }
        if (rounded(px, py, 0, 5, 1.6f, 8, 1.5f)) { r = 139; g = 92; b = 220; }
        if (rounded(px, py, 6, 5, 1.6f, 5.5f, 1.5f)) { r = 71; g = 151; b = 222; }
        red += r; green += g; blue += b;
    }
    return ((red / 9 >> 3) << 11) | ((green / 9 >> 2) << 5) | (blue / 9 >> 3);
}
}
