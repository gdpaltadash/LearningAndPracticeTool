// Off-game tests for the pure logic. Build & run:
//   g++ -std=c++23 -Isrc/core -I<GDReplayFormat>/include tests/test_core.cpp src/core/Macro.cpp -o test_core && ./test_core
#include "Engine.hpp"
#include "Macro.hpp"

#include <gdr/gdr.hpp>

#include <cstdio>
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
    if (g_fail == 0) std::printf("ALL TESTS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
