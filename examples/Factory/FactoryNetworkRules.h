#pragma once
#include <cstddef>
#include <cctype>
#include <cstring>
#include <cstdio>

namespace FactoryNetworkRules {
inline bool validPassword(const char *password) {
    const size_t n = std::strlen(password);
    if (n >= 8 && n <= 63) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(password[i]))) return false;
    return true;
}
inline int zone(int minutes) { return minutes < -720 ? -720 : minutes > 840 ? 840 : minutes; }
inline void zoneLabel(int minutes, char *output, size_t size) {
    const int magnitude = minutes < 0 ? -minutes : minutes;
    std::snprintf(output, size, "UTC%c%02d:%02d", minutes < 0 ? '-' : '+', magnitude / 60, magnitude % 60);
}
}
