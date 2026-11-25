#include "FactoryMusicSearch.h"
#include <cJSON.h>
#include <cstring>

namespace FactoryMusicSearch {
void copyUtf8(char *target, size_t capacity, const char *source) {
    if (!capacity) return;
    size_t n = 0;
    if (source) {
        const size_t length = std::strlen(source);
        n = length < capacity ? length : capacity - 1;
        // Never leave half a UTF-8 glyph at the end of a display string.
        if (n < length) while (n && (static_cast<unsigned char>(source[n]) & 0xc0) == 0x80) --n;
        std::memcpy(target, source, n);
        for (size_t i = 0; i < n; ++i) if (static_cast<unsigned char>(target[i]) < 32) target[i] = ' ';
    }
    target[n] = '\0';
}
const char *stringField(const cJSON *object, const char *name) {
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(field) ? field->valuestring : nullptr;
}
bool parse(const char *json, size_t length, Results &output) {
    output.count = 0;
    if (!json || !length) return false;
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(rows)) { cJSON_Delete(root); return false; }
    const cJSON *row = nullptr;
    cJSON_ArrayForEach(row, rows) {
        const char *kind = stringField(row, "kind"), *url = stringField(row, "previewUrl");
        const char *title = stringField(row, "trackName"), *artist = stringField(row, "artistName");
        if (!kind || std::strcmp(kind, "song") || !title || !*title || !url ||
            std::strncmp(url, "https://", 8) || !url[8] || url[8] == '/' ||
            std::strlen(url) >= sizeof(Track::url)) continue;
        bool valid = true;
        for (const char *p = url; *p; ++p) if (static_cast<unsigned char>(*p) <= 32) valid = false;
        if (!valid) continue;
        bool duplicate = false;
        for (int i = 0; i < output.count; ++i) if (!std::strcmp(output.tracks[i].url, url)) duplicate = true;
        if (duplicate) continue;
        Track &track = output.tracks[output.count++];
        copyUtf8(track.title, sizeof(track.title), title);
        copyUtf8(track.artist, sizeof(track.artist), artist ? artist : "Unknown artist");
        copyUtf8(track.url, sizeof(track.url), url);
        if (output.count == Limit) break;
    }
    cJSON_Delete(root);
    return true;
}
}
