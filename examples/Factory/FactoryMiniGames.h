#pragma once
#include <Arduino_GFX_Library.h>
namespace FactoryMiniGames {
enum class Game : uint8_t { Snake, Maze };
void open(Game game,Arduino_GFX &display);
void close();
void touch(bool down,int16_t x,int16_t y,bool pressed);
void tick();
void drawIcon(Arduino_GFX &display,bool maze,int16_t x,int16_t y);
}
