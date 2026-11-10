#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../Session.hpp"

#include <array>

/// The gear popups: settings of Click Indicators or of the Predictive Buffer.
class SettingsPopup : public geode::Popup {
public:
    enum class Kind { Indicators, Buffer };

protected:
    Kind m_kind = Kind::Indicators;
    std::array<cocos2d::CCLabelBMFont*, 3> m_values{};
    std::array<cocos2d::CCNode*, 3> m_swatches{};
    std::array<ButtonSprite*, 4> m_corners{};

    bool init(Kind kind);

    void buildIndicators();
    void buildBuffer();
    void addToggle(int tag, char const* name, float y, bool value);
    void addStepper(int tag, char const* name, float y);
    void addColorButton(int tag, char const* name, float x, float y);
    void drawSwatch(int tag);
    void refresh();

    void onToggle(cocos2d::CCObject*);
    void onStep(cocos2d::CCObject*);
    void onColor(cocos2d::CCObject*);
    void onCorner(cocos2d::CCObject*);

public:
    static SettingsPopup* create(Kind kind);
};
