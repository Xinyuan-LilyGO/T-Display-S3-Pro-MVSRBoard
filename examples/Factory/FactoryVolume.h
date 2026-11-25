#pragma once
#include <cstdint>

namespace FactoryVolume {
constexpr uint8_t Default = 70;
constexpr uint8_t DownPin = 12, UpPin = 16;
void begin();
bool pollButtons();
uint8_t get();
void set(int value);
}
