// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 028: Camelot wheel helper (position, A / B scale types), match marks, the types in each scale-using sequencer, seqtracks.bin version 4,
// the wheel's display stage. Included from test_main.cpp (uses CHECK, MockHost, mk, PlCtx, freshWith, flatOf from test_params.inc).
#include "seqWheelLogic.h"

namespace
{
// The Model:Samples rule written out again, independently of camelot.h: CAM_TYPES as (scale, offset) per side, the scale intervals.
struct RefType { int sc[2]; int off[2]; }; // [0] = on B, [1] = on A
const RefType kRefTypes[] = { { { 1, 2 }, { 0, 9 } }, { { 3, 3 }, { 2, 2 } }, { { 4, 4 }, { 4, 4 } }, { { 5, 5 }, { 7, 7 } }, { { 6, 7 }, { 0, 9 } },
                              { { 8, 8 }, { 9, 9 } }, { { 9, 9 }, { 9, 9 } } };  // NATURAL DORIAN PHRYGIAN MIXOLYDIAN PENTA BLUES HARM
const std::vector<int> kRefScales[] = { {}, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
                                        { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
int refRootB(int c) { return (11 + 7 * (c - 1)) % 12; }
// the app's A / B type index -> the reference type index (A: Natural Dorian Phrygian Penta Blues Harm; B: Natural Lydian Mixolyd Penta)
const int kA2Ref[6] = { 0, 1, 2, 4, 5, 6 };
const int kB2Ref[4] = { 0, -1 /* Lydian: new, +5 on the major scale */, 3, 4 };
} // namespace

static void test_camelot_rule()
{
    using namespace seqgen::cam;
    // every Camelot number c = 1..12, both sides, every type of the side: tonic, notes and position match the reference rule
    for (int c = 1; c <= 12; c++)
        for (int side = 0; side < 2; side++)
        {
            const int cb = refRootB(c);
            const int pc = side ? (cb + 9) % 12 : cb; // the pad key: the major's root / the relative minor's root
            for (int t = 0; t < (side ? (int)kTypesA : (int)kTypesB); t++)
            {
                Scale s;
                resolve(s, pc, side != 0, side ? (uint8_t)t : 0, side ? 0 : (uint8_t)t);
                CHECK(s.num == c && s.minor == side);
                int refSc, refOff;
                std::vector<int> iv;
                if (side == 0 && t == 1) { refSc = 1; refOff = 5; iv = { 0, 2, 4, 6, 7, 9, 11 }; } // Lydian
                else
                {
                    const RefType& r = kRefTypes[side ? kA2Ref[t] : kB2Ref[t]];
                    refSc = r.sc[side]; refOff = r.off[side];
                    iv = kRefScales[refSc];
                }
                CHECK(s.tonic == (cb + refOff) % 12);
                CHECK(s.n == iv.size());
                for (int i = 0; i < s.n && i < (int)iv.size(); i++) CHECK(s.iv[i] == iv[i]);
            }
        }
    // Natural is today's behaviour bit for bit: the tonic is the pad root, major / natural minor
    for (int pc = 0; pc < 12; pc++)
    {
        Scale a, b;
        resolve(a, pc, false, 0, 0);
        resolve(b, pc, true, 0, 0);
        const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mn[7] = { 0, 2, 3, 5, 7, 8, 10 };
        CHECK(a.tonic == pc && b.tonic == pc && a.n == 7 && b.n == 7);
        for (int i = 0; i < 7; i++) CHECK(a.iv[i] == maj[i] && b.iv[i] == mn[i]);
    }
    Scale s;
    char b[16];
    resolve(s, 0, false, 0, 0);
    CHECK(s.num == 8 && !s.minor);
    label(b, sizeof b, s);
    CHECK(strcmp(b, "8B C") == 0);
    resolve(s, 9, true, 0, 0);
    label(b, sizeof b, s);
    CHECK(strcmp(b, "8A Am") == 0);
    resolve(s, 9, true, 1, 0); // 8A + Dorian
    label(b, sizeof b, s);
    CHECK(strcmp(b, "8A D DOR") == 0);
    resolve(s, 0, true, 0, 0);
    label(b, sizeof b, s);
    CHECK(strcmp(b, "5A Cm") == 0);
    resolve(s, 3, false, 0, 0);
    label(b, sizeof b, s);
    CHECK(strcmp(b, "5B Eb") == 0);
    resolve(s, 0, false, 0, 2); // 8B + Mixolydian = G mixolydian
    label(b, sizeof b, s);
    CHECK(strcmp(b, "8B G MIX") == 0);
    // Strings: tonic + scale list index -> the position
    build(s, 2, SC_DOR);
    CHECK(s.num == 8 && s.minor);
    build(s, 5, SC_LYD);
    CHECK(s.num == 8 && !s.minor);
}

static void test_camelot_marks()
{
    using namespace seqgen::cam;
    Scale a, b;
    resolve(a, 0, false, 0, 0); // 8B
    struct K { int pc; bool mn; Mark m; };
    const K k[] = { { 0, false, M_MATCH }, { 9, true, M_GOOD } /*8A*/, { 7, false, M_GOOD } /*9B*/, { 5, false, M_GOOD } /*7B*/,
                    { 4, true, M_CLASH } /*9A: diagonal*/, { 2, false, M_CLASH } /*10B*/, { 6, false, M_CLASH } };
    for (const K& e : k)
    {
        resolve(b, e.pc, e.mn, 0, 0);
        CHECK(markOf(a, b) == e.m && markOf(b, a) == e.m);
    }
    // 12 and 1 are neighbours; the type does not change the position
    resolve(a, 4, false, 0, 0);  // 12B
    resolve(b, 11, false, 0, 0); // 1B
    CHECK(a.num == 12 && b.num == 1 && markOf(a, b) == M_GOOD);
    resolve(a, 9, true, 0, 0);
    resolve(b, 9, true, 3, 0); // 8A with Penta
    CHECK(markOf(a, b) == M_MATCH);
    // per track: compared with the others that sound; a lead (when set) is the only reference; nothing to compare with = no mark
    Snap t[4];
    memset(t, 0, sizeof t);
    resolve(t[0].s, 0, false, 0, 0); resolve(t[1].s, 9, true, 0, 0); resolve(t[2].s, 2, false, 0, 0); resolve(t[3].s, 0, false, 0, 0);
    for (int i = 0; i < 4; i++) t[i].valid = 1;
    t[0].sounding = t[1].sounding = t[2].sounding = 1;
    uint8_t m[4];
    marks(t, 4, -1, m);
    CHECK(m[0] == M_CLASH && m[1] == M_CLASH && m[2] == M_CLASH && m[3] == M_NONE); // the worst relation counts; track 3 does not sound
    marks(t, 4, 1, m);
    CHECK(m[0] == M_GOOD && m[2] == M_CLASH && m[1] == M_NONE); // the lead is track 1 (8A): 8B is a neighbour, 10B a clash, the lead has no reference
    t[2].sounding = 0;
    marks(t, 4, -1, m);
    CHECK(m[0] == M_GOOD && m[1] == M_GOOD && m[2] == M_NONE);
    t[1].sounding = 0;
    marks(t, 4, -1, m);
    CHECK(m[0] == M_NONE); // alone
}

static void test_camelot_generators()
{
    using namespace seqgen;
    // Chord/Arp: the Triad on the 8A pad (A minor) with Dorian is D F A, Natural is A C E; 8B Mixolydian is G B D
    {
        ChordArp ca;
        int16_t out[8];
        int n = ca.pool(69, 3, true, out); // pad root A, minor row, chord type 3 = Triad
        CHECK(n == 3 && out[0] == 69 && out[1] == 72 && out[2] == 76);
        ca.setCam(1, 0);
        n = ca.pool(69, 3, true, out);
        CHECK(n == 3 && out[0] % 12 == 2 && out[1] % 12 == 5 && out[2] % 12 == 9); // D F A
        ca.setCam(0, 2);
        n = ca.pool(60, 3, false, out);
        CHECK(n == 3 && out[0] % 12 == 7 && out[1] % 12 == 11 && out[2] % 12 == 2); // G B D
        ca.setCam(3, 0); // A minor Penta: degrees 0 2 4 of the five notes A C D E G = A D G
        n = ca.pool(69, 3, true, out);
        CHECK(n == 3 && out[0] == 69 && out[1] % 12 == 2 && out[2] % 12 == 7);
        cam::Scale s;
        CHECK(!ca.camKey(s)); // no root played yet
    }
    // Mono: its own types over the Chord/Arp's key; Natural inherits the lead's type
    {
        SeqContext ctx;
        ChordArp ca;
        ca.setContext(&ctx);
        ca.setParam(ChordArp::P_PMODE, 2);       // locked key
        ca.setParam(ChordArp::P_LOCK_ROOT, 69);  // A
        ca.setParam(ChordArp::P_LOCK_MINOR, 1);
        CHECK(ctx.scaleDeg(2) % 12 == 0);         // third degree of A minor = C
        CHECK(ctx.scaleDeg(2, 1, 0) % 12 == 5);   // own Dorian (8A = D dorian): third degree F
        ca.setCam(1, 0);
        CHECK(ctx.scaleDeg(2) % 12 == 5 && ctx.scaleDeg(2, 0, 0) % 12 == 5); // the lead's flavour is inherited
        CHECK(ctx.scaleDeg(2, 2, 0) % 12 == 7);   // own Phrygian (E phrygian): third degree G
        cam::Scale s;
        CHECK(ca.camKey(s, 2, 0) && s.num == 8 && s.minor && s.sc == cam::SC_PHR);
        Mono mo;  // the Mono itself takes its types from setCam
        mo.setContext(&ctx);
        mo.setCam(2, 0);
        CHECK(mo.camA() == 2);
    }
    // HR Chord Stabs publishes the key with its type; Bass takes it, an own type replaces it
    {
        HrBus bus;
        HrStab st(HrStab::KIND_STABS);
        st.setBus(&bus);
        HrBass bass;
        bass.setBus(&bus);
        st.setParam(HrStab::P_QUALITY, 2);
        st.pad(0, 0, true); st.pad(0, 0, false); // the first root pad
        cam::Scale s;
        CHECK(st.camKey(s) && s.minor);
        const int pc = bus.keyPc;
        CHECK(pc >= 0 && bus.keyMinor == 1 && bus.nScale == 7 && bus.scale[0] == pc);
        st.setCam(1, 0);
        st.camChanged();
        CHECK(bus.nScale == 7 && bus.scale[0] == (pc + 3 + 2) % 12); // Dorian: 2 above the major's root
        CHECK(bass.camKey(s) && s.sc == cam::SC_DOR);                 // the Bass follows the leader's flavour
        bass.setCam(3, 0);                                            // ... its own Penta replaces it
        CHECK(bass.camKey(s) && s.sc == cam::SC_PMIN && s.n == 5);
        st.setCam(0, 0);
        st.camChanged();
        bass.setCam(0, 0);
        CHECK(bass.camKey(s) && s.sc == cam::SC_MIN && bus.scale[0] == pc);
    }
    // HR Chord/Arp: the locked key is published with the type
    {
        HrBus bus;
        HrChordArp hc;
        hc.setBus(&bus);
        hc.setParam(HrChordArp::P_KEYLOCK, 2);
        hc.setParam(HrChordArp::P_KEY, 0);
        CHECK(bus.nScale == 7 && bus.scale[0] == 0);
        hc.setCam(0, 2); // Mixolydian
        hc.camChanged();
        cam::Scale s;
        CHECK(hc.camKey(s) && s.sc == cam::SC_MIX && bus.scale[0] == 7 && bus.keyPc == 0);
    }
}

static void test_camelot_engine()
{
    using namespace seqparams;
    // the settings are table rows of every scale-using type, absent elsewhere; Strings keeps its own Scale list
    for (uint8_t type = 1; type < TYPE_COUNT; type++)
    {
        const bool has = flatOf(type, ID_CAM_A) != 0xFF;
        CHECK(has == seqlogic::hasCam(type));
        CHECK(has == (flatOf(type, ID_CAM_B) != 0xFF));
    }
    const uint8_t kStab = TYPE_HR_FIRST + seqgen::HR_CHORD_STABS;
    MockHost h;
    Engine* e = freshWith(h, 0, TYPE_CHORDARP);
    e->postType(1, TYPE_MONO);
    e->postType(2, kStab);
    e->idleTick();
    e->postParam(0, flatOf(TYPE_CHORDARP, ID_CAM_A), 3);
    e->postParam(0, flatOf(TYPE_CHORDARP, ID_CAM_B), 2);
    e->postParam(1, flatOf(TYPE_MONO, ID_CAM_A), 5);
    e->postParam(2, flatOf(kStab, ID_CAM_B), 1);
    e->idleTick();
    int8_t v[kMaxParams];
    e->paramValues(0, v);
    CHECK(v[flatOf(TYPE_CHORDARP, ID_CAM_A)] == 3 && v[flatOf(TYPE_CHORDARP, ID_CAM_B)] == 2);
    CHECK(e->chordArp(0).camA() == 3 && e->chordArp(0).camB() == 2 && e->mono(1).camA() == 5);
    CHECK(e->hr(2) && e->hr(2)->camB() == 1);
    // sidecar: the default projects keep their version; a type needs version 4; the older versions load
    MockHost h0;
    Engine* e0 = freshWith(h0, 0, TYPE_CHORDARP);
    static uint8_t b0[Engine::kSidecarMax], b1[Engine::kSidecarMax], b3[Engine::kSidecarMax];
    const size_t n0 = e0->saveState(b0, sizeof b0);
    CHECK(b0[4] == Engine::kSidecarVersionV1);
    const size_t n1 = e->saveState(b1, sizeof b1);
    CHECK(b1[4] == Engine::kSidecarVersionV4 && n1 > n0);
    MockHost h2;
    Engine* e2 = mk(h2);
    CHECK(e2->loadState(b1, n1));
    CHECK(e2->type(0) == TYPE_CHORDARP && e2->camA(0) == 3 && e2->camB(0) == 2 && e2->camA(1) == 5 && e2->camB(2) == 1);
    CHECK(e2->chordArp(0).camA() == 3 && e2->mono(1).camA() == 5 && e2->hr(2) && e2->hr(2)->camB() == 1);
    CHECK(e2->saveState(b3, sizeof b3) == n1 && memcmp(b1, b3, n1) == 0);
    CHECK(e2->loadState(b0, n0) && e2->camA(0) == 0 && e2->camB(0) == 0); // version 1 loads, Natural
    // Natural again -> the oldest version that fits
    e->postParam(0, flatOf(TYPE_CHORDARP, ID_CAM_A), 0);
    e->postParam(0, flatOf(TYPE_CHORDARP, ID_CAM_B), 0);
    e->postParam(1, flatOf(TYPE_MONO, ID_CAM_A), 0);
    e->postParam(2, flatOf(kStab, ID_CAM_B), 0);
    e->idleTick();
    CHECK(e->saveState(b3, sizeof b3) > 0 && b3[4] == Engine::kSidecarVersion); // an HR track is there: version 2
    // a cut version 4 file / a wrong CRC is refused
    CHECK(!e2->loadState(b1, n1 - 3));
    b1[n1 - 5] ^= 0x20;
    CHECK(!e2->loadState(b1, n1));
    delete e;
    delete e0;
    delete e2;
}

static void test_camelot_snapshot()
{
    MockHost h;
    Engine* e = freshWith(h, 0, TYPE_CHORDARP);
    seqgen::cam::Snap snap[seqlogic::kTracks];
    uint8_t mk_[seqlogic::kTracks];
    e->camSnapshot(snap, mk_);
    CHECK(!snap[0].valid && mk_[0] == 0);
    e->postPad(0, 4, true); // Chord/Arp root pad (row 0, col 4): 60 = C = 8B
    e->idleTick();
    e->camSnapshot(snap, mk_);
    CHECK(snap[0].valid && snap[0].s.num == 8 && !snap[0].s.minor);
    CHECK(mk_[0] == 0); // nothing else sounds
    delete e;
}

static void test_wheel_draw()
{
    seqwheel::Palette pal = { 0x050505, 0x1C1C1C, 0xFFFFFF, 0x000000, 0x00FFFF, 0xFF8000, 0xFFFFFF, 0x00FF00, 0xFFFF00, 0xFF0000 };
    int worst = 0;
    for (int cur = 0; cur < 24; cur++)
        for (int mark = 0; mark < 4; mark++)
        {
            seqwheel::Model m;
            m.on = 1;
            m.cur = (uint8_t)cur;
            m.mark = (uint8_t)mark;
            m.trail[0] = (uint8_t)((cur + 2) % 24); m.trail[1] = (uint8_t)((cur + 5) % 24); m.trail[2] = (uint8_t)((cur + 9) % 24);
            m.others = 0xFFFFFFu & ~(1u << cur);
            snprintf(m.label, sizeof m.label, "11A F#m");
            PlCtx c;
            seqwheel::draw(c, m, pal);
            const int f = c.fifo() + tracestage::kOwnBytes;
            if (f > worst) worst = f;
            CHECK(f <= tracestage::kStageBytes);
            CHECK(c.minX >= 0 && c.maxX <= 800 && c.minY >= 0 && c.maxY <= 424);
        }
    printf("  Camelot wheel worst case: %d FIFO bytes (limit %d)\n", worst, (int)tracestage::kStageBytes);
    seqwheel::Model off;
    PlCtx c0;
    seqwheel::draw(c0, off, pal);
    CHECK(c0.words == 0);
    // neighbours of 8B (position 14): 7B, 9B and 8A, nobody else
    int nb = 0;
    for (uint8_t p = 0; p < 24; p++) nb += seqwheel::neighbour(14, p) ? 1 : 0;
    CHECK(nb == 3 && seqwheel::neighbour(14, 12) && seqwheel::neighbour(14, 16) && seqwheel::neighbour(14, 15));
}

static void test_camelot_all()
{
    test_camelot_rule();
    test_camelot_marks();
    test_camelot_generators();
    test_camelot_engine();
    test_camelot_snapshot();
    test_wheel_draw();
}
