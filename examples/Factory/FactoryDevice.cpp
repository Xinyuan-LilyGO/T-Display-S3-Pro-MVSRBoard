#include "FactoryDevice.h"
#include "FactoryDeviceRules.h"
#include "FactoryVolume.h"
#include "pin_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>

namespace FactoryDevice {
namespace {
constexpr uint8_t backlightChannel = 1;
Preferences preferences;
Battery current;
bool prefsReady = false, adcReady = false, pendingSave = false;
int light = 100, observedLight = 100, observedVolume = 70, filteredMv = 0;
uint32_t lastSample = 0, changedAt = 0;
uint32_t sampleInterval = 1200;
bool readRegister(uint8_t reg, uint8_t &value) {
    Wire.beginTransmission(SY6970_Address); Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(uint8_t(SY6970_Address),uint8_t(1)) != 1) {
        while (Wire.available()) Wire.read();
        return false;
    }
    value = Wire.read(); return true;
}
bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(SY6970_Address); Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
}
void save() {
    if (prefsReady && pendingSave) {
        if (preferences.getUChar("light",100) != light) preferences.putUChar("light",light);
        if (preferences.getUChar("volume",FactoryVolume::Default) != FactoryVolume::get())
            preferences.putUChar("volume",FactoryVolume::get());
    }
    pendingSave = false;
}
}
void begin() {
    prefsReady = preferences.begin("factory-device",false);
    if (prefsReady) {
        light = FactoryDeviceRules::brightness(preferences.getUChar("light",100));
        FactoryVolume::set(preferences.getUChar("volume",FactoryVolume::Default));
    }
    observedLight = light; observedVolume = FactoryVolume::get();
    ledcSetup(backlightChannel,2000,8); ledcAttachPin(LCD_BL,backlightChannel);
    ledcWrite(backlightChannel,(light*255+50)/100);
    // Only enable ADC conversion. Do not reset the charger or change charging
    // current, target voltage, BATFET, OTG, input limits or watchdog settings.
    uint8_t config = 0;
    adcReady = readRegister(0x02,config) && writeRegister(0x02,config|0xc0);
    lastSample = millis();
    Serial.printf("[Factory] Battery ADC %s; brightness=%d%% volume=%d%%\n",adcReady ? "enabled" : "unavailable",light,observedVolume);
}
void tick() {
    const uint32_t now = millis();
    if (observedLight != light || observedVolume != FactoryVolume::get()) {
        observedLight = light; observedVolume = FactoryVolume::get();
        changedAt = now; pendingSave = true;
    }
    if (pendingSave && now-changedAt >= 1000) save();
    if (now-lastSample < sampleInterval) return;
    lastSample = now; sampleInterval = 5000;
    uint8_t config = 0, voltage = 0, status = 0, bus = 0;
    // A charger reset/watchdog can disable continuous ADC. Restart it, then
    // wait for conversion instead of presenting an old register as new data.
    if (!readRegister(0x02,config)) { current = Battery{}; adcReady = false; filteredMv = 0; return; }
    if (!adcReady || !(config & 0x40)) {
        adcReady = writeRegister(0x02,config|0xc0);
        current = Battery{}; filteredMv = 0; sampleInterval = 1200; return;
    }
    if (!readRegister(0x0e,voltage) || !readRegister(0x0b,status) || !readRegister(0x11,bus)) {
        current = Battery{}; filteredMv = 0; return;
    }
    const int mv = 2304 + (voltage & 0x7f)*20;
    const bool usb = bus & 0x80;
    const unsigned charge = (status >> 3) & 3;
    current.usb = usb;
    current.charging = usb && (charge == 1 || charge == 2);
    current.full = usb && charge == 3;
    current.available = FactoryDeviceRules::batteryPercent(mv) >= 0;
    if (!current.available) { current.percent = -1; current.millivolts = 0; filteredMv = 0; return; }
    filteredMv = filteredMv ? (filteredMv*3+mv+2)/4 : mv;
    current.millivolts = mv;
    current.percent = FactoryDeviceRules::batteryPercent(filteredMv);
    if (current.full && mv >= 4100) current.percent = 100;
}
int brightness() { return light; }
void setBrightness(int percent) {
    light = FactoryDeviceRules::brightness(percent);
    ledcWrite(backlightChannel,(light*255+50)/100);
}
Battery battery() { return current; }
void prepareSleep() {
    pendingSave = true; save();
    uint8_t config = 0;
    if (readRegister(0x02,config)) writeRegister(0x02,config & 0x3f);
    ledcWrite(backlightChannel,0);
}
}
