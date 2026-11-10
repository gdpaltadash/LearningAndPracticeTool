#pragma once

#include <Geode/Geode.hpp>

#include "../core/Engine.hpp"

/// In-level overlay: a scrolling "click lane" at the bottom of the screen,
/// a live accuracy readout, and dots inside the level where macro clicks happen.
class MacroHud : public cocos2d::CCNode {
public:
    static MacroHud* create();
    ~MacroHud() override;

    /// Draw the in-level dots on this layer (the PlayLayer's object layer).
    void attachWorld(cocos2d::CCNode* objectLayer);

    void onNewAttempt();
    void onUser(mpr::UserResult const& result);

    void update(float dt) override;

private:
    bool init() override;

    void drawLane(mpr::Engine const& engine, double nowFrame);
    void drawWorld(mpr::Engine const& engine, cocos2d::CCPoint playerPos, double nowFrame);
    void refreshStatus(mpr::Engine const& engine, int64_t reached);

    cocos2d::CCDrawNode* m_lane = nullptr;
    cocos2d::CCDrawNode* m_world = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_flash = nullptr;
    float m_flashTimer = 0.f;
    float m_statusTimer = 0.f;

    /// press -> release pairs (indices into engine.inputs()), rebuilt when the macro changes
    struct Pair { size_t down; size_t up; };
    std::vector<Pair> m_pairs;
    void const* m_pairsFor = nullptr;
    size_t m_pairsSize = 0;
    void rebuildPairs(mpr::Engine const& engine);
};
