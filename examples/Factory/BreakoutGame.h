#pragma once
#include <cmath>
#include <cstdint>

// Hardware-independent rules; all velocities are pixels per second.
namespace Breakout {
constexpr int Width = 480, Height = 222;
constexpr int Columns = 10, Rows = 3, BrickCount = Columns * Rows;
constexpr int BrickLeft = 10, BrickTop = 40, BrickWidth = 42, BrickHeight = 11;
constexpr int BrickStepX = 46, BrickStepY = 15;
constexpr float Radius = 4, PaddleY = 188, Left = 6, Right = Width - 6, Top = 28;
constexpr float LossY = 207, NormalPaddleWidth = 82, WidePaddleWidth = 116;
enum class State : uint8_t { Ready, Playing, Won, Lost };
enum class Power : uint8_t { Fast, Split, Wide };
enum class Event : uint8_t { None, Wall, Paddle, Brick, Pickup, Miss, Win };
struct Ball {
    float x, y, vx, vy;
    bool active;
    Ball(float px = Width / 2, float py = PaddleY - Radius - 2,
         float dx = 0, float dy = 0, bool enabled = false)
        : x(px), y(py), vx(dx), vy(dy), active(enabled) {}
};
struct Drop {
    float x, y;
    Power type;
    bool active;
    Drop(float px = 0, float py = 0, Power power = Power::Fast, bool enabled = false)
        : x(px), y(py), type(power), active(enabled) {}
};

inline float clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

class Game {
public:
    Ball balls[3];
    Drop drops[6];
    bool bricks[BrickCount] = {};
    State state = State::Ready;
    Event event = Event::None;
    int score = 0, lives = 3, remaining = BrickCount;
    float paddleX = Width / 2, fastTime = 0, wideTime = 0;

    float paddleWidth() const { return wideTime > 0 ? WidePaddleWidth : NormalPaddleWidth; }
    void move(float x) {
        paddleX = clamp(x, Left + paddleWidth() / 2, Right - paddleWidth() / 2);
        if (state == State::Ready) balls[0].x = paddleX;
    }
    void reset() {
        score = 0; lives = 3; remaining = BrickCount; paddleX = Width / 2;
        for (bool &brick : bricks) brick = true;
        prepareServe();
        event = Event::None;
    }
    void launch() {
        if (state != State::Ready) return;
        balls[0] = {paddleX, PaddleY - Radius - 2, 80, -110, true};
        state = State::Playing;
    }
    void apply(Power power) {
        if (power == Power::Fast) fastTime = 8;
        if (power == Power::Wide) wideTime = 10;
        if (power == Power::Split) {
            Ball source;
            for (const Ball &ball : balls) if (ball.active) { source = ball; break; }
            if (source.active) {
                int added = 0;
                for (Ball &ball : balls) if (!ball.active) {
                    const float angle = (++added == 1 ? -0.55f : 0.55f);
                    const float speed = std::sqrt(source.vx * source.vx + source.vy * source.vy);
                    const float vx = clamp(source.vx * std::cos(angle) - source.vy * std::sin(angle),
                                           -speed * 0.88f, speed * 0.88f);
                    // New balls head upward and cannot become trapped in a horizontal orbit.
                    ball = {source.x, source.y, vx, -std::sqrt(speed * speed - vx * vx), true};
                }
            }
        }
        move(paddleX);
        score += 25;
        signal(Event::Pickup);
    }
    void step(float seconds) {
        event = Event::None;
        if (state != State::Playing) return;
        // Bound catch-up after a slow frame; 5 ms substeps prevent tunnelling.
        float pending = clamp(seconds, 0, 0.10f);
        while (pending > 0.00001f && state == State::Playing) {
            const float dt = pending > 0.005f ? 0.005f : pending;
            pending -= dt;
            fastTime = fastTime > dt ? fastTime - dt : 0;
            wideTime = wideTime > dt ? wideTime - dt : 0;
            for (Ball &ball : balls) if (ball.active) advanceBall(ball, dt);
            if (!remaining) { state = State::Won; signal(Event::Win); break; }
            bool alive = false;
            for (const Ball &ball : balls) alive |= ball.active;
            if (!alive) {
                --lives;
                if (lives > 0) prepareServe();
                else state = State::Lost;
                signal(Event::Miss);
                break;
            }
            for (Drop &drop : drops) if (drop.active) {
                const float oldY = drop.y;
                drop.y += 60 * dt;
                if (oldY + 7 <= PaddleY && drop.y + 7 >= PaddleY &&
                    std::fabs(drop.x - paddleX) <= paddleWidth() / 2 + 7) {
                    drop.active = false;
                    apply(drop.type);
                } else if (drop.y > LossY) drop.active = false;
            }
        }
    }

private:
    void signal(Event value) {
        if (static_cast<int>(value) > static_cast<int>(event)) event = value;
    }
    void prepareServe() {
        for (Ball &ball : balls) ball = Ball{};
        for (Drop &drop : drops) drop = Drop{};
        fastTime = wideTime = 0;
        move(paddleX);
        balls[0] = {paddleX, PaddleY - Radius - 2, 0, 0, true};
        state = State::Ready;
    }
    void spawnDrop(int index) {
        const int destroyed = BrickCount - remaining;
        if (destroyed % 4 != 0) return;
        for (Drop &drop : drops) if (!drop.active) {
            // Cycle guarantees that all three power-ups appear during a round.
            drop = {float(BrickLeft + (index % Columns) * BrickStepX + BrickWidth / 2),
                    float(BrickTop + (index / Columns) * BrickStepY + BrickHeight / 2),
                    static_cast<Power>((destroyed / 4 - 1) % 3), true};
            break;
        }
    }
    void advanceBall(Ball &ball, float dt) {
        const float oldX = ball.x, oldY = ball.y;
        const float movement = dt * (fastTime > 0 ? 1.35f : 1.0f);
        ball.x += ball.vx * movement;
        ball.y += ball.vy * movement;
        if (ball.x < Left + Radius) {
            ball.x = Left + Radius; ball.vx = std::fabs(ball.vx); signal(Event::Wall);
        } else if (ball.x > Right - Radius) {
            ball.x = Right - Radius; ball.vx = -std::fabs(ball.vx); signal(Event::Wall);
        }
        if (ball.y < Top + Radius) {
            ball.y = Top + Radius; ball.vy = std::fabs(ball.vy); signal(Event::Wall);
        }
        if (ball.vy > 0 && oldY + Radius <= PaddleY && ball.y + Radius >= PaddleY &&
            std::fabs(ball.x - paddleX) <= paddleWidth() / 2 + Radius) {
            const float offset = clamp((ball.x - paddleX) / (paddleWidth() / 2), -1, 1);
            const float speed = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
            ball.vx = speed * std::sin(offset * 1.05f);
            ball.vy = -speed * std::cos(offset * 1.05f);
            ball.y = PaddleY - Radius;
            signal(Event::Paddle);
        }
        for (int i = 0; i < BrickCount; ++i) {
            if (!bricks[i]) continue;
            const float x = BrickLeft + (i % Columns) * BrickStepX;
            const float y = BrickTop + (i / Columns) * BrickStepY;
            const float dx = ball.x - clamp(ball.x, x, x + BrickWidth);
            const float dy = ball.y - clamp(ball.y, y, y + BrickHeight);
            if (dx * dx + dy * dy > Radius * Radius) continue;
            bricks[i] = false; --remaining; score += 10;
            spawnDrop(i);
            if (oldY <= y - Radius) {
                ball.y = y - Radius; ball.vy = -std::fabs(ball.vy);
            } else if (oldY >= y + BrickHeight + Radius) {
                ball.y = y + BrickHeight + Radius; ball.vy = std::fabs(ball.vy);
            } else if (oldX < x + BrickWidth / 2) {
                ball.x = x - Radius; ball.vx = -std::fabs(ball.vx);
            } else {
                ball.x = x + BrickWidth + Radius; ball.vx = std::fabs(ball.vx);
            }
            signal(Event::Brick);
            break;
        }
        // Resolving a corner hit on an edge brick must not push the ball through a wall.
        if (ball.x < Left + Radius) {
            ball.x = Left + Radius; ball.vx = std::fabs(ball.vx);
        } else if (ball.x > Right - Radius) {
            ball.x = Right - Radius; ball.vx = -std::fabs(ball.vx);
        }
        if (ball.y > LossY) ball.active = false;
    }
};
} // namespace Breakout
