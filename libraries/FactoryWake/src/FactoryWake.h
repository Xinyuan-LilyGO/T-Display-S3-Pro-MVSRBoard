#pragma once
#include <cstdint>
namespace FactoryWake {
bool begin();
int samples();
bool detect(int16_t *pcm);
void clear();
void end();
}
