#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../Session.hpp"

#include <array>

/// Main settings popup, opened from the pause menu.
class PracticePanel : public geode::Popup {
protected:
    geode::Ref<PauseLayer> m_pause;
    cocos2d::CCLabelBMFont* m_macroName = nullptr;
    cocos2d::CCLabelBMFont* m_macroInfo = nullptr;
    cocos2d::CCLabelBMFont* m_warning = nullptr;
    std::array<ButtonSprite*, 4> m_modeSprites{};
    std::array<cocos2d::CCLabelBMFont*, 6> m_values{};
    std::array<CCMenuItemToggler*, 4> m_toggles{};

    bool init(PauseLayer* pause);

    void addStepper(int index, char const* name, float x, float y);
    void addToggle(int index, char const* name, float x, float y, bool value);

    void onLoad(cocos2d::CCObject*);
    void onUnload(cocos2d::CCObject*);
    void onMode(cocos2d::CCObject*);
    void onStep(cocos2d::CCObject*);
    void onToggle(cocos2d::CCObject*);
    void onRestart(cocos2d::CCObject*);
    void onLastResults(cocos2d::CCObject*);

    void refresh();
    void applyLoadedMacro(std::filesystem::path const& path);

public:
    static PracticePanel* create(PauseLayer* pause);
};
