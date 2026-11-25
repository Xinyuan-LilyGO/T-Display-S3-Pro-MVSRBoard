#pragma once
#include <cstdint>

// Each changed digit folds independently. An unchanged digit stays still.
struct FactoryFlipClock {
    static constexpr uint32_t Duration = 280;
    int current[6] = {-1,-1,-1,-1,-1,-1};
    int previous[6] = {-1,-1,-1,-1,-1,-1};
    uint32_t started = 0;
    float progress = 1;
    bool initialized = false, animating = false;

    void update(int hour, int minute, int second, bool valid, uint32_t now) {
        const int digits[6] = {hour / 10, hour % 10, minute / 10, minute % 10, second / 10, second % 10};
        bool changed = false;
        for (int i=0;i<6;++i) changed |= current[i] != (valid ? digits[i] : -1);
        if (!initialized || changed) {
            for (int i=0;i<6;++i) {
                previous[i] = initialized ? current[i] : (valid ? digits[i] : -1);
                current[i] = valid ? digits[i] : -1;
            }
            animating = initialized && valid && previous[0] >= 0;
            initialized = true;
            started = now;
        }
        const uint32_t elapsed = now - started;
        progress = !animating || elapsed >= Duration ? 1.0f : float(elapsed) / Duration;
        if (progress >= 1) animating = false;
    }
};
