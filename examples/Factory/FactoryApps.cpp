// Factory applications adapted from VolosR/PokerS3 and LilyGO's piano example.
// Upstream attribution and license: README.md and LICENSE.upstream.
#include "FactoryApps.h"
#include "FactoryAudio.h"
#include "FactoryBreakout.h"
#include "FactoryNetwork.h"
#include "FactorySoundSequence.h"
#include "FactoryMusic.h"
#include "FactoryXiaozhi.h"
#include "FactoryMiniGames.h"

namespace FactoryApps {
namespace {
Arduino_GFX *display = nullptr;
App current = App::None;

class AppCanvas : public Arduino_Canvas {
public:
    AppCanvas() : Arduino_Canvas(480, 222, nullptr) {}
    void text(const String &value, int16_t x, int16_t y) {
        int16_t bx, by;
        uint16_t w, h;
        getTextBounds(value, 0, 0, &bx, &by, &w, &h);
        setCursor(x - bx, y - by);
        print(value);
    }
    void present() { display->draw16bitRGBBitmap(0, 0, getFramebuffer(), 480, 222); }
} canvas;

namespace Poker {
FactoryAudio audio;
FactorySoundSequence<FactoryAudio> sounds;

void dealSound(bool restart) {
    sounds.stop(audio);
    if (restart) {
        sounds.append(60, 60, 20);
        sounds.append(72, 80, 45);
    }
    const uint8_t notes[] = {72, 74, 77, 79, 84};
    for (uint8_t note : notes) sounds.append(note, 24, 30, 62);
    sounds.play(audio, millis());
}

void selectSound(bool hold) {
    sounds.stop(audio);
    sounds.append(hold ? 84 : 72, 45, 0, 66);
    sounds.play(audio, millis());
}

void betSound() {
    sounds.stop(audio);
    sounds.append(67, 35, 20);
    sounds.append(79, 55, 0);
    sounds.play(audio, millis());
}

void resultSound(int replacements, int rank) {
    sounds.stop(audio);
    // Deal a short tick for each replaced card, then announce the settlement.
    for (int i = 0; i < replacements; ++i)
        sounds.append(69 + i * 2, 22, i + 1 == replacements ? 90 : 22, 60);
    if (rank == 0) { // High card returns less than the stake.
        sounds.append(60, 85, 25, 60);
        sounds.append(53, 150, 0, 60);
    } else if (rank == 1) { // A pair returns the stake: a neutral confirmation.
        sounds.append(72, 65, 25);
        sounds.append(76, 110, 0);
    } else {
        const uint8_t notes[] = {72, 76, 79, 84};
        for (uint8_t note : notes) sounds.append(note, 65, 20, 78);
        if (rank >= 4) { // Straight or better: a longer winning flourish.
            sounds.append(88, 90, 25, 82);
            sounds.append(91, 160, 0, 82);
        }
    }
    sounds.play(audio, millis());
}

constexpr int cardLeft = 6, cardTop = 96, cardWidth = 84, cardHeight = 106;
constexpr int cardStep = 96, holdTop = 205, holdHeight = 15;
constexpr uint16_t tableColor = 0x0863, panelColor = 0x1926;
constexpr uint16_t mutedColor = 0xBDF7, accentColor = 0xFD20;
constexpr uint16_t redSuit = 0xC800, blackSuit = 0x0841;
const char *const cardRanks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
int deck[52], hand[5] = {52, 52, 52, 52, 52};
bool held[5] = {};
bool choosing = false;
float score = 20, win = 0;
int bet = 2, handRank = -1;
const char *const ranks[] = {"HIGH CARD", "PAIR", "TWO PAIRS", "3 OF A KIND",
                            "STRAIGHT", "FLUSH", "FULL HOUSE", "POKER"};
const float multipliers[] = {0.5f, 1, 3, 4, 8, 10, 15, 30};

int evaluateHand() {
    int counts[13] = {};
    bool flush = true;
    for (int i = 0; i < 5; ++i) {
        ++counts[hand[i] % 13];
        if (hand[i] / 13 != hand[0] / 13) flush = false;
    }
    int pairs = 0, three = 0, four = 0, distinct = 0, low = 13, high = 0;
    for (int i = 0; i < 13; ++i) {
        pairs += counts[i] == 2;
        three += counts[i] == 3;
        four += counts[i] == 4;
        if (counts[i]) { ++distinct; low = min(low, i); high = max(high, i); }
    }
    // Artwork order is 2,3,...,10,J,Q,K,A; also support the low-A straight.
    const bool straight = distinct == 5 &&
        (high - low == 4 || (counts[12] && counts[0] && counts[1] && counts[2] && counts[3]));
    if (four) return 7;
    if (three && pairs) return 6;
    if (flush) return 5;
    if (straight) return 4;
    if (three) return 3;
    if (pairs == 2) return 2;
    if (pairs) return 1;
    return 0;
}

String creditText(float value) {
    if (value >= 1000000) return String(value / 1000000, 1) + "M";
    if (value >= 100000) return String(value / 1000, 1) + "K";
    return String(value, 0);
}

void drawStat(const char *label, const String &value, int x, uint16_t color) {
    canvas.setTextSize(1);
    canvas.setTextColor(mutedColor);
    canvas.text(label, x, 7);
    canvas.setTextSize(2);
    canvas.setTextColor(color);
    canvas.text(value, x, 22);
}

// Integer coordinates and solid fills keep every edge on a physical LCD pixel.
// Suit order follows the upstream deck: clubs, diamonds, hearts, spades.
void drawSuit(int suit, int x, int y, uint16_t color) {
    if (suit == 0) {
        canvas.fillCircle(x, y - 9, 8, color);
        canvas.fillCircle(x - 9, y + 2, 8, color);
        canvas.fillCircle(x + 9, y + 2, 8, color);
        canvas.fillTriangle(x, y + 1, x - 6, y + 17, x + 6, y + 17, color);
    } else if (suit == 1) {
        canvas.fillTriangle(x, y - 18, x - 13, y, x + 13, y, color);
        canvas.fillTriangle(x - 13, y, x + 13, y, x, y + 18, color);
    } else if (suit == 2) {
        canvas.fillCircle(x - 8, y - 7, 9, color);
        canvas.fillCircle(x + 8, y - 7, 9, color);
        canvas.fillTriangle(x - 16, y - 3, x + 16, y - 3, x, y + 18, color);
    } else {
        canvas.fillTriangle(x, y - 18, x - 16, y + 3, x + 16, y + 3, color);
        canvas.fillCircle(x - 8, y + 3, 9, color);
        canvas.fillCircle(x + 8, y + 3, 9, color);
        canvas.fillTriangle(x, y, x - 6, y + 19, x + 6, y + 19, color);
    }
}

void drawCard(int index) {
    const int x = cardLeft + index * cardStep;
    const bool keep = choosing && held[index];
    canvas.fillRoundRect(x, cardTop, cardWidth, cardHeight, 5, WHITE);
    canvas.drawRoundRect(x, cardTop, cardWidth, cardHeight, 5, keep ? accentColor : 0xCE79);
    if (keep) canvas.drawRoundRect(x + 1, cardTop + 1, cardWidth - 2, cardHeight - 2, 4, accentColor);
    if (hand[index] == 52) {
        canvas.fillRoundRect(x + 5, cardTop + 5, cardWidth - 10, cardHeight - 10, 3, 0x194E);
        for (int dy = 14; dy < cardHeight - 10; dy += 12) {
            for (int dx = 13; dx < cardWidth - 10; dx += 12) {
                canvas.fillRect(x + dx, cardTop + dy, 3, 3, 0x6B9B);
            }
        }
    } else {
        const int suit = hand[index] / 13;
        const String rank = cardRanks[hand[index] % 13];
        const uint16_t color = suit == 1 || suit == 2 ? redSuit : blackSuit;
        canvas.setTextSize(2);
        canvas.setTextColor(color);
        canvas.text(rank, x + 7, cardTop + 7);
        drawSuit(suit, x + cardWidth / 2, cardTop + cardHeight / 2, color);
        canvas.text(rank, x + cardWidth - 7 - rank.length() * 12, cardTop + cardHeight - 21);
    }
    if (choosing) {
        canvas.fillRoundRect(x, holdTop, cardWidth, holdHeight, 3, keep ? accentColor : panelColor);
        canvas.setTextSize(1);
        canvas.setTextColor(keep ? BLACK : mutedColor);
        canvas.text(keep ? "HOLD" : "SWAP", x + (cardWidth - 24) / 2, holdTop + 4);
    }
}

void drawGame() {
    canvas.fillScreen(tableColor);
    canvas.fillRoundRect(119, 3, 110, 39, 4, panelColor);
    drawStat("SCORE", creditText(score), 8, WHITE);
    drawStat("BET +", String(bet), 127, accentColor);
    drawStat("WIN", creditText(win), 246, CYAN);
    canvas.setTextSize(1);
    canvas.setTextColor(mutedColor);
    canvas.text("VOL " + String(FactoryVolume::get()), 308, 7);
    canvas.fillRoundRect(366, 3, 108, 39, 4, accentColor);
    canvas.setTextColor(BLACK);
    canvas.setTextSize(2);
    const String action = score < 2 && !choosing ? "RESET" : choosing ? "DRAW" : "DEAL";
    canvas.text(action, 366 + (108 - action.length() * 12) / 2, 8);
    canvas.setTextSize(1);
    canvas.text("TAP", 411, 29);
    for (int i = 0; i < 8; ++i) {
        const int x = 6 + (i % 4) * 120, y = 48 + (i / 4) * 22;
        const bool won = !choosing && handRank == i;
        canvas.fillRoundRect(x, y, 114, 18, 3, won ? accentColor : panelColor);
        canvas.setTextColor(won ? BLACK : WHITE);
        canvas.text(ranks[i], x + 4, y + 5);
        const String payout = String(bet * multipliers[i], 0);
        canvas.setTextColor(won ? BLACK : accentColor);
        canvas.text(payout, x + 110 - payout.length() * 6, y + 5);
    }
    for (int i = 0; i < 5; ++i) drawCard(i);
    if (!choosing) {
        canvas.setTextSize(1);
        canvas.setTextColor(mutedColor);
        canvas.text("TAP BET TO CHANGE STAKE    TAP DEAL TO PLAY", 105, holdTop + 4);
    }
    canvas.present();
}

void dealOrDraw() {
    if (!choosing) {
        const bool restart = score < 2;
        if (score < 2) score = 20; // Virtual credits only; restart an exhausted game.
        if (bet > score) bet = 2;
        for (int i = 0; i < 52; ++i) deck[i] = i;
        for (int i = 51; i > 0; --i) {
            const int j = random(i + 1), tmp = deck[i];
            deck[i] = deck[j]; deck[j] = tmp;
        }
        score -= bet;
        win = 0;
        handRank = -1;
        for (int i = 0; i < 5; ++i) { hand[i] = deck[i]; held[i] = false; }
        choosing = true;
        dealSound(restart);
    } else {
        int next = 5;
        for (int i = 0; i < 5; ++i) if (!held[i]) hand[i] = deck[next++];
        handRank = evaluateHand();
        win = bet * multipliers[handRank];
        score += win;
        choosing = false;
        resultSound(next - 5, handRank);
    }
    drawGame();
}


void touch(int x, int y) {
    if (choosing && y >= cardTop && y < holdTop + holdHeight && x >= cardLeft) {
        const int i = (x - cardLeft) / cardStep;
        if (i < 5 && (x - cardLeft) % cardStep < cardWidth) {
            held[i] = !held[i];
            selectSound(held[i]);
            drawGame();
        }
    } else if (x >= 119 && x < 229 && y >= 3 && y < 42) {
        if (!choosing) {
            bet = bet >= 1024 || bet * 2 > score ? 2 : bet * 2;
            betSound();
            drawGame();
        }
    } else if (x >= 366 && x < 474 && y >= 3 && y < 42) {
        dealOrDraw();
    }
}
} // namespace Poker

namespace Piano {
FactoryAudio synth;
int viewStart = 48, activeNote = -1;
static const int whiteIndex[12] = {0,-1,1,-1,2,3,-1,4,-1,5,-1,6};
static const int blackLeft[12] = {-1,0,-1,1,-1,-1,3,-1,4,-1,5,-1};
static const int whiteSemitone[7] = {0,2,4,5,7,9,11};
static const char *const names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static constexpr int top = 68, whiteW = 32, blackW = 20, blackH = 90;

void shiftOctave(int delta) {
    // Always start on C; C7..C9 is the highest complete 25-key window.
    viewStart = constrain(viewStart + delta, 0, 96);
    viewStart = (viewStart / 12) * 12;
}

int keyAt(int x, int y) {
    if (x < 0 || x >= 480 || y < top || y >= 222) return -1;
    if (y < top + blackH) {
        for (int i = 0; i < 25; ++i) {
            int semitone = i % 12;
            if (blackLeft[semitone] < 0) continue;
            int left = ((i / 12) * 7 + blackLeft[semitone] + 1) * whiteW - blackW / 2;
            if (x >= left && x < left + blackW) return viewStart + i;
        }
    }
    int w = x / whiteW;
    return viewStart + (w / 7) * 12 + whiteSemitone[w % 7];
}

void render(int activeNote, uint8_t volume) {
    canvas.fillScreen(0x1082);
    canvas.setTextSize(1);
    canvas.setTextColor(CYAN);
    canvas.text("< OCT", 4, 8);
    canvas.text("OCT >", 440, 8);
    canvas.setTextColor(WHITE);
    canvas.text("C" + String(viewStart / 12 - 1) + " - C" + String(viewStart / 12 + 1), 65, 8);
    canvas.text("VOL " + String(volume), 222, 8);
    if (activeNote < 0) {
        canvas.text("Touch keys to play", 65, 26);
    } else {
        canvas.setTextColor(YELLOW);
        canvas.text(String(names[activeNote % 12]) + String(activeNote / 12 - 1), 65, 26);
        canvas.setTextColor(CYAN);
        const float frequency = 440.0f * powf(2.0f, (activeNote - 69) / 12.0f);
        canvas.text(String(frequency, 1) + " Hz", 330, 8);
    }
    for (int i = 0; i < 25; ++i) {
        int st = i % 12;
        if (whiteIndex[st] < 0) continue;
        int x = ((i / 12) * 7 + whiteIndex[st]) * whiteW;
        canvas.fillRect(x, top, whiteW - 1, 222 - top, activeNote == viewStart + i ? CYAN : WHITE);
        canvas.setTextColor(BLACK);
        canvas.text(String(names[st]) + String((viewStart + i) / 12 - 1), x + 4, 208);
    }
    for (int i = 0; i < 25; ++i) {
        int st = i % 12;
        if (blackLeft[st] < 0) continue;
        int x = ((i / 12) * 7 + blackLeft[st] + 1) * whiteW - blackW / 2;
        canvas.fillRect(x, top, blackW, blackH, activeNote == viewStart + i ? ORANGE : 0x0841);
    }
    canvas.present();
}

void touch(bool down, int x, int y, bool pressed) {
    const int previousView = viewStart;
    if (pressed && y < top) {
        if (x < 60) shiftOctave(-12);
        else if (x >= 420) shiftOctave(12);
    }
    const int note = down ? keyAt(x, y) : -1;
    const bool changed = note != activeNote;
    if (changed) {
        if (activeNote >= 0) synth.noteOff(activeNote);
        if (note >= 0) synth.noteOn(note, 100);
        activeNote = note;
    }
    if (changed || previousView != viewStart) render(activeNote, synth.volume());
}

} // namespace Piano
} // namespace

bool open(App app, Arduino_GFX &output) {
    close();
    display = &output;
    if (app == App::Snake || app == App::Maze) {
        FactoryMiniGames::open(app == App::Maze ? FactoryMiniGames::Game::Maze : FactoryMiniGames::Game::Snake,output);
        current = app;
        return true;
    }
    if (app == App::Xiaozhi) {
        FactoryXiaozhi::open(output);
        current = app;
        return true;
    }
    if (app == App::Music) {
        FactoryMusic::open(output);
        current = app;
        return true;
    }
    if (app == App::Settings || app == App::Clock) {
        FactoryNetwork::open(app == App::Clock, output);
        current = app;
        return true;
    }
    if (app == App::Breakout) {
        if (!FactoryBreakout::open(output)) return false;
        current = app;
        return true;
    }
    if (!canvas.getFramebuffer() && !canvas.begin()) return false;
    canvas.setTextWrap(false);
    canvas.setFont(nullptr);
    canvas.setTextSize(1);
    output.setRotation(1);
    current = app;
    if (app == App::Poker) {
        Poker::audio.begin();
        digitalWrite(MAX98357A_SD_MODE, HIGH);
        Poker::drawGame();
    }
    if (app == App::Piano) {
        Piano::activeNote = -1;
        Piano::synth.begin();
        digitalWrite(MAX98357A_SD_MODE, HIGH);
        Piano::render(-1, Piano::synth.volume());
    }
    return true;
}

void close() {
    if (current == App::Snake || current == App::Maze) FactoryMiniGames::close();
    if (current == App::Xiaozhi) FactoryXiaozhi::closeView();
    if (current == App::Music) FactoryMusic::close();
    if (current == App::Settings || current == App::Clock) FactoryNetwork::close();
    if (current == App::Breakout) FactoryBreakout::close();
    digitalWrite(MAX98357A_SD_MODE, LOW);
    if (current == App::Poker) {
        Poker::sounds.stop(Poker::audio);
        Poker::audio.end();
    }
    if (current == App::Piano) {
        Piano::synth.end();
        Piano::activeNote = -1;
    }
    current = App::None;
}

void touch(bool down, int16_t x, int16_t y, bool pressed) {
    if (current == App::Snake || current == App::Maze) FactoryMiniGames::touch(down,x,y,pressed);
    if (current == App::Xiaozhi && pressed) FactoryXiaozhi::touch(x,y);
    if (current == App::Music && pressed) FactoryMusic::touch(x, y);
    if (current == App::Settings || current == App::Clock) {
        if (pressed) FactoryNetwork::touch(x,y);
        else FactoryNetwork::drag(x,y,down);
    }
    if (current == App::Breakout) FactoryBreakout::touch(down, x, y, pressed);
    if (current == App::Poker && pressed) Poker::touch(x, y);
    if (current == App::Piano) Piano::touch(down, x, y, pressed);
}

void tick() {
    FactoryXiaozhi::tick();
    if (current == App::Poker) Poker::sounds.tick(Poker::audio, millis());
    FactoryNetwork::tick();
    if (current == App::Snake || current == App::Maze) FactoryMiniGames::tick();
    if (current == App::Music) FactoryMusic::tick();
    if (current == App::Breakout) FactoryBreakout::tick();
}

void refreshVolume() {
    if (current == App::Poker) Poker::drawGame();
    if (current == App::Piano) Piano::render(Piano::activeNote, FactoryVolume::get());
    // Breakout reads the master volume in its next strip-rendered frame.
}
} // namespace FactoryApps
