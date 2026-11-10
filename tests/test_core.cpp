// Off-game tests for the pure logic. Build & run:
//   g++ -std=c++23 -Isrc/core -I<GDReplayFormat>/include tests/test_core.cpp src/core/Macro.cpp -o test_core && ./test_core
#include "Engine.hpp"
#include "Macro.hpp"
#include "Buffer.hpp"
#include "PathTable.hpp"

#include <gdr/gdr.hpp>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace mpr;

static int g_fail = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
            g_fail++;                                                                  \
        }                                                                              \
    } while (0)

static std::shared_ptr<Macro> makeMacro(std::vector<MacroInput> in, double fps = 240.0) {
    auto m = std::make_shared<Macro>();
    m->fps = fps;
    m->inputs = std::move(in);
    return m;
}

static std::vector<Due> run(Engine& e, int64_t from, int64_t to) {
    std::vector<Due> out;
    for (int64_t p = from; p <= to; ++p) e.dispatchDue(p, [&](Due const& d) { out.push_back(d); });
    return out;
}

static void testFixerDelaysEarlyClick() {
    Engine e;
    e.setMacro(makeMacro({{100, 1, false, true}, {110, 1, false, false}}));
    Config c; c.maxEarly = 8; e.setConfig(c);
    e.setMode(Mode::EarlyFix);
    e.beginAttempt(0);

    // user presses at frame 95 (5 early)
    auto r = e.onUserInput(95, 1, false, true);
    CHECK(r.swallow && r.delayed && r.matched);
    CHECK(r.offset == -5);
    CHECK(r.applyDelay == 5);

    // nothing is due before frame 100
    CHECK(run(e, 0, 100).empty()); // progress 100 => frames < 100 only
    auto d = run(e, 101, 101);
    CHECK(d.size() == 1);
    CHECK(d[0].frame == 100 && d[0].down && d[0].button == 1);

    // delivered once only
    CHECK(run(e, 101, 200).empty());
}

static void testFixerLateAndTooEarlyAreNotFixed() {
    Engine e;
    e.setMacro(makeMacro({{100, 1, false, true}, {200, 1, false, true}}));
    Config c; c.maxEarly = 4; c.window = 8; e.setConfig(c);
    e.setMode(Mode::EarlyFix);
    e.beginAttempt(0);

    auto late = e.onUserInput(103, 1, false, true); // 3 late
    CHECK(!late.swallow && late.matched && late.offset == 3);

    auto tooEarly = e.onUserInput(190, 1, false, true); // 10 early, maxEarly=4
    CHECK(!tooEarly.swallow && tooEarly.extra);

    auto ok = e.onUserInput(196, 1, false, true); // 4 early => fixed
    CHECK(ok.swallow && ok.delayed && ok.applyDelay == 4);
}

static void testFixerKeepsOrderOfRelease() {
    Engine e;
    // click at 100, release at 130 (long hold)
    e.setMacro(makeMacro({{100, 1, false, true}, {130, 1, false, false}}));
    Config c; c.maxEarly = 8; e.setConfig(c);
    e.setMode(Mode::EarlyFix);
    e.beginAttempt(0);

    auto down = e.onUserInput(95, 1, false, true);   // delayed to 100
    CHECK(down.delayed);
    // user releases at 97 (way too early for a 130 release): must not run before the press
    auto up = e.onUserInput(97, 1, false, false);
    CHECK(up.swallow && up.delayed);
    CHECK(up.userFrame + up.applyDelay > 100);

    auto d = run(e, 0, 300);
    CHECK(d.size() == 2);
    CHECK(d[0].down && !d[1].down);
    CHECK(d[0].frame <= d[1].frame);
}

static void testFixerDifferentButtonsIndependent() {
    Engine e;
    e.setMacro(makeMacro({{100, 1, false, true}, {100, 1, true, true}}));
    Config c; c.maxEarly = 8; e.setConfig(c);
    e.setMode(Mode::EarlyFix);
    e.beginAttempt(0);
    auto a = e.onUserInput(98, 1, false, true);
    auto b = e.onUserInput(99, 1, true, true);
    CHECK(a.matched && a.macroIdx == 0);
    CHECK(b.matched && b.macroIdx == 1);
    CHECK(b.applyDelay == 1);
}

static void testWatchDrivesMacroAndIgnoresUser() {
    Engine e;
    e.setMacro(makeMacro({{10, 1, false, true}, {20, 1, false, false}, {30, 1, false, true}}));
    e.setMode(Mode::Watch);
    e.beginAttempt(0);

    auto r = e.onUserInput(5, 1, false, true);
    CHECK(r.swallow && !r.matched);

    auto d = run(e, 0, 25);
    CHECK(d.size() == 2);
    CHECK(d[0].frame == 10 && d[0].down);
    CHECK(d[1].frame == 20 && !d[1].down);
    d = run(e, 26, 40);
    CHECK(d.size() == 1 && d[0].frame == 30);
}

static void testWatchFrameConvention() {
    // frame F is applied at the first progress > F
    Engine e;
    e.setMacro(makeMacro({{10, 1, false, true}}));
    e.setMode(Mode::Watch);
    e.beginAttempt(0);
    CHECK(run(e, 0, 10).empty());
    CHECK(run(e, 11, 11).size() == 1);
}

static void testTraceScoring() {
    Engine e;
    e.setMacro(makeMacro({
        {100, 1, false, true}, {110, 1, false, false},
        {200, 1, false, true}, {210, 1, false, false},
        {300, 1, false, true}, {310, 1, false, false},
    }));
    Config c; c.window = 8; e.setConfig(c);
    e.setMode(Mode::Trace);
    e.beginAttempt(0);

    auto a = e.onUserInput(100, 1, false, true);  // perfect
    CHECK(a.swallow && a.matched && a.offset == 0);
    auto b = e.onUserInput(108, 1, false, false); // 2 early
    CHECK(b.offset == -2);
    auto d = e.onUserInput(203, 1, false, true);  // 3 late
    CHECK(d.offset == 3);
    auto x = e.onUserInput(250, 1, false, true);  // nothing near it
    CHECK(x.extra);
    // 210 release and the 300/310 pair are never pressed

    auto s = e.summarize(400, true);
    CHECK(s.matched == 3);
    CHECK(s.missed == 3);
    CHECK(s.total == 6);
    CHECK(s.extra == 1);
    CHECK(s.perfect == 1 && s.early == 1 && s.late == 1);
    CHECK(s.worstEarly == -2 && s.worstLate == 3);
    // accuracy: (1 + (1-2/9) + (1-3/9)) / (6 + 1) * 100
    double expect = (1.0 + (1.0 - 2.0 / 9.0) + (1.0 - 3.0 / 9.0)) / 7.0 * 100.0;
    CHECK(std::abs(s.accuracy - expect) < 1e-9);
}

static void testSummaryOnlyCountsReachedInputs() {
    Engine e;
    e.setMacro(makeMacro({{100, 1, false, true}, {500, 1, false, true}}));
    Config c; c.window = 8; e.setConfig(c);
    e.setMode(Mode::Trace);
    e.beginAttempt(0);
    e.onUserInput(100, 1, false, true);
    auto s = e.summarize(200, false); // attempt ended at frame 200
    CHECK(s.total == 1 && s.missed == 0 && s.matched == 1);
    auto s2 = e.summarize(200, true);
    CHECK(s2.total == 2 && s2.missed == 1);
}

static void testCheckpointSkipsPastInputs() {
    Engine e;
    e.setMacro(makeMacro({{10, 1, false, true}, {20, 1, false, false}, {300, 1, false, true}}));
    e.setMode(Mode::Watch);
    e.beginAttempt(250); // respawn at a checkpoint
    auto d = run(e, 250, 400);
    CHECK(d.size() == 1 && d[0].frame == 300);
    auto s = e.summarize(400, true);
    CHECK(s.total == 1); // skipped inputs are not counted as missed
}

static void testTpsScaleAndOffset() {
    Engine e;
    e.setMacro(makeMacro({{480, 1, false, true}}, 480.0));
    Config c; c.gameTps = 240.0; c.frameOffset = 2; e.setConfig(c);
    e.setMode(Mode::Watch);
    e.beginAttempt(0);
    // 480 @ 480tps == 240 @ 240tps, +2 offset
    CHECK(e.inputs()[0].frame == 242);
}

static void testBeginAttemptResetsState() {
    Engine e;
    e.setMacro(makeMacro({{50, 1, false, true}}));
    e.setMode(Mode::Trace);
    e.beginAttempt(0);
    e.onUserInput(50, 1, false, true);
    CHECK(e.summarize(100, true).matched == 1);
    e.beginAttempt(0);
    CHECK(e.summarize(100, true).matched == 0);
    CHECK(e.events().empty());
}

static void testOffModeIsTransparent() {
    Engine e;
    e.setMacro(makeMacro({{50, 1, false, true}}));
    e.setMode(Mode::Off);
    e.beginAttempt(0);
    auto r = e.onUserInput(50, 1, false, true);
    CHECK(!r.swallow);
    CHECK(run(e, 0, 100).empty());
}

// ---- GDR2 loader ---------------------------------------------------------

struct TestReplay : gdr::Replay<TestReplay, gdr::Input<>> {
    TestReplay() : Replay("TestBot", 1) {}
};

static void testLoadGdr2() {
    TestReplay r;
    r.framerate = 240.0;
    r.levelInfo = gdr::Level("Some Level", 1234);
    r.author = "me";
    r.inputs.push_back(gdr::Input<>(100, 1, false, true));
    r.inputs.push_back(gdr::Input<>(110, 1, false, false));
    r.inputs.push_back(gdr::Input<>(150, 1, false, true));
    r.inputs.push_back(gdr::Input<>(160, 1, true, true));
    r.inputs.push_back(gdr::Input<>(170, 1, true, false));
    auto exp = r.exportData();
    CHECK(exp.isOk());
    auto data = exp.unwrap();

    auto lr = loadMacro(data, "test.gdr2");
    CHECK(lr.ok());
    if (!lr.ok()) { std::printf("  err: %s\n", lr.error.c_str()); return; }
    CHECK(lr.macro->inputs.size() == 5);
    CHECK(lr.macro->levelId == 1234);
    CHECK(lr.macro->levelName == "Some Level");
    CHECK(lr.macro->botName == "TestBot");
    CHECK(lr.macro->fps == 240.0);
    CHECK(lr.macro->inputs[0].frame == 100 && lr.macro->inputs[0].down);
    CHECK(lr.macro->inputs[1].frame == 110 && !lr.macro->inputs[1].down);
    // p2 inputs are interleaved in time order
    CHECK(lr.macro->inputs[3].frame == 160 && lr.macro->inputs[3].p2);
    CHECK(lr.macro->inputs[4].frame == 170 && lr.macro->inputs[4].p2 && !lr.macro->inputs[4].down);
}

static void testLoadGdr2Platformer() {
    TestReplay r;
    r.platformer = true;
    r.inputs.push_back(gdr::Input<>(10, 2, false, true));
    r.inputs.push_back(gdr::Input<>(20, 2, false, false));
    r.inputs.push_back(gdr::Input<>(30, 3, false, true));
    auto exp = r.exportData();
    CHECK(exp.isOk());
    auto lr = loadMacro(exp.unwrap(), "p.gdr2");
    CHECK(lr.ok());
    if (!lr.ok()) return;
    CHECK(lr.macro->platformer);
    CHECK(lr.macro->inputs[0].button == 2);
    CHECK(lr.macro->inputs[2].button == 3);
}

static void testLoadRejectsGarbage() {
    auto a = loadMacro({1, 2, 3, 4, 5, 6}, "x");
    CHECK(!a.ok() && !a.error.empty());
    auto b = loadMacro({'G', 'D', 'R', 2, 0xff, 0xff}, "x");
    CHECK(!b.ok());
    auto c = loadMacro({}, "x");
    CHECK(!c.ok());
    // truncated real file must not crash
    TestReplay r;
    for (int i = 0; i < 50; i++) r.inputs.push_back(gdr::Input<>(i * 10, 1, false, i % 2 == 0));
    auto data = r.exportData().unwrap();
    for (size_t cut = 4; cut < data.size(); cut += 3) {
        std::vector<uint8_t> t(data.begin(), data.begin() + cut);
        (void)loadMacro(t, "trunc");
    }
}

static std::vector<uint8_t> bytes(std::string const& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xff); }

static void testOtherFormats() {
    // MegaHack JSON
    auto a = loadMacro(bytes(R"({"_":"x","meta":{"fps":240},"events":[{"frame":10,"down":true},{"frame":20,"down":false},{"frame":30,"x":1.5,"y":2},{"frame":40,"down":true,"p2":true}]})"), "a.mhr.json");
    CHECK(a.ok());
    if (a.ok()) {
        CHECK(a.macro->inputs.size() == 3 && a.macro->fps == 240.0);
        CHECK(a.macro->inputs[0].down && !a.macro->inputs[1].down && a.macro->inputs[2].p2);
    }
    // MegaHack binary
    std::vector<uint8_t> m = {0x48, 0x41, 0x43, 0x4B, 0x50, 0x52, 0x4F, 0x07};
    put32(m, 4); put32(m, 360); put32(m, 0); put32(m, 0); put32(m, 32); put32(m, 2);
    for (int i = 0; i < 2; i++) {
        m.push_back(1); m.push_back(0); m.push_back(i == 0 ? 1 : 0); m.push_back(i == 1 ? 1 : 0);
        put32(m, 100 + i * 50);
        for (int k = 0; k < 24; k++) m.push_back(0);
    }
    auto b = loadMacro(m, "b.mhr");
    CHECK(b.ok());
    if (b.ok()) {
        CHECK(b.macro->fps == 360.0 && b.macro->inputs.size() == 2);
        CHECK(b.macro->inputs[0].frame == 100 && b.macro->inputs[0].down && !b.macro->inputs[0].p2);
        CHECK(b.macro->inputs[1].frame == 150 && !b.macro->inputs[1].down && b.macro->inputs[1].p2);
    }
    // plain text
    auto c = loadMacro(bytes("240\n5 1 0\n9 0 0\n12 1 1\n"), "c.txt");
    CHECK(c.ok());
    if (c.ok()) CHECK(c.macro->inputs.size() == 3 && c.macro->inputs[2].p2 && c.macro->inputs[1].frame == 9);
    // xdBot
    auto d = loadMacro(bytes("240\r\n5|1|1|1|0\r\n9|0|1|1|0\r\n12|1|1|0|0\r\n"), "d.txt");
    CHECK(d.ok());
    if (d.ok()) CHECK(d.macro->botName == "xdBot" && d.macro->inputs[2].p2 && !d.macro->inputs[0].p2);
    // TASBot
    auto e = loadMacro(bytes(R"({"fps":120,"macro":[{"frame":3,"player_1":{"click":1,"x_position":0},"player_2":{"click":0,"x_position":0}},{"frame":8,"player_1":{"click":2,"x_position":0},"player_2":{"click":1,"x_position":0}}]})"), "e.json");
    CHECK(e.ok());
    if (e.ok()) CHECK(e.macro->inputs.size() == 3 && e.macro->fps == 120.0);
    // Echo
    auto f = loadMacro(bytes(R"({"fps":240,"inputs":[{"frame":4,"holding":true},{"frame":9,"holding":false,"player_2":false}]})"), "f.json");
    CHECK(f.ok() && f.macro->inputs.size() == 2);
    // zBot
    std::vector<uint8_t> z;
    float delta = 1.f / 240.f, speed = 1.f;
    uint8_t raw[4];
    std::memcpy(raw, &delta, 4); z.insert(z.end(), raw, raw + 4);
    std::memcpy(raw, &speed, 4); z.insert(z.end(), raw, raw + 4);
    put32(z, 20); z.push_back(0x31); z.push_back(0x31);
    put32(z, 31); z.push_back(0x30); z.push_back(0x31);
    auto g = loadMacro(z, "g.zbf");
    CHECK(g.ok());
    if (g.ok()) CHECK(g.macro->fps == 240.0 && g.macro->inputs.size() == 2 && g.macro->inputs[0].down && !g.macro->inputs[1].down);
    auto g2 = loadMacro(z, "noext");
    CHECK(g2.ok());
    // junk of every kind must not crash
    for (size_t cut = 4; cut < m.size(); cut += 3) (void)loadMacro(std::vector<uint8_t>(m.begin(), m.begin() + cut), "t.mhr");
    (void)loadMacro(bytes("{\"events\":[{\"frame\":"), "t.json");
    (void)loadMacro(bytes("240\n1|1\n"), "t.txt");
    CHECK(!loadMacro(bytes("{\"foo\":1}"), "x.json").ok());
}

static void testStartPosShift() {
    // macro recorded from the level start; the game starts at a StartPos that is macro progress 1000
    auto m = makeMacro({{1100, 1, false, true}, {1110, 1, false, false}});
    Engine e;
    e.setMacro(m);
    e.setMode(Mode::Watch);
    e.beginAttempt(1, 999); // game progress 1 == macro progress 1000
    CHECK(e.shift() == 999);
    auto d = run(e, 1, 90);
    CHECK(d.empty());          // macro frame 1100 is game progress ~101
    d = run(e, 91, 110);
    CHECK(d.size() == 1 && d[0].frame == 1100 && d[0].down);
    // Trace: the user clicks at game frame 100 -> macro frame 1099 -> 1 early
    Engine t;
    t.setMacro(m);
    t.setMode(Mode::Trace);
    t.beginAttempt(1, 999);
    auto r = t.onUserInput(100, 1, false, true);
    CHECK(r.matched && r.offset == -1 && r.userFrame == 1100 - 1);
    // inputs before the StartPos are skipped, not injected
    Engine w;
    w.setMacro(makeMacro({{50, 1, false, true}, {60, 1, false, false}, {1100, 1, false, true}}));
    w.setMode(Mode::Watch);
    w.beginAttempt(1, 999);
    auto dd = run(w, 1, 300);
    CHECK(dd.size() == 1 && dd[0].frame == 1100);
    // config changes keep the shift
    Config c = w.config();
    c.frameOffset = 1;
    w.setConfig(c);
    CHECK(w.shift() == 999);
}

static void testInputBuffer() {
    InputBuffer b;
    b.setWindow(6);
    std::vector<InputBuffer::Out> out;
    auto emit = [&](InputBuffer::Out const& o) { out.push_back(o); };
    bool ready = false;
    auto rdy = [&](int, bool) { return ready; };

    // ready right now: not taken over
    CHECK(!b.onInput(10, 1, false, true, true));
    // not ready: held
    InputBuffer c;
    c.setWindow(6);
    CHECK(c.onInput(10, 1, false, true, false));   // press at frame 10, can't act
    CHECK(c.onInput(13, 1, false, false, false));  // release 3 frames later, still queued
    out.clear();
    ready = false;
    for (int p = 11; p <= 13; ++p) c.step(p, rdy, emit);
    CHECK(out.empty());
    ready = true;
    c.step(14, rdy, emit);                         // now = 13: lands -> press applied, shift = 3
    CHECK(out.size() == 1 && out[0].down);
    for (int p = 15; p <= 16; ++p) c.step(p, rdy, emit);
    CHECK(out.size() == 1);                        // release must keep the 3 frame hold
    c.step(17, rdy, emit);                         // now = 16 = 13 + 3
    CHECK(out.size() == 2 && !out[1].down);
    CHECK(!c.anyWaiting());
    // after draining, a release goes straight through
    CHECK(!c.onInput(40, 1, false, false, false));

    // window expires: the press is applied anyway
    InputBuffer d;
    d.setWindow(4);
    out.clear();
    ready = false;
    d.onInput(100, 1, false, true, false);
    for (int p = 101; p <= 104; ++p) d.step(p, rdy, emit);
    CHECK(out.empty());
    d.step(105, rdy, emit);                        // now = 104 >= 100 + 4
    CHECK(out.size() == 1 && out[0].down);

    // keys are independent and order on one key is kept
    InputBuffer e;
    out.clear();
    e.onInput(10, 1, false, true, false);
    CHECK(!e.onInput(11, 2, false, true, true));   // other button: free
    CHECK(e.onInput(12, 1, false, false, false));
    CHECK(e.onInput(14, 1, false, true, true));    // second tap queued behind the first
    ready = true;
    for (int p = 11; p <= 30; ++p) e.step(p, rdy, emit);
    CHECK(out.size() == 3 && out[0].down && !out[1].down && out[2].down);
}

static void testPathTable() {
    PathTable t;
    for (int i = 0; i < 1000; ++i) t.record(i, 10.f + i * 1.3f, 100.f, std::numeric_limits<float>::quiet_NaN(), 0.f);
    CHECK(t.size() == 1000 && t.dirty());
    auto at = t.at(500);
    CHECK(at && std::fabs(at->x1 - 660.f) < 0.01f);
    CHECK(!t.at(1000) && !t.at(-1));
    auto idx = t.findIndexAtX(10.f + 400 * 1.3f + 0.4f);
    CHECK(idx && *idx == 400);
    CHECK(!t.findIndexAtX(99999.f));
    // first write wins
    t.record(500, 5000.f, 0, 0, 0);
    CHECK(std::fabs(t.at(500)->x1 - 660.f) < 0.01f);
    // persistence
    auto bytes = t.serialize();
    PathTable u;
    CHECK(u.deserialize(bytes) && u.size() == 1000 && !u.dirty());
    CHECK(std::fabs(u.at(999)->x1 - t.at(999)->x1) < 0.001f);
    bytes.resize(bytes.size() - 3);
    CHECK(!u.deserialize(bytes));
    CHECK(!u.deserialize({1, 2, 3}));
    // a drifting run never damages the table; a macro-driven (authoritative) run replaces it
    PathTable v;
    for (int i = 0; i < 100; ++i) v.record(i, i * 2.f, 0, 0, 0);
    for (int i = 0; i < 100; ++i) v.record(i, i * 2.f + 500.f, 0, 0, 0);
    CHECK(v.size() == 100 && std::fabs(v.at(50)->x1 - 100.f) < 0.01f);
    for (int i = 0; i < 100; ++i) v.record(i, i * 2.f + 500.f, 0, 0, 0, true);
    CHECK(std::fabs(v.at(50)->x1 - 600.f) < 0.01f);
    PathTable h;
    h.record(10, 1.f, 2.f, 0, 0);
    h.record(14, 5.f, 2.f, 0, 0);
    CHECK(h.nearest(12) && h.nearest(11)->x1 == 1.f && h.nearest(13)->x1 == 5.f && !h.nearest(100));
}

static void testNeverShift() {
    auto m = makeMacro({{0, 1, false, true}, {10, 1, false, false}});
    Engine e;
    e.setMacro(m);
    e.setMode(Mode::Trace);
    e.beginAttempt(0, kNeverShift);
    CHECK(run(e, 1, 500).empty());
    auto r = e.onUserInput(50, 1, false, true);
    CHECK(r.swallow && !r.extra && e.summarize(100, false).extra == 0);
    // the macro starts later: this step is macro progress 1
    e.beginAttempt(299, 1 - 300);
    auto d = run(e, 300, 320);
    CHECK(d.size() == 2 && d[0].frame == 0 && d[1].frame == 10);
    Engine b;
    b.setMacro(m);
    b.setMode(Mode::EarlyFix);
    b.beginAttempt(0, kNeverShift);
    CHECK(!b.onUserInput(5, 1, false, true).swallow); // buffer with a macro that has not started: pass through
}

static void testNearest() {
    auto m = makeMacro({{100, 1, false, true}});
    Engine e;
    e.setMacro(m);
    e.setMode(Mode::Trace);
    e.beginAttempt(0);
    auto r = e.onUserInput(130, 1, false, true);
    CHECK(r.extra && r.hasNearest && r.nearestOffset == 30);
    auto r2 = e.onUserInput(130, 1, true, true);
    CHECK(r2.extra && !r2.hasNearest);
}

int main() {
    testFixerDelaysEarlyClick();
    testFixerLateAndTooEarlyAreNotFixed();
    testFixerKeepsOrderOfRelease();
    testFixerDifferentButtonsIndependent();
    testWatchDrivesMacroAndIgnoresUser();
    testWatchFrameConvention();
    testTraceScoring();
    testSummaryOnlyCountsReachedInputs();
    testCheckpointSkipsPastInputs();
    testTpsScaleAndOffset();
    testBeginAttemptResetsState();
    testOffModeIsTransparent();
    testLoadGdr2();
    testLoadGdr2Platformer();
    testLoadRejectsGarbage();
    testOtherFormats();
    testStartPosShift();
    testInputBuffer();
    testNeverShift();
    testNearest();
    testPathTable();
    if (g_fail == 0) std::printf("ALL TESTS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
