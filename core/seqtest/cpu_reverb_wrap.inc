// Plan 009 P: the same driver compiled twice - against the frozen pre-change reverb (ref/polyverb, TU cpu_reverb_ref.cpp) and the
// current one (TU cpu_reverb_new.cpp). Needs WRAP_NAME and WRAP_B2 (words of the second buffer) defined by the includer;
// WRAP_SKIP selects ProcessOrSkip (plan 009 P#7). Returns the number of skipped blocks.
#include "reverb.h"
#include <string.h>

int WRAP_NAME(int16_t* l, int16_t* r, int blocks, float time, float damp, float amount, float predelay, bool skip)
{
    static uint16_t b1[16384], b2[WRAP_B2];
    static Reverb rv;
    memset(b1, 0, sizeof(b1));
    memset(b2, 0, sizeof(b2));
    rv.Init(b1, b2);
    rv.SetInputGain(0.1f);
    rv.SetDiffusion(0.625f);
    rv.SetTime(time);
    rv.SetLP(damp);
    rv.SetAmount(amount);
    rv.SetPredelayLength(predelay);
    rv.ResetState();
    (void)skip;
    int skipped = 0;
    for (int b = 0; b < blocks; b++)
    {
#ifdef WRAP_SKIP
        if (skip)
        {
            skipped += rv.ProcessOrSkip(l + b * 128, r + b * 128, 128) ? 0 : 1;
            continue;
        }
#endif
        rv.Process(l + b * 128, r + b * 128, 128);
    }
    return skipped;
}
