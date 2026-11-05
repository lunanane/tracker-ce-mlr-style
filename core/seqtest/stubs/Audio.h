// host stub (plan 009 P tests): the real Audio.h pulls in the whole Teensy audio library
#pragma once
// the real Audio.h is skipped on the host (-DAudio_h_): its quote include would win over this directory
#include "AudioStream.h"
#define AudioNoInterrupts()
#define AudioInterrupts()
