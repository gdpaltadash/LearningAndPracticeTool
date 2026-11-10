#include "IndicatorsPopup.hpp"

#include <Geode/ui/ColorPickPopup.hpp>

using namespace geode::prelude;

namespace {
    enum { TagLane = 0, TagDots = 1, TagHold = 2 };
    enum { StepOpacity = 0, StepHoldOpacity = 1 };
    enum { ColPress = 0, ColRelease = 1, ColHold = 2 };

    ccColor3B& colorRef(int tag) {
        auto& ui = mpr::Session::get().ui;
        switch (tag) {
            case ColPress: return ui.pressColor;
            case ColRelease: return ui.releaseColor;
            default: return ui.holdColor;
        }
    }
}

IndicatorsPopup* IndicatorsPopup::create() {
    auto ret = new IndicatorsPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool IndicatorsPopup::init() {
    if (!Popup::init(300.f, 236.f, "square01_001.png")) return false;
    this->setTitle("Click Indicators");

    auto& ui = mpr::Session::get().ui;

    this->addToggle(TagLane, "Click lane (bottom of screen)", -44.f, ui.showLane);
    this->addToggle(TagDots, "Dots inside the level", -66.f, ui.showDots);
    this->addToggle(TagHold, "Hold line (press to release)", -88.f, ui.holdLine);

    this->addStepper(StepOpacity, "Indicator opacity", -116.f);
    this->addStepper(StepHoldOpacity, "Hold line opacity", -138.f);

    this->addColorButton(ColPress, "Press", -90.f);
    this->addColorButton(ColRelease, "Release", 0.f);
    this->addColorButton(ColHold, "Hold line", 90.f);

    auto note = CCLabelBMFont::create("Tap a color to change it.", "chatFont.fnt");
    note->setScale(0.5f);
    note->setColor(ccc3(180, 180, 180));
    m_mainLayer->addChildAtPosition(note, Anchor::Bottom, ccp(0.f, 16.f));

    this->refresh();
    return true;
}

void IndicatorsPopup::addToggle(int tag, char const* name, float y, bool value) {
    auto toggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(IndicatorsPopup::onToggle), 0.55f);
    toggle->setTag(tag);
    toggle->toggle(value);
    m_buttonMenu->addChildAtPosition(toggle, Anchor::TopLeft, ccp(32.f, y));

    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.62f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(52.f, y), ccp(0.f, 0.5f));
}

void IndicatorsPopup::addStepper(int tag, char const* name, float y) {
    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.62f);
    m_mainLayer->addChildAtPosition(label, Anchor::TopLeft, ccp(22.f, y), ccp(0.f, 0.5f));

    float ax = 190.f;
    auto l = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
    l->setScale(0.75f);
    auto lb = CCMenuItemSpriteExtra::create(l, this, menu_selector(IndicatorsPopup::onStep));
    lb->setTag(tag * 2);
    m_buttonMenu->addChildAtPosition(lb, Anchor::TopLeft, ccp(ax, y));

    auto value = CCLabelBMFont::create("", "bigFont.fnt");
    value->setScale(0.36f);
    (tag == StepOpacity ? m_opacityValue : m_holdOpacityValue) = value;
    m_mainLayer->addChildAtPosition(value, Anchor::TopLeft, ccp(ax + 32.f, y));

    auto r = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
    r->setScale(0.75f);
    auto rb = CCMenuItemSpriteExtra::create(r, this, menu_selector(IndicatorsPopup::onStep));
    rb->setTag(tag * 2 + 1);
    m_buttonMenu->addChildAtPosition(rb, Anchor::TopLeft, ccp(ax + 64.f, y));
}

void IndicatorsPopup::addColorButton(int tag, char const* name, float x) {
    // a plain drawn rectangle keeps this independent from any sprite frame names
    auto swatch = CCNode::create();
    swatch->setContentSize(ccp(54.f, 26.f));
    auto draw = CCDrawNode::create();
    draw->setTag(1);
    swatch->addChild(draw);
    m_swatches[tag] = swatch;
    this->drawSwatch(tag);

    auto btn = CCMenuItemSpriteExtra::create(swatch, this, menu_selector(IndicatorsPopup::onColor));
    btn->setTag(tag);
    m_buttonMenu->addChildAtPosition(btn, Anchor::Center, ccp(x, -34.f));

    auto label = CCLabelBMFont::create(name, "chatFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::Center, ccp(x, -54.f));
}

void IndicatorsPopup::drawSwatch(int tag) {
    auto* draw = static_cast<CCDrawNode*>(m_swatches[tag]->getChildByTag(1));
    if (!draw) return;
    auto c = colorRef(tag);
    draw->clear();
    CCPoint pts[4] = {ccp(0.f, 0.f), ccp(54.f, 0.f), ccp(54.f, 26.f), ccp(0.f, 26.f)};
    draw->drawPolygon(
        pts, 4,
        ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f},
        1.5f, ccColor4F{1.f, 1.f, 1.f, 0.9f}
    );
}

void IndicatorsPopup::refresh() {
    auto& ui = mpr::Session::get().ui;
    m_opacityValue->setString(fmt::format("{}%", ui.indicatorOpacity).c_str());
    m_holdOpacityValue->setString(fmt::format("{}%", ui.holdOpacity).c_str());
}

void IndicatorsPopup::onToggle(CCObject* sender) {
    auto& s = mpr::Session::get();
    auto* t = static_cast<CCMenuItemToggler*>(sender);
    bool value = !t->isToggled(); // flips after the callback
    switch (t->getTag()) {
        case TagLane: s.ui.showLane = value; break;
        case TagDots: s.ui.showDots = value; break;
        case TagHold: s.ui.holdLine = value; break;
    }
    s.saveSettings();
}

void IndicatorsPopup::onStep(CCObject* sender) {
    auto& s = mpr::Session::get();
    int tag = static_cast<CCNode*>(sender)->getTag();
    int dir = (tag % 2) ? 1 : -1;
    if (tag / 2 == StepOpacity) s.ui.indicatorOpacity = std::clamp(s.ui.indicatorOpacity + dir * 5, 5, 100);
    else s.ui.holdOpacity = std::clamp(s.ui.holdOpacity + dir * 5, 5, 100);
    s.saveSettings();
    this->refresh();
}

void IndicatorsPopup::onColor(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    auto popup = ColorPickPopup::create(colorRef(tag));
    popup->setCallback([self = Ref(this), tag](ccColor4B const& c) {
        colorRef(tag) = ccColor3B{c.r, c.g, c.b};
        mpr::Session::get().saveSettings();
        self->drawSwatch(tag);
    });
    popup->show();
}
