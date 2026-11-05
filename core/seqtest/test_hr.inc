// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
// Glue tests of the ported HYPER_RASP sequencer types (plan 008 D): type list, pads, takeover, transport, silence on type change, sidecar.
// Included by test_main.cpp (uses its MockHost / mk / playUSteps / idle helpers).

static const uint8_t kStabType = TYPE_HR_FIRST + seqgen::HR_CHORD_STABS;
static const uint8_t kArpType = TYPE_HR_FIRST + seqgen::HR_ARP_LINES;
static const uint8_t kArpSeqType = TYPE_HR_FIRST + seqgen::HR_ARP_SEQ;
static const uint8_t kHrChordArpType = TYPE_HR_FIRST + seqgen::HR_CHORD_ARP;

static void pressPad(Engine& e, uint8_t track, int col, int row)
{
    e.applyPad(track, (uint8_t)(row * 12 + col), true);
    e.applyPad(track, (uint8_t)(row * 12 + col), false);
}

static void test_hr_type_numbers_and_names()
{
    CHECK(TYPE_HR_FIRST == 3 && TYPE_PLINKY == TYPE_HR_FIRST + seqgen::HR_COUNT && TYPE_COUNT == TYPE_PLINKY + 1);
    CHECK(!isHrType(TYPE_OFF) && !isHrType(TYPE_CHORDARP) && !isHrType(TYPE_MONO) && isHrType(TYPE_HR_FIRST) && isHrType(TYPE_PLINKY - 1) && !isHrType(TYPE_PLINKY) && !isHrType(TYPE_COUNT));
    // old files: the earlier types keep their numbers
    CHECK(TYPE_OFF == 0 && TYPE_CHORDARP == 1 && TYPE_MONO == 2);
    for (uint8_t t = 0; t < TYPE_COUNT; t++)
    {
        CHECK(strlen(seqplay::typeShort(t)) >= 3 && strlen(seqplay::typeShort(t)) <= 4);
        CHECK(strlen(seqplay::typeName(t)) >= 3 && strlen(seqplay::typeName(t)) <= 12);
        for (uint8_t u = 0; u < t; u++)
            CHECK(strcmp(seqplay::typeName(t), seqplay::typeName(u)) != 0 && strcmp(seqplay::typeShort(t), seqplay::typeShort(u)) != 0);
    }
    CHECK(strcmp(seqplay::typeName(kHrChordArpType), seqplay::typeName(TYPE_CHORDARP)) != 0); // HYPER_RASP chord_arp is another script than Chord/Arp
}

static void test_hr_pads_sound_and_take_over()
{
    MockHost h;
    Engine* e = mk(h);
    e->applyType(2, kStabType);
    CHECK(e->type(2) == kStabType && e->hr(2) != 0 && e->hr(0) == 0);
    pressPad(*e, 2, 0, 0); // root 1 = C: the triad, latched
    CHECK(e->takenOver(2));
    CHECK(h.count('B', 2) == 1 && h.count('+', 2) == 3);
    CHECK(h.log[1].note == 60 && h.log[2].note == 64 && h.log[3].note == 67 && h.log[1].vol == velocityToVolume(78));
    CHECK(e->soundingCount(2) == 3);
    pressPad(*e, 2, 0, 0); // the same root again unlatches
    CHECK(!e->takenOver(2));
    CHECK(h.count('-', 2) == 3 && h.count('E', 2) == 1);
    uint8_t out[kPads];
    e->leds(2, out);
    CHECK(out[0] == 3 && out[11] == 3 && out[8] == 3); // roots dim, the extra pads (col 8..) are lit
    pressPad(*e, 2, 0, 0);
    e->leds(2, out);
    CHECK(out[0] == 15); // the blink of the hit
    delete e;
}

static void test_hr_clock_runs_only_while_the_transport_plays()
{
    MockHost h;
    Engine* e = mk(h);
    e->applyType(0, kStabType);
    pressPad(*e, 0, 0, 0);
    int ons = h.count('+', 0);
    CHECK(ons == 3);
    idle(*e, 4000); // the transport stopped: no pulse reaches HYPER_RASP scripts, the chord rings until the next press (the script's own rule)
    CHECK(h.count('+', 0) == ons && h.count('-', 0) == 0 && e->takenOver(0));
    int u = 1;
    playUSteps(*e, 48 * 4, u); // transport playing: start_playback, then clock pulses; the offbeat pattern hits on step 3
    CHECK(h.count('+', 0) > ons);
    CHECK(e->takenOver(0));
    idle(*e, 10); // the transport stops: stop_playback silences the voices; the latch stays
    CHECK(h.count('+', 0) == h.count('-', 0));
    CHECK(e->hr(0)->getParam(seqgen::HrStab::P_ROOT) == 1);
    delete e;
}

static void test_hr_type_change_silences_and_releases()
{
    MockHost h;
    Engine* e = mk(h);
    e->applyType(4, kArpType);
    pressPad(*e, 4, 1, 0);
    CHECK(h.count('+', 4) == 1 && e->takenOver(4));
    e->applyType(4, TYPE_MONO);
    CHECK(h.count('+', 4) == h.count('-', 4));
    CHECK(!e->takenOver(4) && e->hr(4) == 0);
    e->applyType(4, kHrChordArpType); // a script with its own latched chord and a held pad
    e->applyPad(4, 0, true);
    CHECK(h.count('+', 4) == 3 + 1); // the one arp note of the earlier type plus the E major triad
    e->applyType(4, TYPE_OFF);
    CHECK(h.count('+', 4) == h.count('-', 4) && !e->takenOver(4));
    // the type list can pick any HR type (postType): while nothing plays it applies at once
    e->postType(6, kArpSeqType);
    e->drain();
    CHECK(e->type(6) == kArpSeqType && e->hr(6)->hrType() == seqgen::HR_ARP_SEQ);
    delete e;
}

static void test_hr_tracks_share_the_scale_bus()
{
    MockHost h;
    Engine* e = mk(h);
    e->applyType(0, kStabType);
    e->applyType(1, kArpType);
    e->hr(1)->setParam(seqgen::HrStab::P_ROLE, 2); // follower
    pressPad(*e, 0, 4, 0); // root 5 = E major: the shared scale is E major, the root E
    CHECK(e->hrBus().nScale == 7 && e->hrBus().scale[0] == 4 && e->hrBus().root == 64);
    pressPad(*e, 1, 0, 0); // follower key 1: the tonic of the published scale, no publishing of its own
    CHECK(e->hrBus().root == 64);
    CHECK(h.count('+', 1) == 1 && h.log[h.log.size() - 1].note == 64);
    delete e;
}

static void test_hr_sidecar_roundtrip_and_old_files()
{
    MockHost h1, h2;
    Engine* a = mk(h1);
    a->applyType(1, kArpType);
    a->hr(1)->setParam(seqgen::HrStab::P_OCTAVE, -1);
    a->hr(1)->setParam(seqgen::HrStab::P_RATE, 4);
    pressPad(*a, 1, 4, 2); // pattern 4
    a->applyType(5, kHrChordArpType);
    a->hr(5)->setParam(seqgen::HrChordArp::P_TYPE, 7);
    a->hr(5)->setParam(seqgen::HrChordArp::P_VEL, 90);
    a->applyType(7, kArpSeqType);
    pressPad(*a, 7, 0, 3); // store step 1
    a->applyType(8, TYPE_CHORDARP);
    a->applyType(9, kStabType);
    uint8_t buf[Engine::kSidecarMax];
    size_t n = a->saveState(buf, sizeof(buf));
    CHECK(n > 100 && n <= Engine::kSidecarMax);
    printf("  sidecar with HR scripts: %d bytes (max %d)\n", (int)n, (int)Engine::kSidecarMax);
    uint16_t hashA = a->stateHash();
    Engine* b = mk(h2);
    CHECK(b->loadState(buf, n));
    CHECK(b->type(1) == kArpType && b->type(5) == kHrChordArpType && b->type(7) == kArpSeqType && b->type(8) == TYPE_CHORDARP && b->type(9) == kStabType);
    CHECK(b->hr(1)->getParam(seqgen::HrStab::P_OCTAVE) == -1 && b->hr(1)->getParam(seqgen::HrStab::P_RATE) == 4 && b->hr(1)->getParam(seqgen::HrStab::P_PATTERN) == 4);
    CHECK(b->hr(5)->getParam(seqgen::HrChordArp::P_TYPE) == 7 && b->hr(5)->getParam(seqgen::HrChordArp::P_VEL) == 90);
    CHECK(b->hr(7)->getParam(seqgen::HrArpSeq::P_PLAYING) == 0);
    uint8_t out[kPads];
    b->leds(7, out);
    CHECK(out[3 * 12 + 0] == 10); // the stored step is still there
    CHECK(b->stateHash() == hashA);
    uint8_t buf2[Engine::kSidecarMax];
    size_t n2 = b->saveState(buf2, sizeof(buf2));
    CHECK(n2 == n && memcmp(buf, buf2, n) == 0);

    // a file without any HR type has no trailer: exactly the layout of the earlier firmware (16 records of type + two blocks)
    Engine* c = mk(h2);
    c->applyType(0, TYPE_MONO);
    c->applyType(3, TYPE_CHORDARP);
    uint8_t old[Engine::kSidecarMax];
    size_t on = c->saveState(old, sizeof(old));
    size_t p = 8;
    for (int i = 0; i < kTracks; i++)
    {
        p++;
        p += 1 + old[p];
        p += 1 + old[p];
    }
    CHECK(p + 2 == on); // payload ends right after the last track record: no trailer
    // ... and it loads (it is what every file written before plan 008 D looks like)
    Engine* d = mk(h2);
    CHECK(d->loadState(old, on) && d->type(0) == TYPE_MONO && d->type(3) == TYPE_CHORDARP && d->hr(0) == 0);

    // a damaged HR block turns only that track Off
    uint8_t bad[Engine::kSidecarMax];
    memcpy(bad, buf, n);
    size_t q = 8;
    for (int i = 0; i < kTracks; i++)
    {
        q++;
        q += 1 + bad[q];
        q += 1 + bad[q];
    }
    size_t hrOff[kTracks];
    for (int i = 0; i < kTracks; i++)
    {
        hrOff[i] = q + 1;
        q += 1 + bad[q];
    }
    CHECK(q + 2 == n);
    bad[hrOff[1]] = 99; // version of the arp lines block (track 1)
    fixCrc(bad, n);
    Engine* f = mk(h2);
    CHECK(f->loadState(bad, n));
    CHECK(f->type(1) == TYPE_OFF && f->type(5) == kHrChordArpType && f->type(9) == kStabType);
    // a broken trailer (length chain) is a bad frame
    memcpy(bad, buf, n);
    bad[hrOff[0] - 1] = 200;
    fixCrc(bad, n);
    uint16_t hf = f->stateHash();
    CHECK(!f->loadState(bad, n) && f->stateHash() == hf);
    // an HR type without a block (hand-edited file): fresh defaults, not an error
    memcpy(bad, old, on);
    bad[8] = kStabType; // track 0
    fixCrc(bad, on);
    Engine* g = mk(h2);
    CHECK(g->loadState(bad, on) && g->type(0) == kStabType && g->hr(0)->getParam(seqgen::HrStab::P_RATE) == 6);
    delete a; delete b; delete c; delete d; delete f; delete g;
}

static void test_hr_state_hash_follows_the_settings_only()
{
    MockHost h;
    Engine* e = mk(h);
    e->applyType(0, kArpType);
    uint16_t h0 = e->stateHash();
    pressPad(*e, 0, 0, 0); // latching a root is performance, not a setting
    CHECK(e->stateHash() == h0);
    e->hr(0)->setParam(seqgen::HrStab::P_VEL, 64);
    CHECK(e->stateHash() != h0);
    delete e;
}
