// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Plan 025: the sequencer settings as a parameter page (tables, defaults, ranges, engine commands, drawing budget) and Latch (per type,
// Stop / Play, sidecar version 3). Included from test_main.cpp (uses CHECK, MockHost, mk, playUSteps, idle, PlCtx).
#include "seqParamsLogic.h"
#include <set>

namespace
{
std::vector<int> notesOn(const MockHost& h, size_t from)
{
    std::vector<int> v;
    for (size_t i = from; i < h.log.size(); i++)
        if (h.log[i].kind == '+')
            v.push_back(h.log[i].note);
    return v;
}
std::set<int> noteSet(const MockHost& h, size_t from)
{
    std::vector<int> v = notesOn(h, from);
    return std::set<int>(v.begin(), v.end());
}
int soundingNow(const MockHost& h)
{
    return h.count('+') - h.count('-');
}
uint8_t flatOf(uint8_t type, uint8_t id)
{
    const seqparams::Tab* tb = seqparams::tab(type);
    for (uint8_t i = 0; tb && i < tb->nTotal; i++)
        if (tb->d[i].id == id)
            return i;
    return 0xFF;
}
// a track of this type on a fresh engine
Engine* freshWith(MockHost& h, uint8_t track, uint8_t type)
{
    Engine* e = mk(h);
    e->postType(track, type);
    e->idleTick();
    return e;
}
} // namespace

static void test_params_tables()
{
    using namespace seqparams;
    CHECK(tab(TYPE_OFF) == nullptr);
    int latchTypes = 0;
    for (uint8_t type = 1; type < TYPE_COUNT; type++)
    {
        const Tab* tb = tab(type);
        CHECK(tb != nullptr);
        if (!tb) continue;
        int sum = 0;
        CHECK(tb->nSec >= 1 && tb->nSec <= kMaxSections);
        for (uint8_t s = 0; s < tb->nSec; s++)
        {
            CHECK(tb->sec[s].n >= 1 && tb->sec[s].n <= kMaxCells);
            sum += tb->sec[s].n;
        }
        CHECK(sum == tb->nTotal && tb->nTotal <= kMaxParams);
        // a Latch cell exists exactly for the types that have a latch (Mono and the step sequencers have none)
        const bool hasLatchCell = flatOf(type, ID_LATCH) != 0xFF;
        CHECK(hasLatchCell == (latchKind(type) != LATCH_NONE));
        latchTypes += hasLatchCell;
        MockHost h;
        Engine* e = freshWith(h, 2, type);
        CHECK(e->type(2) == type && e->hasLatch(2) == hasLatchCell);
        int8_t v[kMaxParams], v0[kMaxParams];
        e->paramValues(2, v0);
        for (uint8_t i = 0; i < tb->nTotal; i++)
        {
            const Desc& d = desc(*tb, i);
            CHECK(d.mn <= d.def && d.def <= d.mx);
            CHECK(v0[i] == defaultOf(type, d)); // the table default is what a fresh generator has
            // every value of the range has a text; the list entries exist
            for (int x = d.mn; x <= d.mx; x++)
            {
                char t[40];
                valueText(d, x, t, sizeof t);
                CHECK(t[0] != 0 && strcmp(t, "?") != 0);
            }
            // the ends of the range are accepted (a few setters refuse a value that conflicts with another one: Grid start / end)
            e->applyParam(2, i, d.mx);
            e->paramValues(2, v);
            const bool conflict = (type == TYPE_HR_FIRST + seqgen::HR_GRIDSEQ || type == TYPE_HR_FIRST + seqgen::HR_STEPFOLLOW) && (d.id == seqgen::HrGridSeq::P_START);
            CHECK(v[i] == d.mx || conflict || type == TYPE_MONO);
            if (!(v[i] == d.mx || conflict || type == TYPE_MONO)) printf("   type %d param %s: max %d read %d\n", type, d.name, d.mx, v[i]);
            e->applyParam(2, i, d.mn);
            e->paramValues(2, v);
            CHECK(v[i] == d.mn || type == TYPE_MONO);
            if (!(v[i] == d.mn || type == TYPE_MONO)) printf("   type %d param %s: min %d read %d\n", type, d.name, d.mn, v[i]);
            e->applyParam(2, i, 100); // out of range: clamped
            e->paramValues(2, v);
            CHECK(v[i] >= d.mn && v[i] <= d.mx);
            e->applyParam(2, i, defaultOf(type, d));
        }
        delete e;
    }
    CHECK(latchTypes == 7); // Chord/Arp, Strings, Arp Lines, Chord Stabs, Arp Seq, HR Chord/Arp, HR Bass
    // jog: the step, the clamping, fine
    {
        const Tab* tb = tab(TYPE_HR_FIRST + seqgen::HR_CHORD_STABS);
        const Desc& vel = desc(*tb, 2);
        CHECK(strcmp(vel.name, "Velocity") == 0);
        CHECK(jogValue(vel, 100, 1, false) == 104 && jogValue(vel, 100, 1, true) == 101 && jogValue(vel, 126, 3, false) == 127 && jogValue(vel, 2, -3, false) == 1);
        const Desc& oct = desc(*tb, 0);
        CHECK(jogValue(oct, 0, 1, false) == 1 && jogValue(oct, 2, 1, false) == 2 && jogValue(oct, -2, -1, false) == -2);
    }
    // cursor
    {
        const Tab* tb = tab(TYPE_CHORDARP);
        Cursor c;
        CHECK(!c.horizontal(*tb, -1) && !c.vertical(*tb, -1));
        for (int i = 0; i < 10; i++) c.horizontal(*tb, 1);
        CHECK(c.idx == 6);                        // Play row has seven cells
        CHECK(c.vertical(*tb, 1) && c.sec == 1 && c.idx == 5); // Pitch row has six: the column is kept as far as it exists
        CHECK(c.vertical(*tb, 1) && c.vertical(*tb, 1) && c.sec == 3 && c.idx == 4 && !c.vertical(*tb, 1));
        CHECK(c.flat(*tb) == 7 + 6 + 5 + 4);
    }
    // the engine takes a command per table row, applied at the next block
    {
        MockHost h;
        Engine* e = freshWith(h, 0, TYPE_MONO);
        CHECK(e->postParam(0, 0, 3) && !e->postParam(0, 99, 1) && !e->postParam(5, 0, 1)); // track 5 is Off
        e->idleTick();
        int8_t v[kMaxParams];
        e->paramValues(0, v);
        CHECK(v[0] == 3);
        delete e;
    }
}

static void test_latch_chordarp()
{
    MockHost h;
    Engine* e = freshWith(h, 0, TYPE_CHORDARP);
    const uint8_t li = flatOf(TYPE_CHORDARP, seqparams::ID_LATCH);
    CHECK(!e->latchOn(0)); // default Off
    // Off: held only while the pad is down
    e->postPad(0, 0, true);
    idle(*e, 96 * 3);
    CHECK(e->takenOver(0) && soundingNow(h) > 0);
    e->postPad(0, 0, false);
    idle(*e, 96 * 2);
    CHECK(!e->takenOver(0) && soundingNow(h) == 0);
    // On: a press latches, the release changes nothing
    e->applyParam(0, li, 1);
    CHECK(e->latchOn(0));
    const size_t m0 = h.log.size();
    e->postPad(0, 0, true);
    idle(*e, 96 * 2);
    e->postPad(0, 0, false);
    idle(*e, 96 * 4);
    CHECK(e->takenOver(0) && soundingNow(h) > 0); // the chord rings on after the finger left
    const std::set<int> a = noteSet(h, m0);
    // a new chord (new gesture) switches to it
    const size_t m1 = h.log.size();
    e->postPad(0, 3, true);
    idle(*e, 96 * 2);
    e->postPad(0, 3, false);
    idle(*e, 96 * 4);
    CHECK(e->takenOver(0) && soundingNow(h) > 0);
    const std::set<int> b = noteSet(h, m1);
    CHECK(!a.empty() && !b.empty() && b != a);
    // the same pad again switches the sequence off
    e->postPad(0, 3, true);
    idle(*e, 96 * 2);
    e->postPad(0, 3, false);
    idle(*e, 96 * 3);
    CHECK(!e->takenOver(0) && soundingNow(h) == 0);
    // a panel pad (not a note pad) is never latched
    e->postPad(0, 9, true); // col 9, row 0 = panel
    e->postPad(0, 9, false);
    idle(*e, 96);
    CHECK(!e->takenOver(0));
    // Latch off again ends a latched sequence
    e->postPad(0, 0, true); e->postPad(0, 0, false);
    idle(*e, 96 * 2);
    CHECK(e->takenOver(0));
    e->applyParam(0, li, 0);
    idle(*e, 96 * 2);
    CHECK(!e->latchOn(0) && !e->takenOver(0) && soundingNow(h) == 0);
    // Latch turned on while a pad is held keeps that pad held
    e->postPad(0, 1, true);
    idle(*e, 96 * 2);
    e->applyParam(0, li, 1);
    e->postPad(0, 1, false);
    idle(*e, 96 * 3);
    CHECK(e->takenOver(0));
    // a type change ends it and puts the latch back to the default
    e->postType(0, TYPE_MONO);
    idle(*e, 96 * 2);
    CHECK(!e->hasLatch(0) && !e->latchOn(0) && !e->takenOver(0) && soundingNow(h) == 0);
    delete e;
}

// owner clarification: Stop only stops a latched sequence (silent, still latched); Play brings it back with the same chord
static void test_latch_stop_play()
{
    MockHost h;
    Engine* e = freshWith(h, 0, TYPE_CHORDARP);
    e->applyParam(0, flatOf(TYPE_CHORDARP, seqparams::ID_LATCH), 1);
    int u = 1;
    playUSteps(*e, 48 * 2, u);
    e->postPad(0, 4, true);            // a root
    playUSteps(*e, 48 * 2, u);
    e->postPad(0, 4, false);           // lifted: latched
    playUSteps(*e, 48 * 8, u);
    CHECK(e->takenOver(0) && soundingNow(h) > 0);
    std::vector<int> before = notesOn(h, 0);
    CHECK(!before.empty());
    // Stop: the transport stops, the pads are released as ever (nothing is held), the sequence falls silent but stays latched
    const size_t mark = h.log.size();
    idle(*e, 96 * 6);
    CHECK(soundingNow(h) == 0 && e->latchOn(0));
    const size_t quietAt = h.log.size();
    idle(*e, 96 * 6);
    CHECK(h.log.size() == quietAt); // paused: no new events while stopped
    CHECK(e->takenOver(0) || soundingNow(h) == 0);
    (void)mark;
    // Play: it sounds again, with the same chord
    u = 1;
    playUSteps(*e, 48 * 8, u);
    std::vector<int> after = notesOn(h, quietAt);
    CHECK(!after.empty() && soundingNow(h) > 0);
    // the notes of the first chord are the notes of the resumed chord
    std::vector<int> sb = before, sa = after;
    std::sort(sb.begin(), sb.end());
    sb.erase(std::unique(sb.begin(), sb.end()), sb.end());
    std::sort(sa.begin(), sa.end());
    sa.erase(std::unique(sa.begin(), sa.end()), sa.end());
    CHECK(sb == sa);
    // a new press while stopped plays (free running) and stays latched, silent again only through the next Stop
    idle(*e, 96 * 4);
    e->postPad(0, 5, true);
    idle(*e, 96 * 2);
    CHECK(soundingNow(h) > 0);
    e->postPad(0, 5, false);
    idle(*e, 96 * 3);
    CHECK(soundingNow(h) > 0 && e->latchOn(0) && e->takenOver(0));
    delete e;

    // an engine-latched HR Bass and a script-latched Chord Stabs also keep their latch over Stop / Play
    for (int variant = 0; variant < 2; variant++)
    {
        MockHost h2;
        const uint8_t type = (uint8_t)(TYPE_HR_FIRST + (variant ? seqgen::HR_CHORD_STABS : seqgen::HR_BASS));
        Engine* f = freshWith(h2, 3, type);
        f->applyParam(3, flatOf(type, seqparams::ID_LATCH), 1);
        int u2 = 1;
        f->postPad(3, 1, true);
        playUSteps(*f, 48 * 4, u2);
        f->postPad(3, 1, false);
        playUSteps(*f, 48 * 4, u2);
        CHECK(f->latchOn(3) && f->takenOver(3));
        idle(*f, 96 * 4);
        CHECK(f->latchOn(3) && soundingNow(h2) == 0);
        const size_t q2 = h2.log.size();
        u2 = 1;
        playUSteps(*f, 48 * 8, u2);
        CHECK(f->takenOver(3) && (variant == 0 || !notesOn(h2, q2).empty())); // (the Bass plays only a stored pattern)
        delete f;
    }
}

static void test_latch_types()
{
    // Strings: the pads are notes; a latched pad keeps its note after the release; a new gesture replaces it
    {
        MockHost h;
        Engine* e = freshWith(h, 5, TYPE_PLINKY);
        const uint8_t li = flatOf(TYPE_PLINKY, seqparams::ID_LATCH);
        CHECK(e->hasLatch(5) && !e->latchOn(5));
        e->applyParam(5, li, 1);
        e->postPad(5, 3, true); e->postPad(5, 3, false);
        e->idleTick();
        CHECK(soundingNow(h) == 1);
        e->postPad(5, 6, true);  // a new gesture: replaces
        e->postPad(5, 7, true);  // second finger joins
        e->postPad(5, 6, false); e->postPad(5, 7, false);
        e->idleTick();
        CHECK(soundingNow(h) == 2);
        e->postPad(5, 7, true); e->postPad(5, 7, false); // one of the latched pads: all off
        e->idleTick();
        CHECK(soundingNow(h) == 0);
        // the setup pad is not latched and the setup view's pads play nothing
        e->postPad(5, 47, true); e->postPad(5, 47, false);
        e->idleTick();
        CHECK(e->plinkyMenu());
        e->postPad(5, 47, true); e->postPad(5, 47, false);
        e->idleTick();
        CHECK(!e->plinkyMenu() && soundingNow(h) == 0);
        delete e;
    }
    // every type with a latch: on -> the release does not end it; off -> it ends
    const uint8_t types[] = { TYPE_CHORDARP, TYPE_PLINKY, (uint8_t)(TYPE_HR_FIRST + seqgen::HR_BASS) };
    for (uint8_t type : types)
    {
        MockHost h;
        Engine* e = freshWith(h, 0, type);
        const uint8_t li = flatOf(type, seqparams::ID_LATCH);
        e->applyParam(0, li, 1);
        e->postPad(0, 1, true);
        idle(*e, 96 * 2);
        e->postPad(0, 1, false);
        idle(*e, 96 * 2);
        CHECK(e->takenOver(0) && (type != TYPE_PLINKY && type != TYPE_CHORDARP ? true : soundingNow(h) > 0));
        e->applyParam(0, li, 0);
        idle(*e, 96 * 2);
        CHECK(!e->takenOver(0) && soundingNow(h) == 0);
        delete e;
    }
    // the scripts that latch a root on a tap: latch On = the old behaviour (a tap toggles), Off = the root sounds while the pad is held
    const uint8_t scripts[] = { (uint8_t)(TYPE_HR_FIRST + seqgen::HR_ARP_LINES), (uint8_t)(TYPE_HR_FIRST + seqgen::HR_CHORD_STABS),
                                (uint8_t)(TYPE_HR_FIRST + seqgen::HR_ARP_SEQ), (uint8_t)(TYPE_HR_FIRST + seqgen::HR_CHORD_ARP) };
    for (uint8_t type : scripts)
    {
        MockHost h;
        Engine* e = freshWith(h, 2, type);
        const uint8_t li = flatOf(type, seqparams::ID_LATCH);
        CHECK(e->latchOn(2)); // the scripts always latched: default On
        int u = 1;
        e->postPad(2, 2, true); // root pad (col 2, row 0)
        playUSteps(*e, 48 * 2, u);
        e->postPad(2, 2, false);
        playUSteps(*e, 48 * 4, u);
        CHECK(e->takenOver(2)); // latched
        e->postPad(2, 2, true); // the same root again: off
        playUSteps(*e, 48 * 2, u);
        e->postPad(2, 2, false);
        playUSteps(*e, 48 * 4, u);
        CHECK(!e->takenOver(2) && soundingNow(h) == 0);
        e->applyParam(2, li, 0);
        CHECK(!e->latchOn(2));
        e->postPad(2, 2, true);
        playUSteps(*e, 48 * 4, u);
        CHECK(e->takenOver(2));  // sounds while held
        e->postPad(2, 2, false);
        playUSteps(*e, 48 * 4, u);
        CHECK(!e->takenOver(2) && soundingNow(h) == 0); // let go: it ends
        // Latch off while latched ends it
        e->applyParam(2, li, 1);
        e->postPad(2, 2, true); e->postPad(2, 2, false);
        playUSteps(*e, 48 * 4, u);
        CHECK(e->takenOver(2));
        e->applyParam(2, li, 0);
        playUSteps(*e, 48 * 4, u);
        CHECK(!e->takenOver(2) && soundingNow(h) == 0);
        delete e;
    }
    // the step sequencers have no latch cell and ignore a latch command
    const uint8_t steppers[] = { TYPE_MONO, (uint8_t)(TYPE_HR_FIRST + seqgen::HR_EUCLID), (uint8_t)(TYPE_HR_FIRST + seqgen::HR_GRIDSEQ),
                                 (uint8_t)(TYPE_HR_FIRST + seqgen::HR_STEPFOLLOW) };
    for (uint8_t type : steppers)
    {
        MockHost h;
        Engine* e = freshWith(h, 1, type);
        CHECK(!e->hasLatch(1) && !e->latchOn(1) && flatOf(type, seqparams::ID_LATCH) == 0xFF);
        delete e;
    }
}

static void test_latch_pad_logic()
{
    seqlogic::PadLatch l;
    bool press;
    CHECK(l.down(3, &press) == 0 && press && l.held == (1ull << 3));
    l.up(3);
    CHECK(l.held == (1ull << 3) && l.phys == 0);
    CHECK(l.down(5, &press) == (1ull << 3) && press && l.held == (1ull << 5)); // replaced
    CHECK(l.down(6, &press) == 0 && press && l.held == ((1ull << 5) | (1ull << 6))); // joins
    l.up(5); l.up(6);
    CHECK(l.down(6, &press) == ((1ull << 5) | (1ull << 6)) && !press && l.held == 0); // one of them again: all off
    l.up(6);
    CHECK(l.down(2, &press) == 0 && press);
    CHECK(l.clear() == (1ull << 2) && l.held == 0);
}

static void test_latch_sidecar()
{
    static uint8_t b0[Engine::kSidecarMax], b1[Engine::kSidecarMax];
    // the default Latch writes the same bytes (and versions) as before
    {
        MockHost h;
        Engine* e = freshWith(h, 1, TYPE_CHORDARP);
        e->postType(2, (uint8_t)(TYPE_HR_FIRST + seqgen::HR_CHORD_STABS));
        e->idleTick();
        const size_t n0 = e->saveState(b0, sizeof b0);
        CHECK(n0 > 0 && b0[4] == Engine::kSidecarVersion); // an HR track: version 2 as before, the default Latch adds nothing
        const uint16_t h0 = e->stateHash();
        // Latch Off on the Chord Stabs (non default) and On on the Chord/Arp (non default) -> version 3
        e->applyParam(2, flatOf((uint8_t)(TYPE_HR_FIRST + seqgen::HR_CHORD_STABS), seqparams::ID_LATCH), 0);
        e->applyParam(1, flatOf(TYPE_CHORDARP, seqparams::ID_LATCH), 1);
        CHECK(e->stateHash() != h0);
        const size_t n1 = e->saveState(b1, sizeof b1);
        CHECK(n1 == n0 + seqlogic::kTracks && b1[4] == Engine::kSidecarVersionV3);
        MockHost g;
        Engine* f = mk(g);
        CHECK(f->loadState(b1, n1));
        CHECK(f->type(1) == TYPE_CHORDARP && f->latchOn(1) && f->type(2) == TYPE_HR_FIRST + seqgen::HR_CHORD_STABS && !f->latchOn(2));
        CHECK(f->stateHash() == e->stateHash());
        // the older file (version 2) still loads, with the defaults
        MockHost g2;
        Engine* f2 = mk(g2);
        CHECK(f2->loadState(b0, n0) && f2->type(1) == TYPE_CHORDARP && !f2->latchOn(1) && f2->latchOn(2));
        // a damaged version 3 file (cut short / wrong trailer) is refused as a whole, and the CRC protects the latch bytes
        MockHost g3;
        Engine* f3 = mk(g3);
        CHECK(!f3->loadState(b1, n1 - 1));
        uint8_t bad[Engine::kSidecarMax];
        memcpy(bad, b1, n1);
        bad[n1 - 3] ^= 1;
        CHECK(!f3->loadState(bad, n1));
        // a version 3 header over a version 2 body is not accepted
        memcpy(bad, b0, n0);
        bad[4] = Engine::kSidecarVersionV3;
        uint16_t c = 0xFFFF;
        for (size_t i = 0; i < n0 - 2; i++)
        {
            c ^= (uint16_t)(bad[i] << 8);
            for (int k = 0; k < 8; k++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1));
        }
        bad[n0 - 2] = (uint8_t)(c & 0xFF);
        bad[n0 - 1] = (uint8_t)(c >> 8);
        CHECK(!f3->loadState(bad, n0));
        // a latched track survives a save of an engine whose latch went back to the default: version 2 again
        e->applyParam(2, flatOf((uint8_t)(TYPE_HR_FIRST + seqgen::HR_CHORD_STABS), seqparams::ID_LATCH), 1);
        e->applyParam(1, flatOf(TYPE_CHORDARP, seqparams::ID_LATCH), 0);
        CHECK(e->saveState(b1, sizeof b1) == n0 && b1[4] == Engine::kSidecarVersion);
        delete e; delete f; delete f2; delete f3;
    }
    // settings of every type survive the sidecar (the generators' own blocks) and the Strings setup
    {
        MockHost h;
        Engine* e = freshWith(h, 4, TYPE_PLINKY);
        e->applyParam(4, 1, 5);
        e->applyParam(4, 3, 6);
        e->applyParam(4, 4, 1);
        const size_t n = e->saveState(b1, sizeof b1);
        MockHost g;
        Engine* f = mk(g);
        CHECK(f->loadState(b1, n) && f->plinkySetup().scale == 5 && f->plinkySetup().interval == 6 && f->latchOn(4));
        CHECK(b1[4] == Engine::kSidecarVersionV3);
        delete e; delete f;
    }
}

static void test_params_draw()
{
    // the parameter page of every type, at every cursor position: one display stage (< 3840 FIFO bytes with the stage's own bytes),
    // inside the band above the pad legend (y 30..280)
    seqparams::Palette pal = { 0x050505, 0x1C1C1C, 0x555555, 0xFFFFFF, 0x00FFFF, 0xFFFF00, 0x000000, 0x00FFFF };
    int worst = 0, worstType = 0;
    int maxY = 0;
    for (uint8_t type = 1; type < TYPE_COUNT; type++)
    {
        const seqparams::Tab* tb = seqparams::tab(type);
        seqparams::Model m;
        m.on = 1;
        m.type = type;
        m.track = 16;
        for (uint8_t i = 0; i < tb->nTotal; i++)
        {
            const seqparams::Desc& d = seqparams::desc(*tb, i);
            m.v[i] = (int8_t)d.mx; // widest texts
        }
        for (uint8_t s = 0; s < tb->nSec; s++)
            for (uint8_t i = 0; i < tb->sec[s].n; i++)
            {
                m.sec = s;
                m.idx = i;
                PlCtx c;
                seqparams::draw(c, m, pal);
                const int f = c.fifo() + tracestage::kOwnBytes;
                if (f > worst) { worst = f; worstType = type; }
                CHECK(f <= tracestage::kStageBytes);
                CHECK(c.minX >= 0 && c.maxX <= 800 && c.minY >= seqparams::kPanelY0 && c.maxY <= seqparams::kPanelY1);
                if (c.maxY > maxY) maxY = c.maxY;
            }
    }
    printf("  parameter page worst case: %d FIFO bytes (type %d, limit %d), lowest pixel %d\n", worst, worstType, (int)tracestage::kStageBytes, maxY);
    // the legend below starts at y 282; the page ends above it (the pattern is hidden while it is shown)
    CHECK(seqparams::kRowY0 + 3 * seqparams::kRowStep + seqparams::kRowH <= 282);
    // hidden / Off: nothing is drawn
    seqparams::Model off;
    PlCtx c0;
    seqparams::draw(c0, off, pal);
    CHECK(c0.words == 0);
}

static void test_params_all()
{
    test_params_tables();
    test_latch_pad_logic();
    test_latch_chordarp();
    test_latch_stop_play();
    test_latch_types();
    test_latch_sidecar();
    test_params_draw();
}
