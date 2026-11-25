#pragma once
#include <Arduino_GFX_Library.h>

namespace FactoryBreakout {
bool open(Arduino_GFX &display);
void close();
void touch(bool down, int16_t x, int16_t y, bool pressed);
void tick();
void drawIcon(Arduino_GFX &display, int16_t x, int16_t y);
}
