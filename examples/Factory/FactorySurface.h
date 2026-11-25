#pragma once
#include <Arduino_GFX_Library.h>
#include "BreakoutRaster.h"

// One allocation-free drawing surface shared by the game and network/clock UI.
class FactorySurface : public Arduino_GFX {
public:
    FactorySurface() : Arduino_GFX(480, 222) {}
    bool begin(int32_t = GFX_NOT_DEFINED) override { return true; }
    int clipTop() const { return strip.top; }
    int clipBottom() const { return strip.top + strip.rows; }
    void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
        strip.pixel(x, y, color);
    }
    void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        strip.rect(x, y, w, 1, color);
    }
    void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
        strip.rect(x, y, 1, h, color);
    }
    void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h,
                                 uint16_t color) override {
        strip.rect(x, y, w, h, color);
    }
    void present(Arduino_GFX &display, void (*paint)(), bool force) {
        force |= !hashesValid;
        const bool portrait = display.width() < display.height();
        if (strip.width != display.width()) {
            strip.resize(portrait); setRotation(portrait ? 1 : 0); force = true;
        }
        const int count = (strip.height+FactoryGraphics::StripBuffer::RowsPerStrip-1)/FactoryGraphics::StripBuffer::RowsPerStrip;
        for (int i = 0; i < count; ++i) {
            const int y = i * FactoryGraphics::StripBuffer::RowsPerStrip;
            strip.select(y);
            paint();
            const uint32_t hash = strip.fingerprint();
            if (force || hashes[i] != hash) {
                display.draw16bitRGBBitmap(0, y, strip.pixels, strip.width, strip.rows);
                hashes[i] = hash;
            }
        }
        hashesValid = true;
    }
    // Exact-row writes for the home screen's hardware scroll ring. These writes
    // bypass full-frame rendering/hashing, and invalidate that cache for apps.
    void presentRegion(Arduino_GFX &display, void (*paint)(), int top, int bottom) {
        const bool portrait = display.width() < display.height();
        if (strip.width != display.width()) {
            strip.resize(portrait); setRotation(portrait ? 1 : 0);
        }
        top = std::max(0, top); bottom = std::min(strip.height, bottom);
        if (top >= bottom) return;
        hashesValid = false;
        for (int y = top; y < bottom; y += strip.rows) {
            strip.select(y);
            strip.rows = std::min(strip.rows, bottom - y);
            paint();
            display.draw16bitRGBBitmap(0, y, strip.pixels, strip.width, strip.rows);
        }
    }
private:
    FactoryGraphics::StripBuffer strip;
    bool hashesValid = false;
    uint32_t hashes[(480+FactoryGraphics::StripBuffer::RowsPerStrip-1)/FactoryGraphics::StripBuffer::RowsPerStrip] = {};
};

FactorySurface &factorySurface();
