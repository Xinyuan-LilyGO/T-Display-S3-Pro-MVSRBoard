#pragma once
#include <Arduino_GFX_Library.h>
namespace FactoryXiaozhi {
void open(Arduino_GFX &display);
void closeView(); // Stops a conversation but keeps local wake-word detection.
void touch(int16_t x, int16_t y);
void tick();
bool takeWakeRequest(); // UI must release the foreground app's audio first.
void talk();
void prepareSleep();
void drawIcon(Arduino_GFX &display, int16_t x, int16_t y);
}
