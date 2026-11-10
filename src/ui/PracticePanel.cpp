#include "PracticePanel.hpp"

#include "ResultsPopup.hpp"
#include "SettingsPopup.hpp"

#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace {
    enum Step {
        StepWindow = 1,
        StepOffset = 2,
        StepTps = 3,
        StepVolume = 4,
        StepStartAdjust = 5,
    };

    enum Tag { TagEnable = 1, TagIndicators = 2, TagBuffer = 3 };

    constexpr float kPopupW = 540.f;
    constexpr float kPopupH = 290.f;

    // dark settings panel (right side)
    constexpr float kPanelX = 244.f;
    constexpr float kPanelTop = 86.f;     // distance from the top of the popup
    constexpr float kPanelW = 284.f;
    constexpr float kPanelH = 160.f;

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
            case StepWindow: return fmt::format("{}f", c.window);
            case StepOffset: return fmt::format("{:+}f", c.frameOffset);
            case StepTps: return c.gameTps <= 0.0 ? std::string("Auto") : fmt::format("{:.0f}", c.gameTps);
            case StepVolume: return s.ui.macroVolume <= 0 ? std::string("Off") : fmt::format("{}%", s.ui.macroVolume);
            case StepStartAdjust: return fmt::format("{:+}f", s.ui.startAdjust);
        }
        return "";
    }

    mpr::Mode modeFromTag(int tag) {
        switch (tag) {
            case 2: return mpr::Mode::Watch;
            case 3: return mpr::Mode::Trace;
            default: return mpr::Mode::Off;
        }
    }

    /// Shrinks a label until it fits `maxWidth`.
    void fitWidth(CCLabelBMFont* label, float baseScale, float maxWidth) {
        label->setScale(baseScale);
        float w = label->getContentSize().width * baseScale;
        if (w > maxWidth && w > 0.f) label->setScale(baseScale * maxWidth / w);
    }

    CCNode* gearSprite() {
        CCNode* spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
        if (!spr) spr = ButtonSprite::create("Set", 26, true, "goldFont.fnt", "GJ_button_04.png", 22.f, 0.5f);
        else spr->setScale(0.62f);
        return spr;
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
    if (!Popup::init(kPopupW, kPopupH, "square01_001.png")) return false;
    m_pause = pause;

    auto& s = mpr::Session::get();
    auto top = [&](CCNode* n, float x, float y, CCPoint anchor = ccp(0.f, 0.5f)) {
        m_mainLayer->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y), anchor);
    };
    auto topBtn = [&](CCNode* n, float x, float y) {
        m_buttonMenu->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y));
    };

    // ---- header: title, logo, master switch ---------------------------------------------
    auto title = CCLabelBMFont::create("Practice Tool", "bigFont.fnt");
    fitWidth(title, 0.8f, 280.f);
    top(title, 44.f, 26.f);

    if (auto logo = CCSprite::create("macro-button.png"_spr)) {
        logo->setScale(30.f / std::max(logo->getContentSize().width, 1.f));
        top(logo, 352.f, 26.f, ccp(0.5f, 0.5f));
    }

    auto enableLabel = CCLabelBMFont::create("Enable Mod", "goldFont.fnt");
    enableLabel->setScale(0.5f);
    top(enableLabel, 490.f, 26.f, ccp(1.f, 0.5f));

    auto enable = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PracticePanel::onToggle), 0.7f);
    enable->setTag(TagEnable);
    enable->toggle(s.ui.enabled);
    topBtn(enable, 512.f, 26.f);

    // ---- macro ---------------------------------------------------------------------------
    auto macroHeading = CCLabelBMFont::create("Load Macro", "bigFont.fnt");
    macroHeading->setScale(0.46f);
    top(macroHeading, 20.f, 62.f);

    auto loadSpr = ButtonSprite::create("Load", 62, true, "goldFont.fnt", "GJ_button_01.png", 26.f, 0.6f);
    auto loadBtn = CCMenuItemSpriteExtra::create(loadSpr, this, menu_selector(PracticePanel::onLoad));
    topBtn(loadBtn, 52.f, 86.f);

    auto unloadSpr = ButtonSprite::create("Unload", 62, true, "goldFont.fnt", "GJ_button_06.png", 26.f, 0.55f);
    auto unloadBtn = CCMenuItemSpriteExtra::create(unloadSpr, this, menu_selector(PracticePanel::onUnload));
    topBtn(unloadBtn, 134.f, 86.f);

    m_macroName = CCLabelBMFont::create("", "bigFont.fnt");
    top(m_macroName, 22.f, 110.f);

    m_macroInfo = CCLabelBMFont::create("", "chatFont.fnt");
    m_macroInfo->setColor(ccc3(200, 200, 200));
    top(m_macroInfo, 22.f, 124.f);

    // ---- practice mode ---------------------------------------------------------------------
    auto modeHeading = CCLabelBMFont::create("Practice Mode", "bigFont.fnt");
    modeHeading->setScale(0.46f);
    top(modeHeading, 20.f, 148.f);

    char const* modeNames[2] = {"Watch", "Trace"};
    for (int i = 0; i < 2; ++i) {
        auto spr = ButtonSprite::create(modeNames[i], 132, true, "goldFont.fnt", "GJ_button_04.png", 30.f, 0.7f);
        m_modeSprites[i] = spr;
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PracticePanel::onMode));
        btn->setTag(2 + i);
        topBtn(btn, 92.f, 172.f + 34.f * i);
    }

    // ---- click indicators / predictive buffer: label + switch + gear, aligned -----------------
    auto addFeature = [&](char const* name, int tag, float y, bool value) {
        auto label = CCLabelBMFont::create(name, "bigFont.fnt");
        fitWidth(label, 0.42f, 140.f);
        top(label, 20.f, y);

        auto toggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PracticePanel::onToggle), 0.62f);
        toggle->setTag(tag);
        toggle->toggle(value);
        topBtn(toggle, 178.f, y);

        auto gear = CCMenuItemSpriteExtra::create(gearSprite(), this, menu_selector(PracticePanel::onGear));
        gear->setTag(tag);
        topBtn(gear, 214.f, y);
    };
    addFeature("Click Indicators", TagIndicators, 244.f, s.ui.indicators);
    addFeature("Predictive Buffer", TagBuffer, 268.f, s.ui.buffer);

    // ---- settings panel (right) ------------------------------------------------------------
    auto heading = CCLabelBMFont::create("Settings:", "bigFont.fnt");
    heading->setScale(0.5f);
    top(heading, kPanelX + kPanelW / 2.f, 64.f, ccp(0.5f, 0.5f));

    auto bg = CCScale9Sprite::create("square02_001.png");
    bg->setContentSize(ccp(kPanelW, kPanelH));
    bg->setColor(ccc3(0, 18, 38));
    bg->setOpacity(200);
    m_mainLayer->addChildAtPosition(
        bg, Anchor::TopLeft, ccp(kPanelX + kPanelW / 2.f, -(kPanelTop + kPanelH / 2.f))
    );

    m_hint = CCLabelBMFont::create("", "chatFont.fnt", kPanelW - 20.f, kCCTextAlignmentLeft);
    m_hint->setScale(0.5f);
    m_hint->setColor(ccc3(255, 190, 60));
    m_hint->setAnchorPoint(ccp(0.f, 1.f));
    top(m_hint, kPanelX + 10.f, kPanelTop + kPanelH - 8.f, ccp(0.f, 0.f));

    // ---- bottom buttons ------------------------------------------------------------------
    auto restartSpr = ButtonSprite::create("Restart Level", 124, true, "goldFont.fnt", "GJ_button_01.png", 28.f, 0.6f);
    auto restartBtn = CCMenuItemSpriteExtra::create(restartSpr, this, menu_selector(PracticePanel::onRestart));
    topBtn(restartBtn, kPanelX + 72.f, 268.f);

    auto resultsSpr = ButtonSprite::create("Last Results", 124, true, "goldFont.fnt", "GJ_button_02.png", 28.f, 0.6f);
    auto resultsBtn = CCMenuItemSpriteExtra::create(resultsSpr, this, menu_selector(PracticePanel::onLastResults));
    topBtn(resultsBtn, kPanelX + kPanelW - 72.f, 268.f);

    this->rebuildSettings();
    this->refresh();
    return true;
}

void PracticePanel::rebuildSettings() {
    for (auto* n : m_dynamic) n->removeFromParent();
    m_dynamic.clear();
    m_values.fill(nullptr);
    m_packName = nullptr;
    m_originSprites = {};

    auto mode = mpr::Session::get().userMode;
    std::vector<std::pair<int, char const*>> rows;
    switch (mode) {
        case mpr::Mode::Watch:
            rows = {{StepOffset, "Macro frame offset"}, {StepTps, "Game TPS"},
                    {StepVolume, "Click volume"}, {StepStartAdjust, "StartPos adjust"}};
            break;
        case mpr::Mode::Trace:
            rows = {{StepWindow, "Scoring window"}, {StepOffset, "Macro frame offset"}, {StepTps, "Game TPS"},
                    {StepVolume, "Click volume"}, {StepStartAdjust, "StartPos adjust"}};
            break;
        default:
            break;
    }

    if (rows.empty()) {
        auto hint = CCLabelBMFont::create(
            "Pick Watch or Trace to see its settings.", "chatFont.fnt", kPanelW - 40.f, kCCTextAlignmentCenter
        );
        hint->setScale(0.62f);
        hint->setColor(ccc3(190, 190, 190));
        m_mainLayer->addChildAtPosition(
            hint, Anchor::TopLeft, ccp(kPanelX + kPanelW / 2.f, -(kPanelTop + kPanelH / 2.f - 6.f))
        );
        m_dynamic.push_back(hint);
        return;
    }

    float y = kPanelTop + 14.f;
    this->addOriginRow(y);
    y += 19.f;
    for (auto const& [index, name] : rows) {
        this->addStepper(index, name, y);
        y += 19.f;
    }
    this->addPackRow(y);
    this->refresh();
}

void PracticePanel::addStepper(int index, char const* name, float y) {
    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(kPanelX + 12.f, -y), ccp(0.f, 0.5f));
    m_dynamic.push_back(label);

    float ax = kPanelX + kPanelW - 76.f;

    auto leftSpr = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
    leftSpr->setScale(0.72f);
    auto leftBtn = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(PracticePanel::onStep));
    leftBtn->setTag(index * 2);
    m_buttonMenu->addChildAtPosition(leftBtn, Anchor::TopLeft, ccp(ax, -y));
    m_dynamic.push_back(leftBtn);

    auto value = CCLabelBMFont::create("", "bigFont.fnt");
    value->setScale(0.34f);
    m_values[index] = value;
    m_mainLayer->addChildAtPosition(value, Anchor::TopLeft, ccp(ax + 31.f, -y));
    m_dynamic.push_back(value);

    auto rightSpr = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
    rightSpr->setScale(0.72f);
    auto rightBtn = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(PracticePanel::onStep));
    rightBtn->setTag(index * 2 + 1);
    m_buttonMenu->addChildAtPosition(rightBtn, Anchor::TopLeft, ccp(ax + 62.f, -y));
    m_dynamic.push_back(rightBtn);
}

void PracticePanel::addOriginRow(float y) {
    auto label = CCLabelBMFont::create("Macro starts at", "chatFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(kPanelX + 12.f, -y), ccp(0.f, 0.5f));
    m_dynamic.push_back(label);

    char const* names[3] = {"Auto", "Level start", "StartPos"};
    int widths[3] = {40, 66, 56};
    float centers[3] = {125.f, 182.f, 247.f};
    for (int i = 0; i < 3; ++i) {
        auto spr = ButtonSprite::create(names[i], widths[i], true, "goldFont.fnt", "GJ_button_04.png", 20.f, 0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PracticePanel::onOrigin));
        btn->setTag(i);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, ccp(kPanelX + centers[i], -y));
        m_dynamic.push_back(btn);
        m_originSprites[i] = spr;
    }
}

void PracticePanel::onOrigin(CCObject* sender) {
    auto& s = mpr::Session::get();
    int tag = static_cast<CCNode*>(sender)->getTag();
    s.autoNote.clear();
    if (tag == 0) {
        s.setOriginAuto();
    }
    else if (tag == 2) {
        if (!s.setOriginHere()) {
            FLAlertLayer::create(
                "StartPos needed",
                "Start the level from the <cy>StartPos</c> the macro was recorded from, "
                "then open this window and press <cy>StartPos</c> again.",
                "OK"
            )->show();
        }
    }
    else {
        s.setOriginLevelStart();
    }
    this->rebuildSettings();
    this->refresh();
}

void PracticePanel::addPackRow(float y) {
    auto label = CCLabelBMFont::create("Click pack", "chatFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(kPanelX + 12.f, -y), ccp(0.f, 0.5f));
    m_dynamic.push_back(label);

    auto selSpr = ButtonSprite::create("Select", 50, true, "goldFont.fnt", "GJ_button_02.png", 22.f, 0.5f);
    auto sel = CCMenuItemSpriteExtra::create(selSpr, this, menu_selector(PracticePanel::onClickPack));
    m_buttonMenu->addChildAtPosition(sel, Anchor::TopLeft, ccp(kPanelX + 112.f, -y));
    m_dynamic.push_back(sel);

    auto defSpr = ButtonSprite::create("Default", 50, true, "goldFont.fnt", "GJ_button_04.png", 22.f, 0.5f);
    auto def = CCMenuItemSpriteExtra::create(defSpr, this, menu_selector(PracticePanel::onClickPackReset));
    m_buttonMenu->addChildAtPosition(def, Anchor::TopLeft, ccp(kPanelX + 184.f, -y));
    m_dynamic.push_back(def);

    m_packName = CCLabelBMFont::create("", "chatFont.fnt");
    m_packName->setColor(ccc3(200, 200, 200));
    m_mainLayer->addChildAtPosition(m_packName, Anchor::TopLeft, ccp(kPanelX + 220.f, -y), ccp(0.f, 0.5f));
    m_dynamic.push_back(m_packName);
}

void PracticePanel::refresh() {
    auto& s = mpr::Session::get();
    auto const& macro = s.engine.macro();

    if (macro) {
        m_macroName->setString(macro->fileName.empty() ? "Macro" : macro->fileName.c_str());
        m_macroName->setColor(ccc3(255, 255, 255));
    }
    else {
        m_macroName->setString("No macro");
        m_macroName->setColor(ccc3(180, 180, 180));
    }
    fitWidth(m_macroName, 0.38f, 205.f);

    m_macroInfo->setString(s.macroInfoLine().c_str());
    fitWidth(m_macroInfo, 0.55f, 210.f);

    std::string hint = s.levelMismatchWarning();
    auto start = s.startPosHint();
    if (!start.empty()) hint += (hint.empty() ? "" : "\n") + start;
    m_hint->setString(hint.c_str());

    for (int i = 0; i < 2; ++i) {
        bool selected = s.userMode == modeFromTag(2 + i);
        m_modeSprites[i]->updateBGImage(selected ? "GJ_button_01.png" : "GJ_button_04.png");
    }

    for (size_t i = 0; i < m_values.size(); ++i) {
        if (m_values[i]) m_values[i]->setString(stepValueText(static_cast<int>(i)).c_str());
    }
    for (int i = 0; i < 3; ++i) {
        if (!m_originSprites[i]) continue;
        bool selected = static_cast<int>(s.originMode) == i;
        m_originSprites[i]->updateBGImage(selected ? "GJ_button_01.png" : "GJ_button_04.png");
    }
    if (m_packName) {
        m_packName->setString(s.clickPackName().c_str());
        fitWidth(m_packName, 0.55f, kPanelX + kPanelW - 6.f - (kPanelX + 220.f));
    }
}

void PracticePanel::onMode(CCObject* sender) {
    auto& s = mpr::Session::get();
    auto mode = modeFromTag(static_cast<CCNode*>(sender)->getTag());

    if (mode == s.userMode) mode = mpr::Mode::Off; // pressing the selected mode again turns it off
    if (mode != mpr::Mode::Off && !s.engine.macro()) {
        FLAlertLayer::create("No macro", "Load a macro first.", "OK")->show();
        return;
    }
    if (!s.ui.enabled && mode != mpr::Mode::Off) {
        FLAlertLayer::create("Mod disabled", "Turn on <cy>Enable Mod</c> first.", "OK")->show();
        return;
    }
    s.setUserMode(mode);
    this->rebuildSettings();
    this->refresh();

    if (mode != mpr::Mode::Off) {
        FLAlertLayer::create(
            "Restart needed",
            "The macro drives the game from the start of the level.\n"
            "Press <cy>Restart Level</c> to begin.",
            "OK"
        )->show();
    }
}

void PracticePanel::onToggle(CCObject* sender) {
    auto& s = mpr::Session::get();
    auto* toggle = static_cast<CCMenuItemToggler*>(sender);
    // the toggler flips *after* the callback, so the new state is the opposite of isToggled()
    bool value = !toggle->isToggled();
    switch (toggle->getTag()) {
        case TagEnable: s.ui.enabled = value; break;
        case TagIndicators: s.ui.indicators = value; break;
        case TagBuffer: s.ui.buffer = value; break;
        default: break;
    }
    if (!s.ui.enabled) s.userMode = mpr::Mode::Off;
    s.settingsChanged();
    this->rebuildSettings();
    this->refresh();
}

void PracticePanel::onGear(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    SettingsPopup::create(tag == TagBuffer ? SettingsPopup::Kind::Buffer : SettingsPopup::Kind::Indicators)->show();
}

void PracticePanel::onStep(CCObject* sender) {
    auto& s = mpr::Session::get();
    int tag = static_cast<CCNode*>(sender)->getTag();
    int index = tag / 2;
    int dir = (tag % 2) ? 1 : -1;

    auto cfg = s.engine.config();
    switch (index) {
        case StepWindow: cfg.window = std::clamp(cfg.window + dir, 1, 60); break;
        case StepOffset: cfg.frameOffset = std::clamp(cfg.frameOffset + dir, -10, 10); break;
        case StepTps: {
            int i = tpsIndex(static_cast<int>(cfg.gameTps));
            i = std::clamp(i + dir, 0, static_cast<int>(kTpsValues.size()) - 1);
            cfg.gameTps = static_cast<double>(kTpsValues[i]);
            break;
        }
        case StepVolume: s.ui.macroVolume = std::clamp(s.ui.macroVolume + dir * 10, 0, 250); break;
        case StepStartAdjust: s.ui.startAdjust = std::clamp(s.ui.startAdjust + dir, -10, 10); break;
        default: break;
    }
    s.pushConfig(cfg);
    this->refresh();

    // let the player hear the new volume
    if (index == StepVolume) s.playClick(true);
}

void PracticePanel::onClickPack(CCObject*) {
    geode::async::spawn(
        geode::utils::file::pick(geode::utils::file::PickMode::OpenFolder, {}),
        [self = Ref(this)](geode::utils::file::PickResult result) {
            if (!result.isOk()) {
                FLAlertLayer::create("Could not open folder", "The folder picker failed.", "OK")->show();
                return;
            }
            auto picked = std::move(result).unwrap();
            if (!picked.has_value()) return; // cancelled

            auto& s = mpr::Session::get();
            s.setClickPack(*picked);
            if (s.packClicks.empty() && s.packReleases.empty()) {
                FLAlertLayer::create(
                    "Empty click pack",
                    "No .wav / .mp3 / .ogg / .flac sounds were found in that folder.\n"
                    "Release sounds are files or folders with <cy>release</c> in the name.",
                    "OK"
                )->show();
            }
            else {
                s.playClick(true);
            }
            self->refresh();
        }
    );
}

void PracticePanel::onClickPackReset(CCObject*) {
    mpr::Session::get().resetClickPack();
    this->refresh();
}

void PracticePanel::onLoad(CCObject*) {
    geode::utils::file::FilePickOptions options;
    // macros of other bots (MegaHack, zBot, xdBot, TASBot, Echo, plain text...) are detected by content,
    // so no filter is applied: extensions vary too much between bots.
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
    this->rebuildSettings();
    this->refresh();
}

void PracticePanel::onUnload(CCObject*) {
    mpr::Session::get().clearMacro();
    this->rebuildSettings();
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
        FLAlertLayer::create("No results", "Pick Watch or Trace and play a run first.", "OK")->show();
        return;
    }
    // live results of the current attempt (or the last finished one)
    auto data = s.lastResults.value_or(s.makeResults(false));
    ResultsPopup::create(data)->show();
}
