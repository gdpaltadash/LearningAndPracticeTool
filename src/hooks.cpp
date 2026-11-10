#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
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

        if (g_injecting || !session.engine.active() || !isThePlayLayer(this)) {
            return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }

        // same convention zBot uses to record: the event belongs to the step before currentProgress
        bool p2 = !isPlayer1 && m_levelSettings->m_twoPlayerMode && m_gameState.m_isDualMode;
        int64_t frame = static_cast<int64_t>(m_gameState.m_currentProgress) - 1;

        auto result = session.engine.onUserInput(frame, button, p2, down);

        // feedback for what the player did
        if (session.engine.mode() != mpr::Mode::Watch) {
            if (session.hud) session.hud->onUser(result);
        }

        if (result.swallow) return; // Watch / Trace: ignored. Early Fix: forwarded later by us.

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Once per physics step: forward macro inputs / delayed early inputs that are due.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto& session = mpr::Session::get();
        if (!session.engine.active() || !isThePlayLayer(this)) return;

        session.onStep(this);

        int64_t progress = static_cast<int64_t>(m_gameState.m_currentProgress);
        session.engine.dispatchDue(progress, [&](mpr::Due const& due) {
            g_injecting = true;
            // zBot convention: the third argument is "isPlayer1"
            GJBaseGameLayer::handleButton(due.down, due.button, !due.p2);
            g_injecting = false;
            session.onInjected(this, due);
        });
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
        if (mpr::Session::get().onLevelComplete(this)) return; // swallowed: nothing is saved
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
