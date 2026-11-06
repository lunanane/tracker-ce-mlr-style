// Sequencer tracks mod -- PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// v0.4.1: the PolySynth pitch view on the DEVICE call sequence, with the real interface/seqPlay/pitchTrace.cpp and traceControl.cpp
// (compiled against devstub/: a line by line model of the stock display.cpp step machine, its refresh queue and the 8172 byte frame).
// The engine is the real plinkyvoice::Engine, driven block by block like AudioSynthPlinky::update (synth_plinky.cpp: render only while
// isActive()), the main loop runs at a jittery device cadence: the module update (as Interface::update calls it: only while the
// module's moduleRefresh is set, read from the real module sources), then one display.update() step. Checked on the composed frame
// (what step3 hands the FT81x): the pitch lines of every sounding pool voice, in the colour of its instance, whichever entry point
// played the note.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#define FEATURE_PLINKY 1
#include "plinky_voice.h"
#include "../interface/seqPlay/pitchTrace.cpp"
#include "../interface/seqPlay/traceControl.cpp"

DlBuild g_dl;
cDisplay display;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                \
    do                                                          \
    {                                                           \
        g_checks++;                                             \
        if (!(c))                                               \
        {                                                       \
            g_fail++;                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                       \
    } while (0)

// ---- the device side the view reads (seqPlinky.cpp forwards to the engine of the audio graph) -------------------------------------
static plinkyvoice::Engine* g_eng = nullptr;
static uint64_t g_us = 0; // simulated time
uint32_t seqNowMs() { return (uint32_t)(g_us / 1000u); }
void seqPlinkyLiveVoice(int voice, bool* on, uint8_t* inst, float* pitch, float* env, uint32_t* serial)
{
    g_eng->liveVoice(voice, *on, *inst, *pitch, *env, *serial);
}

// ---- the stock controls around the view ------------------------------------------------------------------------------------------
struct Dummy : cDisplayControl
{
    int bytes;
    Dummy(strControlProperties* p) : cDisplayControl(controlIdDummy, 1), bytes(p ? p->x : 0) { style = p ? p->style : 0; }
    void update() { g_dl.bytes += bytes; }
};
static int dummy(int bytes, bool shown)
{
    strControlProperties p;
    p.x = bytes;
    p.style = shown ? controlStyleShow : 0;
    const int h = display.createControl<Dummy>(&p);
    display.refreshControl(h);
    return h;
}

// the body of a module function in the real source (CRLF or LF), "" when missing
static std::string body(const char* file, const char* sig)
{
    std::string s;
    FILE* fp = fopen(file, "rb");
    if (!fp) return "";
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) s.append(buf, n);
    fclose(fp);
    size_t p = s.find(sig);
    if (p == std::string::npos) return "";
    p = s.find('{', p);
    int depth = 0;
    for (size_t i = p; i < s.size(); i++)
    {
        if (s[i] == '{') depth++;
        if (s[i] == '}' && --depth == 0) return s.substr(p, i - p + 1);
    }
    return "";
}

// ---- one module on the device ----------------------------------------------------------------------------------------------------
struct Module
{
    bool instrEditor;
    uint8_t moduleRefresh = 0;
    bool clears = false; // the module's update() clears moduleRefresh (read from the source)
    bool reaches = false; // its update() reaches pitchTraceView.update()
    void start()
    {
        display.destroyAll();
        pitchTraceView.forget();
        if (instrEditor) // gInstrEdit.cpp: the view's control first (drawn on top), then the page
        {
            pitchTraceView.createControl();
            dummy(600, true);  // title bar
            for (int i = 0; i < 8; i++) dummy(160, true); // labels / bars of the strip
            dummy(2500, false); // the parameter page, hidden while the pitch view is shown
            const std::string u = body("../interface/instrumentEditor/fIinstrEdit.cpp", "void cInstrumentEditor::update()");
            const std::string r = body("../interface/instrumentEditor/gInstrEdit.cpp", "void cInstrumentEditor::plinkyRefresh()");
            clears = u.find("moduleRefresh = 0") != std::string::npos;
            reaches = u.find("plinkyRefresh()") != std::string::npos && r.find("pitchTraceView.update()") != std::string::npos;
        }
        else // gPatternEdit.cpp: frame, labels, background label, the Play mode overlay (trace, legend), then the pattern controls
        {
            dummy(16, true);
            for (int i = 0; i < 8; i++) dummy(160, true);
            dummy(100, true);
            pitchTraceView.createControl();
            dummy(300, false); // legend, hidden while the pitch view is shown
            dummy(6000, false); // pattern control, hidden in the PolySynth view
            const std::string u = body("../interface/patternEditor/fPatternEdit.cpp", "void cPatternEditor::update()");
            const std::string o = body("../interface/seqPlay/seqPlayOverlay.cpp", "void SeqPlayOverlay::update()");
            clears = u.find("moduleRefresh = 0") != std::string::npos;
            reaches = u.find("seqPlayOverlay.update()") != std::string::npos && o.find("pitchTraceView.update()") != std::string::npos;
        }
        CHECK(reaches);
        moduleRefresh = 1; // start()
    }
    void update() // Interface::update: onScreenModule->update() only while moduleRefresh
    {
        if (!moduleRefresh) return;
        if (clears) moduleRefresh = 0;
        if (reaches) pitchTraceView.update();
    }
};

// ---- the simulated device --------------------------------------------------------------------------------------------------------
struct Device
{
    uint64_t nextBlock = 0, nextPass = 0;
    unsigned rnd = 12345;
    Module* m = nullptr;
    void audio()
    {
        if (!g_eng->isActive()) return; // AudioSynthPlinky::update
        int16_t l[plinkyvoice::kBlock], r[plinkyvoice::kBlock];
        g_eng->render(l, r);
    }
    void run(int ms)
    {
        const uint64_t end = g_us + (uint64_t)ms * 1000u;
        while (g_us < end)
        {
            if (g_us >= nextBlock)
            {
                audio();
                nextBlock += 2902;
            }
            if (g_us >= nextPass)
            {
                m->update();
                display.update();
                rnd = rnd * 1664525u + 1013904223u;
                nextPass = g_us + 700 + (rnd >> 16) % 3000; // 0.7 .. 3.7 ms per main loop pass
            }
            g_us += 100;
        }
    }
};

struct Seen
{
    bool traceShown = false;
    int strips = 0, points = 0;
    int perInst[3] = { 0, 0, 0 };
};
static Seen seen(int traceCtl)
{
    Seen s;
    for (int c : display.last.controls) s.traceShown |= c == traceCtl;
    const cDisplayControl* t = display.controlsTable[traceCtl];
    if (!s.traceShown || !t) return s;
    for (const DlOp& o : t->ram)
    {
        if (o.kind != 'S') continue;
        for (int k = 0; k < 3; k++)
            for (int rank = 0; rank < 4; rank++)
                if (o.col == pitchtrace::lineColor(k, rank))
                {
                    s.perInst[k] += o.n;
                    s.strips++;
                    s.points += o.n;
                }
    }
    return s;
}

static int traceControlIndex()
{
    for (int i = 0; i < display.controlsCount; i++)
        if (dynamic_cast<cSeqTraceControl*>(display.controlsTable[i])) return i;
    return -1;
}

static void scenario(bool instrEditor)
{
    plinkyvoice::Engine* e = new plinkyvoice::Engine();
    g_eng = e;
    for (int k = 0; k < plinkyvoice::kInstances; k++) e->setParam(plinkyvoice::P_SUSTAIN, 100, k);
    Module m;
    m.instrEditor = instrEditor;
    Device d;
    d.m = &m;
    d.nextBlock = g_us;
    d.nextPass = g_us;
    m.start();
    d.run(300); // the page is up
    pitchTraceView.show(true, "FX2: close"); // FX2
    d.run(200);
    const int tc = traceControlIndex();
    CHECK(tc >= 0);
    // a sustained chord on instance 1 and a note on instance 2
    e->noteOn(60, 100, 0);
    e->noteOn(64, 100, 0);
    e->noteOn(67, 100, 0);
    e->noteOn(72, 100, 1);
    d.run(1500);
    Seen s = seen(tc);
    printf("  %s: frames %d, trace in frame %d, strips %d, points %d (inst %d/%d/%d), frame %d B\n", instrEditor ? "instrument page" : "Play mode",
           display.frames, s.traceShown, s.strips, s.points, s.perInst[0], s.perInst[1], s.perInst[2], display.last.bytes);
    CHECK(s.traceShown);
    CHECK(s.perInst[0] >= 3 * 20); // three lines of instance 1
    CHECK(s.perInst[1] >= 20);     // one of instance 2
    // a staccato stab on instance 3 (note on, off two blocks later, the engine idles right after)
    e->setParam(plinkyvoice::P_RELEASE, 2, 2);
    for (int k = 0; k < 2; k++) e->allNotesOff(k);
    d.run(800);
    e->noteOn(55, 100, 2);
    d.run(6);
    e->noteOff(55, 2);
    d.run(400);
    s = seen(tc);
    CHECK(s.traceShown);
    CHECK(s.perInst[2] >= 2);
    pitchTraceView.show(false, nullptr);
    pitchTraceView.forget();
    display.destroyAll();
    delete e;
    g_eng = nullptr;
}

// all 8 pool voices across the three instances at once (any entry point: sendMidiNoteOn / pattern / pads / launcher / external MIDI all
// end in seqPlinkyNote -> Engine::noteOn of the channel's instance, seqPlinky.cpp), whichever instance the page shows; pitch bend moves
// the instance's lines
static void scenarioAllVoices()
{
    plinkyvoice::Engine* e = new plinkyvoice::Engine();
    g_eng = e;
    for (int k = 0; k < plinkyvoice::kInstances; k++)
    {
        e->setParam(plinkyvoice::P_SUSTAIN, 100, k);
        e->setParam(plinkyvoice::P_VOICES, 4, k);
    }
    Module m;
    m.instrEditor = false;
    Device d;
    d.m = &m;
    d.nextBlock = g_us;
    d.nextPass = g_us;
    m.start();
    d.run(100);
    pitchTraceView.show(true, "FX2: close");
    d.run(100);
    const int tc = traceControlIndex();
    const uint8_t notes[8] = { 48, 52, 55, 60, 64, 67, 72, 76 };
    const int inst[8] = { 0, 0, 0, 1, 1, 1, 2, 2 };
    for (int i = 0; i < 8; i++) e->noteOn(notes[i], 100, inst[i]);
    d.run(1200);
    Seen s = seen(tc);
    printf("  all voices: strips %d, points %d (inst %d/%d/%d), sounding %d, frame %d B\n", s.strips, s.points, s.perInst[0], s.perInst[1],
           s.perInst[2], e->activeVoices(), display.last.bytes);
    CHECK(e->activeVoices() == 8);
    CHECK(s.traceShown);
    CHECK(s.strips >= 8);
    CHECK(s.perInst[0] >= 3 * 16 && s.perInst[1] >= 3 * 16 && s.perInst[2] >= 2 * 16);
    // the topmost line (instance 3) before and after a bend of +2 semitones
    int topBefore = 1 << 30;
    for (const DlOp& o : display.controlsTable[tc]->ram)
        if (o.kind == 'S' && o.col == pitchtrace::lineColor(2, 1)) topBefore = o.minY < topBefore ? o.minY : topBefore;
    e->pitchBend(16383, 2);
    d.run(1200);
    int topAfter = 1 << 30;
    for (const DlOp& o : display.controlsTable[tc]->ram)
        if (o.kind == 'S' && o.col == pitchtrace::lineColor(2, 1)) topAfter = o.minY < topAfter ? o.minY : topAfter;
    CHECK(topBefore < (1 << 30) && topAfter < topBefore); // higher pitch = smaller y
    pitchTraceView.show(false, nullptr);
    pitchTraceView.forget();
    display.destroyAll();
    delete e;
    g_eng = nullptr;
}

int main()
{
    printf("pitch view, device call sequence:\n");
    scenario(false);
    scenario(true);
    scenarioAllVoices();
    printf("pitchdevice: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
