#pragma once
#include <stdint.h>

namespace FactoryDeviceRules {
inline int brightness(int value) { return value < 10 ? 10 : value > 100 ? 100 : value; }
inline int slider(int x, int minimum) {
    if (x < 30) x = 30;
    if (x > 192) x = 192;
    return minimum + ((x - 30) * (100 - minimum) + 81) / 162;
}
// Typical single-cell Li-ion discharge curve. SY6970 measures voltage, not
// remaining capacity; charging, load, temperature and cell age affect this estimate.
inline int batteryPercent(int mv) {
    if (mv < 2500 || mv > 4350) return -1;
    static const int voltage[] = {3300,3500,3600,3700,3800,3900,4000,4100,4200};
    static const int percent[] = {0,5,10,25,50,70,82,92,100};
    if (mv <= voltage[0]) return 0;
    for (unsigned i = 1; i < sizeof(voltage)/sizeof(voltage[0]); ++i) {
        if (mv <= voltage[i]) return percent[i-1] +
            (mv-voltage[i-1])*(percent[i]-percent[i-1])/(voltage[i]-voltage[i-1]);
    }
    return 100;
}
}
