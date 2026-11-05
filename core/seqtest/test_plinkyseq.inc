// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 010 B: the Plinky sequencer type (pad -> note mapping, type switch / claim, takeover, recording, sidecar) and the parameter
// overlay (navigation, value steps, display-list size). Included from test_main.cpp (uses CHECK, MockHost, mk, playRec, drainOps, Head).
#include "seqPlinkySeqLogic.h"
#include "seqPlinkyUiLogic.h"
#include "seqPlayLogic.h"

namespace
{
// stand-in for the FT81x display list: counts words like CountCtx of test_cut.inc, bounds of everything drawn
struct PlCtx
{
    int words = 0, rects = 0, texts = 0;
    int minX = 9999, minY = 9999, maxX = -1, maxY = -1;
    uint8_t prim = 0;
    bool phase = false;
    void ensure(uint8_t w)
    {
        if (prim == w) return;
        if (prim) words++;
        if (w == 1) words += 2;
        prim = w;
    }
    void bound(int x0, int y0, int x1, int y1)
    {
        if (x0 < minX) minX = x0;
        if (y0 < minY) minY = y0;
        if (x1 > maxX) maxX = x1;
        if (y1 > maxY) maxY = y1;
    }
    void color(uint32_t) { words++; }
    void rect(int x0, int y0, int x1, int y1)
    {
        ensure(1);
        words += 2;
        rects++;
        bound(x0, y0, x1, y1);
    }
    void textPhase() { ensure(0); words++; phase = true; }
    void text(int x, int y, int, int opt, const char* s)
    {
        ensure(0);
        const int n = (int)strlen(s);
        words += n * 2 + 2;
        texts++;
        const int w = n * 9, h = 17;
        const int x0 = (opt & 1) ? x - w / 2 : ((opt & 4) ? x - w : x);
        const int y0 = (opt & 2) ? y - h / 2 : y;
        bound(x0, y0, x0 + w, y0 + h);
    }
    void finish() { ensure(0); }
    int bytes() const { return words * 4; }
};
} // namespace

static void test_plinky_pad_map()
{
    using namespace seqplinky;
    Setup s; // C minor, octave 2, strings 3 degrees apart
    CHECK(s.root == 0 && s.scale == 1 && s.octave == 2 && s.interval == 3);
    // bottom row = lowest note of a string, top row 3 degrees higher; row 0 is the top row
    CHECK(padNote(s, 0, 3) == 36);               // C2 (MIDI 36)
    CHECK(padNote(s, 0, 2) == 38 && padNote(s, 0, 1) == 39 && padNote(s, 0, 0) == 41);
    // the next string starts 3 degrees up: its bottom note is the top-but-one... of the first string (guitar-like overlap)
    CHECK(padNote(s, 1, 3) == padNote(s, 0, 0));
    CHECK(padNote(s, 2, 3) == degreeNote(s, 6) && degreeNote(s, 6) == 36 + 10);
    // degrees beyond the scale size wrap into the next octave
    CHECK(degreeNote(s, 7) == 48 && degreeNote(s, 14) == 60);
    CHECK(padNote(s, 11, 0) == degreeNote(s, 36));
    // root and scale change the notes
    Setup d = s;
    d.root = 2;
    d.scale = 0; // D major
    CHECK(padNote(d, 0, 3) == 38 && padNote(d, 0, 2) == 40 && padNote(d, 0, 1) == 42);
    // octave
    d.octave = 3;
    CHECK(padNote(d, 0, 3) == 50);
    // every setup keeps every pad inside 0..127 and on a unique string layout (monotonic along a string and along the columns)
    for (int sc = 0; sc < kScales; sc++)
        for (int oct = 1; oct <= 4; oct++)
            for (int iv = 1; iv <= 7; iv++)
                for (int root = 0; root < 12; root += 5)
                {
                    Setup t;
                    t.scale = (uint8_t)sc;
                    t.octave = (uint8_t)oct;
                    t.interval = (uint8_t)iv;
                    t.root = (uint8_t)root;
                    for (int c = 0; c < 12; c++)
                        for (int r = 0; r < 4; r++)
                        {
                            CHECK(padNote(t, (uint8_t)c, (uint8_t)r) <= 127);
                            if (r > 0) CHECK(padNote(t, (uint8_t)c, (uint8_t)r) <= padNote(t, (uint8_t)c, (uint8_t)(r - 1))); // up the string = up in pitch
                        }
                }
    // setup pads
    Setup u;
    CHECK(setupPad(u, 7, 0) && u.root == 7);
    CHECK(!setupPad(u, 7, 0));
    CHECK(setupPad(u, 0, 1) && u.scale == 0 && setupPad(u, 10, 1) && u.octave == 3 && setupPad(u, 11, 1) && u.octave == 4);
    CHECK(setupPad(u, 6, 2) && u.interval == 7 && setupPad(u, 0, 2) && u.interval == 1);
    CHECK(!setupPad(u, 8, 2) && !setupPad(u, 3, 3));
    // save / load keeps the four values and clamps rubbish
    uint8_t b[4];
    u.save(b);
    Setup v;
    CHECK(v.load(b, 4) && v == u && !v.load(b, 3));
    uint8_t bad[4] = { 99, 99, 0, 0 };
    CHECK(v.load(bad, 4) && v.root == 0 && v.scale == 1 && v.octave == 2 && v.interval == 3);
    CHECK(isMenuPad(47) && !isMenuPad(46) && !isMenuPad(11));
    // names
    char nb[8];
    CHECK(strcmp(noteName(60, nb), "C3") == 0 && strcmp(noteName(61, nb), "C#3") == 0 && strcmp(noteName(24, nb), "C0") == 0 &&
          strcmp(noteName(12, nb), "C-1") == 0);
    // LED levels: roots shine, held pads are full, the Setup pad shows the view
    CHECK(padLevel(s, false, 0, 3, false) == 8 && padLevel(s, false, 1, 3, false) == 2 && padLevel(s, false, 1, 3, true) == 15);
    CHECK(padLevel(s, false, 11, 3, false) == 4 && padLevel(s, true, 11, 3, false) == 15);
    CHECK(padLevel(s, true, 0, 0, false) == 15 && padLevel(s, true, 1, 0, false) == 3 && padLevel(s, true, 5, 3, false) == 0);
    // the legend shows note names in words (<= 6 chars) and a root marker, the setup view root / scale / octave / spacing
    seqplay::LegendState st;
    st.type = seqlogic::TYPE_PLINKY;
    seqplay::LegendCell lc = seqplay::legendCell(st, 0, 3);
    CHECK(lc.zone == seqplay::Z_NOTE && strcmp(lc.txt, "C1") == 0 && lc.on);
    lc = seqplay::legendCell(st, 11, 3);
    CHECK(lc.zone == seqplay::Z_MODE && strcmp(lc.txt, "Setup") == 0);
    st.plMenu = 1;
    CHECK(strcmp(seqplay::legendCell(st, 11, 3).txt, "Notes") == 0 && strcmp(seqplay::legendCell(st, 1, 0).txt, "C#") == 0);
    CHECK(seqplay::legendCell(st, 1, 1).on && strcmp(seqplay::legendCell(st, 1, 1).txt, "Minor") == 0);
    CHECK(strcmp(seqplay::legendCell(st, 3, 2).txt, "Str+4") == 0 && seqplay::legendCell(st, 2, 2).on);
    for (int sc = 0; sc < kScales; sc++) CHECK(strlen(scaleDef((uint8_t)sc).shortName) <= 6);
    char title[96];
    seqplay::legendTitle(st, title, sizeof title);
    CHECK(strstr(title, "POLYSYNTH") && strstr(title, "Minor") && strlen(seqplay::legendHelp(st)) <= 88);
    st.plMenu = 0;
    CHECK(strlen(seqplay::legendHelp(st)) <= 88);
    // strip texts and the type name
    CHECK(strcmp(seqplay::typeName(seqlogic::TYPE_PLINKY), "PolySynth") == 0 && strcmp(seqplay::typeShort(seqlogic::TYPE_PLINKY), "POLY") == 0);
    CHECK(!seqlogic::isHrType(seqlogic::TYPE_PLINKY) && seqlogic::TYPE_PLINKY == seqlogic::TYPE_HR_FIRST + seqlogic::kHrScripts &&
          seqlogic::TYPE_COUNT == seqlogic::TYPE_PLINKY + 1);
    seqplay::StripCell cell;
    cell.type = seqlogic::TYPE_PLINKY;
    char l1[16], l2[16];
    seqplay::stripLine1(l1, sizeof l1, cell);
    seqplay::stripLine2(l2, sizeof l2, cell);
    CHECK(strcmp(l1, "1 POLY") == 0 && strcmp(l2, "voice") == 0);
    cell.takenOver = true;
    seqplay::stripLine2(l2, sizeof l2, cell);
    CHECK(strcmp(l2, "live") == 0);
    CHECK(seqlogic::isNotePad(seqlogic::TYPE_PLINKY, 0) && !seqlogic::isNotePad(seqlogic::TYPE_PLINKY, 47));
    CHECK(seqlogic::wantTakeover(seqlogic::TYPE_PLINKY, 1, 0, false, true) && !seqlogic::wantTakeover(seqlogic::TYPE_PLINKY, 0, 0, false, true));
}

static void test_plinky_type_and_pads()
{
    using namespace seqlogic;
    MockHost h;
    Engine* e = mk(h);
    e->postType(2, TYPE_PLINKY);
    e->idleTick();
    CHECK(e->type(2) == TYPE_PLINKY && e->plinkyOwner() == 2);
    // a second Plinky track is refused: one voice, the track that has it keeps it
    e->postType(5, TYPE_PLINKY);
    e->idleTick();
    CHECK(e->type(5) == TYPE_OFF && e->type(2) == TYPE_PLINKY && e->plinkyOwner() == 2);
    // a held pad starts the voice: takeover, note, default velocity 100 -> volume 79
    e->postPad(2, 36, true); // row 3, col 0 = C2
    e->idleTick();
    CHECK(h.count('B', 2) == 1 && h.count('+', 2) == 1 && h.log.back().note == 36 && h.log.back().vol == velocityToVolume(100));
    CHECK(e->takenOver(2));
    // pad pressure: used when it is there
    e->postPad(2, 37, true, 127);
    e->idleTick();
    CHECK(h.log.back().kind == '+' && h.log.back().note == 41 && h.log.back().vol == 100);
    // polyphony: both notes sound until their pads are released
    e->postPad(2, 36, false);
    e->idleTick();
    CHECK(h.count('-', 2) == 1 && e->takenOver(2));
    e->postPad(2, 37, false);
    e->idleTick();
    CHECK(h.count('-', 2) == 2 && !e->takenOver(2) && h.count('E', 2) == 1);
    // two pads of the same note (neighbouring strings overlap): one note-on, the note ends with the last pad
    h.log.clear();
    {
        // col 0 row 0 and col 1 row 3 are the same note
        const uint8_t a = 0, b = (uint8_t)(3 * 12 + 1);
        CHECK(seqplinky::padNote(e->plinkySetup(), 0, 0) == seqplinky::padNote(e->plinkySetup(), 1, 3));
        e->postPad(2, a, true);
        e->postPad(2, b, true);
        e->idleTick();
        CHECK(h.count('+') == 1);
        e->postPad(2, a, false);
        e->idleTick();
        CHECK(h.count('-') == 0);
        e->postPad(2, b, false);
        e->idleTick();
        CHECK(h.count('-') == 1);
    }
    // the Setup pad switches to the setup view: held notes end, pads there play nothing and change the key
    h.log.clear();
    e->postPad(2, 36, true);
    e->idleTick();
    e->postPad(2, 47, true);
    e->idleTick();
    CHECK(e->plinkyMenu() && h.count('-') == 1);
    e->postPad(2, 47, false);
    e->postPad(2, 36, false);
    e->idleTick();
    e->postPad(2, 2, true); // setup view, row 0 col 2: root D
    e->postPad(2, 2, false);
    e->postPad(2, 12 + 0, true); // row 1 col 0: Major
    e->postPad(2, 12 + 0, false);
    e->idleTick();
    CHECK(e->plinkySetup().root == 2 && e->plinkySetup().scale == 0 && h.count('+') == 1); // only the note before the switch
    CHECK(!e->takenOver(2));
    e->postPad(2, 47, true);
    e->postPad(2, 47, false);
    e->idleTick();
    CHECK(!e->plinkyMenu());
    h.log.clear();
    e->postPad(2, 36, true);
    e->idleTick();
    CHECK(h.log.back().kind == '+' && h.log.back().note == 38); // D2 now
    // leaving the type releases the sounding note; the voice can be claimed by another track afterwards
    e->postType(2, TYPE_OFF);
    e->idleTick();
    CHECK(h.count('-', 2) >= 1 && e->plinkyOwner() == 0xFF);
    e->postType(5, TYPE_PLINKY);
    e->idleTick();
    CHECK(e->type(5) == TYPE_PLINKY && e->plinkySetup().root == 2); // the setup belongs to the voice
    delete e;
}

static void test_plinky_pending_and_stop()
{
    using namespace seqlogic;
    MockHost h;
    Engine* e = mk(h);
    e->postType(0, TYPE_CHORDARP); // a running track: type changes wait for the bar
    e->idleTick();
    int u = 1;
    playUSteps(*e, 8, u);
    e->postType(3, TYPE_PLINKY);
    playUSteps(*e, 1, u);
    CHECK(e->pendingType(3) == TYPE_PLINKY && e->plinkyOwner() == 3 && e->plinkyOwner(false) == 0xFF);
    // while it is pending a second request can be recognised by the UI (plinkyOwner with pending) and the engine refuses it
    e->postType(4, TYPE_PLINKY);
    playUSteps(*e, 48 * 24 * 2, u);
    CHECK(e->type(3) == TYPE_PLINKY && e->type(4) == TYPE_OFF);
    // a Plinky track does not take over on its own: nothing is held, nothing sounds
    CHECK(!e->takenOver(3));
    delete e;
}

static void test_plinky_record()
{
    using namespace seqlogic;
    MockHost h;
    h.instr = 7; // takeoverBegin's instrument in the mock: the glue picks the MIDI instrument of the voice channel
    Engine* e = mk(h);
    e->postType(2, TYPE_PLINKY);
    e->idleTick();
    Head hd;
    playRec(*e, h, hd, 5);
    e->postRec(2, Engine::REC_PUNCH);
    playRec(*e, h, hd, 1);
    CHECK(e->recState() == seqrec::Planner::PUNCH);
    e->postPad(2, 36, true, 90); // C, Eb, G of C minor: one chord of the same pulse
    e->postPad(2, 12, true, 90);
    e->postPad(2, 25, true, 90);
    playRec(*e, h, hd, 48 * 4);
    e->postPad(2, 36, false);
    e->postPad(2, 12, false);
    e->postPad(2, 25, false);
    playRec(*e, h, hd, 48 * 2);
    std::vector<seqrec::Op> ops = drainOps(*e);
    int notes = 0, chords = 0;
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].kind == seqrec::OP_NOTE)
        {
            notes++;
            CHECK(ops[i].instrument == 7 && ops[i].track == 2);
            if (ops[i].chord != seqrec::kNoChord) chords++;
        }
    CHECK(notes >= 1 && chords >= 1); // three pads: a MIDI chord FX on the first step (MIDI instrument), polyphony survives
    CHECK(h.count('+', 2) == 3);
    e->postRec(2, Engine::REC_STOP);
    playRec(*e, h, hd, 1);
    delete e;
}

static void test_plinky_sidecar()
{
    using namespace seqlogic;
    MockHost h;
    Engine* e = mk(h);
    e->postType(6, TYPE_PLINKY);
    e->idleTick();
    e->postPad(6, 47, true); e->postPad(6, 47, false);
    e->postPad(6, 5, true); e->postPad(6, 5, false);   // root F
    e->postPad(6, 12 + 6, true); e->postPad(6, 12 + 6, false); // Pent min
    e->postPad(6, 24 + 4, true); e->postPad(6, 24 + 4, false); // strings 5 apart
    e->postPad(6, 47, true); e->postPad(6, 47, false);
    e->idleTick();
    const seqplinky::Setup want = e->plinkySetup();
    CHECK(want.root == 5 && want.scale == 6 && want.interval == 5);
    uint16_t h0 = e->stateHash();
    static uint8_t buf[Engine::kSidecarMax];
    size_t n = e->saveState(buf, sizeof(buf));
    CHECK(n > 0);
    MockHost h2;
    Engine* f = mk(h2);
    CHECK(f->loadState(buf, n));
    CHECK(f->type(6) == TYPE_PLINKY && f->plinkySetup() == want && f->stateHash() == h0);
    // the setup is part of the change detection
    f->postPad(6, 47, true); f->postPad(6, 47, false);
    f->postPad(6, 1, true); f->postPad(6, 1, false);
    f->idleTick();
    CHECK(f->stateHash() != h0);
    // a file without any Plinky track does not carry the trailer: the byte layout of old files is unchanged
    MockHost h3;
    Engine* g = mk(h3);
    g->postType(1, TYPE_CHORDARP);
    g->idleTick();
    uint8_t b2[Engine::kSidecarMax];
    size_t n2 = g->saveState(b2, sizeof(b2));
    CHECK(n2 > 0 && b2[4] == Engine::kSidecarVersionV1); // no track needs a trailer: version 1, as before
    CHECK(g->loadState(b2, n2) && g->type(1) == TYPE_CHORDARP);
    // with a Plinky track the file is longer (the optional trailer) and is version 2; files without a trailer stay version 1
    CHECK(n > n2 && buf[4] == Engine::kSidecarVersion); // the PolySynth setup needs the trailer: version 2
    delete e; delete f; delete g;
}

static void test_plinky_overlay_logic()
{
    using namespace seqplinky;
    using namespace plinkyvoice;
    // every voice parameter except the channel is in exactly one cell
    int seen[P_COUNT] = {};
    int cells = 0;
    for (int s = 0; s < kSections; s++)
    {
        CHECK(section((uint8_t)s).count >= 1 && section((uint8_t)s).count <= kMaxCells);
        for (int i = 0; i < section((uint8_t)s).count; i++)
        {
            seen[section((uint8_t)s).param[i]]++;
            cells++;
        }
    }
    for (int p = 0; p < P_COUNT; p++) CHECK(seen[p] == (p == P_CHANNEL ? 0 : 1));
    CHECK(cells == P_COUNT - 1);
    CHECK(strcmp(section(0).name, "Oscillator") == 0 && strcmp(section(4).name, "Voice") == 0);
    // navigation: Left / Right inside the section (no wrap), Up / Down between sections (no wrap, column kept as far as it exists)
    Cursor c;
    CHECK(c.param() == P_SHAPE);
    CHECK(!c.horizontal(-1) && c.horizontal(1) && c.param() == P_DETUNE && c.horizontal(1) && c.horizontal(1) && !c.horizontal(1) && c.param() == P_NOISE);
    CHECK(c.vertical(1) && c.sec == 1 && c.idx == 2 && c.param() == P_DRIVE); // Filter has three: column 3 -> 2
    CHECK(c.vertical(1) && c.sec == 2 && c.idx == 2 && c.param() == P_SUSTAIN); // Envelope has four
    c = Cursor();
    CHECK(!c.vertical(-1));
    for (int i = 0; i < 10; i++) c.vertical(1);
    CHECK(c.sec == 4 && !c.vertical(1));
    CHECK(c.horizontal(1) && c.horizontal(1) && c.horizontal(1) && c.horizontal(1) && c.horizontal(1) && !c.horizontal(1) && c.param() == P_DELAY);
    CHECK(c.vertical(-1) && c.sec == 3 && c.idx == 2 && c.param() == P_LFO_DEST);
    // every cursor position resolves to a valid parameter
    for (int s = 0; s < kSections; s++)
        for (int i = 0; i < section((uint8_t)s).count; i++)
        {
            Cursor q;
            q.sec = (uint8_t)s;
            q.idx = (uint8_t)i;
            CHECK(q.param() >= 0 && q.param() < P_COUNT);
        }
    // jog: coarse 5 on 0..100 parameters, 1 on short ranges, Shift = fine 1, clamped to the range
    CHECK(jogValue(P_CUTOFF, 60, 1, false) == 65 && jogValue(P_CUTOFF, 60, -1, false) == 55 && jogValue(P_CUTOFF, 60, 1, true) == 61);
    CHECK(jogValue(P_CUTOFF, 98, 1, false) == 100 && jogValue(P_CUTOFF, 2, -1, false) == 0);
    CHECK(jogValue(P_INTERVAL, 12, 1, false) == 13 && jogValue(P_VOICES, 4, 1, false) == 4 && jogValue(P_VOICES, 1, -1, false) == 1);
    CHECK(jogValue(P_LFO_DEST, 0, 1, false) == 1 && jogValue(P_LFO_DEST, 2, 1, false) == 2);
    CHECK(jogValue(P_CUTOFF, 60, 5, true) == 63); // a fast spin counts at most 3 detents
    // texts
    char t[16];
    valueText(P_INTERVAL, 12, t, sizeof t);
    CHECK(strcmp(t, "+0 st") == 0);
    valueText(P_INTERVAL, 24, t, sizeof t);
    CHECK(strcmp(t, "+12 st") == 0);
    valueText(P_LFO_DEST, 2, t, sizeof t);
    CHECK(strcmp(t, "Cutoff") == 0);
    valueText(P_SHAPE, 0, t, sizeof t);
    CHECK(strcmp(t, "Saws") == 0);
    valueText(P_WIDTH, 0, t, sizeof t);
    CHECK(strcmp(t, "Mono") == 0);
    valueText(P_LEVEL, 70, t, sizeof t);
    CHECK(strcmp(t, "70") == 0);
}

// v0.3.1 minor 5: the claimed channel avoids the channels the pattern and the last used instrument use
static void test_plinky_claim_channel()
{
    using namespace seqplinky;
    CHECK(claimChannel(0) == 16);
    CHECK(claimChannel(1u << 15) == 15); // MIDI 16 used
    CHECK(claimChannel((1u << 15) | (1u << 14) | 1u) == 14);
    CHECK(claimChannel(0xFFFFu) == 16);  // everything used: MIDI 16
    CHECK(claimChannel(0x7FFFu) == 16);  // only MIDI 16 free
    CHECK(claimChannel(0xFFFEu) == 1);   // only MIDI 1 free
    CHECK(claimChannel(1u << 2) == 16);  // an external synth on MIDI 3 is not touched
}

static void test_plinky_overlay_draw_budget()
{
    using namespace seqplinky;
    using namespace plinkyvoice;
    Palette pal = { 0x111111, 0x222222, 0x555555, 0xffffff, 0x00ffff, 0xffff00, 0x000000, 0x00ffff };
    Model m;
    for (int i = 0; i < P_COUNT; i++) m.v[i] = (uint8_t)paramInfo(i).max; // worst case: longest value texts, full bars
    m.v[P_INTERVAL] = 24;
    m.v[P_LFO_DEST] = 2;
    m.v[P_SHAPE] = 100;
    m.track = 16;
    m.channel = 16;
    m.cpu = 100;
    m.peak = 100;
    int worst = 0, best = 99999;
    for (int s = 0; s < kSections; s++)
        for (int i = 0; i < section((uint8_t)s).count; i++)
        {
            m.sec = (uint8_t)s;
            m.idx = (uint8_t)i;
            PlCtx c;
            draw(c, m, pal);
            if (c.bytes() > worst) worst = c.bytes();
            if (c.bytes() < best) best = c.bytes();
            // inside the screen, below the stock title bar, above the screen button labels
            CHECK(c.minX >= 0 && c.maxX <= 800 && c.minY >= 30 && c.maxY <= 424);
            CHECK(c.phase);
        }
    m = Model();
    PlCtx z;
    draw(z, m, pal);
    printf("  PolySynth overlay display list: worst %d bytes, empty bars %d bytes, %d rects, %d texts\n", worst, z.bytes(), z.rects, z.texts);
    // the frame holds the pattern editor under the overlay as well: the overlay keeps to a share of the 8172 bytes
    CHECK(worst <= 3600);
    // cells and rows do not overlap and fit: 6 cells in the widest row
    CHECK(kCellX0 + 5 * kCellStep + kCellW <= kBoxX1);
    CHECK(kRowY0 + (kSections - 1) * kRowStep + kRowH <= 396);
    // the Voice row text widths: the longest names and values fit a 108 px cell at 9 px per character
    for (int p = 0; p < P_COUNT; p++)
    {
        char t[16];
        for (int v = paramInfo(p).min; v <= paramInfo(p).max; v++)
        {
            valueText(p, v, t, sizeof t);
            CHECK(strlen(t) * 9 <= (size_t)kCellW - 6);
        }
        CHECK(strlen(cellName(p)) * 9 <= (size_t)kCellW - 6);
    }
}
