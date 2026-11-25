#pragma once
#include <cstdint>

// Hardware-independent layout and touch rules for the portrait launcher.
namespace FactoryHome {
constexpr int AppCount = 10, Columns = 2, VisibleRows = 4;
constexpr int Width = 222, Height = 480, GridTop = 38, RowHeight = 108;
constexpr int ColumnWidth = Width / Columns;
constexpr int GridBottom = GridTop + VisibleRows * RowHeight;
constexpr int ViewportHeight = VisibleRows * RowHeight;
constexpr int TotalRows = (AppCount + Columns - 1) / Columns;
constexpr int MaxOffset = TotalRows > VisibleRows ? (TotalRows - VisibleRows) * RowHeight : 0;

inline int clampOffset(int value) {
    return value < 0 ? 0 : value > MaxOffset ? MaxOffset : value;
}
inline int alignedOffset(int value) {
    return clampOffset((clampOffset(value) + RowHeight / 2) / RowHeight * RowHeight);
}
inline int appAt(int x, int y, int offset) {
    if (x < 0 || x >= Width || y < GridTop || y >= GridBottom) return -1;
    const int index = (y - GridTop + clampOffset(offset)) / RowHeight * Columns + x / ColumnWidth;
    return index < AppCount ? index : -1;
}

struct RowRange { int first, count; };
inline RowRange exposedRows(int before, int after) {
    before = clampOffset(before); after = clampOffset(after);
    const int distance = after > before ? after - before : before - after;
    if (distance >= ViewportHeight) return {after, ViewportHeight};
    return {after > before ? before + ViewportHeight : after, distance};
}
struct RingSpan { int panelY, count, paintOffset; };
inline RingSpan ringSpan(int first, int count) {
    const int row = first % ViewportHeight;
    const int available = ViewportHeight - row;
    return {GridTop + row, count < available ? count : available, first - row};
}

class Scroll {
public:
    int offset() const { return offset_; }
    bool moving() const { return settling_ || (active_ && moved_ && vertical_); }

    // Only an accepted press can begin a gesture. Release reports often contain
    // no coordinates, so taps use the last valid down position.
    int touch(bool down, int x, int y, bool pressed, uint32_t now) {
        if (pressed) {
            const bool wasSettling = settling_;
            settling_ = false;
            active_ = appAt(x, y, offset_) >= 0;
            moved_ = false;
            vertical_ = false;
            tapAllowed_ = !wasSettling;
            startX_ = lastX_ = x; startY_ = lastY_ = y;
            startOffset_ = offset_;
            started_ = now;
            sampledAt_ = now; velocity_ = 0;
            app_ = appAt(x, y, offset_);
            // Touching the fixed status bar must not interrupt row alignment.
            if (!active_) settle(alignedOffset(offset_), now);
        }
        if (!active_) return -1;
        if (down) {
            if (y != lastY_) {
                const uint32_t elapsed = now - sampledAt_;
                if (elapsed > 0) {
                    int speed = (lastY_ - y) * 1000 / static_cast<int>(elapsed);
                    speed = speed < -2000 ? -2000 : speed > 2000 ? 2000 : speed;
                    velocity_ = elapsed > 100 ? speed : (velocity_ + speed) / 2;
                }
                sampledAt_ = now;
            }
            lastX_ = x; lastY_ = y;
            const int dx = magnitude(x - startX_), dy = magnitude(y - startY_);
            if (!moved_ && (dx > 8 || dy > 8)) {
                moved_ = true;
                vertical_ = dy > dx;
            }
            if (moved_ && vertical_) offset_ = clampOffset(startOffset_ + startY_ - y);
            return -1;
        }

        active_ = false;
        const int tapped = !moved_ && tapAllowed_ && now - started_ < 700 &&
            appAt(lastX_, lastY_, offset_) == app_ ? app_ : -1;
        const int travel = startY_ - lastY_;
        // A deliberate short swipe advances a row; a small drag settles back.
        int bias = moved_ && vertical_ && magnitude(travel) >= RowHeight / 4 ?
            (travel > 0 ? RowHeight / 4 : -RowHeight / 4) : 0;
        // A quick flick carries toward the next row; a stationary finger never
        // adds momentum on release, even if the earlier drag was fast.
        if (moved_ && vertical_ && magnitude(travel) >= 18 &&
            now - sampledAt_ < 80 && magnitude(velocity_) >= 300)
            bias = velocity_ > 0 ? RowHeight / 2 : -RowHeight / 2;
        settle(alignedOffset(offset_ + bias), now);
        return tapped;
    }

    void tick(uint32_t now) {
        if (!settling_) return;
        const uint32_t elapsed = now - settledAt_;
        constexpr int duration = 160;
        if (elapsed >= duration) {
            offset_ = target_; settling_ = false; return;
        }
        const int remaining = duration - static_cast<int>(elapsed);
        offset_ = target_ + (settleFrom_ - target_) * remaining * remaining / (duration * duration);
    }

    // Home/BOOT, app switching and failed touch reads cancel a pending tap.
    void cancel() {
        active_ = settling_ = false;
        offset_ = alignedOffset(offset_);
    }

private:
    static int magnitude(int value) { return value < 0 ? -value : value; }
    void settle(int target, uint32_t now) {
        settleFrom_ = offset_; target_ = target; settledAt_ = now;
        settling_ = target_ != offset_;
    }
    int offset_ = 0, startOffset_ = 0, settleFrom_ = 0, target_ = 0;
    int startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0, app_ = -1;
    int velocity_ = 0;
    uint32_t started_ = 0, settledAt_ = 0;
    uint32_t sampledAt_ = 0;
    bool active_ = false, moved_ = false, vertical_ = false;
    bool tapAllowed_ = true, settling_ = false;
};
} // namespace FactoryHome
