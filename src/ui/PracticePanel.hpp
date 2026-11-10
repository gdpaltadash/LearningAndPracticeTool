#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../Session.hpp"

#include <array>
#include <vector>

/// Main settings popup, opened from the pause menu. Compact: only the settings
/// that matter for the selected mode are shown.
class PracticePanel : public geode::Popup {
protected:
    geode::Ref<PauseLayer> m_pause;
    cocos2d::CCLabelBMFont* m_macroName = nullptr;
    cocos2d::CCLabelBMFont* m_macroInfo = nullptr;
    cocos2d::CCLabelBMFont* m_warning = nullptr;
    cocos2d::CCLabelBMFont* m_packLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hint = nullptr;
    std::array<ButtonSprite*, 4> m_modeSprites{};
    std::array<cocos2d::CCLabelBMFont*, 5> m_values{};

    /// nodes of the mode-specific area, removed and rebuilt when the mode changes
    std::vector<cocos2d::CCNode*> m_dynamic;

    bool init(PauseLayer* pause);

    void rebuildSettings();
    void addStepper(int index, char const* name, float y);

    void onLoad(cocos2d::CCObject*);
    void onUnload(cocos2d::CCObject*);
    void onMode(cocos2d::CCObject*);
    void onStep(cocos2d::CCObject*);
    void onClickPack(cocos2d::CCObject*);
    void onClickPackReset(cocos2d::CCObject*);
    void onIndicators(cocos2d::CCObject*);
    void onRestart(cocos2d::CCObject*);
    void onLastResults(cocos2d::CCObject*);

    void refresh();
    void applyLoadedMacro(std::filesystem::path const& path);

public:
    static PracticePanel* create(PauseLayer* pause);
};
