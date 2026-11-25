#include "FactoryText.h"
#include "FactoryMusicFont.h"
int factoryTextWidth(const char *text) {
    int width=0;
    while (*text) width+=FactoryMusicFont::advances[FactoryMusicFont::index(FactoryMusicFont::next(text))];
    return width;
}
void factoryDrawText(FactorySurface &surface, const char *text, int x, int y, int width,
                     uint16_t foreground, uint16_t background) {
    FactoryMusicFont::draw(surface, text, x, y, width, foreground, background);
}
