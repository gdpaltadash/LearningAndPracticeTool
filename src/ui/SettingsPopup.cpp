#include "SettingsPopup.hpp"

#include <Geode/ui/ColorPickPopup.hpp>

using namespace geode::prelude;

namespace {
    // toggle tags
    enum { TogHold = 2, TogIdle = 3, TogUsesMacro = 4 };
    // stepper tags (value label index)
    // fine steps at the low end so very faint indicators can be picked
    int stepFor(int v, int dir) { return (v < 10 || (v == 10 && dir < 0)) ? 1 : 5; }
    enum { StepOpacity = 0, StepHoldOpacity = 1, StepWindow = 2, StepDotSize = 3, StepLineWidth = 4 };
    enum { ColPress = 0, ColRelease = 1, ColHold = 2 };

    ccColor3B& colorRef(int tag) {
        auto& ui = mpr::Session::get().ui;
        switch (tag) {
            case ColPress: return ui.pressColor;
            case ColRelease: return ui.releaseColor;
            default: return ui.holdColor;
        }
    }

    constexpr float kW = 340.f;
    constexpr float kHIndicators = 292.f;
    constexpr float kHBuffer = 236.f;
}

SettingsPopup* SettingsPopup::create(Kind kind) {
    auto ret = new SettingsPopup();
    if (ret->init(kind)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SettingsPopup::init(Kind kind) {
    if (!Popup::init(kW, kind == Kind::Indicators ? kHIndicators : kHBuffer, "square01_001.png")) return false;
    m_kind = kind;

    auto title = CCLabelBMFont::create(
        kind == Kind::Indicators ? "Click Indicators" : "Predictive Buffer", "bigFont.fnt"
    );
    title->setScale(0.6f);
    m_mainLayer->addChildAtPosition(title, Anchor::Top, ccp(0.f, -26.f));

    if (kind == Kind::Indicators) this->buildIndicators();
    else this->buildBuffer();

    this->refresh();
    return true;
}

// ----------------------------------------------------------------------------------------

void SettingsPopup::buildIndicators() {
    auto& ui = mpr::Session::get().ui;
    // indicator type: Lane = scrolling lane on screen, Dot = a dot in the level where the input happens
    char const* typeNames[3] = {"Lane", "Dot", "Both"};
    for (int i = 0; i < 3; ++i) {
        auto spr = ButtonSprite::create(typeNames[i], 74, true, "goldFont.fnt", "GJ_button_04.png", 24.f, 0.6f);
        m_types[i] = spr;
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onType));
        btn->setTag(i);
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, ccp(80.f + 90.f * i, -58.f));
    }

    this->addToggle(TogHold, "Hold line (press to release)", 92.f, ui.holdLine);
    this->addToggle(TogIdle, "Also show with no mode active", 114.f, ui.indicatorsWhenIdle);

    this->addStepper(StepOpacity, "Indicator opacity", 142.f);
    this->addStepper(StepHoldOpacity, "Hold line opacity", 164.f);
    this->addStepper(StepDotSize, "Dot size (in level)", 186.f);
    this->addStepper(StepLineWidth, "Hold line width (in level)", 208.f);

    this->addColorButton(ColPress, "Press", 70.f, 244.f);
    this->addColorButton(ColRelease, "Release", 170.f, 244.f);
    this->addColorButton(ColHold, "Hold line", 270.f, 244.f);
}

void SettingsPopup::buildBuffer() {
    auto& s = mpr::Session::get();
    this->addStepper(StepWindow, "Buffer window (frames)", 58.f);
    this->addToggle(TogUsesMacro, "Aim at the loaded macro", 84.f, s.ui.bufferUsesMacro);

    auto cornerLabel = CCLabelBMFont::create("End screen watermark corner", "chatFont.fnt");
    cornerLabel->setScale(0.62f);
    m_mainLayer->addChildAtPosition(cornerLabel, Anchor::TopLeft, ccp(26.f, -114.f), ccp(0.f, 0.5f));

    // one corner at a time
    char const* names[4] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    for (int i = 0; i < 4; ++i) {
        auto spr = ButtonSprite::create(names[i], 92, true, "goldFont.fnt", "GJ_button_04.png", 22.f, 0.5f);
        m_corners[i] = spr;
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onCorner));
        btn->setTag(i);
        float x = 100.f + (i % 2) * 124.f;
        float y = 138.f + (i / 2) * 26.f;
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, ccp(x + 12.f, -y));
    }

    auto info = CCLabelBMFont::create(
        "Without a macro, a click made while you can't act yet (cube in the air, no orb) is held "
        "until you can, up to the window. With a macro it delays early clicks to the macro's frame.",
        "chatFont.fnt", kW - 40.f, kCCTextAlignmentLeft
    );
    info->setScale(0.5f);
    info->setColor(ccc3(180, 180, 180));
    m_mainLayer->addChildAtPosition(info, Anchor::Bottom, ccp(0.f, 26.f));
}

void SettingsPopup::addToggle(int tag, char const* name, float y, bool value) {
    auto toggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(SettingsPopup::onToggle), 0.55f);
    toggle->setTag(tag);
    toggle->toggle(value);
    m_buttonMenu->addChildAtPosition(toggle, Anchor::TopLeft, ccp(38.f, -y));

    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.62f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(58.f, -y), ccp(0.f, 0.5f));
}

void SettingsPopup::addStepper(int tag, char const* name, float y) {
    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.62f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(28.f, -y), ccp(0.f, 0.5f));

    float ax = kW - 116.f;
    auto l = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
    l->setScale(0.72f);
    auto lb = CCMenuItemSpriteExtra::create(l, this, menu_selector(SettingsPopup::onStep));
    lb->setTag(tag * 2);
    m_buttonMenu->addChildAtPosition(lb, Anchor::TopLeft, ccp(ax, -y));

    auto value = CCLabelBMFont::create("", "bigFont.fnt");
    value->setScale(0.34f);
    m_values[tag] = value;
    m_mainLayer->addChildAtPosition(value, Anchor::TopLeft, ccp(ax + 32.f, -y));

    auto r = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
    r->setScale(0.72f);
    auto rb = CCMenuItemSpriteExtra::create(r, this, menu_selector(SettingsPopup::onStep));
    rb->setTag(tag * 2 + 1);
    m_buttonMenu->addChildAtPosition(rb, Anchor::TopLeft, ccp(ax + 64.f, -y));
}

void SettingsPopup::addColorButton(int tag, char const* name, float x, float y) {
    // a drawn rectangle keeps this independent from sprite frame names
    auto swatch = CCNode::create();
    swatch->setContentSize(ccp(56.f, 26.f));
    auto draw = CCDrawNode::create();
    draw->setTag(1);
    swatch->addChild(draw);
    m_swatches[tag] = swatch;
    this->drawSwatch(tag);

    auto btn = CCMenuItemSpriteExtra::create(swatch, this, menu_selector(SettingsPopup::onColor));
    btn->setTag(tag);
    m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, ccp(x, -y));

    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(x, -(y + 24.f)));
}

void SettingsPopup::drawSwatch(int tag) {
    auto* draw = static_cast<CCDrawNode*>(m_swatches[tag]->getChildByTag(1));
    if (!draw) return;
    auto c = colorRef(tag);
    draw->clear();
    CCPoint pts[4] = {ccp(0.f, 0.f), ccp(56.f, 0.f), ccp(56.f, 26.f), ccp(0.f, 26.f)};
    draw->drawPolygon(
        pts, 4,
        ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f},
        1.5f, ccColor4F{1.f, 1.f, 1.f, 0.9f}
    );
}

void SettingsPopup::refresh() {
    auto& s = mpr::Session::get();
    if (m_values[StepOpacity]) m_values[StepOpacity]->setString(fmt::format("{}%", s.ui.indicatorOpacity).c_str());
    if (m_values[StepHoldOpacity]) m_values[StepHoldOpacity]->setString(fmt::format("{}%", s.ui.holdOpacity).c_str());
    if (m_values[StepWindow]) m_values[StepWindow]->setString(fmt::format("{}f", s.engine.config().maxEarly).c_str());
    if (m_values[StepDotSize]) m_values[StepDotSize]->setString(fmt::format("{}", s.ui.dotSize).c_str());
    if (m_values[StepLineWidth]) m_values[StepLineWidth]->setString(fmt::format("{}", s.ui.lineWidth).c_str());
    for (int i = 0; i < 3; ++i) {
        if (!m_types[i]) continue;
        bool sel = static_cast<int>(s.ui.indicatorType) == i;
        m_types[i]->updateBGImage(sel ? "GJ_button_01.png" : "GJ_button_04.png");
    }
    for (int i = 0; i < 4; ++i) {
        if (!m_corners[i]) continue;
        bool sel = static_cast<int>(s.ui.bufferCorner) == i;
        m_corners[i]->updateBGImage(sel ? "GJ_button_01.png" : "GJ_button_04.png");
    }
}

// ----------------------------------------------------------------------------------------

void SettingsPopup::onToggle(CCObject* sender) {
    auto& s = mpr::Session::get();
    auto* t = static_cast<CCMenuItemToggler*>(sender);
    bool value = !t->isToggled(); // flips after the callback
    switch (t->getTag()) {
        case TogHold: s.ui.holdLine = value; break;
        case TogIdle: s.ui.indicatorsWhenIdle = value; break;
        case TogUsesMacro: s.ui.bufferUsesMacro = value; break;
    }
    s.settingsChanged();
}

void SettingsPopup::onStep(CCObject* sender) {
    auto& s = mpr::Session::get();
    int tag = static_cast<CCNode*>(sender)->getTag();
    int dir = (tag % 2) ? 1 : -1;
    switch (tag / 2) {
        case StepOpacity: s.ui.indicatorOpacity = std::clamp(s.ui.indicatorOpacity + dir * stepFor(s.ui.indicatorOpacity, dir), 1, 100); break;
        case StepHoldOpacity: s.ui.holdOpacity = std::clamp(s.ui.holdOpacity + dir * stepFor(s.ui.holdOpacity, dir), 1, 100); break;
        case StepDotSize: s.ui.dotSize = std::clamp(s.ui.dotSize + dir, 2, 14); break;
        case StepLineWidth: s.ui.lineWidth = std::clamp(s.ui.lineWidth + dir, 1, 8); break;
        case StepWindow: {
            auto cfg = s.engine.config();
            cfg.maxEarly = std::clamp(cfg.maxEarly + dir, 1, 60);
            s.pushConfig(cfg);
            break;
        }
    }
    s.saveSettings();
    this->refresh();
}

void SettingsPopup::onType(CCObject* sender) {
    auto& s = mpr::Session::get();
    s.ui.indicatorType = static_cast<mpr::IndicatorType>(std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 2));
    s.saveSettings();
    this->refresh();
}

void SettingsPopup::onCorner(CCObject* sender) {
    auto& s = mpr::Session::get();
    s.ui.bufferCorner = static_cast<mpr::Corner>(std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 3));
    s.saveSettings();
    this->refresh();
}

void SettingsPopup::onColor(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    auto popup = ColorPickPopup::create(colorRef(tag));
    popup->setCallback([self = Ref(this), tag](ccColor4B const& c) {
        colorRef(tag) = ccColor3B{c.r, c.g, c.b};
        mpr::Session::get().saveSettings();
        self->drawSwatch(tag);
    });
    popup->show();
}
