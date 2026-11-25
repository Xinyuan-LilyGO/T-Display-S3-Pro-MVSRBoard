#pragma once
#include <Arduino_GFX_Library.h>

namespace FactoryApps {
enum class App : uint8_t { None, Poker, Piano, Breakout, Settings, Clock, Music, Xiaozhi, Snake, Maze };
bool open(App app, Arduino_GFX &display);
void close();
void touch(bool down, int16_t x, int16_t y, bool pressed);
void tick();
void refreshVolume();
}
