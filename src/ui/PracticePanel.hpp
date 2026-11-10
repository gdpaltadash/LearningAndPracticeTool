#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../Session.hpp"

#include <array>
#include <vector>

/// Main window, opened from the pause menu.
///   left:  macro, practice mode, click indicators and predictive buffer switches (+ gear buttons)
///   right: the settings of the selected practice mode
class PracticePanel : public geode::Popup {
protected:
    geode::Ref<PauseLayer> m_pause;
    cocos2d::CCLabelBMFont* m_macroName = nullptr;
    cocos2d::CCLabelBMFont* m_macroInfo = nullptr;
    cocos2d::CCLabelBMFont* m_hint = nullptr;
    cocos2d::CCLabelBMFont* m_packName = nullptr;
    std::array<ButtonSprite*, 2> m_modeSprites{};
    std::array<cocos2d::CCLabelBMFont*, 6> m_values{};

    /// nodes of the mode-specific area, removed and rebuilt when the mode changes
    std::vector<cocos2d::CCNode*> m_dynamic;

    bool init(PauseLayer* pause);

    void rebuildSettings();
    void addStepper(int index, char const* name, float y);
    void addPackRow(float y);

    void onLoad(cocos2d::CCObject*);
    void onUnload(cocos2d::CCObject*);
    void onMode(cocos2d::CCObject*);
    void onStep(cocos2d::CCObject*);
    void onToggle(cocos2d::CCObject*);
    void onGear(cocos2d::CCObject*);
    void onClickPack(cocos2d::CCObject*);
    void onClickPackReset(cocos2d::CCObject*);
    void onRestart(cocos2d::CCObject*);
    void onLastResults(cocos2d::CCObject*);

    void refresh();
    void applyLoadedMacro(std::filesystem::path const& path);

public:
    static PracticePanel* create(PauseLayer* pause);
};
