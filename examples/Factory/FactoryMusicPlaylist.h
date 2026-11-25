#pragma once
#include <cstddef>

namespace FactoryMusicPlaylist {
struct Track { const char *title; const char *subtitle; const char *url; };
// Public SoundHelix examples: https://www.soundhelix.com/audio-examples
// Replace these with direct MP3 URLs (not a music service's HTML song page).
// Keep URLs under 500 bytes and display titles in ASCII for the built-in font.
static const Track tracks[] = {
    {"SoundHelix 1", "Online demo / SoundHelix", "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3"},
    {"SoundHelix 2", "Online demo / SoundHelix", "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-2.mp3"},
    {"SoundHelix 3", "Online demo / SoundHelix", "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-3.mp3"}
};
constexpr size_t count = sizeof(tracks) / sizeof(tracks[0]);
static_assert(count > 0 && count <= 255, "Playlist must contain 1 to 255 tracks");
}
