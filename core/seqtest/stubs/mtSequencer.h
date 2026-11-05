// host stub (plan 009 P tests): only the tempo getter the delay needs
#pragma once
struct StubSequencer
{
    float getCurrentTempo() { return 120.0f; }
};
extern StubSequencer sequencer;
