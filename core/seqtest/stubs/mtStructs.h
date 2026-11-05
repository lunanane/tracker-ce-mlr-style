// host stub (plan 009 P tests): the few project values the limiter reads
#pragma once
#include <stdint.h>
const int16_t MAX_SIGNED_16BIT = 32767;
enum limiterMode
{
    limiterModeSubtle,
    limiterModeHard
};
struct StubProject
{
    struct
    {
        uint16_t limiterAttack = 10;
        uint16_t limiterRelease = 100;
        uint16_t limiterThreshold = 20000;
        uint8_t limiterSidechain = 0;
        uint8_t limiterMode = limiterModeSubtle;
    } values;
};
extern StubProject mtProject;
