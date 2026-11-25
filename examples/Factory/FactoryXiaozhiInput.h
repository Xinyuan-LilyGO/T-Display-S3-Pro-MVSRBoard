#pragma once
#include <stdint.h>
#include <stddef.h>

// Inspect the two PCM lanes before WakeNet/Opus. A disconnected or underclocked
// PDM input can produce a fixed near-full-scale value, not digital silence.
namespace FactoryXiaozhiInput {
struct Level {
    uint32_t absolute = 0, ac = 0, span = 0;
    int32_t dc = 0;
    bool valid = false;
};
inline Level measure(const int16_t *stereo, size_t samples, unsigned lane) {
    Level level;
    if (!stereo || !samples || lane > 1) return level;
    int64_t sum = 0;
    uint64_t absolute = 0, ac = 0;
    size_t clipped = 0;
    int32_t low = 32767, high = -32768;
    for (size_t i = 0; i < samples; ++i) {
        const int32_t value = stereo[2*i+lane];
        sum += value; absolute += value < 0 ? -value : value;
        if (value < low) low = value;
        if (value > high) high = value;
        if (value <= -32760 || value >= 32760) ++clipped;
    }
    level.dc = int32_t(sum / int64_t(samples));
    for (size_t i = 0; i < samples; ++i) {
        const int32_t value = int32_t(stereo[2*i+lane]) - level.dc;
        ac += value < 0 ? -value : value;
    }
    level.absolute = uint32_t(absolute / samples);
    level.ac = uint32_t(ac / samples); level.span = uint32_t(high - low);
    level.valid = level.span > 0 && level.dc > -28000 && level.dc < 28000 && clipped * 10 < samples * 9;
    return level;
}
inline int select(const Level &left, const Level &right, int previous) {
    if (!left.valid && !right.valid) return -1;
    if (!left.valid) return 1;
    if (!right.valid) return 0;
    // Hysteresis prevents switching lanes on every small change in room noise.
    if (previous == 1) return left.ac > right.ac * 2 + 32 ? 0 : 1;
    return right.ac > left.ac * 2 + 32 ? 1 : 0;
}
}
