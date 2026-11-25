#include "FactoryVolume.h"
#include "FactoryButton.h"
#include <atomic>

namespace FactoryVolume {
namespace {
std::atomic<uint32_t> master{Default};
FactoryButton downButton(DownPin), upButton(UpPin);
}

void begin() {
    downButton.begin();
    upButton.begin();
}

uint8_t get() { return master.load(std::memory_order_relaxed); }

void set(int value) {
    master.store(value < 0 ? 0 : value > 100 ? 100 : value, std::memory_order_relaxed);
}

bool pollButtons() {
    const int previous = get();
    const bool down = downButton.pressed(), up = upButton.pressed();
    if (down || up) set(previous + (int(up) - int(down)) * 5);
    return previous != get();
}
} // namespace FactoryVolume
