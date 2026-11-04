// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqTracks.h"
#include "mtSequencer.h"
#include "mtStructs.h"

extern Sequencer sequencer;
extern strMtProject mtProject;

seqlogic::TapDetector seqPatternTap;

namespace
{
// Instrument used for a taken-over track while it plays (chosen when the takeover begins).
uint8_t s_instrument[seqlogic::kTracks];
// Sample tracks are monophonic: the note that currently sounds (-1 = none).
int16_t s_sampleNote[seqlogic::kTracks];

class SeqHost : public seqtracks::Host
{
  public:
    SeqHost()
    {
        for (int i = 0; i < seqlogic::kTracks; i++)
        {
            s_instrument[i] = 0;
            s_sampleNote[i] = -1;
        }
        seqEngine.setHost(this);
    }
    void takeoverBegin(uint8_t track) override
    {
        s_instrument[track] = sequencer.seqPickInstrument(track);
        s_sampleNote[track] = -1;
        sequencer.seqTakeoverBegin(track);
    }
    void takeoverEnd(uint8_t track) override { s_sampleNote[track] = -1; }
    void noteOn(uint8_t track, uint8_t note, int8_t volume) override
    {
        if (isSampleTrack(track))
        {
            if (s_sampleNote[track] >= 0)
                sequencer.seqNoteOff(track, (uint8_t)s_sampleNote[track], s_instrument[track]);
            s_sampleNote[track] = note;
        }
        sequencer.seqNoteOn(track, note, s_instrument[track], volume);
    }
    void noteOff(uint8_t track, uint8_t note) override
    {
        if (isSampleTrack(track))
        {
            if (s_sampleNote[track] != note)
                return; // an older chord note on a monophonic voice: already replaced
            s_sampleNote[track] = -1;
        }
        sequencer.seqNoteOff(track, note, s_instrument[track]);
    }
    bool muted(uint8_t track) override { return sequencer.seqTrackMuted(track); }
};
} // namespace

seqtracks::Engine seqEngine;
static SeqHost s_host;

// ---------------------------------------------------------------- Sequencer members used by the glue
uint8_t Sequencer::seqPickInstrument(uint8_t track)
{
    return changeInstrumentInSpecificOrder(mtProject.values.lastUsedInstrument, 0, !isSampleTrack(track));
}

// The pattern note of this track goes quiet and the pattern stops driving the track until the takeover ends.
void Sequencer::seqTakeoverBegin(uint8_t track)
{
    stopOpenNoteOnTrack(track);
    strPlayer::strPlayerTrack& p = player.track[track];
    p.micromove.reset();
    p.roll.reset();
    p.arp.reset();
}

void Sequencer::seqNoteOn(uint8_t track, uint8_t note, uint8_t instrument, int8_t volume)
{
    strPlayer::strPlayerTrack::strSendStep step;
    step.note = (int8_t)note;
    step.instrument = instrument;
    step.volume = volume;
    sendNoteOn(track, &step);
}

void Sequencer::seqNoteOff(uint8_t track, uint8_t note, uint8_t instrument)
{
    strPlayer::strPlayerTrack::strSendStep step;
    step.note = (int8_t)note;
    step.instrument = instrument;
    sendNoteOff(track, &step, STEP_NOTE_OFF_DEFAULT);
}
