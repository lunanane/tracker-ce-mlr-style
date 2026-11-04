// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// A pad layer of the Session view (plans/002-session-view.md). The Session module owns the screen buttons (group select
// per layer, button 7 = CLIPS, button 8 = CUT) and the Play button; a layer gets all 48 pads, the jog wheel and Rec.
//   CLIPS = clip launcher (clipsLayer.*)        CUT = MLR page (cutSessionLayer.h)
//
// Pad numbering: col 0..11, row 0..3 (0-based), pad index = row * 12 + col. LED levels 0..15.
// A layer must not touch the audio side directly; use the main-loop API of seqLauncher.h / seqTracks.h.
#pragma once
#include <stdint.h>

namespace seqsession {

enum ArrowDir : uint8_t { ARROW_LEFT = 0, ARROW_RIGHT = 1, ARROW_UP = 2, ARROW_DOWN = 3 };

struct SessionLayer
{
    virtual const char* name() const = 0;                       // "CLIPS", "CUT" (screen title)
    virtual void enter(uint8_t firstTrack) { setGroup(firstTrack); } // the layer becomes the visible one
    virtual void leave() {}                                     // hidden or Session left: release held pads / gestures
    virtual void setGroup(uint8_t firstTrack) = 0;              // group of 8 tracks: firstTrack = 0 (1-8) or 8 (9-16)
    virtual void pad(uint8_t col, uint8_t row, bool down) = 0;  // all 48 pads
    virtual void leds(uint8_t out[48]) = 0;                     // levels 0..15 for all 48 pads
    virtual uint8_t groupSize() const { return 8; }             // tracks per group: 8 (buttons 1-2) or 4 (buttons 1-4)
    virtual void rec(bool down) { (void)down; }                 // Rec button pressed / released (CLIPS ignores it)
    virtual void update(uint32_t nowMs) { (void)nowMs; }       // main loop, once per pass while visible
    virtual bool jog(int16_t value, bool shift) { (void)value; (void)shift; return false; } // true = handled
    virtual void info(char lines[4][48]) = 0;                   // the four text lines under the title ("" = nothing)

    // ---- screen of the layer (plan 005 step 0): the Session module is a thin host. A layer draws its own screen with its own
    // display controls (created in createControls, which the host calls from initDisplayControls), so R2 (CLIPS grid) and R4
    // (CUT lanes) never edit the same screen code. The defaults draw nothing: the host then shows the title and info lines.
    virtual void createControls() {}                 // module start: create this layer's controls (hidden until shown)
    virtual void destroyControls() {}                // module stop
    virtual bool refresh(bool force) { (void)force; return false; } // main loop; true = it changed the screen (host synchronises)
    virtual bool button(uint8_t i, bool down) { (void)i; (void)down; return false; } // screen button 0..7 (press and release); true = handled
    virtual bool buttonLabel(uint8_t i, char out[12]) { (void)i; (void)out; return false; } // true = out holds the label of button i
    virtual bool arrow(ArrowDir dir, bool shift) { (void)dir; (void)shift; return false; } // Left / Right / Up / Down keys; true = handled
    virtual bool drawsOwnScreen() const { return false; } // true: the host hides its title / info lines for this layer

  protected:
    ~SessionLayer() {}
};

} // namespace seqsession
