#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Session.hpp"
#include "ui/Hud.hpp"
#include "ui/PracticePanel.hpp"

using namespace geode::prelude;

namespace {
    // true while the mod itself is forwarding an input, so our handleButton hook ignores it
    bool g_injecting = false;

    bool isThePlayLayer(GJBaseGameLayer* layer) {
        return layer && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == layer;
    }
}

$execute {
    mpr::Session::get().loadSettings();
}

// ------------------------------------------------------------------------------------
// Input handling
// ------------------------------------------------------------------------------------

class $modify(MPGameLayer, GJBaseGameLayer) {
    // Every real click / release goes through here.
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& session = mpr::Session::get();

        if (g_injecting || !session.ui.enabled || !isThePlayLayer(this)) {
            return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }

        // same convention zBot uses to record: the event belongs to the step before currentProgress
        bool p2 = !isPlayer1 && m_levelSettings->m_twoPlayerMode && m_gameState.m_isDualMode;
        int64_t frame = session.tickOf(this) - 1;

        if (session.engine.active()) {
            auto result = session.engine.onUserInput(frame, button, p2, down);

            // feedback for what the player did
            if (session.engine.mode() != mpr::Mode::Watch && session.hud) session.hud->onUser(result);

            if (result.swallow) return; // Watch / Trace: ignored. Buffer with a macro: forwarded later by us.
        }
        else if (session.stateBufferActive()) {
            bool ready = session.playerReady(this, button, p2);
            if (session.buffer.onInput(frame, button, p2, down, ready)) return; // held back
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Once per physics step: forward macro inputs / delayed early inputs that are due.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto& session = mpr::Session::get();
        if (!session.ui.enabled || !isThePlayLayer(this)) return;

        session.onStep(this);

        int64_t progress = session.tickOf(this);
        if (session.engine.active()) {
            session.engine.dispatchDue(progress, [&](mpr::Due const& due) {
                g_injecting = true;
                // zBot convention: the third argument is "isPlayer1"
                GJBaseGameLayer::handleButton(due.down, due.button, !due.p2);
                g_injecting = false;
                session.onInjected(this, due);
            });
        }
        else if (session.stateBufferActive()) {
            session.buffer.step(
                progress,
                [&](int button, bool p2) { return session.playerReady(this, button, p2); },
                [&](mpr::InputBuffer::Out const& out) {
                    g_injecting = true;
                    GJBaseGameLayer::handleButton(out.down, out.button, !out.p2);
                    g_injecting = false;
                }
            );
        }
    }
};

// ------------------------------------------------------------------------------------
// Level lifecycle
// ------------------------------------------------------------------------------------

class $modify(MPPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        mpr::Session::get().onEnterLevel(this);
        return true;
    }

    void resetLevel() {
        auto& session = mpr::Session::get();

        // The attempt counter is saved with the level: undo any increment made while the mod is active.
        bool lock = session.locksStats();
        int attemptsBefore = lock && m_level ? m_level->m_attempts.value() : 0;

        PlayLayer::resetLevel();

        if (lock && m_level && m_level->m_attempts.value() != attemptsBefore) {
            m_level->m_attempts = attemptsBefore;
        }

        session.onResetLevel(this);
    }

    void levelComplete() {
        // The real end screen is shown (with the watermark). Anything that would be saved is
        // blocked below while the mod is controlling the run.
        mpr::Session::get().onLevelComplete(this);
        PlayLayer::levelComplete();
    }

    void onQuit() {
        mpr::Session::get().onExitLevel();
        PlayLayer::onQuit();
    }
};

// ------------------------------------------------------------------------------------
// Stats lock: nothing the mod touches may be saved
// ------------------------------------------------------------------------------------

class $modify(MPGameLevel, GJGameLevel) {
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts, bool isChkValid) {
        if (mpr::Session::get().locksStats()) return;
        GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
    }
};

class $modify(MPStatsManager, GameStatsManager) {
    void incrementStat(char const* key, int amount) {
        if (mpr::Session::get().locksStats()) return;
        GameStatsManager::incrementStat(key, amount);
    }

    void completedLevel(GJGameLevel* level) {
        if (mpr::Session::get().locksStats()) return;
        GameStatsManager::completedLevel(level);
    }
};

class $modify(MPGameManager, GameManager) {
    void reportAchievementWithID(char const* key, int percent, bool dontNotify) {
        if (mpr::Session::get().locksStats()) return;
        GameManager::reportAchievementWithID(key, percent, dontNotify);
    }
};

// ------------------------------------------------------------------------------------
// End screen watermarks
// ------------------------------------------------------------------------------------

class $modify(MPEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();

        auto& s = mpr::Session::get();
        if (!s.toolUsed() || !m_mainLayer) return;

        auto win = CCDirector::get()->getWinSize();
        constexpr float margin = 8.f;

        // -- "Practice tool used", top left --
        auto title = CCLabelBMFont::create("Practice tool used", "goldFont.fnt");
        title->setScale(0.5f);
        title->setAnchorPoint(ccp(0.f, 1.f));
        title->setPosition(ccp(margin, win.height - margin));
        title->setOpacity(210);
        m_mainLayer->addChild(title, 50);

        std::string detail;
        auto add = [&](std::string const& part) {
            if (!detail.empty()) detail += "  -  ";
            detail += part;
        };
        if (s.engine.mode() == mpr::Mode::Watch) add("Watch");
        if (s.engine.mode() == mpr::Mode::Trace) {
            auto sum = s.engine.summarize(s.reached, true);
            add(fmt::format("Trace {:.1f}%", sum.accuracy));
        }
        if (s.ui.indicators && s.hasMacro()) add("Click indicators");
        float blockHeight = title->getScaledContentSize().height;
        if (!detail.empty()) {
            auto line = CCLabelBMFont::create(detail.c_str(), "chatFont.fnt");
            line->setScale(0.55f);
            line->setAnchorPoint(ccp(0.f, 1.f));
            line->setPosition(ccp(margin, win.height - margin - blockHeight - 1.f));
            line->setOpacity(190);
            m_mainLayer->addChild(line, 50);
            blockHeight += line->getScaledContentSize().height + 1.f;
        }

        // -- Predictive Input Buffer watermark, one corner at a time --
        if (s.ui.enabled && s.ui.buffer) { // watermarks cannot be turned off
            auto text = fmt::format("Predictive Input Buffer ({}f)", s.engine.config().maxEarly);
            auto mark = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
            mark->setScale(0.42f);
            mark->setOpacity(210);

            float x = margin, y = margin;
            CCPoint anchor = ccp(0.f, 0.f);
            switch (s.ui.bufferCorner) {
                case mpr::Corner::TopLeft:
                    anchor = ccp(0.f, 1.f);
                    y = win.height - margin - blockHeight - 4.f; // below the "Practice tool used" block
                    break;
                case mpr::Corner::TopRight:
                    anchor = ccp(1.f, 1.f);
                    x = win.width - margin;
                    y = win.height - margin;
                    break;
                case mpr::Corner::BottomLeft:
                    anchor = ccp(0.f, 0.f);
                    break;
                case mpr::Corner::BottomRight:
                    anchor = ccp(1.f, 0.f);
                    x = win.width - margin;
                    break;
            }
            mark->setAnchorPoint(anchor);
            mark->setPosition(ccp(x, y));
            m_mainLayer->addChild(mark, 50);
        }
    }
};

// ------------------------------------------------------------------------------------
// Pause menu entry
// ------------------------------------------------------------------------------------

class $modify(MPPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::get()->getWinSize();

        CCNode* spr = CCSprite::create("macro-button.png"_spr);
        if (spr) {
            spr->setScale(0.55f);
        }
        else {
            spr = ButtonSprite::create("Practice Tool", 110, true, "goldFont.fnt", "GJ_button_01.png", 28.f, 0.6f);
        }
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MPPauseLayer::onMacroPractice));

        auto menu = CCMenu::create();
        menu->setPosition(ccp(0.f, 0.f));
        btn->setPosition(ccp(winSize.width - 45.f, 45.f));
        menu->addChild(btn);
        this->addChild(menu, 10);
    }

    void onMacroPractice(CCObject*) {
        PracticePanel::create(this)->show();
    }
};
