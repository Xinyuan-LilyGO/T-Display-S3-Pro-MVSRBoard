#pragma once
#include <stdint.h>

namespace FactoryDevice {
struct Battery {
    int percent = -1, millivolts = 0;
    bool available = false, usb = false, charging = false, full = false;
};
void begin(); // Main/UI task, after Wire has been initialized by touch.
void tick();  // All charger I2C access stays in the same task as touch.
int brightness();
void setBrightness(int percent);
Battery battery();
void prepareSleep();
}
