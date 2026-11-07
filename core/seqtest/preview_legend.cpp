// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Host preview of the Play legend with the sequencer settings page above it (plan 025): compiles the real seqPlayLegend.cpp against a stub of
// the display list macros (legendstub/) and dumps its draw ops (the settings page control, then the legend control on top of it) for
// legend_preview.py.   usage: preview_legend <dir>      writes <dir>/legend_<name>.ops      (make legend-preview)
#include "../interface/seqPlay/seqPlayLegend.cpp"
#include <string.h>

FILE* g_ops = nullptr;
int g_prim = 0;
cDisplayStub display;

static void scene(const char* dir, const char* name, uint8_t type, uint8_t sec, uint8_t idx, bool tweak)
{
    char path[512];
    snprintf(path, sizeof path, "%s/legend_%s.ops", dir, name);
    g_ops = fopen(path, "w");
    if (!g_ops)
        return;
    static SeqPlayViewData legend, params;
    legend = SeqPlayViewData();
    params = SeqPlayViewData();
    legend.mode = SeqPlayViewData::LEGEND;
    legend.type = type;
    legend.plSetup = seqplinky::Setup();
    if (seqlogic::isHrType(type))
    {
        static seqgen::HrSlot slot;
        static seqgen::HrBus bus;
        seqgen::HrScript* s = slot.create((uint8_t)(type - seqlogic::TYPE_HR_FIRST));
        s->setBus(&bus);
        seqplay::fillHrSnap(legend.hr, *s);
    }
    const seqparams::Tab* tb = seqparams::tab(type);
    params.mode = SeqPlayViewData::PARAMS;
    seqparams::Model& m = params.prm;
    m.on = 1;
    m.type = type;
    m.track = 3;
    m.sec = sec;
    m.idx = idx;
    snprintf(m.key, sizeof m.key, "8A D DOR");
    params.keyMark = 2;
    legend.keyMark = 3;
    snprintf(legend.key, sizeof legend.key, "11A F#m");
    for (uint8_t i = 0; i < tb->nTotal; i++)
    {
        const seqparams::Desc& d = seqparams::desc(*tb, i);
        int v = seqparams::defaultOf(type, d);
        if (tweak && d.mx - d.mn >= 2 && (i % 3) == 1) // a few non default values so the bars differ
            v = d.mn + (d.mx - d.mn) * 2 / 3;
        m.v[i] = (int8_t)v;
    }
    strControlProperties pp = { 0, 0, 800, 480, 0, &params, nullptr };
    cSeqPlayLegend ctlParams(&pp);
    strControlProperties pl = { 0, 0, 800, 480, 0, &legend, nullptr };
    cSeqPlayLegend ctlLegend(&pl);
    ctlParams.update();
    ctlLegend.update();
    fclose(g_ops);
    printf("%s\n", path);
}

static void wheel(const char* dir, const char* name, uint8_t cur, uint8_t mark, const char* label)
{
    char path[512];
    snprintf(path, sizeof path, "%s/legend_%s.ops", dir, name);
    g_ops = fopen(path, "w");
    if (!g_ops)
        return;
    static SeqPlayViewData v;
    v = SeqPlayViewData();
    v.mode = SeqPlayViewData::WHEEL;
    seqwheel::Model& m = v.wheel;
    m.on = 1;
    m.cur = cur;
    m.mark = mark;
    m.trail[0] = (uint8_t)((cur + 2) % 24); m.trail[1] = (uint8_t)((cur + 5) % 24);
    m.others = (1u << ((cur + 2) % 24)) | (1u << 4) | (1u << 23);
    snprintf(m.label, sizeof m.label, "%s", label);
    strControlProperties pp = { 0, 0, 800, 480, 0, &v, nullptr };
    cSeqPlayLegend ctl(&pp);
    ctl.update();
    fclose(g_ops);
    printf("%s\n", path);
}

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : ".";
    scene(dir, "chordarp", seqlogic::TYPE_CHORDARP, 0, 6, true);   // cursor on Latch
    scene(dir, "mono", seqlogic::TYPE_MONO, 0, 0, true);
    scene(dir, "strings", seqlogic::TYPE_PLINKY, 0, 4, true);
    scene(dir, "hr_chordarp", seqlogic::TYPE_HR_FIRST + seqgen::HR_CHORD_ARP, 1, 2, true);
    scene(dir, "hr_stabs", seqlogic::TYPE_HR_FIRST + seqgen::HR_CHORD_STABS, 1, 3, true);
    wheel(dir, "wheel", 15, 2, "8A D DOR");           // 8A, neighbour of another track
    wheel(dir, "wheel_match", 14, 1, "8B C");
    wheel(dir, "wheel_clash", 21, 3, "11A F#m");
    return 0;
}
