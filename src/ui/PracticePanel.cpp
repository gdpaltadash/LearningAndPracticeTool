#include "PracticePanel.hpp"

#include "ResultsPopup.hpp"

#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
    // stepper indices
    enum Step {
        StepMaxEarly = 0,
        StepWindow = 1,
        StepOffset = 2,
        StepTps = 3,
        StepMacroVol = 4,
        StepUserVol = 5,
    };

    // toggle indices
    enum Toggle {
        ToggleMacroSounds = 0,
        ToggleUserSounds = 1,
        ToggleLane = 2,
        ToggleDots = 3,
    };

    constexpr std::array<int, 9> kTpsValues = {0, 60, 120, 180, 240, 360, 480, 720, 960};

    int tpsIndex(int tps) {
        for (size_t i = 0; i < kTpsValues.size(); ++i) {
            if (kTpsValues[i] == tps) return static_cast<int>(i);
        }
        return 0;
    }

    std::string stepValueText(int index) {
        auto const& s = mpr::Session::get();
        auto const& c = s.engine.config();
        switch (index) {
            case StepMaxEarly: return fmt::format("{}f", c.maxEarly);
            case StepWindow: return fmt::format("{}f", c.window);
            case StepOffset: return fmt::format("{:+}f", c.frameOffset);
            case StepTps: return c.gameTps <= 0.0 ? std::string("Auto") : fmt::format("{:.0f}", c.gameTps);
            case StepMacroVol: return fmt::format("{}%", s.ui.macroVolume);
            case StepUserVol: return fmt::format("{}%", s.ui.userVolume);
        }
        return "";
    }

    mpr::Mode modeFromTag(int tag) {
        switch (tag) {
            case 1: return mpr::Mode::EarlyFix;
            case 2: return mpr::Mode::Watch;
            case 3: return mpr::Mode::Trace;
            default: return mpr::Mode::Off;
        }
    }
}

PracticePanel* PracticePanel::create(PauseLayer* pause) {
    auto ret = new PracticePanel();
    if (ret->init(pause)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PracticePanel::init(PauseLayer* pause) {
    if (!Popup::init(440.f, 340.f, "square01_001.png")) return false;
    m_pause = pause;
    this->setTitle("Macro Practice");

    auto& s = mpr::Session::get();

    // ---- macro info + load / unload -------------------------------------------------
    m_macroName = CCLabelBMFont::create("", "bigFont.fnt");
    m_macroName->setScale(0.4f);
    m_mainLayer->addChildAtPosition(m_macroName, Anchor::TopLeft, ccp(22.f, -56.f), ccp(0.f, 0.5f));

    m_macroInfo = CCLabelBMFont::create("", "chatFont.fnt");
    m_macroInfo->setScale(0.6f);
    m_macroInfo->setColor(ccc3(200, 200, 200));
    m_mainLayer->addChildAtPosition(m_macroInfo, Anchor::TopLeft, ccp(22.f, -74.f), ccp(0.f, 0.5f));

    m_warning = CCLabelBMFont::create("", "chatFont.fnt");
    m_warning->setScale(0.55f);
    m_warning->setColor(ccc3(255, 190, 60));
    m_mainLayer->addChildAtPosition(m_warning, Anchor::TopLeft, ccp(22.f, -90.f), ccp(0.f, 0.5f));

    auto loadSpr = ButtonSprite::create("Load Macro", 90, true, "goldFont.fnt", "GJ_button_01.png", 26.f, 0.6f);
    auto loadBtn = CCMenuItemSpriteExtra::create(loadSpr, this, menu_selector(PracticePanel::onLoad));
    m_buttonMenu->addChildAtPosition(loadBtn, Anchor::TopRight, ccp(-62.f, -58.f));

    auto unloadSpr = ButtonSprite::create("Unload", 90, true, "goldFont.fnt", "GJ_button_06.png", 22.f, 0.5f);
    auto unloadBtn = CCMenuItemSpriteExtra::create(unloadSpr, this, menu_selector(PracticePanel::onUnload));
    m_buttonMenu->addChildAtPosition(unloadBtn, Anchor::TopRight, ccp(-62.f, -84.f));

    // ---- mode selector -----------------------------------------------------------------
    auto modeLabel = CCLabelBMFont::create("Mode", "goldFont.fnt");
    modeLabel->setScale(0.6f);
    m_mainLayer->addChildAtPosition(modeLabel, Anchor::TopLeft, ccp(22.f, -122.f), ccp(0.f, 0.5f));

    char const* modeNames[4] = {"Off", "Early Fix", "Watch", "Trace"};
    for (int i = 0; i < 4; ++i) {
        auto spr = ButtonSprite::create(modeNames[i], 76, true, "goldFont.fnt", "GJ_button_04.png", 26.f, 0.6f);
        m_modeSprites[i] = spr;
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PracticePanel::onMode));
        btn->setTag(i);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, ccp(132.f + 84.f * i, -122.f));
    }

    // ---- steppers ----------------------------------------------------------------------
    this->addStepper(StepMaxEarly, "Max early frames", 22.f, -160.f);
    this->addStepper(StepWindow, "Scoring window", 22.f, -188.f);
    this->addStepper(StepOffset, "Macro frame offset", 22.f, -216.f);
    this->addStepper(StepTps, "Game TPS", 232.f, -160.f);
    this->addStepper(StepMacroVol, "Macro click volume", 232.f, -188.f);
    this->addStepper(StepUserVol, "My click volume", 232.f, -216.f);

    // ---- toggles -------------------------------------------------------------------------
    this->addToggle(ToggleMacroSounds, "Macro click sounds", 22.f, -250.f, s.ui.macroSounds);
    this->addToggle(ToggleUserSounds, "My click sounds", 22.f, -276.f, s.ui.userSounds);
    this->addToggle(ToggleLane, "Click lane (HUD)", 232.f, -250.f, s.ui.showLane);
    this->addToggle(ToggleDots, "Dots in level", 232.f, -276.f, s.ui.showDots);

    // ---- bottom buttons ------------------------------------------------------------------
    auto restartSpr = ButtonSprite::create("Restart Level", 120, true, "goldFont.fnt", "GJ_button_01.png", 28.f, 0.65f);
    auto restartBtn = CCMenuItemSpriteExtra::create(restartSpr, this, menu_selector(PracticePanel::onRestart));
    m_buttonMenu->addChildAtPosition(restartBtn, Anchor::Bottom, ccp(-76.f, 24.f));

    auto resultsSpr = ButtonSprite::create("Last Results", 120, true, "goldFont.fnt", "GJ_button_02.png", 28.f, 0.65f);
    auto resultsBtn = CCMenuItemSpriteExtra::create(resultsSpr, this, menu_selector(PracticePanel::onLastResults));
    m_buttonMenu->addChildAtPosition(resultsBtn, Anchor::Bottom, ccp(76.f, 24.f));

    this->refresh();
    return true;
}

void PracticePanel::addStepper(int index, char const* name, float x, float y) {
    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(x, y), ccp(0.f, 0.5f));

    float arrowsX = x + 128.f;

    auto leftSpr = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
    leftSpr->setScale(0.8f);
    auto leftBtn = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(PracticePanel::onStep));
    leftBtn->setTag(index * 2);
    m_buttonMenu->addChildAtPosition(leftBtn, Anchor::TopLeft, ccp(arrowsX, y));

    auto value = CCLabelBMFont::create("", "bigFont.fnt");
    value->setScale(0.38f);
    m_values[index] = value;
    m_mainLayer->addChildAtPosition(value, Anchor::TopLeft, ccp(arrowsX + 33.f, y));

    auto rightSpr = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
    rightSpr->setScale(0.8f);
    auto rightBtn = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(PracticePanel::onStep));
    rightBtn->setTag(index * 2 + 1);
    m_buttonMenu->addChildAtPosition(rightBtn, Anchor::TopLeft, ccp(arrowsX + 66.f, y));
}

void PracticePanel::addToggle(int index, char const* name, float x, float y, bool value) {
    auto toggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PracticePanel::onToggle), 0.6f);
    toggle->setTag(index);
    toggle->toggle(value);
    m_toggles[index] = toggle;
    m_buttonMenu->addChildAtPosition(toggle, Anchor::TopLeft, ccp(x + 12.f, y));

    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(x + 32.f, y), ccp(0.f, 0.5f));
}

void PracticePanel::refresh() {
    auto& s = mpr::Session::get();
    auto const& macro = s.engine.macro();

    if (macro) {
        m_macroName->setString(macro->fileName.empty() ? "Macro" : macro->fileName.c_str());
        m_macroName->setColor(ccc3(255, 255, 255));
    }
    else {
        m_macroName->setString("No macro loaded");
        m_macroName->setColor(ccc3(180, 180, 180));
    }
    // keep long file names inside the popup
    m_macroName->setScale(0.4f);
    float maxW = 250.f;
    if (m_macroName->getContentSize().width * 0.4f > maxW) {
        m_macroName->setScale(maxW / m_macroName->getContentSize().width);
    }

    m_macroInfo->setString(s.macroInfoLine().c_str());
    m_warning->setString(s.levelMismatchWarning().c_str());

    for (int i = 0; i < 4; ++i) {
        bool selected = s.engine.mode() == modeFromTag(i);
        m_modeSprites[i]->updateBGImage(selected ? "GJ_button_01.png" : "GJ_button_04.png");
    }

    for (int i = 0; i < 6; ++i) {
        m_values[i]->setString(stepValueText(i).c_str());
    }
}

void PracticePanel::onMode(CCObject* sender) {
    auto& s = mpr::Session::get();
    auto mode = modeFromTag(static_cast<CCNode*>(sender)->getTag());

    if (mode != mpr::Mode::Off && !s.engine.macro()) {
        FLAlertLayer::create("No macro", "Load a macro first.", "OK")->show();
        return;
    }
    s.setMode(mode);

    if (mode == mpr::Mode::Watch || mode == mpr::Mode::Trace) {
        FLAlertLayer::create(
            "Restart needed",
            "The macro drives the game from the start of the level.\n"
            "Press <cy>Restart Level</c> to begin.",
            "OK"
        )->show();
    }
    this->refresh();
}

void PracticePanel::onStep(CCObject* sender) {
    auto& s = mpr::Session::get();
    int tag = static_cast<CCNode*>(sender)->getTag();
    int index = tag / 2;
    int dir = (tag % 2) ? 1 : -1;

    auto cfg = s.engine.config();
    switch (index) {
        case StepMaxEarly: cfg.maxEarly = std::clamp(cfg.maxEarly + dir, 1, 60); break;
        case StepWindow: cfg.window = std::clamp(cfg.window + dir, 1, 60); break;
        case StepOffset: cfg.frameOffset = std::clamp(cfg.frameOffset + dir, -10, 10); break;
        case StepTps: {
            int i = tpsIndex(static_cast<int>(cfg.gameTps));
            i = std::clamp(i + dir, 0, static_cast<int>(kTpsValues.size()) - 1);
            cfg.gameTps = static_cast<double>(kTpsValues[i]);
            break;
        }
        case StepMacroVol: s.ui.macroVolume = std::clamp(s.ui.macroVolume + dir * 10, 0, 100); break;
        case StepUserVol: s.ui.userVolume = std::clamp(s.ui.userVolume + dir * 10, 0, 100); break;
        default: break;
    }
    s.pushConfig(cfg);
    this->refresh();

    // let the player hear the new volume
    if (index == StepMacroVol) s.playClick(true, false);
    if (index == StepUserVol) s.playClick(true, true);
}

void PracticePanel::onToggle(CCObject* sender) {
    auto& s = mpr::Session::get();
    auto* toggle = static_cast<CCMenuItemToggler*>(sender);
    // the toggler flips *after* the callback, so the new state is the opposite of isToggled()
    bool value = !toggle->isToggled();
    switch (toggle->getTag()) {
        case ToggleMacroSounds: s.ui.macroSounds = value; break;
        case ToggleUserSounds: s.ui.userSounds = value; break;
        case ToggleLane: s.ui.showLane = value; break;
        case ToggleDots: s.ui.showDots = value; break;
        default: break;
    }
    s.saveSettings();
}

void PracticePanel::onLoad(CCObject*) {
    geode::utils::file::FilePickOptions options;
    options.filters.push_back(geode::utils::file::FilePickOptions::Filter{
        .description = "GDR2 macros",
        .files = {"*.gdr2", "*.gdr"},
    });

    geode::async::spawn(
        geode::utils::file::pick(geode::utils::file::PickMode::OpenFile, options),
        [self = Ref(this)](geode::utils::file::PickResult result) {
            if (!result.isOk()) {
                FLAlertLayer::create("Could not open file", "The file picker failed.", "OK")->show();
                return;
            }
            auto picked = std::move(result).unwrap();
            if (!picked.has_value()) return; // cancelled
            self->applyLoadedMacro(*picked);
        }
    );
}

void PracticePanel::applyLoadedMacro(std::filesystem::path const& path) {
    auto& s = mpr::Session::get();
    std::string error;
    if (!s.loadMacroFile(path, error)) {
        FLAlertLayer::create("Could not load macro", error, "OK")->show();
        return;
    }
    this->refresh();
}

void PracticePanel::onUnload(CCObject*) {
    mpr::Session::get().clearMacro();
    this->refresh();
}

void PracticePanel::onRestart(CCObject*) {
    auto pause = m_pause;
    this->onClose(nullptr);
    if (pause) pause->onRestartFull(nullptr);
}

void PracticePanel::onLastResults(CCObject*) {
    auto& s = mpr::Session::get();
    if (!s.engine.active()) {
        FLAlertLayer::create("No results", "Pick a mode and play a run first.", "OK")->show();
        return;
    }
    // live results of the current attempt (or the last finished one)
    auto data = s.lastResults.value_or(s.makeResults(false));
    ResultsPopup::create(data)->show();
}
