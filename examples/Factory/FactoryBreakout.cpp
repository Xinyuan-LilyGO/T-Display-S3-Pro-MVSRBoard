#include "FactoryBreakout.h"
#include "BreakoutGame.h"
#include "FactorySurface.h"
#include "FactoryAudio.h"
#include <cstring>

namespace FactoryBreakout {
namespace {
constexpr uint16_t ink = RGB565(36, 48, 75);
constexpr uint16_t blue = RGB565(60, 122, 226);
constexpr uint16_t violet = RGB565(130, 89, 219);
constexpr uint16_t teal = RGB565(36, 161, 166);
constexpr uint16_t muted = RGB565(112, 125, 148);
constexpr uint16_t rowColors[] = {
    RGB565(130, 89, 219), RGB565(86, 113, 225), RGB565(51, 150, 209),
    RGB565(36, 169, 169), RGB565(224, 107, 143)};
constexpr uint8_t motorChannel = 2; // Backlight uses channel 1.
Arduino_GFX *const canvas = &factorySurface();
Arduino_GFX *screen = nullptr;
FactoryAudio audio;
Breakout::Game game;
bool active = false, firstFrame = true;
uint32_t lastFrame = 0, noteUntil = 0, motorUntil = 0, lastEffect = 0;
int soundingNote = -1;

void text(const char *value, int x, int y, int size, uint16_t color) {
    canvas->setTextSize(size);
    canvas->setTextColor(color);
    canvas->setCursor(x, y);
    canvas->print(value);
}

void center(const char *value, int y, int size, uint16_t color) {
    text(value, (Breakout::Width - int(strlen(value)) * 6 * size) / 2, y, size, color);
}

void drawScene() {
    canvas->fillScreen(WHITE);
    text("BRICKS", 10, 7, 2, ink);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "SCORE %03d", game.score);
    text(buffer, 112, 11, 1, blue);
    snprintf(buffer, sizeof(buffer), "LIVES %d", game.lives);
    text(buffer, 202, 11, 1, muted);
    snprintf(buffer, sizeof(buffer), "VOL %u", unsigned(FactoryVolume::get()));
    text(buffer, 421, 11, 1, muted);
    if (game.fastTime > 0 || game.wideTime > 0) {
        snprintf(buffer, sizeof(buffer), "F:%ds W:%ds", int(ceilf(game.fastTime)), int(ceilf(game.wideTime)));
        text(buffer, 280, 11, 1, violet);
    }
    canvas->drawFastHLine(6, int(Breakout::Top) - 1, Breakout::Width - 12, RGB565(228, 234, 243));
    canvas->drawFastVLine(5, int(Breakout::Top), 177, RGB565(235, 239, 246));
    canvas->drawFastVLine(int(Breakout::Right), int(Breakout::Top), 177, RGB565(235, 239, 246));
    for (int i = 0; i < Breakout::BrickCount; ++i) if (game.bricks[i]) {
        const int x = Breakout::BrickLeft + (i % Breakout::Columns) * Breakout::BrickStepX;
        const int y = Breakout::BrickTop + (i / Breakout::Columns) * Breakout::BrickStepY;
        canvas->fillRoundRect(x, y, Breakout::BrickWidth, Breakout::BrickHeight, 3,
                              rowColors[i / Breakout::Columns]);
        canvas->drawFastHLine(x + 4, y + 3, Breakout::BrickWidth - 8, RGB565(215, 231, 251));
    }
    for (const auto &drop : game.drops) if (drop.active) {
        const int type = static_cast<int>(drop.type);
        const uint16_t color = type == 0 ? violet : type == 1 ? blue : teal;
        canvas->fillRoundRect(int(drop.x) - 7, int(drop.y) - 7, 15, 15, 4, color);
        char label[2] = {"FMW"[type], 0};
        text(label, int(drop.x) - 2, int(drop.y) - 3, 1, WHITE);
    }
    canvas->fillRoundRect(int(game.paddleX - game.paddleWidth() / 2), int(Breakout::PaddleY) + 3,
                          int(game.paddleWidth()), 9, 4, RGB565(219, 229, 247));
    canvas->fillRoundRect(int(game.paddleX - game.paddleWidth() / 2), int(Breakout::PaddleY),
                          int(game.paddleWidth()), 9, 4, game.wideTime > 0 ? teal : blue);
    for (const auto &ball : game.balls) if (ball.active) {
        canvas->fillCircle(lroundf(ball.x), lroundf(ball.y), int(Breakout::Radius), violet);
        canvas->fillCircle(lroundf(ball.x) - 1, lroundf(ball.y) - 1, 1, WHITE);
    }
    if (game.state != Breakout::State::Playing) {
        canvas->fillRoundRect(125, 96, 230, 73, 9, RGB565(244, 247, 253));
        const char *title = game.state == Breakout::State::Won ? "ALL CLEAR!" :
                            game.state == Breakout::State::Lost ? "GAME OVER" : "READY?";
        center(title, 106, 2, blue);
        center(game.state == Breakout::State::Ready ? "Swipe to move paddle" : "Tap to play again", 135, 1, ink);
        center(game.state == Breakout::State::Ready ? "Tap to launch" : "30 bricks / 3 lives", 153, 1, muted);
    }
    text("F:FAST", 10, 212, 1, violet);
    text("M:3 BALLS", 70, 212, 1, blue);
    text("W:WIDE", 150, 212, 1, teal);
    text("Slide anywhere to move", 330, 212, 1, muted);
}

void render() {
    factorySurface().present(*screen, drawScene, firstFrame);
    firstFrame = false;
}

void feedback(Breakout::Event event, uint32_t now) {
    if (event == Breakout::Event::None) return;
    // Rate-limit dense multiball collisions; terminal/pickup sounds take priority.
    if (event < Breakout::Event::Pickup && now - lastEffect < 45) return;
    lastEffect = now;
    const int notes[] = {0, 67, 74, 81, 88, 45, 93};
    if (soundingNote >= 0) audio.noteOff(soundingNote);
    soundingNote = notes[static_cast<int>(event)];
    audio.noteOn(soundingNote, 80);
    noteUntil = now + (event >= Breakout::Event::Pickup ? 150 : 45);
    if (event != Breakout::Event::Wall) {
        ledcWrite(motorChannel, event == Breakout::Event::Miss ? 180 : 115);
        motorUntil = now + (event >= Breakout::Event::Pickup ? 85 : 25);
    }
}
} // namespace

bool open(Arduino_GFX &display) {
    close();
    screen = &display;
    screen->setRotation(1);
    canvas->setTextWrap(false);
    canvas->setFont(nullptr);
    digitalWrite(RT9080_EN, HIGH);
    ledcSetup(motorChannel, 12000, 8);
    ledcAttachPin(VIBRATINO_MOTOR_PWM, motorChannel);
    ledcWrite(motorChannel, 0);
    audio.begin();
    digitalWrite(MAX98357A_SD_MODE, HIGH);
    active = true;
    firstFrame = true;
    soundingNote = -1;
    noteUntil = motorUntil = 0;
    game.reset();
    render();
    lastFrame = millis();
    lastEffect = lastFrame - 100;
    return true;
}

void close() {
    if (active) {
        ledcWrite(motorChannel, 0);
        ledcDetachPin(VIBRATINO_MOTOR_PWM);
        pinMode(VIBRATINO_MOTOR_PWM, OUTPUT);
        digitalWrite(VIBRATINO_MOTOR_PWM, LOW);
        digitalWrite(MAX98357A_SD_MODE, LOW);
        audio.end();
    }
    active = false;
    soundingNote = -1;
    noteUntil = motorUntil = 0;
    screen = nullptr;
}

void touch(bool down, int16_t x, int16_t, bool pressed) {
    if (!active) return;
    if (pressed && (game.state == Breakout::State::Won || game.state == Breakout::State::Lost)) {
        game.reset();
        game.move(x);
        return;
    }
    if (down) game.move(x);
    if (pressed && game.state == Breakout::State::Ready) {
        game.launch();
        lastFrame = millis();
    }
}

void tick() {
    if (!active) return;
    const uint32_t now = millis();
    if (soundingNote >= 0 && static_cast<int32_t>(now - noteUntil) >= 0) {
        audio.noteOff(soundingNote);
        soundingNote = -1;
    }
    if (motorUntil && static_cast<int32_t>(now - motorUntil) >= 0) {
        ledcWrite(motorChannel, 0);
        motorUntil = 0;
    }
    if (now - lastFrame < 25) return;
    const float dt = (now - lastFrame) / 1000.0f;
    lastFrame = now;
    game.step(dt);
    feedback(game.event, now);
    render();
}

void drawIcon(Arduino_GFX &display, int16_t x, int16_t y) {
    for (int row = 0; row < 2; ++row) for (int column = 0; column < 3; ++column)
        display.fillRoundRect(x - 23 + column * 16, y - 20 + row * 10, 14, 8, 2,
                               rowColors[row + column]);
    display.fillCircle(x + 8, y + 8, 4, violet);
    display.drawLine(x + 12, y + 3, x + 16, y - 1, RGB565(188, 171, 230));
    display.fillRoundRect(x - 17, y + 20, 34, 6, 3, blue);
}
} // namespace FactoryBreakout
