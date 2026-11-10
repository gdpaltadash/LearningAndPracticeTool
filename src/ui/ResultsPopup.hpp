#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../Session.hpp"

class ResultsPopup : public geode::Popup {
protected:
    bool init(mpr::ResultsData const& data);

public:
    static ResultsPopup* create(mpr::ResultsData const& data);
};
