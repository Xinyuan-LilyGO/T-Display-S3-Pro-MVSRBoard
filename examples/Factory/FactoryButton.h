#pragma once
#include <Arduino.h>

// One event per debounced press; holding the button does not repeat.
class FactoryButton {
public:
    explicit FactoryButton(uint8_t pin) : _pin(pin) {}

    void begin() {
        pinMode(_pin, INPUT_PULLUP);
        _raw = _stable = digitalRead(_pin) == LOW;
        _changedAt = millis();
    }

    bool pressed() {
        const bool raw = digitalRead(_pin) == LOW;
        const uint32_t now = millis();
        if (raw != _raw) {
            _raw = raw;
            _changedAt = now;
        }
        if (now - _changedAt >= 30 && _stable != _raw) {
            _stable = _raw;
            return _stable;
        }
        return false;
    }

private:
    uint8_t _pin;
    bool _raw = false, _stable = false;
    uint32_t _changedAt = 0;
};

