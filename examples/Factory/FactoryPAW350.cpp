#include "FactoryPAW350.h"
#include <Arduino.h>
#include <Wire.h>

namespace FactoryPAW350 {
namespace {
constexpr uint8_t Address=0x33, ProductID=0x88;
constexpr uint8_t RegEvent=0x02, RegDeltaX=0x03, RegDeltaY=0x04;
bool active=false, ready=false, reportedMissing=false;
uint32_t lastPoll=0,lastProbe=0;
uint32_t lastMotionLog=0;
int peakX=0,peakY=0,lastDX=0,lastDY=0;
unsigned motionSamples=0;

// PAW-A350 example uses 100 kHz. Restore the shared bus clock on every exit;
// never call Wire.begin/end or change the touch panel's pins/configuration.
struct BusClock {
    uint32_t previous=Wire.getClock();
    BusClock() { if (previous!=100000) Wire.setClock(100000); }
    ~BusClock() { if (previous!=100000) Wire.setClock(previous); }
};
bool read(uint8_t reg,uint8_t *data,uint8_t length) {
    Wire.beginTransmission(Address); Wire.write(reg);
    if (Wire.endTransmission(false)!=0) return false;
    if (Wire.requestFrom(Address,length)!=length) {
        while (Wire.available()) Wire.read();
        return false;
    }
    for (uint8_t i=0;i<length;++i) data[i]=Wire.read();
    return true;
}
bool readMotion(int &dx,int &dy) {
    uint8_t x=0,y=0;
    // Explicitly address both registers. Do not assume that a two-byte read
    // from DELTA_X auto-increments the device's pointer to DELTA_Y.
    if (!read(RegDeltaX,&x,1) || !read(RegDeltaY,&y,1)) return false;
    dx=static_cast<int8_t>(x); dy=static_cast<int8_t>(y);
    return true;
}
void resetMotionLog(uint32_t now) {
    lastMotionLog=now; peakX=peakY=lastDX=lastDY=0; motionSamples=0;
}
void logMotion(uint32_t now,int dx,int dy) {
    if (dx || dy) {
        ++motionSamples; lastDX=dx; lastDY=dy;
        const int ax=dx<0 ? -dx : dx,ay=dy<0 ? -dy : dy;
        if (ax>peakX) peakX=ax;
        if (ay>peakY) peakY=ay;
    }
    // One short summary per 500 ms, not one print per 10 ms sample. Peaks keep
    // opposite-direction movements from cancelling and hiding a working axis.
    if (now-lastMotionLog>=500) {
        if (motionSamples) Serial.printf("[PAW350] raw DX=%d DY=%d peakX=%d peakY=%d samples=%u\n",
                                        lastDX,lastDY,peakX,peakY,motionSamples);
        resetMotionLog(now);
    }
}
void failed(uint32_t now) {
    ready=false; lastProbe=now;
    if (!reportedMissing) {
        Serial.println("[Factory] PAW-A350 unavailable; connect hardware to unlock the game");
        reportedMissing=true;
    }
}
}
void open() {
    active=true; ready=false; reportedMissing=false;
    lastPoll=millis()-10; lastProbe=millis()-2000;
    resetMotionLog(millis());
}
void close() { active=ready=false; }
bool available() { return ready; }
bool poll(uint32_t now,int &dx,int &dy) {
    dx=dy=0;
    if (!active || now-lastPoll<10) return false;
    lastPoll=now;
    if (!ready && now-lastProbe<2000) return false;
    BusClock clock;
    if (!ready) {
        lastProbe=now;
        uint8_t id=0,revision=0;
        int staleX=0,staleY=0;
        if (!read(0x00,&id,1) || id!=ProductID || !read(0x01,&revision,1) || !readMotion(staleX,staleY)) {
            failed(now); return false;
        }
        ready=true; reportedMissing=false;
        resetMotionLog(now);
        Serial.printf("[Factory] PAW-A350 ready: ID=0x%02X revision=0x%02X\n",id,revision);
        Serial.println("[PAW350] X=0x03 Y=0x04, separate register reads; raw logs ignore game rotation");
        return false;
    }
    uint8_t event=0;
    if (!read(RegEvent,&event,1)) { failed(now); return false; }
    if (!(event&0x80)) { logMotion(now,0,0); return false; }
    if (!readMotion(dx,dy)) { failed(now); return false; }
    logMotion(now,dx,dy);
    return dx || dy;
}
} // namespace FactoryPAW350
