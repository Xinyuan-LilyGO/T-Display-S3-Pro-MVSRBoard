#include "FactoryNetwork.h"
#include "FactoryNetworkRules.h"
#include "FactorySurface.h"
#include "FactoryFlipClock.h"
#include "FactoryFlipDigits.h"
#include "FactorySettingsUI.h"
#include "FactoryDeviceRules.h"
#include "FactoryVolume.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <time.h>

namespace FactoryNetwork {
namespace {
enum class View { Settings, Clock, Networks, Keyboard, Status, Zone, Brightness, Volume, Battery };
struct Network { String ssid; int rssi = -100; bool secure = true; };
constexpr uint16_t bg = RGB565(242, 246, 252), ink = RGB565(33, 47, 72);
constexpr uint16_t muted = RGB565(109, 125, 148), blue = RGB565(53, 113, 224);
constexpr uint16_t green = RGB565(35, 164, 123), red = RGB565(213, 70, 91);
constexpr uint16_t cyan = RGB565(36, 220, 230);
constexpr int clockCardTop = 16, clockHalfHeight = 56, clockCardWidth = 64;
constexpr int clockGlyphWidth = 54, clockGlyphHeight = 88;
constexpr int clockZoneX = 102, clockZoneY = 146, clockZoneWidth = 210, clockControlHeight = 36;
constexpr int clockWifiX = 322, clockWifiWidth = 56;
Arduino_GFX *screen = nullptr;
Arduino_GFX *canvas = &factorySurface();
Preferences preferences;
Network networks[20];
View view = View::Settings;
bool rootClock = false, visible = false, dirty = true, force = true;
void (*wifiReturn)() = nullptr;
bool prefsReady = false, connected = false, connecting = false, saveOnSuccess = false;
bool scanning = false, scanFailed = false, showPassword = false, shift = false, editingSSID = false;
bool manualNetwork = false;
int networkCount = 0, firstNetwork = 0, keyboardMode = 0;
int timezoneMinutes = 480, draftZone = 480;
String savedSSID, savedPassword, selectedSSID, password, entry, hint, statusMessage;
bool selectedSecure = true;
uint32_t connectStarted = 0, scanStarted = 0, lastUI = 0;
time_t shownTime = 0;
tm clockTime = {};
bool validTime = false;
FactoryFlipClock flip;
FactorySettingsUI::State settingsState;
bool sliderDragging = false;

bool portrait(View page) {
    return page==View::Settings || page==View::Brightness || page==View::Volume || page==View::Battery ||
        (page==View::Zone && !rootClock);
}
void sliderValue(int x) {
    if (view==View::Brightness) FactoryDevice::setBrightness(FactoryDeviceRules::slider(x,10));
    else if (view==View::Volume) FactoryVolume::set(FactoryDeviceRules::slider(x,0));
    dirty = true;
}

void text(const String &value, int x, int y, int size = 1, uint16_t color = ink) {
    canvas->setTextSize(size); canvas->setTextColor(color);
    canvas->setCursor(x, y); canvas->print(value);
}
void centered(const String &value, int x, int y, int width, int size, uint16_t color) {
    text(value, x + (width - int(value.length()) * 6 * size) / 2, y, size, color);
}
String shorten(const String &value, int count) {
    return int(value.length()) > count ? value.substring(0, count - 3) + "..." : value;
}
bool hit(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}
void button(const char *label, int x, int y, int width, int height, bool primary = false) {
    canvas->fillRoundRect(x, y + 2, width, height, 7, RGB565(221, 229, 242));
    canvas->fillRoundRect(x, y, width, height, 7, primary ? blue : WHITE);
    centered(label, x, y + (height - 8) / 2, width, 1, primary ? WHITE : ink);
}
void wifiSymbol(Arduino_GFX &target, int x, int y, uint16_t color, bool online) {
    for (int radius = 8; radius <= 22; radius += 7) {
        for (int angle = -140; angle < -40; angle += 4) {
            const float a = angle * 0.0174533f, b = (angle + 4) * 0.0174533f;
            for (int thickness = 0; thickness < 2; ++thickness)
                target.drawLine(x + lroundf(cosf(a) * (radius + thickness)),
                                y + 10 + lroundf(sinf(a) * (radius + thickness)),
                                x + lroundf(cosf(b) * (radius + thickness)),
                                y + 10 + lroundf(sinf(b) * (radius + thickness)), color);
        }
    }
    target.fillCircle(x, y + 10, 3, color);
    if (!online) {
        target.drawLine(x - 17, y - 17, x + 18, y + 16, color);
        target.drawLine(x - 16, y - 17, x + 19, y + 16, color);
    }
}
void header(const char *title) {
    canvas->fillScreen(bg);
    button("< Back", 10, 8, 66, 29);
    text(title, 90, 14, 2);

}
void changeView(View next) {
    if (view != next || force) {
        if (screen) screen->setRotation(portrait(next) ? 0 : 1);
        sliderDragging = false;
        force = true;
        if (next == View::Clock) flip.initialized = false;
    }
    view = next; dirty = true;
}
void goRoot() {
    if (wifiReturn) {
        void (*callback)() = wifiReturn;
        close();
        callback();
        return;
    }
    changeView(rootClock ? View::Clock : View::Settings);
}

void startScan() {
    changeView(View::Networks);
    if (scanning || connecting) return;
    WiFi.scanDelete();
    networkCount = firstNetwork = 0;
    scanFailed = false;
    const int result = WiFi.scanNetworks(true, true);
    scanning = result == WIFI_SCAN_RUNNING;
    scanStarted = millis();
    if (!scanning) scanFailed = true;
}

void startConnection() {
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(selectedSSID.c_str(), selectedSecure ? password.c_str() : nullptr);
    connectStarted = millis();
    connecting = true;
    connected = false;
    saveOnSuccess = true;
    statusMessage = "Connecting...";
    changeView(View::Status);
}

void keyboard(bool ssid) {
    editingSSID = ssid;
    entry = ssid ? String() : password;
    hint = ssid ? "Enter a network name (max 32 bytes)" : "Password stays on this device";
    keyboardMode = 0; shift = false; showPassword = false;
    changeView(View::Keyboard);
}

const char *keys(int row) {
    static const char *letters[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const char *numbers[] = {"1234567890", "!@#$%^&*()", "-_=+[]{}?/"};
    static const char *symbols[] = {"`~\\|;:'\",.", "<>", ""};
    return keyboardMode == 0 ? letters[row] : keyboardMode == 1 ? numbers[row] : symbols[row];
}

void drawSettings() {
    FactorySettingsUI::draw(*canvas,FactorySettingsUI::Page::List,settingsState);
}
void drawNetworks() {
    header("Wi-Fi");
    button("Scan", 390, 15, 80, 27);
    if (connecting) {
        centered("Connection in progress...", 10, 92, 460, 2, blue);
    } else if (scanning) {
        centered("Finding nearby networks...", 10, 93, 460, 2, blue);
    } else if (!networkCount) {
        centered(scanFailed ? "Scan failed. Try again." : "No networks found", 10, 92, 460, 2, muted);
        centered("Use a nearby 2.4 GHz network", 10, 125, 460, 1, muted);
    } else {
        for (int row = 0; row < 4 && firstNetwork + row < networkCount; ++row) {
            const Network &net = networks[firstNetwork + row];
            const int y = 49 + row * 32;
            canvas->fillRoundRect(10, y, 460, 29, 6, WHITE);
            const int bars = net.rssi > -55 ? 4 : net.rssi > -67 ? 3 : net.rssi > -78 ? 2 : 1;
            for (int i = 0; i < 4; ++i)
                canvas->fillRect(20 + i * 5, y + 21 - i * 4, 3, 4 + i * 4,
                                 i < bars ? blue : RGB565(220, 227, 237));
            text(shorten(net.ssid, 31), 51, y + 7, 2);
            text(net.secure ? "LOCK" : "OPEN", 435, y + 11, 1, muted);
        }
    }
    button("< Prev", 10, 185, 75, 29);
    button("Next >", 91, 185, 75, 29);
    button("Manual / Hidden", 175, 185, 145, 29);
    button(connected ? "Connected" : "Status", 330, 185, 140, 29, true);
}

void drawKeyboard() {
    canvas->fillScreen(bg);
    button("< Back", 10, 5, 66, 27);
    text(editingSSID ? "Network name" : "Wi-Fi password", 90, 10, 2);
    text(editingSSID ? "2.4 GHz Wi-Fi" : shorten(selectedSSID, 35), 90, 30, 1, muted);
    canvas->fillRoundRect(10, 43, 371, 27, 5, WHITE);
    String value;
    if (editingSSID || showPassword) value = entry;
    else for (size_t i = 0; i < entry.length(); ++i) value += '*';
    if (value.length() > 29) value = "<" + value.substring(value.length() - 28);
    text(value.length() ? value : "_", 18, 49, 2, ink);
    button(editingSSID ? "Clear" : showPassword ? "Hide" : "Show", 390, 43, 80, 27);
    text(hint, 12, 74, 1, muted);
    for (int row = 0; row < 3; ++row) {
        const char *line = keys(row);
        const int count = strlen(line), left = (480 - count * 46) / 2;
        for (int i = 0; i < count; ++i) {
            char label[2] = {line[i], 0};
            if (shift && keyboardMode == 0) label[0] = toupper(label[0]);
            button(label, left + i * 46, 86 + row * 30, 43, 26);
        }
    }
    button(shift ? "SHIFT" : "Shift", 10, 182, 52, 32, shift);
    button(keyboardMode == 0 ? "123" : keyboardMode == 1 ? "#+=" : "abc", 68, 182, 52, 32);
    button("Space", 126, 182, 98, 32);
    button("Delete", 230, 182, 74, 32);
    button(editingSSID ? "Next" : "Connect", 310, 182, 160, 32, true);
}

void drawStatus() {
    header("Connection");
    const uint16_t color = connected ? green : connecting ? blue : red;
    wifiSymbol(*canvas, 240, 66, color, connected || connecting);
    centered(connected ? "Connected" : statusMessage.length() ? statusMessage : "Not connected",
             10, 100, 460, 2, color);
    centered(shorten(connected ? WiFi.SSID() : selectedSSID, 48), 10, 131, 460, 1, ink);
    centered(connected ? WiFi.localIP().toString() : connecting ? "Please wait (up to 20 seconds)" :
             "Check password and 2.4 GHz signal", 10, 150, 460, 1, muted);
    button(connecting ? "Cancel" : "Networks", 55, 181, 175, 32);
    button("Done", 250, 181, 175, 32, true);
}

void drawFlipHalf(int digit, int x, bool topHalf, int height, uint8_t brightness = 255) {
    if (height <= 0) return;
    constexpr int seam = clockCardTop + clockHalfHeight;
    const int panelY = topHalf ? seam - height : seam;
    const uint8_t level = (topHalf ? 28 : 22) * brightness / 255;
    const uint16_t panel = RGB565(level, level, level);
    canvas->fillRoundRect(x, panelY, clockCardWidth, height, 5, panel);
    // Project the corresponding half of the glyph onto the moving flap.
    const int first = max(panelY, factorySurface().clipTop());
    const int end = min(panelY + height, factorySurface().clipBottom());
    const int glyph = digit < 0 ? 10 : digit;
    for (int y = first; y < end; ++y) {
        const int glyphRow = (y - panelY) * clockHalfHeight / height +
                             (topHalf ? 0 : clockHalfHeight) -
                             (2 * clockHalfHeight - clockGlyphHeight) / 2;
        if (glyphRow < 0 || glyphRow >= clockGlyphHeight) continue;
        const int sourceRow = glyphRow * 72 / clockGlyphHeight;
        int run = 0;
        uint16_t runColor = panel;
        for (int dx = 0; dx <= clockGlyphWidth; ++dx) {
            uint16_t color = panel;
            if (dx < clockGlyphWidth) {
                const int sourceX = dx * 48 / clockGlyphWidth;
                const uint8_t packed = pgm_read_byte(&kFlipDigits[glyph][sourceRow * 24 + sourceX / 2]);
                const int alpha = sourceX & 1 ? packed & 15 : packed >> 4;
                const int shade = level + (245 * brightness / 255 - level) * alpha / 15;
                color = RGB565(shade, shade, shade);
            }
            if (dx == 0) runColor = color;
            if (color != runColor || dx == clockGlyphWidth) {
                if (runColor != panel)
                    canvas->drawFastHLine(x + (clockCardWidth - clockGlyphWidth) / 2 + run,
                                          y, dx - run, runColor);
                run = dx; runColor = color;
            }
        }
    }
}

void drawFlipCard(int index, int x) {
    const int current = flip.current[index], old = flip.previous[index];
    const float p = flip.progress;
    const bool moving = flip.animating && old != current;
    constexpr int seam = clockCardTop + clockHalfHeight;
    canvas->fillRoundRect(x, clockCardTop + 2, clockCardWidth, clockHalfHeight * 2, 6, RGB565(8,8,8));
    drawFlipHalf(current, x, true, clockHalfHeight);
    drawFlipHalf(moving ? old : current, x, false, clockHalfHeight);
    if (moving && p < 0.5f) {
        const int height = lroundf(cosf(p * 3.14159265f) * clockHalfHeight);
        drawFlipHalf(old, x, true, height, 255 - int(160 * p));
    } else if (moving) {
        const int height = lroundf(sinf((p - 0.5f) * 3.14159265f) * clockHalfHeight);
        drawFlipHalf(current, x, false, height, 175 + int(160 * (p - 0.5f)));
    }
    canvas->fillRect(x + 1, seam - 1, clockCardWidth - 2, 2, BLACK);
    canvas->fillRoundRect(x + 1, seam - 4, 3, 8, 1, RGB565(68,68,68));
    canvas->fillRoundRect(x + clockCardWidth - 4, seam - 4, 3, 8, 1, RGB565(68,68,68));
}

void drawClock() {
    canvas->fillScreen(BLACK);
    const int positions[] = {13,83,173,243,333,403};
    for (int i = 0; i < 6; ++i) drawFlipCard(i, positions[i]);
    for (int x : {160, 320}) {
        canvas->fillCircle(x, 58, 2, RGB565(156,156,156));
        canvas->fillCircle(x, 86, 2, RGB565(156,156,156));
    }
    char zone[20]; FactoryNetworkRules::zoneLabel(timezoneMinutes, zone, sizeof(zone));
    canvas->fillRoundRect(clockZoneX, clockZoneY, clockZoneWidth, clockControlHeight, 8, RGB565(22,22,22));
    centered(String(zone) + "  >", clockZoneX, clockZoneY + 10, clockZoneWidth, 2, RGB565(215,215,215));
    canvas->fillRoundRect(clockWifiX, clockZoneY, clockWifiWidth, clockControlHeight, 8, RGB565(22,22,22));
    wifiSymbol(*canvas, clockWifiX + clockWifiWidth / 2, clockZoneY + 18, connected ? green : red, connected);
    static const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    if (validTime) {
        char date[20]; strftime(date, sizeof(date), "%Y-%m-%d", &clockTime);
        centered(String(days[clockTime.tm_wday]) + "  |  " + date,
                 0, 199, 480, 2, RGB565(178,178,178));
    } else {
        centered(connected ? "Syncing internet time..." : "Tap Wi-Fi to set the clock", 0, 202, 480, 1, muted);
    }
}
void drawZone() {
    header("Time zone");
    char zone[20]; FactoryNetworkRules::zoneLabel(draftZone, zone, sizeof(zone));
    centered(zone, 10, 65, 460, 3, blue);
    centered("Manual UTC offset / 15-minute steps", 10, 104, 460, 1, muted);
    button("-1 hour", 20, 129, 104, 35);
    button("-15 min", 132, 129, 104, 35);
    button("+15 min", 244, 129, 104, 35);
    button("+1 hour", 356, 129, 104, 35);
    button("Cancel", 70, 180, 155, 33);
    button("Apply", 255, 180, 155, 33, true);
}

void paint() {
    switch (view) {
        case View::Clock: drawClock(); break;
        case View::Settings: drawSettings(); break;
        case View::Networks: drawNetworks(); break;
        case View::Keyboard: drawKeyboard(); break;
        case View::Status: drawStatus(); break;
        case View::Zone:
            if (rootClock) drawZone();
            else FactorySettingsUI::draw(*canvas,FactorySettingsUI::Page::Zone,settingsState);
            break;
        case View::Brightness: FactorySettingsUI::draw(*canvas,FactorySettingsUI::Page::Brightness,settingsState); break;
        case View::Volume: FactorySettingsUI::draw(*canvas,FactorySettingsUI::Page::Volume,settingsState); break;
        case View::Battery: FactorySettingsUI::draw(*canvas,FactorySettingsUI::Page::Battery,settingsState); break;
    }
}
void render() {
    if (!visible) return;
    // Take one snapshot for all strips so a second cannot change halfway through a frame.
    shownTime = time(nullptr);
    validTime = shownTime >= 1700000000;
    const time_t local = shownTime + timezoneMinutes * 60;
    gmtime_r(&local, &clockTime);
    if (validTime) strftime(settingsState.clock,sizeof(settingsState.clock),"%H:%M",&clockTime);
    else snprintf(settingsState.clock,sizeof(settingsState.clock),"--:--");
    settingsState.brightness = FactoryDevice::brightness(); settingsState.volume = FactoryVolume::get();
    settingsState.battery = FactoryDevice::battery();
    settingsState.wifi = connected ? "Connected" : connecting ? "Connecting..." : "Not connected";
    FactoryNetworkRules::zoneLabel(view==View::Zone ? draftZone : timezoneMinutes,settingsState.zone,sizeof(settingsState.zone));
    if (view == View::Clock)
        flip.update(clockTime.tm_hour, clockTime.tm_min, clockTime.tm_sec, validTime, millis());
    factorySurface().present(*screen, paint, force);
    force = dirty = false;
    lastUI = millis();
}

void acceptKey() {
    if (editingSSID) {
        if (!entry.length()) { hint = "Network name cannot be empty"; return; }
        selectedSSID = entry;
        selectedSecure = true;
        password = "";
        keyboard(false);
        hint = "Leave empty only for an open network";
    } else {
        if (!entry.length() && manualNetwork) selectedSecure = false;
        else if (!FactoryNetworkRules::validPassword(entry.c_str())) {
            hint = "Use 8-63 characters (or 64 hex digits)"; return;
        }
        password = entry;
        startConnection();
    }
}
void keyboardTouch(int x, int y) {
    if (hit(x,y,10,5,66,27)) { entry = ""; password = ""; changeView(View::Networks); return; }
    if (hit(x,y,390,43,80,27)) {
        if (editingSSID) entry = ""; else showPassword = !showPassword;
        return;
    }
    if (y >= 86 && y < 176) {
        const int row = (y - 86) / 30;
        const char *line = keys(row);
        const int count = strlen(line), left = (480 - count * 46) / 2;
        const int index = (x - left) / 46;
        if (x >= left && index >= 0 && index < count &&
            hit(x,y,left+index*46,86+row*30,43,26)) {
            if (entry.length() >= (editingSSID ? 32u : 64u)) { hint = "Maximum length reached"; return; }
            char ch = line[index];
            if (shift && keyboardMode == 0) ch = toupper(ch);
            entry += ch;
        }
        return;
    }
    if (y < 182 || y >= 214) return;
    if (hit(x,y,10,182,52,32)) shift = !shift;
    else if (hit(x,y,68,182,52,32)) keyboardMode = (keyboardMode + 1) % 3;
    else if (hit(x,y,126,182,98,32) && entry.length() < (editingSSID ? 32u : 64u)) entry += ' ';
    else if (hit(x,y,230,182,74,32) && entry.length()) entry.remove(entry.length()-1);
    else if (hit(x,y,310,182,160,32)) acceptKey();
}
} // namespace

void begin() {
    prefsReady = preferences.begin("factory-net", false);
    if (prefsReady) {
        timezoneMinutes = FactoryNetworkRules::zone(preferences.getInt("tz", 480));
        savedSSID = preferences.getString("ssid", "");
        savedPassword = preferences.getString("pass", "");
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    if (savedSSID.length()) {
        selectedSSID = savedSSID;
        WiFi.begin(savedSSID.c_str(), savedPassword.length() ? savedPassword.c_str() : nullptr);
        connecting = true; connectStarted = millis();
        statusMessage = "Connecting...";
    }
}

void open(bool clock, Arduino_GFX &display) {
    wifiReturn = nullptr;
    screen = &display;
    rootClock = clock; visible = true; force = true;

    canvas->setTextWrap(false); canvas->setFont(nullptr);
    goRoot(); render();
}
void openWifi(Arduino_GFX &display, void (*onBack)()) {
    screen = &display;
    wifiReturn = onBack;
    visible = true; force = true;
    canvas->setTextWrap(false); canvas->setFont(nullptr);
    startScan(); render();
}
void close() {
    wifiReturn = nullptr;
    visible = false; screen = nullptr;
    entry = ""; showPassword = false;
    if (!connecting) password = "";
}
void refresh() { dirty = true; }
bool isPortrait() { return visible && portrait(view); }
void localTimeLabel(char *output,size_t size) {
    const time_t utc = time(nullptr);
    if (utc < 1700000000) { snprintf(output,size,"--:--"); return; }
    const time_t local = utc + timezoneMinutes*60;
    tm value = {}; gmtime_r(&local,&value); strftime(output,size,"%H:%M",&value);
}
void drag(int16_t x,int16_t, bool down) {
    if (!down) { sliderDragging = false; return; }
    if (visible && sliderDragging && (view==View::Brightness || view==View::Volume)) sliderValue(x);
}

void touch(int16_t x, int16_t y) {
    if (!visible) return;
    dirty = true;
    if (view == View::Keyboard) { keyboardTouch(x,y); return; }
    if (view == View::Clock) {
        if (hit(x,y,clockWifiX,clockZoneY,clockWifiWidth,clockControlHeight)) startScan();
        else if (hit(x,y,clockZoneX,clockZoneY,clockZoneWidth,clockControlHeight)) {
            draftZone = timezoneMinutes; changeView(View::Zone);
        }
        return;
    }
    if (view == View::Settings) {
        if (hit(x,y,12,FactorySettingsUI::ListTop,198,FactorySettingsUI::RowHeight*5)) {
            switch ((y-FactorySettingsUI::ListTop)/FactorySettingsUI::RowHeight) {
                case 0: startScan(); break;
                case 1: changeView(View::Brightness); break;
                case 2: changeView(View::Volume); break;
                case 3: changeView(View::Battery); break;
                case 4: draftZone=timezoneMinutes; changeView(View::Zone); break;
            }
        }
        return;
    }
    if (portrait(view)) {
        if (hit(x,y,8,36,130,38)) { goRoot(); return; }
        if (view==View::Brightness || view==View::Volume) {
            if (hit(x,y,18,266,186,66)) { sliderDragging = true; sliderValue(x); }
            else {
                const int step=hit(x,y,30,350,64,44)?-5:hit(x,y,128,350,64,44)?5:0;
                if (step && view==View::Brightness) FactoryDevice::setBrightness(FactoryDevice::brightness()+step);
                if (step && view==View::Volume) FactoryVolume::set(FactoryVolume::get()+step);
            }
        } else if (view==View::Zone) {
            if (hit(x,y,20,294,85,43)) draftZone-=60;
            else if (hit(x,y,117,294,85,43)) draftZone+=60;
            else if (hit(x,y,20,349,85,43)) draftZone-=15;
            else if (hit(x,y,117,349,85,43)) draftZone+=15;
            draftZone=FactoryNetworkRules::zone(draftZone);
            if (hit(x,y,20,414,182,44)) {
                timezoneMinutes=draftZone;
                if (prefsReady) preferences.putInt("tz",timezoneMinutes);
                goRoot();
            }
        }
        return;
    }
    if (hit(x,y,10,8,66,29)) { goRoot(); return; }
    if (view == View::Zone) {
        if (hit(x,y,20,129,104,35)) draftZone -= 60;
        else if (hit(x,y,132,129,104,35)) draftZone -= 15;
        else if (hit(x,y,244,129,104,35)) draftZone += 15;
        else if (hit(x,y,356,129,104,35)) draftZone += 60;
        draftZone = FactoryNetworkRules::zone(draftZone);
        if (hit(x,y,70,180,155,33)) goRoot();
        if (hit(x,y,255,180,155,33)) {
            timezoneMinutes = draftZone;
            if (prefsReady) preferences.putInt("tz", timezoneMinutes);
            goRoot();
        }
    } else if (view == View::Networks) {
        if (hit(x,y,390,15,80,27)) startScan();
        else if (hit(x,y,330,185,140,29)) changeView(View::Status);
        else if (!connecting && !scanning) {
            if (hit(x,y,10,185,75,29)) firstNetwork = max(0, firstNetwork - 4);
            else if (hit(x,y,91,185,75,29) && firstNetwork + 4 < networkCount) firstNetwork += 4;
            else if (hit(x,y,175,185,145,29)) { manualNetwork = true; keyboard(true); }
            else if (x >= 10 && x < 470 && y >= 49 && y < 177) {
                const int index = firstNetwork + (y-49)/32;
                if (index >= networkCount) return;
                selectedSSID = networks[index].ssid;
                manualNetwork = false;
                selectedSecure = networks[index].secure;
                password = selectedSSID == savedSSID ? savedPassword : String();
                if (connected && selectedSSID == WiFi.SSID()) changeView(View::Status);
                else if (!selectedSecure) { password = ""; startConnection(); }
                else keyboard(false);
            }
        }
    } else if (view == View::Status) {
        if (hit(x,y,250,181,175,32)) goRoot();
        if (hit(x,y,55,181,175,32)) {
            if (connecting) {
                WiFi.setAutoReconnect(false); WiFi.disconnect(false, false);
                connecting = saveOnSuccess = false;
                password = ""; statusMessage = "Connection cancelled";
            }
            startScan();
        }
    }
}

void tick() {
    const uint32_t now = millis();
    const bool online = WiFi.status() == WL_CONNECTED &&
                         (!connecting || WiFi.SSID() == selectedSSID);
    if (online && !connected) {
        connected = true; connecting = false;
        configTime(0, 0, "ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org");
        statusMessage = "Connected";
        if (saveOnSuccess) {
            savedSSID = selectedSSID; savedPassword = password;
            if (prefsReady) {
                preferences.putString("ssid", savedSSID);
                preferences.putString("pass", savedPassword);
            }
        }
        saveOnSuccess = false; password = ""; entry = "";
        dirty = true;
    } else if (!online && connected) {
        connected = false; statusMessage = "Connection lost"; dirty = true;
    }
    if (connecting && !online && now - connectStarted >= 20000) {
        WiFi.setAutoReconnect(false); WiFi.disconnect(false, false);
        connecting = saveOnSuccess = false;
        password = ""; entry = "";
        statusMessage = "Could not connect";
        dirty = true;
    }
    if (scanning) {
        const int result = WiFi.scanComplete();
        if (result >= 0) {
            networkCount = 0;
            for (int i = 0; i < result && networkCount < 20; ++i) {
                const String ssid = WiFi.SSID(i);
                if (!ssid.length()) continue;
                bool duplicate = false;
                for (int j = 0; j < networkCount; ++j) duplicate |= networks[j].ssid == ssid;
                if (duplicate) continue;
                networks[networkCount].ssid = ssid;
                networks[networkCount].rssi = WiFi.RSSI(i);
                networks[networkCount].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
                ++networkCount;
            }
            WiFi.scanDelete(); scanning = false; dirty = true;
        } else if (result == WIFI_SCAN_FAILED || now - scanStarted > 15000) {
            esp_wifi_scan_stop(); WiFi.scanDelete();
            scanning = false; scanFailed = true; dirty = true;
        }
    }
    if (visible && portrait(view)) {
        const auto b=FactoryDevice::battery(); const auto &old=settingsState.battery;
        if (settingsState.volume!=FactoryVolume::get() || settingsState.brightness!=FactoryDevice::brightness() ||
            time(nullptr)/60!=shownTime/60 || b.percent!=old.percent || b.millivolts!=old.millivolts ||
            b.available!=old.available || b.usb!=old.usb || b.charging!=old.charging || b.full!=old.full) dirty=true;
    }
    if (visible && ((view == View::Clock && time(nullptr) != shownTime) ||
                    ((dirty || (view == View::Clock && flip.animating)) && now - lastUI >= 24))) render();
}

void drawIcon(Arduino_GFX &display, bool clock, int16_t x, int16_t y) {
    if (clock) {
        display.fillCircle(x,y,23,blue);
        display.fillCircle(x,y,19,WHITE);
        display.fillRoundRect(x-2,y-14,4,16,2,blue);
        display.drawLine(x,y,x+12,y+7,cyan);
        display.drawLine(x,y+1,x+12,y+8,cyan);
        display.fillCircle(x,y,3,blue);
    } else {
        for (int i=0;i<8;++i) {
            const float a = i * 0.785398f;
            display.fillCircle(x+lroundf(cosf(a)*21),y+lroundf(sinf(a)*21),5,muted);
        }
        display.fillCircle(x,y,21,muted);
        display.fillCircle(x,y,10,WHITE);
        display.fillCircle(x,y,6,blue);
    }
}
} // namespace FactoryNetwork
