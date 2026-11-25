#pragma once
#include <Arduino_GFX_Library.h>

namespace FactoryMusic {
void open(Arduino_GFX &display);
void close();
void touch(int16_t x, int16_t y);
void tick();
void drawIcon(Arduino_GFX &display, int16_t x, int16_t y,
              int16_t clipTop = 0, int16_t clipBottom = 480);
}
