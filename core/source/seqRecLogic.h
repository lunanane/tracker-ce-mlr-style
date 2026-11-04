// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// The Rec key of the mod (plan 008 A): one rule for Play mode and the MLR (CUT) layer. Pure logic, no Polyend includes,
// host-tested in core/seqtest (test_rec.inc) with a model of the keys driver.
//
// What the hardware sends for one physical press of a key (platforms/mk66/3rdparty/Buttons/Keys.cpp, Keys::update): PRESS at
// once, then HOLD every 333 ms (then 233, 143, 98, 78, 63 ms ... while the key stays down), RELEASE when it is let go. Rec is a
// toggle: only the PRESS means anything. A HOLD or RELEASE read as "Rec up" ended an MLR take after the first hold event (v0.2.4).
#pragma once
#include <stdint.h>

namespace seqrecui {

enum KeyState : uint8_t { KEY_RELEASE = 0, KEY_PRESS = 1, KEY_HOLD = 2 }; // = buttonRelease / buttonPress / buttonHold

// The event the Rec toggle sees for a key state: true = the key was pressed (toggle), false = nothing to do (hold, release).
inline bool isToggle(uint8_t state) { return state == KEY_PRESS; }

// What the Session host passes to a layer's rec(bool down): 1 = press, 0 = release, -1 = nothing (a hold is not a release).
inline int8_t layerEvent(uint8_t state) { return state == KEY_PRESS ? 1 : (state == KEY_RELEASE ? 0 : -1); }

// Play mode: the user's idea of "a take is on", kept on the main loop next to the audio-side planner state. Two presses that come
// before the audio side published its state still toggle (on -> off), a bounce of the key (second press within kBounceMs) is
// ignored, a take that ends by itself (loop done, transport stop, track Off ...) clears the flag, a start that never showed up
// on the audio side is dropped after kStartTimeoutMs.
class RecToggle
{
  public:
    enum Act : uint8_t { NONE = 0, START = 1, STOP = 2 };
    enum { kBounceMs = 60, kStartTimeoutMs = 400 };

    // A Rec press at time `now` (ms). engineActive: the planner is armed / recording (published by the audio side).
    Act press(uint32_t now, bool engineActive)
    {
        if (havePress_ && (uint32_t)(now - lastPress_) < (uint32_t)kBounceMs)
            return NONE;
        havePress_ = true;
        lastPress_ = now;
        if (on_ || engineActive)
        {
            on_ = false;
            seen_ = false;
            return STOP;
        }
        on_ = true;
        seen_ = false;
        since_ = now;
        return START;
    }
    // The start was refused (Off track, launched clip ...): nothing is on.
    void cancel()
    {
        on_ = false;
        seen_ = false;
    }
    // Once per main loop pass. Returns true once when a take that had been seen on the audio side ended without a press.
    bool update(uint32_t now, bool engineActive)
    {
        if (!on_)
            return false;
        if (engineActive)
        {
            seen_ = true;
            return false;
        }
        if (seen_)
        {
            on_ = false;
            seen_ = false;
            return true;
        }
        if ((uint32_t)(now - since_) >= (uint32_t)kStartTimeoutMs)
            on_ = false; // the audio side never started it (queue full, transport reset)
        return false;
    }
    void reset() { *this = RecToggle(); }
    bool on() const { return on_; }

  private:
    bool on_ = false;
    bool seen_ = false;
    bool havePress_ = false;
    uint32_t lastPress_ = 0;
    uint32_t since_ = 0;
};

// Why the audio side ended a take (published with the state; the Play screen names the unexpected ones).
enum EndWhy : uint8_t
{
    END_USER = 0,  // Rec pressed again, panic, Play mode left, pattern changed
    END_LOOP = 1,  // Shift + Rec: the loop is complete
    END_STOPPED = 2, // the transport stopped or the clock went silent
    END_OFF = 3,   // the recorded track was set to Off
    END_RESET = 4, // the mod's state was reset (project load)
    END_PATTERN = 5 // the pattern changed under the take (song mode, scene, pattern queue)
};
inline const char* endText(uint8_t why)
{
    switch (why)
    {
        case END_STOPPED: return "rec: transport stopped";
        case END_OFF: return "rec: track set Off";
        case END_RESET: return "rec: reset";
        case END_PATTERN: return "rec: pattern changed";
        default: return 0; // expected endings are silent
    }
}

} // namespace seqrecui
