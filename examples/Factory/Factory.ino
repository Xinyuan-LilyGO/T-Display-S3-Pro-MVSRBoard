/*
 * T-Display-S3-Pro-MVSRBoard - Factory UI
 *
 * LilyGo_logo.png is converted to LilyGo_logo.h in this folder and rendered
 * from flash as an RGB565 image with a 1-bit transparency mask.
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "LilyGo_logo.h"
#include "FactoryApps.h"
#include "FactoryXiaozhi.h"
#include "FactoryButton.h"
#include "FactoryBreakout.h"
#include "FactoryVolume.h"
#include "FactoryNetwork.h"
#include "FactoryMusic.h"
#include "FactoryDevice.h"
#include "FactorySettingsUI.h"
#include "FactoryHome.h"
#include "FactoryHomeDisplay.h"
#include "FactoryMiniGames.h"
#include "FactorySurface.h"

// Keep Arduino's generated entry-point declarations outside the UI namespace.
void setup();
void loop();

namespace
{
constexpr uint16_t kBackground = WHITE;
constexpr uint16_t kBlack = BLACK;
constexpr uint16_t kProductText = RGB565(46, 52, 66);

constexpr uint8_t kLetterCount = 6;
constexpr int16_t kLetterWidth = 18;
constexpr int16_t kLilygoX = (LCD_WIDTH - kLetterCount * kLetterWidth) / 2;
constexpr int16_t kLilygoBaseY = 232;

constexpr uint8_t kAppCount = FactoryHome::AppCount;
constexpr uint8_t kAppColumns = FactoryHome::Columns;
constexpr int16_t kGridTop = FactoryHome::GridTop;
constexpr int16_t kRowHeight = FactoryHome::RowHeight;
constexpr int16_t kColumnWidth = LCD_WIDTH / kAppColumns;
constexpr int16_t kIconRadius = 32;
constexpr int16_t kIconCenterOffsetY = 40;
constexpr int16_t kAppLabelGap = 12;
constexpr uint8_t kAppTextSize = 2;
constexpr uint8_t kReturnButtonPin = GPIO_NUM_0; // Physical BOOT-0 button
constexpr uint8_t kSleepAppIndex = 9;
constexpr uint8_t kSettingsAppIndex = 0;
constexpr uint8_t kXiaozhiAppIndex = 1;
constexpr uint8_t kClockAppIndex = 2;
constexpr uint8_t kBreakoutAppIndex = 3;
constexpr uint8_t kPokerAppIndex = 4;
constexpr uint8_t kPianoAppIndex = 5;
constexpr uint8_t kMusicAppIndex = 8;
constexpr uint8_t kSnakeAppIndex = 6;
constexpr uint8_t kMazeAppIndex = 7;
constexpr uint32_t kSleepDelayMs = 5000;

const char *const kAppNames[kAppCount] = {
    "Settings", "Xiaozhi", "Clock", "Bricks",
    "PokerS3", "Piano", "Snake", "Maze", "Music", "Sleep"};

static_assert(LCD_WIDTH == FactoryHome::Width && LCD_HEIGHT == FactoryHome::Height,
              "Home layout expects the board's portrait resolution");

Arduino_DataBus *bus = new Arduino_HWSPI(
    LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);

Arduino_GFX *gfx = new Arduino_ST7796(
    bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT,
    49, 0, 49, 0);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> CST226SE(new Arduino_CST2xxSE(
    IIC_Bus, CST226SE_DEVICE_ADDRESS, TOUCH_RST, TOUCH_INT,
    Arduino_IIC_Touch_Interrupt));

enum class Page : uint8_t
{
    HOME,
    DEVELOPING,
    SLEEP,
    POKER,
    PIANO,
    BREAKOUT,
    SETTINGS,
    CLOCK,
    MUSIC,
    XIAOZHI,
    SNAKE,
    MAZE,
};

Page currentPage = Page::HOME;
bool touchReady = false;
uint32_t lastTouchMillis = 0;
FactoryButton homeButton(kReturnButtonPin);
bool touchDown = false;
bool ignoreTouchUntilRelease = false;
uint32_t lastTouchPollMillis = 0;
uint32_t sleepStartedMillis = 0;
uint8_t lastSleepSeconds = 0;
FactoryHome::Scroll homeScroll;
FactoryHomeDisplay homeDisplay(*bus, *gfx);
FactorySettingsUI::State homeStatus;
int16_t paintedHomeOffset = -1;
uint32_t lastHomePaintMillis = 0;

void Arduino_IIC_Touch_Interrupt(void)
{
    CST226SE->IIC_Interrupt_Flag = true;
}

void centerText(const char *text, int16_t y, uint8_t size, uint16_t color)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    gfx->setTextSize(size);
    gfx->getTextBounds(text, 0, y, &x1, &y1, &width, &height);
    gfx->setCursor((LCD_WIDTH - width) / 2 - x1, y);
    gfx->setTextColor(color);
    gfx->print(text);
}

void drawLetter(char letter, uint8_t index, int16_t y)
{
    gfx->setTextSize(3);
    gfx->setTextColor(kBlack);
    gfx->setCursor(kLilygoX + index * kLetterWidth, y);
    gfx->print(letter);
}

int16_t letterY(uint16_t frame, uint8_t index)
{
    // Each character has a different phase, so the word moves as a wave.
    const float phase = frame * 0.24f - index * 0.92f;
    const int16_t offset = static_cast<int16_t>(sinf(phase) * 7.0f);
    return kLilygoBaseY - offset;
}

void eraseLetter(uint8_t index, int16_t y)
{
    // Only the individual glyph area is cleared; the page is never refreshed.
    gfx->fillRect(kLilygoX + index * kLetterWidth, y - 2,
                  kLetterWidth, 28, kBackground);
}

void drawStaticStartupScene()
{
    constexpr int16_t logoX = (LCD_WIDTH - 120) / 2;
    constexpr int16_t logoY = 98;

    gfx->fillScreen(kBackground);
    gfx->draw16bitRGBBitmapWithMask(logoX, logoY,
                                    kLilyGoLogoPixels, kLilyGoLogoMask,
                                    kLilyGoLogoWidth, kLilyGoLogoHeight);
    centerText("T-Display S3 Pro MVSRBoard", 292, 1, kProductText);
}

void animateLilygo()
{
    constexpr char word[kLetterCount + 1] = "LILYGO";
    constexpr uint16_t totalFrames = 58;
    int16_t previousY[kLetterCount];

    for (uint8_t i = 0; i < kLetterCount; ++i)
    {
        previousY[i] = kLilygoBaseY;
        drawLetter(word[i], i, previousY[i]);
    }

    for (uint16_t frame = 0; frame < totalFrames; ++frame)
    {
        FactoryVolume::pollButtons();
        for (uint8_t i = 0; i < kLetterCount; ++i)
        {
            const int16_t nextY = letterY(frame, i);
            if (nextY == previousY[i])
            {
                continue;
            }

            eraseLetter(i, previousY[i]);
            drawLetter(word[i], i, nextY);
            previousY[i] = nextY;
        }
        delay(32);
    }

    for (uint8_t i = 0; i < kLetterCount; ++i)
    {
        eraseLetter(i, previousY[i]);
        drawLetter(word[i], i, kLilygoBaseY);
    }
}

void drawIconBackground(Arduino_GFX &canvas, int16_t x, int16_t y, int16_t radius)
{
    // Soft shadow below a white disc, with no dark outline.
    canvas.fillCircle(x + 1, y + 3, radius + 4, RGB565(248, 248, 248));
    canvas.fillCircle(x + 1, y + 3, radius + 3, RGB565(242, 242, 242));
    canvas.fillCircle(x + 1, y + 3, radius + 2, RGB565(234, 234, 234));
    canvas.fillCircle(x + 1, y + 3, radius + 1, RGB565(226, 226, 226));
    canvas.fillCircle(x, y, radius, kBackground);
}

void drawSleepZ(Arduino_GFX &canvas, int16_t x, int16_t y, int16_t width, int16_t height,
                uint16_t color)
{
    // Rounded strokes keep the small sleep letters legible at native resolution.
    const int16_t stroke = height <= 8 ? 2 : 3;
    canvas.fillRoundRect(x, y, width, stroke, 1, color);
    canvas.fillRoundRect(x, y + height - stroke, width, stroke, 1, color);
    for (int16_t offset = 0; offset < stroke; ++offset)
    {
        canvas.drawLine(x + width - 2, y + offset,
                       x + 1, y + height - stroke + offset, color);
    }
}

void drawSleepIcon(Arduino_GFX &canvas, int16_t x, int16_t y, int16_t radius)
{
    // A blue-to-violet crescent, opening toward the three descending Z letters.
    const int16_t moonX = x - radius / 12;
    const int16_t moonY = y + radius / 12;
    const int16_t moonRadius = radius * 4 / 5;
    const int16_t cutX = x + radius / 3;
    const int16_t cutY = y - radius / 3;
    for (int16_t dy = -moonRadius; dy <= moonRadius; ++dy)
    {
        const int16_t span = static_cast<int16_t>(sqrtf(
            moonRadius * moonRadius - dy * dy));
        const int16_t left = moonX - span;
        const int16_t right = moonX + span;
        const int16_t rowY = moonY + dy;
        const int16_t cutDy = rowY - cutY;
        const int16_t progress = dy + moonRadius;
        const uint8_t red = 90 + 40 * progress / (2 * moonRadius);
        const uint8_t green = 130 - 42 * progress / (2 * moonRadius);
        const uint8_t blue = 230 - 18 * progress / (2 * moonRadius);
        const uint16_t color = RGB565(red, green, blue);

        // Draw only the crescent itself so the white badge and shadow stay intact.
        if (abs(cutDy) <= moonRadius)
        {
            const int16_t cutSpan = static_cast<int16_t>(sqrtf(
                moonRadius * moonRadius - cutDy * cutDy));
            const int16_t leftEnd = min(right, static_cast<int16_t>(cutX - cutSpan - 1));
            const int16_t rightStart = max(left, static_cast<int16_t>(cutX + cutSpan + 1));
            if (leftEnd >= left)
            {
                canvas.drawFastHLine(left, rowY, leftEnd - left + 1, color);
            }
            if (rightStart <= right)
            {
                canvas.drawFastHLine(rightStart, rowY, right - rightStart + 1, color);
            }
        }
        else
        {
            canvas.drawFastHLine(left, rowY, right - left + 1, color);
        }
    }

    drawSleepZ(canvas, x - radius / 16, y - radius / 2,
                radius / 3, radius / 3, RGB565(108, 86, 196));
    drawSleepZ(canvas, x + radius * 2 / 5, y - radius / 6,
                radius / 4, radius / 4, RGB565(74, 143, 216));
    drawSleepZ(canvas, x + radius / 6, y + radius / 6,
                radius / 5, radius / 5, RGB565(89, 170, 190));
}

void drawApp(FactorySurface &canvas, uint8_t index)
{
    const uint8_t row = index / kAppColumns;
    const uint8_t column = index % kAppColumns;
    const int16_t centerX = column * kColumnWidth + kColumnWidth / 2;
    const int16_t centerY = kGridTop + row * kRowHeight + kIconCenterOffsetY - homeDisplay.paintOffset();
    if (centerY + kIconRadius + kAppLabelGap + 16 <= canvas.clipTop() ||
        centerY - kIconRadius - 1 >= canvas.clipBottom()) return;

    drawIconBackground(canvas, centerX, centerY, kIconRadius);
    if (index == kSettingsAppIndex || index == kClockAppIndex)
    {
        FactoryNetwork::drawIcon(canvas, index == kClockAppIndex, centerX, centerY);
    }
    else if (index == kXiaozhiAppIndex)
    {
        FactoryXiaozhi::drawIcon(canvas, centerX, centerY);
    }
    else if (index == kSleepAppIndex)
    {
        drawSleepIcon(canvas, centerX, centerY, kIconRadius);
    }
    else if (index == kBreakoutAppIndex)
    {
        FactoryBreakout::drawIcon(canvas, centerX, centerY);
    }
    else if (index == kPokerAppIndex)
    {
        canvas.fillRoundRect(centerX - 23, centerY - 22, 29, 39, 4, RGB565(38, 67, 120));
        canvas.fillRoundRect(centerX - 5, centerY - 16, 29, 39, 4, WHITE);
        canvas.drawRoundRect(centerX - 5, centerY - 16, 29, 39, 4, kBlack);
        canvas.fillCircle(centerX + 5, centerY - 2, 5, RED);
        canvas.fillCircle(centerX + 13, centerY - 2, 5, RED);
        canvas.fillTriangle(centerX, centerY, centerX + 18, centerY, centerX + 9, centerY + 12, RED);
    }
    else if (index == kMusicAppIndex)
    {
        FactoryMusic::drawIcon(canvas, centerX, centerY, canvas.clipTop(), canvas.clipBottom());
    }
    else if (index == kSnakeAppIndex || index == kMazeAppIndex)
    {
        FactoryMiniGames::drawIcon(canvas, index == kMazeAppIndex, centerX, centerY);
    }
    else if (index == kPianoAppIndex)
    {
        canvas.fillRoundRect(centerX - 26, centerY - 20, 53, 40, 3, kBlack);
        for (int key = 0; key < 5; ++key)
            canvas.fillRect(centerX - 23 + key * 10, centerY - 17, 9, 34, WHITE);
        for (int key = 0; key < 4; ++key)
            if (key != 2) canvas.fillRect(centerX - 17 + key * 10, centerY - 17, 6, 20, kBlack);
    }

    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    canvas.setFont(nullptr);
    canvas.setTextWrap(false);
    canvas.setTextSize(kAppTextSize);
    canvas.getTextBounds(kAppNames[index], 0, 0, &x1, &y1, &width, &height);
    canvas.setCursor(centerX - width / 2 - x1,
                   centerY + kIconRadius + kAppLabelGap - y1);
    canvas.setTextColor(kBlack);
    canvas.print(kAppNames[index]);
}

void paintHomeStrip()
{
    FactorySurface &canvas = factorySurface();
    canvas.fillScreen(kBackground);
    if (canvas.clipBottom() > kGridTop && canvas.clipTop() < FactoryHome::GridBottom)
    {
        for (uint8_t index = 0; index < kAppCount; ++index) drawApp(canvas, index);
    }
    // These regions stay fixed in the controller's scrolling definition.
    if (canvas.clipTop() < kGridTop)
    {
        canvas.fillRect(0, 0, LCD_WIDTH, kGridTop, kBackground);
        FactorySettingsUI::statusBar(canvas, homeStatus);
    }
    if (canvas.clipBottom() > FactoryHome::GridBottom)
        canvas.fillRect(0, FactoryHome::GridBottom, LCD_WIDTH,
                        LCD_HEIGHT - FactoryHome::GridBottom, kBackground);
}

void drawHomeStatus(bool force)
{
    static uint32_t lastCheck = 0;
    static bool statusDirty = true;
    const uint32_t now = millis();
    homeScroll.tick(now);
    if (force || now - lastCheck >= 500)
    {
        lastCheck = now;
        FactorySettingsUI::State next;
        FactoryNetwork::localTimeLabel(next.clock, sizeof(next.clock));
        next.battery = FactoryDevice::battery();
        statusDirty |= strcmp(next.clock, homeStatus.clock) ||
            next.battery.percent != homeStatus.battery.percent ||
            next.battery.available != homeStatus.battery.available ||
            next.battery.charging != homeStatus.battery.charging;
        homeStatus = next;
    }
    if (!force && (now - lastHomePaintMillis < 16 ||
        (!statusDirty && paintedHomeOffset == homeScroll.offset()))) return;

    // Shift existing pixels in LCD GRAM and paint only newly exposed rows.
    homeDisplay.present(homeScroll.offset(), paintHomeStrip, force, statusDirty);
    paintedHomeOffset = homeScroll.offset();
    lastHomePaintMillis = now;
    statusDirty = false;
}

void drawHomePage()
{
    drawHomeStatus(true);
}

void drawDevelopingPage()
{
    gfx->fillScreen(kBackground);
    centerText("developing", 218, 2, kBlack);

}

void drawSleepCountdown(uint8_t seconds)
{
    char text[24];
    snprintf(text, sizeof(text), "Sleeping in %u s", static_cast<unsigned>(seconds));
    gfx->fillRect(0, 258, LCD_WIDTH, 20, kBackground);
    centerText(text, 260, 2, kBlack);
    lastSleepSeconds = seconds;
}

void openSleepPage()
{
    currentPage = Page::SLEEP;
    gfx->fillScreen(kBackground);
    drawIconBackground(*gfx, LCD_WIDTH / 2, 164, 36);
    drawSleepIcon(*gfx, LCD_WIDTH / 2, 164, 36);
    centerText("Sleep", 218, 3, kBlack);
    centerText("Press BOOT to wake", 304, 1, kBlack);

    drawSleepCountdown(kSleepDelayMs / 1000);
    sleepStartedMillis = millis();
}

void handleSleep()
{
    if (currentPage != Page::SLEEP)
    {
        return;
    }

    const uint32_t elapsed = millis() - sleepStartedMillis;
    if (elapsed < kSleepDelayMs)
    {
        const uint8_t seconds = (kSleepDelayMs - elapsed + 999) / 1000;
        if (seconds != lastSleepSeconds)
        {
            drawSleepCountdown(seconds);
        }
        return;
    }

    // Do not enter sleep with the active-low wake button already held down.
    if (digitalRead(kReturnButtonPin) == LOW)
    {
        return;
    }
    // Explicitly configure the RTC input and its idle-high bias for EXT0.
    const bool wakeReady =
        rtc_gpio_init(GPIO_NUM_0) == ESP_OK &&
        rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK &&
        rtc_gpio_pullup_en(GPIO_NUM_0) == ESP_OK &&
        rtc_gpio_pulldown_dis(GPIO_NUM_0) == ESP_OK &&
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW) == ESP_OK;
    if (!wakeReady)
    {
        rtc_gpio_deinit(GPIO_NUM_0);
        pinMode(kReturnButtonPin, INPUT_PULLUP);
        currentPage = Page::DEVELOPING;
        gfx->fillScreen(kBackground);
        centerText("Sleep setup failed", 218, 2, kBlack);
        return;
    }
    Serial.println("[Factory] Entering deep sleep; BOOT-0 wakes on LOW");
    if (touchReady)
    {
        CST226SE->IIC_Write_Device_State(
            Arduino_IIC_Touch::Device::TOUCH_DEVICE_SLEEP_MODE,
            Arduino_IIC_Touch::Device_State::TOUCH_DEVICE_ON);
    }
    FactoryDevice::prepareSleep();
    gfx->displayOff();
    FactoryApps::close();
    FactoryXiaozhi::prepareSleep();
    digitalWrite(RT9080_EN, LOW);
    // HWCDC::flush() can wait forever without a USB host draining the queue.
    // Never wait for serial output here: the next step must actually enter sleep.
    esp_deep_sleep_start();
}

void returnToHome()
{
    homeScroll.cancel();
    ignoreTouchUntilRelease = true;
    if (currentPage != Page::HOME)
    {
        FactoryApps::close();
        gfx->setRotation(0);
        currentPage = Page::HOME;
        drawHomePage();
    }
}

void openApplication(uint8_t index)
{
    if (index >= kAppCount) return;
    homeScroll.cancel();
    homeDisplay.end();
    ignoreTouchUntilRelease = true;
    if (index == kSleepAppIndex)
    {
        openSleepPage();
    }
    else if (index == kSnakeAppIndex || index == kMazeAppIndex)
    {
        FactoryApps::open(index == kMazeAppIndex ? FactoryApps::App::Maze : FactoryApps::App::Snake, *gfx);
        currentPage = index == kMazeAppIndex ? Page::MAZE : Page::SNAKE;
    }
    else if (index == kSettingsAppIndex || index == kClockAppIndex ||
             index == kPokerAppIndex || index == kPianoAppIndex || index == kBreakoutAppIndex ||
             index == kMusicAppIndex || index == kXiaozhiAppIndex)
    {
        const bool poker = index == kPokerAppIndex;
        const bool breakout = index == kBreakoutAppIndex;
        const FactoryApps::App app = index == kSettingsAppIndex ? FactoryApps::App::Settings :
            index == kXiaozhiAppIndex ? FactoryApps::App::Xiaozhi :
            index == kClockAppIndex ? FactoryApps::App::Clock : breakout ? FactoryApps::App::Breakout :
            poker ? FactoryApps::App::Poker : index == kMusicAppIndex ? FactoryApps::App::Music : FactoryApps::App::Piano;
        if (FactoryApps::open(app, *gfx))
        {
            currentPage = index == kSettingsAppIndex ? Page::SETTINGS :
                index == kXiaozhiAppIndex ? Page::XIAOZHI :
                index == kClockAppIndex ? Page::CLOCK : breakout ? Page::BREAKOUT : poker ? Page::POKER :
                index == kMusicAppIndex ? Page::MUSIC : Page::PIANO;
        }
        else
        {
            gfx->setRotation(0);
            currentPage = Page::DEVELOPING;
            gfx->fillScreen(kBackground);
            centerText("Canvas alloc failed", 218, 1, kBlack);
        }
    }
    else
    {
        currentPage = Page::DEVELOPING;
        drawDevelopingPage();
    }
}

void handleTouch()
{
    // Poll release reports as well as presses so the piano cannot keep sounding
    // after a finger lifts. The same controller and I2C bus serve every page.
    const uint32_t now = millis();
    if (!touchReady || now - lastTouchPollMillis < 10) return;
    lastTouchPollMillis = now;
    CST226SE->IIC_Interrupt_Flag = false;

    uint8_t report[7];
    Wire.beginTransmission(CST226SE_DEVICE_ADDRESS);
    Wire.write(static_cast<uint8_t>(0x00));
    if (Wire.endTransmission() != 0 ||
        Wire.requestFrom(static_cast<uint8_t>(CST226SE_DEVICE_ADDRESS),
                         static_cast<uint8_t>(sizeof(report))) != sizeof(report))
    {
        while (Wire.available()) Wire.read();
        FactoryApps::touch(false, 0, 0, false);
        homeScroll.cancel();
        if (touchDown) ignoreTouchUntilRelease = true;
        touchDown = false;
        return;
    }
    for (size_t i = 0; i < sizeof(report); ++i) report[i] = Wire.read();

    // The bezel Home key has no coordinate fingers; handle it first.
    if (report[0] == 0x83 && report[1] == 0x17 && report[5] == 0x80)
    {
        returnToHome();
        lastTouchMillis = now;
        return;
    }

    const uint8_t fingers = report[5] & 0x7F;
    const int16_t x = (report[1] << 4) | ((report[3] >> 4) & 0x0F);
    const int16_t y = (report[2] << 4) | (report[3] & 0x0F);
    const bool fingerReport = report[6] == 0xAB && report[0] != 0xAB &&
        !(report[5] & 0x80) && fingers > 0 && fingers <= 5 &&
        (report[0] & 0x0F) == 0x06;
    const bool down = fingerReport && x < LCD_WIDTH && y < LCD_HEIGHT;
    bool pressed = down && !touchDown;
    touchDown = down;
    // The tap opening an app must not also play a note or deal a hand.
    if (ignoreTouchUntilRelease)
    {
        if (!down) ignoreTouchUntilRelease = false;
        return;
    }
    if (pressed)
    {
        if (now - lastTouchMillis < 150) pressed = false;
        else lastTouchMillis = now;
    }

    if (currentPage == Page::BREAKOUT || currentPage == Page::POKER || currentPage == Page::PIANO ||
        currentPage == Page::SETTINGS || currentPage == Page::CLOCK || currentPage == Page::MUSIC ||
        currentPage == Page::XIAOZHI || currentPage == Page::SNAKE || currentPage == Page::MAZE)
    {
        if ((currentPage == Page::SETTINGS || currentPage == Page::CLOCK) && FactoryNetwork::isPortrait())
            FactoryApps::touch(down, x, y, pressed);
        else
            FactoryApps::touch(down, y, LCD_WIDTH - 1 - x, pressed);
    }
    else if (currentPage == Page::HOME)
    {
        if (fingerReport && (fingers > 1 || !down))
        {
            homeScroll.cancel();
            ignoreTouchUntilRelease = true;
            return;
        }
        const int index = homeScroll.touch(down, x, y, pressed, now);
        if (index >= 0)
        {
            openApplication(static_cast<uint8_t>(index));
            ignoreTouchUntilRelease = false; // This tap already ended on release.
        }
    }
}

void handleReturnButtons()
{
    if (homeButton.pressed())
    {
        returnToHome();
        lastTouchMillis = millis();
    }
}

void handleVolumeButtons()
{
    if (FactoryVolume::pollButtons()) FactoryApps::refreshVolume();
}
void initializeTouch()
{
    touchReady = CST226SE->begin();
    if (touchReady)
    {
        Serial.println("[Factory] CST226SE touch ready");
    }
    else
    {
        Serial.println("[Factory] CST226SE touch unavailable");
    }
}

void showBootAnimation()
{
    drawStaticStartupScene();
    animateLilygo();
    delay(650);
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.printf("[Factory] Wake cause: %d (EXT0=%d)\n",
                  static_cast<int>(esp_sleep_get_wakeup_cause()),
                  static_cast<int>(ESP_SLEEP_WAKEUP_EXT0));
    Serial.printf("[Factory] Reset reason: %d, PSRAM found=%d size=%u free=%u\n",
                  static_cast<int>(esp_reset_reason()), psramFound(), ESP.getPsramSize(), ESP.getFreePsram());

    pinMode(LCD_BL, OUTPUT);
    // EXT0 wake leaves GPIO0 in RTC mode; restore it for the normal Home key.
    rtc_gpio_deinit(GPIO_NUM_0);
    pinMode(kReturnButtonPin, INPUT_PULLUP);
    homeButton.begin();
    FactoryVolume::begin();
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(RT9080_EN, OUTPUT);
    digitalWrite(RT9080_EN, HIGH);
    pinMode(MAX98357A_SD_MODE, OUTPUT);
    digitalWrite(MAX98357A_SD_MODE, LOW);
    pinMode(VIBRATINO_MOTOR_PWM, OUTPUT);
    digitalWrite(VIBRATINO_MOTOR_PWM, LOW);
    randomSeed(esp_random());

    gfx->begin();
    gfx->setRotation(0);
    initializeTouch();
    Wire.setTimeOut(20);
    FactoryDevice::begin();
    showBootAnimation();
    FactoryDevice::tick();
    FactoryNetwork::begin();
    drawHomePage();
}

void loop()
{
    handleVolumeButtons();
    handleReturnButtons();
    handleTouch();
    // Battery I2C reads and deferred NVS writes can wait until a drag settles.
    if (currentPage != Page::HOME || !homeScroll.moving()) FactoryDevice::tick();
    FactoryApps::tick();
    if (currentPage == Page::HOME) drawHomeStatus(false);
    if (FactoryXiaozhi::takeWakeRequest() && currentPage != Page::SLEEP)
    {
        if (currentPage != Page::XIAOZHI) openApplication(kXiaozhiAppIndex);
        FactoryXiaozhi::talk();
    }
    handleSleep();
    delay(currentPage == Page::HOME && homeScroll.moving() ? 1 : 5);
}

