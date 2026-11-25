#include "FactoryMusic.h"
#include "FactoryMusicControl.h"
#include "FactoryMusicPlaylist.h"
#include "FactoryMusicSearch.h"
#include "FactoryMusicIcon.h"
#include "FactoryText.h"
#include "FactoryNetwork.h"
#include "FactorySurface.h"
#include "FactoryVolume.h"
#include "pin_config.h"
#include <Audio.h>
#include <WiFi.h>
#include <atomic>
#include <new>
#include <freertos/semphr.h>

namespace FactoryMusic {
namespace {
enum class State : uint8_t { Ready, Offline, Connecting, Buffering, Playing, Paused, Finished, Error, NoMemory };
struct Snapshot {
    State state = State::Ready;
    uint32_t revision = 0, seconds = 0, duration = 0;
};
struct Command {
    FactoryMusicControl playback;
    bool search = false;
    char query[65] = {};
    char url[500] = {};
};
enum class View { Player, Keyboard, Results };
Arduino_GFX *screen = nullptr;
QueueHandle_t commands = nullptr, searchReplies = nullptr;
SemaphoreHandle_t stopped = nullptr;
std::atomic<bool> quitting{false}, ended{false};
std::atomic<uint32_t> requestedRevision{0};
uint32_t searchRevision = 0, searchStarted = 0; // Owned by playerTask.
portMUX_TYPE snapshotLock = portMUX_INITIALIZER_UNLOCKED;
Snapshot shared, shown;
FactoryMusicControl control;
FactoryMusicSearch::Track catalog[FactoryMusicSearch::Limit];
FactoryMusicSearch::Results results;
FactoryMusicSearch::Results workerReply; // Only playerTask writes this buffer.
int catalogCount = 0, firstResult = 0, keyboardMode = 0;
bool preview = false, searchBusy = false;
uint32_t pendingSearch = 0, lastSearch = 0;
String query, searchHint;
View view = View::Player;
bool wifiPage = false, online = false, force = true, dirty = true;
uint32_t lastPaint = 0;
constexpr uint16_t ink = RGB565(32, 39, 59), muted = RGB565(120, 130, 148);
constexpr uint16_t purple = RGB565(112, 81, 215), pale = RGB565(242, 238, 253);
constexpr uint16_t line = RGB565(230, 233, 241), green = RGB565(37, 161, 123);

void publish(const Snapshot &value) {
    portENTER_CRITICAL(&snapshotLock);
    shared = value;
    portEXIT_CRITICAL(&snapshotLock);
}
Snapshot readSnapshot() {
    portENTER_CRITICAL(&snapshotLock);
    const Snapshot value = shared;
    portEXIT_CRITICAL(&snapshotLock);
    return value;
}

bool searchCancelled() {
    return quitting.load() || requestedRevision.load() != searchRevision || millis() - searchStarted > 15000;
}

// This task is the ONLY caller of Audio APIs, including construction/destruction.
// DNS/TLS/stream reads never run on the touch/display loop.
void playerTask(void *) {
    Audio *audio = nullptr;
    FactoryMusicControl desired;
    char playbackUrl[500] = {};
    Snapshot state;
    bool session = false, failed = false;
    uint32_t progressAt = millis(), previousTime = 0;
    uint8_t volume = FactoryVolume::get();
    while (!quitting.load()) {
        Command next;
        if (xQueueReceive(commands, &next, 1) == pdTRUE) {
            if (next.playback.revision != desired.revision) {
                // Release the old TLS socket/decoder before another stream or
                // search, avoiding concurrent large TLS buffers and stale URLs.
                delete audio; audio = nullptr;
                session = failed = false;
                ended.store(false);
                state = Snapshot();
                state.revision = next.playback.revision;
                previousTime = 0;
            }
            desired = next.playback;
            memcpy(playbackUrl, next.url, sizeof(playbackUrl));
            if (next.search) {
                workerReply.revision = searchRevision = desired.revision;
                searchStarted = millis();
                FactoryMusicSearch::search(next.query, workerReply, searchCancelled);
                if (!quitting.load()) xQueueOverwrite(searchReplies, &workerReply);
                continue;
            }
        }
        if (WiFi.status() != WL_CONNECTED) {
            if (session) audio->stopSong();
            session = false;
            state.state = State::Offline;
        } else if (!desired.play) {
            // Web-file pause is not supported by Audio::pauseResume(). Keep its
            // decoder/buffer intact and stop feeding I2S; the UI mutes the amp.
            // TCP backpressure holds the download until playback resumes.
            state.state = session ? State::Paused : State::Ready;
            progressAt = millis();
        } else if (failed) {
            state.state = State::Error;
        } else if (!session) {
            state.state = State::Connecting;
            publish(state);
            ended.store(false);
            if (!audio) {
                audio = new (std::nothrow) Audio(false, 3, I2S_NUM_1);
                if (!audio || !audio->setPinout(MAX98357A_BCLK, MAX98357A_LRCLK, MAX98357A_DATA)) {
                    delete audio; audio = nullptr;
                    failed = true; state.state = State::NoMemory; publish(state);
                    continue;
                }
                audio->setBufsize(16 * 1024, 64 * 1024);
                audio->setConnectionTimeout(2500, 3500);
                audio->forceMono(true);
                audio->setVolumeSteps(100);
                volume = FactoryVolume::get();
                audio->setVolume(volume);
            }
            session = audio->connecttohost(playbackUrl);
            failed = !session;
            progressAt = millis();
            previousTime = 0;
            state.seconds = state.duration = 0;
            state.state = session ? State::Buffering : State::Error;
        } else {
            const uint8_t master = FactoryVolume::get();
            if (master != volume) { audio->setVolume(master); volume = master; }
            audio->loop();
            state.seconds = audio->getAudioCurrentTime();
            state.duration = audio->getAudioFileDuration();
            if (state.seconds != previousTime) {
                previousTime = state.seconds;
                progressAt = millis();
            }
            if (ended.load()) {
                state.state = State::Finished;
                // Wait for the UI's next-track command; never restart this URL.
                desired.play = false;
            } else if (!audio->isRunning() || millis() - progressAt >= 20000) {
                audio->stopSong();
                failed = true;
                state.state = State::Error;
            } else {
                state.state = state.seconds ? State::Playing : State::Buffering;
            }
        }
        // Preserve Finished until a new command arrives, so a slow render
        // cannot miss the end-of-song event.
        if (ended.load() && session && !failed) state.state = State::Finished;
        publish(state);
        vTaskDelay(1);
    }
    delete audio; // Closes socket, frees decoders and uninstalls I2S before ACK.
    xSemaphoreGive(stopped);
    vTaskDelete(nullptr);
}

void text(const String &value, int x, int y, int size = 1, uint16_t color = ink) {
    auto &c = factorySurface();
    c.setFont(nullptr);
    c.setTextSize(size); c.setTextColor(color); c.setCursor(x, y); c.print(value);
}
void utfText(const char *value, int x, int y, int width, uint16_t color = ink, uint16_t background = WHITE) {
    factoryDrawText(factorySurface(), value, x, y, width, color, background);
}
String timeText(uint32_t seconds) {
    char value[20];
    snprintf(value, sizeof(value), "%lu:%02lu", static_cast<unsigned long>(seconds / 60),
             static_cast<unsigned long>(seconds % 60));
    return value;
}
const char *statusText() {
    switch (shown.state) {
    case State::Offline: return "Connect Wi-Fi to listen";
    case State::Connecting: return "Connecting to music server...";
    case State::Buffering: return "Buffering...";
    case State::Playing: return "Now playing";
    case State::Paused: return "Paused";
    case State::Finished: return "Track finished";
    case State::Error: return "Stream unavailable - tap play";
    case State::NoMemory: return "Player unavailable - reopen app";
    default: return "Tap play to start";
    }
}
void skipIcon(int x, int y, bool next) {
    auto &c = factorySurface();
    const int sign = next ? 1 : -1;
    c.fillTriangle(x - sign * 7, y - 10, x - sign * 7, y + 10, x + sign * 6, y, ink);
    c.fillRoundRect(x + sign * 10 - 2, y - 10, 4, 20, 1, ink);
}
bool hit(int x, int y, int left, int top, int w, int h) {
    return x >= left && x < left + w && y >= top && y < top + h;
}
void button(const char *label, int x, int y, int w, int h, bool primary = false) {
    factorySurface().fillRoundRect(x, y, w, h, 7, primary ? purple : pale);
    text(label, x + (w - int(strlen(label)) * 6) / 2, y + (h - 8) / 2, 1, primary ? WHITE : ink);
}
const char *keys(int row) {
    static const char *letters[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const char *numbers[] = {"1234567890", "-.'&()/!?", ",:+_@#"};
    return keyboardMode ? numbers[row] : letters[row];
}
void searchHeader(const char *title) {
    factorySurface().fillScreen(WHITE);
    button("< Back", 10, 6, 66, 30);
    text(title, 88, 12, 2);
    text("iTunes / PREVIEW", 370, 18, 1, purple);
}
void paintKeyboard() {
    searchHeader("Search");
    auto &c = factorySurface();
    c.fillRoundRect(10, 43, 370, 28, 6, pale);
    const String visible = query.length() > 28 ? "<" + query.substring(query.length() - 27) : query;
    text(visible.length() ? visible : "Song / artist", 18, 49, visible.length() ? 2 : 1, ink);
    button("Clear", 390, 43, 80, 28);
    text(searchHint.length() ? searchHint : "English / pinyin / numbers", 12, 76, 1, muted);
    for (int row = 0; row < 3; ++row) {
        const char *line = keys(row);
        const int n = strlen(line), left = (480 - n * 46) / 2;
        for (int i = 0; i < n; ++i) {
            char label[2] = {line[i], 0};
            button(label, left + i * 46, 89 + row * 30, 43, 27);
        }
    }
    button(keyboardMode ? "ABC" : "123", 10, 183, 60, 31);
    button("Space", 80, 183, 120, 31);
    button("Delete", 210, 183, 90, 31);
    button("Search", 310, 183, 160, 31, true);
}
void paintResults() {
    searchHeader("Results");
    if (searchBusy || searchHint.length() || !results.count) {
        text(searchBusy ? "Searching..." : searchHint.length() ? searchHint : "No playable previews found",
             32, 83, 1, ink);
        text("Search a song title or artist name", 32, 111, 1, muted);
        text("Short previews only - not full songs", 32, 134, 1, purple);
        button("Edit search", 10, 183, 460, 31, true);
        return;
    }
    for (int i = 0; i < 3 && firstResult + i < results.count; ++i) {
        const int top = 43 + i * 46;
        const auto &track = results.tracks[firstResult + i];
        auto &c = factorySurface();
        const uint16_t rowColor = i % 2 ? RGB565(246, 248, 252) : pale;
        c.fillRoundRect(10, top, 460, 43, 8, rowColor);
        c.fillTriangle(24, top + 15, 24, top + 29, 34, top + 22, purple);
        utfText(track.title, 46, top + 3, 340, ink, rowColor);
        utfText(track.artist, 46, top + 23, 376, muted, rowColor);
        utfText("试听", 412, top + 3, 54, purple, rowColor);
    }
    button("< Prev", 10, 183, 84, 31);
    button("Edit search", 104, 183, 176, 31, true);
    text(String(firstResult / 3 + 1) + "/" + String((results.count + 2) / 3), 295, 195, 1, muted);
    button("Next >", 346, 183, 124, 31);
}
void paint() {
    if (view == View::Keyboard) { paintKeyboard(); return; }
    if (view == View::Results) { paintResults(); return; }
    auto &c = factorySurface();
    c.fillScreen(WHITE);
    text("Music", 18, 15, 2);
    text(String(control.track + 1) + " / " + String(catalogCount), 98, 20, 1, muted);
    if (preview) {
        c.fillRoundRect(160, 12, 78, 24, 8, pale);
        text("PREVIEW", 178, 20, 1, purple);
    }
    button("Search", 252, 8, 98, 32);
    c.fillRoundRect(362, 8, 106, 32, 12, online ? RGB565(232, 248, 241) : pale);
    const uint16_t wifiColor = online ? green : purple;
    for (int i = 0; i < 3; ++i) c.fillRoundRect(373 + 6 * i, 28 - 5 * i, 4, 5 + 5 * i, 1, wifiColor);
    text(online ? "Wi-Fi" : "Connect", 399, 20, 1, wifiColor);

    // Colored vinyl artwork; each track has its own label color.
    const uint16_t colors[] = {purple, RGB565(29, 155, 166), RGB565(219, 105, 142)};
    const uint16_t accent = colors[control.track % 3];
    c.fillRoundRect(16, 52, 144, 150, 18, line);
    c.fillRoundRect(16, 49, 144, 150, 18, pale);
    c.fillCircle(88, 121, 54, ink);
    c.drawCircle(88, 121, 46, RGB565(68, 73, 94));
    c.drawCircle(88, 121, 38, RGB565(68, 73, 94));
    c.fillCircle(88, 121, 25, accent);
    c.fillCircle(88, 121, 5, WHITE);
    c.drawLine(49, 95, 62, 82, RGB565(165, 170, 192));
    c.drawLine(52, 98, 65, 85, RGB565(165, 170, 192));
    const auto &track = catalog[control.track];
    utfText(track.title, 180, 55, 282);
    utfText(track.artist, 180, 80, 282, muted);
    if (preview) utfText("iTunes 试听", 28, 178, 124, purple, pale);
    text(statusText(), 180, 105, 1, shown.state == State::Error ? RGB565(201, 61, 90) : accent);
    c.fillRoundRect(180, 128, 282, 4, 2, line);
    if (shown.duration && shown.seconds) {
        const uint32_t elapsed = min(shown.seconds, shown.duration);
        const int filled = uint64_t(elapsed) * 282 / shown.duration;
        c.fillRoundRect(180, 128, max(filled, 4), 4, 2, accent);
    }
    text(timeText(shown.seconds), 180, 140, 1, muted);
    const String duration = shown.duration ? timeText(shown.duration) : "--:--";
    text(duration, 462 - duration.length() * 6, 140, 1, muted);
    c.fillCircle(321, 183, 25, accent);
    if (control.play && shown.state != State::Error && shown.state != State::NoMemory &&
        shown.state != State::Offline && shown.state != State::Finished) {
        c.fillRoundRect(313, 174, 5, 18, 1, WHITE);
        c.fillRoundRect(324, 174, 5, 18, 1, WHITE);
    } else c.fillTriangle(315, 172, 315, 194, 332, 183, WHITE);
    skipIcon(247, 183, false);
    skipIcon(395, 183, true);
}
void render() {
    if (!screen || wifiPage) return;
    auto &c = factorySurface();
    c.setTextWrap(false); c.setFont(nullptr);
    c.present(*screen, paint, force);
    force = dirty = false; lastPaint = millis();
}
void sendControl(bool search = false) {
    Command message;
    message.playback = control;
    message.search = search;
    FactoryMusicSearch::copyUtf8(message.query, sizeof(message.query), query.c_str());
    FactoryMusicSearch::copyUtf8(message.url, sizeof(message.url), catalog[control.track].url);
    requestedRevision.store(control.revision);
    if (commands) xQueueOverwrite(commands, &message);
}
void cancelSearch() {
    searchBusy = false;
    ++control.revision;
    control.play = false;
    sendControl();
    if (searchReplies) xQueueReset(searchReplies);
}
void backFromWifi() {
    wifiPage = false; force = true;
    screen->setRotation(1);
    render();
}
void openWifi() {
    if (searchBusy) cancelSearch();
    control.play = false;
    sendControl();
    digitalWrite(MAX98357A_SD_MODE, LOW);
    wifiPage = true;
    FactoryNetwork::openWifi(*screen, backFromWifi);
}
void startSearch() {
    query.trim();
    if (!query.length()) { searchHint = "Enter a song or artist first"; return; }
    if (!online) { openWifi(); return; }
    if (!commands) { searchHint = "Player unavailable - reopen app"; return; }
    if (lastSearch && millis() - lastSearch < 3500) { searchHint = "Please wait a moment before searching again"; return; }
    lastSearch = millis();
    control.play = false;
    pendingSearch = ++control.revision;
    searchBusy = true; searchHint = ""; results.count = 0; firstResult = 0;
    view = View::Results; force = true;
    if (searchReplies) xQueueReset(searchReplies);
    sendControl(true);
}
void searchTouch(int x, int y) {
    if (hit(x,y,10,6,66,30)) {
        cancelSearch(); view = View::Player; force = true; render(); return;
    }
    if (view == View::Results) {
        if (y >= 183 && y < 214) {
            if (searchBusy || searchHint.length() || !results.count || hit(x,y,104,183,176,31)) {
                cancelSearch(); view = View::Keyboard; searchHint = ""; force = true;
            } else if (hit(x,y,10,183,84,31)) firstResult = max(0, firstResult - 3);
            else if (hit(x,y,346,183,124,31) && firstResult + 3 < results.count) firstResult += 3;
        } else if (!searchBusy && !searchHint.length() && x >= 10 && x < 470 && y >= 43 && y < 181) {
            const int index = firstResult + (y - 43) / 46;
            if ((y - 43) % 46 < 43 && index < results.count) {
                catalogCount = results.count;
                memcpy(catalog, results.tracks, catalogCount * sizeof(catalog[0]));
                preview = true; control.track = index; control.play = true; ++control.revision;
                view = View::Player; force = true; sendControl();
            }
        }
        render(); return;
    }
    if (hit(x,y,390,43,80,28)) query = "";
    else if (y >= 89 && y < 179) {
        const int row = (y - 89) / 30;
        const char *line = keys(row);
        const int n = strlen(line), left = (480 - n * 46) / 2;
        const int index = (x - left) / 46;
        if (x >= left && index < n && (x - left) % 46 < 43 && (y - 89) % 30 < 27) {
            if (query.length() < 64) { query += line[index]; searchHint = ""; }
            else searchHint = "Maximum 64 characters";
        }
    } else if (hit(x,y,10,183,60,31)) keyboardMode = !keyboardMode;
    else if (hit(x,y,80,183,120,31) && query.length() < 64) query += ' ';
    else if (hit(x,y,210,183,90,31) && query.length()) query.remove(query.length() - 1);
    else if (hit(x,y,310,183,160,31)) startSearch();
    render();
}
} // namespace

// Audio's end-of-stream callback executes on playerTask, never on the UI task.
void streamEnded() { ended.store(true); }

void open(Arduino_GFX &display) {
    screen = &display;
    display.setRotation(1);
    wifiPage = false; force = dirty = true; searchBusy = false; view = View::Player;
    if (!catalogCount) {
        static_assert(FactoryMusicPlaylist::count <= FactoryMusicSearch::Limit, "At most 8 demo tracks");
        catalogCount = FactoryMusicPlaylist::count;
        for (int i = 0; i < catalogCount; ++i) {
            const auto &source = FactoryMusicPlaylist::tracks[i];
            auto &target = catalog[i];
            FactoryMusicSearch::copyUtf8(target.title, sizeof(target.title), source.title);
            FactoryMusicSearch::copyUtf8(target.artist, sizeof(target.artist), source.subtitle);
            FactoryMusicSearch::copyUtf8(target.url, sizeof(target.url), source.url);
        }
    }
    online = WiFi.status() == WL_CONNECTED;
    control.play = false;
    ++control.revision;
    shown = Snapshot(); shown.revision = control.revision;
    shown.state = online ? State::Ready : State::Offline;
    publish(shown);
    quitting.store(false); ended.store(false);
    commands = xQueueCreate(1, sizeof(Command));
    searchReplies = xQueueCreate(1, sizeof(FactoryMusicSearch::Results));
    stopped = xSemaphoreCreateBinary();
    bool started = false;
    if (commands && stopped && searchReplies) {
        sendControl();
        started = xTaskCreatePinnedToCore(playerTask, "wifi-music", 12288, nullptr, 2, nullptr, 0) == pdPASS;
    }
    if (!started) {
        if (commands) vQueueDelete(commands);
        if (searchReplies) vQueueDelete(searchReplies);
        if (stopped) vSemaphoreDelete(stopped);
        commands = searchReplies = nullptr; stopped = nullptr;
        shown.state = State::NoMemory; publish(shown);
    }
    render();
}
void close() {
    digitalWrite(MAX98357A_SD_MODE, LOW);
    if (wifiPage) FactoryNetwork::close();
    if (stopped) {
        quitting.store(true);
        // Never force-delete a task while TLS/I2S owns memory or locks.
        xSemaphoreTake(stopped, portMAX_DELAY);
        vQueueDelete(commands); vQueueDelete(searchReplies); vSemaphoreDelete(stopped);
        commands = searchReplies = nullptr; stopped = nullptr;
    }
    screen = nullptr; wifiPage = false;
}
void touch(int16_t x, int16_t y) {
    if (wifiPage) { FactoryNetwork::touch(x, y); return; }
    if (view != View::Player) { searchTouch(x, y); return; }
    if (hit(x,y,252,8,98,32)) {
        cancelSearch(); digitalWrite(MAX98357A_SD_MODE, LOW);
        view = View::Keyboard; searchHint = ""; force = true; render(); return;
    }
    if (x >= 354 && y >= 4 && y < 44) { openWifi(); return; }
    if (!commands || y < 156 || y >= 216) return;
    if (x >= 217 && x < 277) control.step(-1, catalogCount);
    else if (x >= 365 && x < 425) control.step(1, catalogCount);
    else if (x >= 291 && x < 351) {
        if (!online) { openWifi(); return; }
        control.toggle(shown.state == State::Error || shown.state == State::Finished);
    } else return;
    sendControl();
    if (!control.play) digitalWrite(MAX98357A_SD_MODE, LOW);
    render();
}
void tick() {
    if (!screen) return;
    const bool wasOnline = online;
    const Snapshot previous = shown;
    online = WiFi.status() == WL_CONNECTED;
    if (searchReplies && xQueueReceive(searchReplies, &results, 0) == pdTRUE &&
        searchBusy && results.revision == pendingSearch) {
        searchBusy = false; dirty = true;
        using Status = FactoryMusicSearch::Status;
        switch (results.status) {
        case Status::Ok: searchHint = ""; break;
        case Status::Offline: searchHint = "Wi-Fi disconnected - reconnect and try again"; break;
        case Status::RateLimited: searchHint = "Too many searches - please try again later"; break;
        case Status::Cancelled: searchHint = "Search timed out - please try again"; break;
        default: searchHint = "Search unavailable - please try again"; break;
        }
    }
    const Snapshot state = readSnapshot();
    if (state.revision == control.revision) {
        shown = state;
        if (state.state == State::Finished && control.play && !wifiPage) {
            control.step(1, catalogCount);
            sendControl();
        }
    } else {
        shown = Snapshot();
        shown.state = online ? State::Connecting : State::Offline;
    }
    const bool audible = !wifiPage && control.play && online &&
        (shown.state == State::Playing || shown.state == State::Buffering);
    digitalWrite(MAX98357A_SD_MODE, audible ? HIGH : LOW);
    if (previous.state != shown.state || previous.seconds != shown.seconds ||
        previous.duration != shown.duration || wasOnline != online) dirty = true;
    if (!wifiPage && (force || dirty) && millis() - lastPaint >= 120) render();
}
void drawIcon(Arduino_GFX &display, int16_t x, int16_t y, int16_t clipTop, int16_t clipBottom) {
    uint16_t row[55];
    for (int dy = -27; dy <= 27; ++dy) {
        if (y + dy < clipTop || y + dy >= clipBottom) continue;
        const int half = sqrtf(27 * 27 - dy * dy);
        for (int dx = -half; dx <= half; ++dx) row[dx + half] = FactoryMusicIcon::pixel(dx, dy);
        display.draw16bitRGBBitmap(x - half, y + dy, row, 2 * half + 1, 1);
    }
}
} // namespace FactoryMusic

void audio_eof_stream(const char *) { FactoryMusic::streamEnded(); }
