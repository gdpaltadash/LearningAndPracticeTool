#include "Hud.hpp"

#include "../Session.hpp"

#include <map>

using namespace geode::prelude;

namespace {
    constexpr float kLaneWidth = 300.f;
    constexpr float kLaneHeight = 22.f;
    constexpr float kNowFraction = 0.28f;   // where the "now" line sits inside the lane
    constexpr float kLookAheadSeconds = 0.75f;

    ccColor4F col(float r, float g, float b, float a = 1.f) {
        return ccColor4F{r, g, b, a};
    }

    ccColor4F offsetColor(int offset, bool matched) {
        if (!matched) return col(0.65f, 0.65f, 0.65f);
        if (offset == 0) return col(1.f, 0.88f, 0.25f);   // perfect
        if (offset < 0) return col(0.35f, 0.7f, 1.f);     // early
        return col(1.f, 0.55f, 0.2f);                     // late
    }

    ccColor4F fromRgb(ccColor3B c, float a) {
        return ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, a};
    }

    ccColor3B toRgb(ccColor4F c) {
        return ccColor3B{
            static_cast<GLubyte>(c.r * 255.f),
            static_cast<GLubyte>(c.g * 255.f),
            static_cast<GLubyte>(c.b * 255.f)
        };
    }
}

MacroHud* MacroHud::create() {
    auto ret = new MacroHud();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

MacroHud::~MacroHud() {
    auto& s = mpr::Session::get();
    if (s.hud == this) s.hud = nullptr;
    if (m_world) {
        m_world->removeFromParent();
        m_world->release();
        m_world = nullptr;
    }
}

bool MacroHud::init() {
    if (!CCNode::init()) return false;

    auto winSize = CCDirector::get()->getWinSize();
    this->setPosition(ccp(winSize.width / 2.f, 38.f));

    m_lane = CCDrawNode::create();
    this->addChild(m_lane, 1);

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_status->setScale(0.32f);
    m_status->setPosition(ccp(0.f, kLaneHeight / 2.f + 14.f));
    this->addChild(m_status, 2);

    m_flash = CCLabelBMFont::create("", "bigFont.fnt");
    m_flash->setScale(0.38f);
    m_flash->setOpacity(0);
    m_flash->setPosition(ccp(-kLaneWidth / 2.f + kLaneWidth * kNowFraction, kLaneHeight / 2.f + 34.f));
    this->addChild(m_flash, 3);

    this->setVisible(false);
    this->scheduleUpdate();
    return true;
}

void MacroHud::attachWorld(CCNode* objectLayer) {
    if (!objectLayer) return;
    m_world = CCDrawNode::create();
    m_world->retain();
    objectLayer->addChild(m_world, 5000);
}

void MacroHud::onNewAttempt() {
    m_flashTimer = 0.f;
    if (m_flash) m_flash->setOpacity(0);
    m_statusTimer = 1.f; // force refresh
}

void MacroHud::onUser(mpr::UserResult const& r) {
    auto& s = mpr::Session::get();
    auto const& engine = s.engine;

    std::string text;
    ccColor4F color = col(1.f, 1.f, 1.f);

    if (r.extra) {
        text = "No match";
        color = offsetColor(0, false);
    }
    else if (r.delayed) {
        text = fmt::format("Fixed {}f early", -r.offset);
        color = col(0.4f, 1.f, 0.55f);
    }
    else if (r.offset == 0) {
        text = "Perfect";
        color = offsetColor(0, true);
    }
    else {
        text = fmt::format("{:+}f ({:+.0f} ms)", r.offset, engine.framesToMs(r.offset));
        color = offsetColor(r.offset, true);
    }

    m_flash->setString(text.c_str());
    m_flash->setColor(toRgb(color));
    m_flash->setOpacity(255);
    m_flashTimer = 1.1f;
}

void MacroHud::refreshStatus(mpr::Engine const& engine, int64_t reached) {
    auto sum = engine.summarize(reached, false);
    std::string text;
    switch (engine.mode()) {
        case mpr::Mode::Watch:
            text = "WATCH";
            break;
        case mpr::Mode::EarlyFix:
            text = fmt::format("BUFFER  fixed {}", sum.fixed);
            break;
        case mpr::Mode::Trace:
            text = fmt::format("TRACE  {:.1f}%  {}/{}", sum.accuracy, sum.matched, sum.total);
            break;
        default:
            break;
    }
    m_status->setString(text.c_str());
}

void MacroHud::update(float dt) {
    auto& s = mpr::Session::get();
    auto* pl = PlayLayer::get();
    auto const& engine = s.engine;

    bool show = pl && s.indicatorsVisible();
    this->setVisible(show);
    if (m_world) m_world->setVisible(show && s.ui.showDots);
    if (!show) return;

    // flash fade-out
    if (m_flashTimer > 0.f) {
        m_flashTimer -= dt;
        float a = std::clamp(m_flashTimer / 0.5f, 0.f, 1.f);
        m_flash->setOpacity(static_cast<GLubyte>(255.f * a));
    }

    m_statusTimer += dt;
    if (m_statusTimer >= 0.1f) {
        m_statusTimer = 0.f;
        refreshStatus(engine, s.reached);
    }

    double now = static_cast<double>(pl->m_gameState.m_currentProgress) - 1.0 + static_cast<double>(engine.shift());

    m_lane->clear();
    if (s.ui.showLane) {
        m_lane->setVisible(true);
        drawLane(engine, now);
    }
    else {
        m_lane->setVisible(false);
    }

    if (m_world && s.ui.showDots) {
        drawWorld(engine, pl->m_player1 ? pl->m_player1->getPosition() : ccp(0, 0), now);
    }
}

void MacroHud::drawLane(mpr::Engine const& engine, double nowFrame) {
    auto& s = mpr::Session::get();

    float left = -kLaneWidth / 2.f;
    float right = kLaneWidth / 2.f;
    float top = kLaneHeight / 2.f;
    float bottom = -kLaneHeight / 2.f;
    float nowX = left + kLaneWidth * kNowFraction;

    // background
    CCPoint bg[4] = { ccp(left, bottom), ccp(right, bottom), ccp(right, top), ccp(left, top) };
    m_lane->drawPolygon(bg, 4, col(0.f, 0.f, 0.f, 0.45f), 1.f, col(1.f, 1.f, 1.f, 0.25f));

    double tps = engine.tps();
    double aheadFrames = tps * kLookAheadSeconds;
    float pxPerFrame = static_cast<float>((right - nowX) / aheadFrames);
    double trailFrames = (nowX - left) / pxPerFrame;

    auto xOf = [&](double frame) {
        return nowX + static_cast<float>((frame - nowFrame) * pxPerFrame);
    };

    auto const& inputs = engine.inputs();
    int64_t lo = static_cast<int64_t>(nowFrame - trailFrames) - 1;
    int64_t hi = static_cast<int64_t>(nowFrame + aheadFrames) + 1;

    auto it = std::lower_bound(inputs.begin(), inputs.end(), lo, [](mpr::Engine::Eff const& e, int64_t f) {
        return e.frame < f;
    });

    this->rebuildPairs(engine);
    float baseA = static_cast<float>(s.ui.indicatorOpacity) / 100.f;
    auto laneY = [](bool p2) { return p2 ? -4.f : 3.f; };

    // hold lines first so the circles sit on top of them
    if (s.ui.holdLine) {
        auto lineColor = fromRgb(s.ui.holdColor, static_cast<float>(s.ui.holdOpacity) / 100.f);
        for (auto const& pr : m_pairs) {
            auto const& d = inputs[pr.down];
            auto const& u = inputs[pr.up];
            if (d.skipped || u.frame < lo || d.frame > hi) continue;
            float x1 = std::max(xOf(static_cast<double>(d.frame)), left + 1.f);
            float x2 = std::min(xOf(static_cast<double>(u.frame)), right - 1.f);
            if (x2 <= x1) continue;
            m_lane->drawSegment(ccp(x1, laneY(d.p2)), ccp(x2, laneY(d.p2)), 1.6f, lineColor);
        }
    }

    // macro inputs: both circles, press and release only differ in color
    for (; it != inputs.end() && it->frame <= hi; ++it) {
        if (it->skipped) continue;
        float x = xOf(static_cast<double>(it->frame));
        bool done = it->matched || it->injected || static_cast<double>(it->frame) <= nowFrame;
        float alpha = baseA * (done ? 0.4f : 1.f);
        m_lane->drawDot(ccp(x, laneY(it->p2)), 4.f, fromRgb(it->down ? s.ui.pressColor : s.ui.releaseColor, alpha));
    }

    // what the player actually did this attempt
    for (auto const& ev : engine.events()) {
        if (ev.userFrame < lo || ev.userFrame > hi) continue;
        float x = xOf(static_cast<double>(ev.userFrame));
        ccColor4F c = ev.delayed ? col(0.4f, 1.f, 0.55f) : offsetColor(ev.offset, ev.matched);

        // a delayed (fixed) input: draw where it was applied
        if (ev.delayed) {
            float x2 = xOf(static_cast<double>(ev.userFrame + ev.applyDelay));
            m_lane->drawSegment(ccp(x, 0.f), ccp(x2, 0.f), 0.8f, col(0.4f, 1.f, 0.55f, 0.7f));
        }
        m_lane->drawSegment(ccp(x, bottom + 2.f), ccp(x, top - 2.f), 1.2f, c);
    }

    // "now" line on top
    m_lane->drawSegment(ccp(nowX, bottom - 3.f), ccp(nowX, top + 3.f), 1.4f, col(1.f, 1.f, 1.f, 0.95f));
}

void MacroHud::drawWorld(mpr::Engine const& engine, CCPoint playerPos, double nowFrame) {
    m_world->clear();

    auto& s = mpr::Session::get();
    auto const& inputs = engine.inputs();

    constexpr float kBehind = 120.f;
    constexpr float kAhead = 1400.f;
    float baseA = static_cast<float>(s.ui.indicatorOpacity) / 100.f;

    auto visible = [&](CCPoint const& p) {
        return p.x >= playerPos.x - kBehind && p.x <= playerPos.x + kAhead;
    };
    // where the player was when each input happened (measured while the level was played from the start)
    auto where = [&](mpr::Engine::Eff const& e) { return s.pathPointFor(e.frame, e.p2); };

    // hold lines: from where the press happened to where the release happened
    if (s.ui.holdLine) {
        this->rebuildPairs(engine);
        auto lineColor = fromRgb(s.ui.holdColor, static_cast<float>(s.ui.holdOpacity) / 100.f);
        for (auto const& pr : m_pairs) {
            if (inputs[pr.down].skipped) continue;
            auto a = where(inputs[pr.down]);
            auto b = where(inputs[pr.up]);
            if (!a || !b) continue;
            if (!visible(*a) && !visible(*b)) continue;
            m_world->drawSegment(*a, *b, 2.f, lineColor);
        }
    }

    for (auto const& e : inputs) {
        if (e.skipped) continue;
        auto p = where(e);
        if (!p || !visible(*p)) continue;

        bool done = e.matched || e.injected || static_cast<double>(e.frame) <= nowFrame;
        float alpha = baseA * (done ? 0.4f : 1.f);
        m_world->drawDot(*p, 6.f, fromRgb(e.down ? s.ui.pressColor : s.ui.releaseColor, alpha));
    }
}

void MacroHud::rebuildPairs(mpr::Engine const& engine) {
    auto const* key = engine.macro().get();
    auto const& inputs = engine.inputs();
    if (key == m_pairsFor && inputs.size() == m_pairsSize) return;
    m_pairsFor = key;
    m_pairsSize = inputs.size();
    m_pairs.clear();

    // open press per (player, button)
    std::map<std::pair<bool, int>, size_t> open;
    for (size_t i = 0; i < inputs.size(); ++i) {
        auto k = std::make_pair(inputs[i].p2, inputs[i].button);
        if (inputs[i].down) {
            open[k] = i;
        }
        else if (auto f = open.find(k); f != open.end()) {
            m_pairs.push_back({f->second, i});
            open.erase(f);
        }
    }
}
