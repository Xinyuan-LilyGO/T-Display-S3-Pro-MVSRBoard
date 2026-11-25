#pragma once
#include <Arduino_GFX_Library.h>
#include <cstddef>
namespace FactoryNetwork {
void begin();
void open(bool clock, Arduino_GFX &display);
void openWifi(Arduino_GFX &display, void (*onBack)());
void close();
void touch(int16_t x, int16_t y);
void drag(int16_t x, int16_t y, bool down);
void localTimeLabel(char *output, size_t size);
void tick(); // Network service continues on the home page and in other apps.
void refresh();
bool isPortrait();
void drawIcon(Arduino_GFX &display, bool clock, int16_t x, int16_t y);
}
