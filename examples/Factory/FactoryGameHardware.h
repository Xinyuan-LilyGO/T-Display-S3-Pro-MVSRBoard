#pragma once
#include "FactoryMiniGameRules.h"

namespace FactoryGameRules {
// Used by both games: reconnecting never resumes play or replays a held tap.
class HardwareGate {
public:
    void reset(bool available=false) { ready_=available; waitRelease_=true; }
    bool ready() const { return ready_; }
    bool update(bool available,State &state) {
        if (!available && state==State::Playing) state=State::Paused;
        if (available==ready_) return false;
        ready_=available; waitRelease_=true;
        return true;
    }
    bool touch(bool down) {
        if (!ready_) { waitRelease_=true; return false; }
        if (waitRelease_) {
            if (!down) waitRelease_=false;
            return false;
        }
        return true;
    }
private:
    bool ready_=false,waitRelease_=true;
};
}
