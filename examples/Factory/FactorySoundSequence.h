#pragma once
#include <cstdint>

// Small, allocation-free melody player. tick() never sleeps or waits for a note.
template<class Audio>
class FactorySoundSequence {
public:
    bool append(uint8_t note, uint16_t duration, uint16_t gap = 20, uint8_t velocity = 72) {
        if (count >= Capacity) return false;
        steps[count++] = {note, velocity, duration, gap};
        return true;
    }
    void stop(Audio &audio) {
        if (holding) audio.noteOff(steps[index].note);
        holding = running = false;
        count = index = 0;
    }
    void play(Audio &audio, uint32_t now) {
        if (holding) audio.noteOff(steps[index].note);
        holding = false;
        index = 0;
        running = count != 0;
        if (running) startNote(audio, now);
    }
    void tick(Audio &audio, uint32_t now) {
        if (!running || static_cast<int32_t>(now - deadline) < 0) return;
        if (holding) {
            audio.noteOff(steps[index].note);
            holding = false;
            deadline = now + steps[index].gap;
            if (steps[index].gap) return;
        }
        if (++index >= count) running = false;
        else startNote(audio, now);
    }
    bool playing() const { return running; }
private:
    struct Step { uint8_t note, velocity; uint16_t duration, gap; };
    static constexpr uint8_t Capacity = 20;
    Step steps[Capacity] = {};
    uint8_t count = 0, index = 0;
    bool running = false, holding = false;
    uint32_t deadline = 0;
    void startNote(Audio &audio, uint32_t now) {
        audio.noteOn(steps[index].note, steps[index].velocity);
        deadline = now + steps[index].duration;
        holding = true;
    }
};
