#pragma once
#include <cstdint>

// Copyable command sent through a single-slot queue: the latest touch wins.
struct FactoryMusicControl {
    uint32_t revision = 0;
    uint8_t track = 0;
    bool play = false;
    void step(int direction, int count) {
        track = (int(track) + direction + count) % count;
        play = true;
        ++revision;
    }
    void toggle(bool retry) {
        if (retry) { play = true; ++revision; }
        else play = !play;
    }
};
