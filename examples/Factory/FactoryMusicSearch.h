#pragma once
#include <cstddef>
#include <cstdint>

namespace FactoryMusicSearch {
constexpr int Limit = 8;
struct Track {
    char title[128] = {};
    char artist[128] = {};
    char url[500] = {};
};
enum class Status : uint8_t { Ok, Offline, NetworkError, RateLimited, InvalidResponse, Cancelled };
struct Results {
    uint32_t revision = 0;
    Status status = Status::Ok;
    uint8_t count = 0;
    Track tracks[Limit];
};
// Parsing is shared with host tests. Only tracks with usable HTTPS previews
// are returned; API trackTimeMillis is the FULL song length, never preview time.
bool parse(const char *json, size_t length, Results &output);
void copyUtf8(char *target, size_t capacity, const char *source);
void search(const char *query, Results &output, bool (*cancelled)());
}
