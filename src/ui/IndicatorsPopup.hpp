#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../Session.hpp"

/// Click indicator settings: visibility, colors, opacity and the hold line.
class IndicatorsPopup : public geode::Popup {
protected:
    cocos2d::CCLabelBMFont* m_opacityValue = nullptr;
    cocos2d::CCLabelBMFont* m_holdOpacityValue = nullptr;
    cocos2d::CCNode* m_swatches[3] = {nullptr, nullptr, nullptr};

    bool init();

    void addToggle(int tag, char const* name, float y, bool value);
    void addStepper(int tag, char const* name, float y);
    void addColorButton(int tag, char const* name, float x);
    void drawSwatch(int tag);
    void refresh();

    void onToggle(cocos2d::CCObject*);
    void onStep(cocos2d::CCObject*);
    void onColor(cocos2d::CCObject*);

public:
    static IndicatorsPopup* create();
};
