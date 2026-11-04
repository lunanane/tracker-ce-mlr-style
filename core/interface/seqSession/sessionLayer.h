// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// A pad layer of the Session view (plans/002-session-view.md). The Session module owns the screen buttons, the Play
// button and the two layer-switch pads (col 11, rows 1 + 2 in 1-based numbers); a layer gets everything else.
//   CLIPS = clip launcher (clipsLayer.*)        CUT = MLR page (another component, default: CutStubLayer)
//
// Pad numbering: col 0..11, row 0..3 (0-based), pad index = row * 12 + col. LED levels 0..15.
// A layer must not touch the audio side directly; use the main-loop API of seqLauncher.h / seqTracks.h.
#pragma once
#include <stdint.h>

namespace seqsession {

struct SessionLayer
{
    virtual const char* name() const = 0;                       // "CLIPS", "CUT" (screen title)
    virtual void enter(uint8_t firstTrack) { setGroup(firstTrack); } // the layer becomes the visible one
    virtual void leave() {}                                     // hidden or Session left: release held pads / gestures
    virtual void setGroup(uint8_t firstTrack) = 0;              // group of 8 tracks: firstTrack = 0 (1-8) or 8 (9-16)
    virtual void pad(uint8_t col, uint8_t row, bool down) = 0;  // never called for the layer-switch pads (col 10, rows 0-1)
    virtual void leds(uint8_t out[48]) = 0;                     // levels 0..15 for all 48 pads (module overlays the switch pads)
    virtual void update(uint32_t nowMs) { (void)nowMs; }       // main loop, once per pass while visible
    virtual bool jog(int16_t value, bool shift) { (void)value; (void)shift; return false; } // true = handled
    virtual void info(char lines[4][48]) = 0;                   // the four text lines under the title ("" = nothing)

  protected:
    ~SessionLayer() {}
};

// Placeholder for the CUT layer until the MLR component is plugged in with cSeqSession::setCutLayer().
struct CutStubLayer : SessionLayer
{
    const char* name() const override { return "CUT"; }
    void setGroup(uint8_t) override {}
    void pad(uint8_t, uint8_t, bool) override {}
    void leds(uint8_t out[48]) override
    {
        for (int i = 0; i < 48; i++)
            out[i] = 0;
    }
    void info(char lines[4][48]) override
    {
        const char* t[4] = { "CUT - coming", "", "", "" };
        for (int i = 0; i < 4; i++)
        {
            int j = 0;
            for (; t[i][j] && j < 47; j++)
                lines[i][j] = t[i][j];
            lines[i][j] = 0;
        }
    }
};

} // namespace seqsession
