#include "ResultsPopup.hpp"

using namespace geode::prelude;

namespace {
    struct Row {
        std::string label;
        std::string value;
    };

    std::string framesWithMs(double frames, double tps, bool sign) {
        double ms = frames * 1000.0 / tps;
        if (sign) return fmt::format("{:+.1f}f ({:+.0f} ms)", frames, ms);
        return fmt::format("{:.1f}f ({:.0f} ms)", frames, ms);
    }
}

bool ResultsPopup::init(mpr::ResultsData const& data) {
    auto const& s = data.summary;

    std::vector<Row> rows;
    std::string title;

    switch (data.mode) {
        case mpr::Mode::Watch:
            title = "Macro Finished";
            rows.push_back({"Macro", data.macroName});
            rows.push_back({"Mode", "Watch (nothing was saved)"});
            break;

        case mpr::Mode::EarlyFix:
        case mpr::Mode::Trace: {
            bool fix = data.mode == mpr::Mode::EarlyFix;
            title = fix ? "Early Fix Results" : "Trace Results";

            rows.push_back({fix ? "Timing accuracy" : "Accuracy", fmt::format("{:.1f}%", s.accuracy)});
            rows.push_back({"Inputs hit", fmt::format("{} / {}", s.matched, s.total)});
            rows.push_back({"Perfect (0f)", std::to_string(s.perfect)});
            rows.push_back({"Early", std::to_string(s.early)});
            rows.push_back({"Late", std::to_string(s.late)});
            rows.push_back({"Missed", std::to_string(s.missed)});
            rows.push_back({"Extra clicks", std::to_string(s.extra)});
            if (fix) {
                rows.push_back({
                    "Clicks fixed",
                    s.fixed > 0 ? fmt::format("{} (avg {:.1f}f early)", s.fixed, s.fixedAvgFrames) : "0"
                });
            }
            if (s.matched > 0) {
                rows.push_back({"Average offset", framesWithMs(s.meanOffset, data.tps, true)});
                rows.push_back({"Average error", framesWithMs(s.meanAbs, data.tps, false)});
                rows.push_back({"Worst early", fmt::format("{}f", s.worstEarly)});
                rows.push_back({"Worst late", fmt::format("{:+}f", s.worstLate)});
            }
            break;
        }

        default:
            title = "Results";
            break;
    }

    float rowH = 17.f;
    float height = 84.f + rowH * static_cast<float>(rows.size()) + 34.f;
    float width = 330.f;

    if (!Popup::init(width, height, "square01_001.png")) return false;
    this->setTitle(title);

    float y = height - 58.f;
    for (auto const& row : rows) {
        auto label = CCLabelBMFont::create(row.label.c_str(), "chatFont.fnt");
        label->setScale(0.62f);
        label->setColor(ccc3(210, 210, 210));
        m_mainLayer->addChildAtPosition(label, Anchor::BottomLeft, ccp(26.f, y), ccp(0.f, 0.5f));

        auto value = CCLabelBMFont::create(row.value.c_str(), "chatFont.fnt");
        value->setScale(0.62f);
        m_mainLayer->addChildAtPosition(value, Anchor::BottomRight, ccp(-26.f, y), ccp(1.f, 0.5f));

        y -= rowH;
    }

    auto okSpr = ButtonSprite::create("OK", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    auto okBtn = CCMenuItemSpriteExtra::create(okSpr, this, menu_selector(ResultsPopup::onClose));
    m_buttonMenu->addChildAtPosition(okBtn, Anchor::Bottom, ccp(0.f, 26.f));

    return true;
}

ResultsPopup* ResultsPopup::create(mpr::ResultsData const& data) {
    auto ret = new ResultsPopup();
    if (ret->init(data)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
