#pragma once
#include "FactorySurface.h"
int factoryTextWidth(const char *text);
void factoryDrawText(FactorySurface &surface, const char *text, int x, int y, int width,
                     uint16_t foreground, uint16_t background);
