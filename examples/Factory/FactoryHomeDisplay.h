#pragma once
#include <Arduino_DataBus.h>
#include "FactoryHome.h"
#include "FactorySurface.h"

// ST7796S VSCRDEF (0x33) / VSCSAD (0x37), datasheet sections 9.2.25/9.2.29.
// Portrait rotation 0 has MADCTL.MV=0 and no vertical panel offset. The LCD's
// own GRAM is the scrolling buffer; no extra framebuffer/PSRAM is needed.
class FactoryHomeDisplay {
public:
    FactoryHomeDisplay(Arduino_DataBus &bus, Arduino_GFX &display)
        : bus_(bus), display_(display) {}

    int paintOffset() const { return paintOffset_; }
    void present(int offset, void (*paint)(), bool force, bool statusDirty) {
        using namespace FactoryHome;
        offset = clampOffset(offset);
        if (!active_ || force) {
            defineScroll(GridTop, ViewportHeight, Height - GridBottom,
                         GridTop + offset % ViewportHeight);
            active_ = true;
            paintOffset_ = 0;
            factorySurface().presentRegion(display_, paint, 0, GridTop);
            factorySurface().presentRegion(display_, paint, GridBottom, Height);
            writeContent(offset, ViewportHeight, paint);
        } else {
            const auto range = exposedRows(offset_, offset);
            writeContent(range.first, range.count, paint);
            if (statusDirty) {
                paintOffset_ = 0;
                factorySurface().presentRegion(display_, paint, 0, GridTop);
            }
        }
        if (force || offset != offset_ || !positioned_) {
            bus_.beginWrite();
            bus_.writeC8D16(0x37, GridTop + offset % ViewportHeight);
            bus_.endWrite();
            positioned_ = true;
        }
        offset_ = offset;
    }

    // Reset before ANY other page draws or rotates, including Sleep/test and
    // background Xiaozhi wake. Other apps continue using ordinary coordinates.
    void end() {
        if (!active_) return;
        defineScroll(0, FactoryHome::Height, 0, 0);
        active_ = positioned_ = false;
    }

private:
    void defineScroll(int top, int height, int bottom, int start) {
        bus_.beginWrite(); bus_.writeCommand(0x33);
        bus_.write16(top); bus_.write16(height); bus_.write16(bottom);
        bus_.writeC8D16(0x37, start);
        bus_.endWrite();
    }
    void writeContent(int first, int count, void (*paint)()) {
        while (count > 0) {
            const auto span = FactoryHome::ringSpan(first, count);
            paintOffset_ = span.paintOffset;
            factorySurface().presentRegion(display_, paint, span.panelY, span.panelY + span.count);
            first += span.count; count -= span.count;
        }
    }
    Arduino_DataBus &bus_;
    Arduino_GFX &display_;
    int offset_ = 0, paintOffset_ = 0;
    bool active_ = false, positioned_ = false;
};
