#pragma once

#include <Geode/Geode.hpp>

#include "core/Engine.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class MacroHud;

namespace mpr {

    struct UiSettings {
        bool showLane = true;
        bool showDots = true;
        int macroVolume = 60; // percent, 0 = no click sounds

        // click indicators (lane + in-level): both are circles, only the color differs
        cocos2d::ccColor3B pressColor = {80, 242, 128};
        cocos2d::ccColor3B releaseColor = {255, 115, 102};
        int indicatorOpacity = 90; // percent

        // line from the press indicator to its release indicator (shows how long to hold)
        bool holdLine = true;
        cocos2d::ccColor3B holdColor = {255, 255, 255};
        int holdOpacity = 55; // percent
    };

    /// Data shown by the results popup.
    struct ResultsData {
        Mode mode = Mode::Off;
        Summary summary;
        double tps = 240.0;
        std::string macroName;
        bool finished = false;
    };

    /// Global state of the mod. Everything runs on the main thread.
    class Session {
    public:
        static Session& get();

        Engine engine;
        UiSettings ui;
        MacroHud* hud = nullptr;

        /// Player position at the moment each macro input was applied (same indexing as engine.inputs()).
        std::vector<std::optional<cocos2d::CCPoint>> positions;

        std::filesystem::path macroPath;
        bool inLevel = false;
        bool completeHandled = false;
        int64_t reached = 0; // last event frame of the current attempt

        std::optional<ResultsData> lastResults;

        // ---- settings / persistence ----
        void loadSettings();
        void saveSettings();
        void pushConfig(Config const& cfg);

        // ---- macro ----
        bool loadMacroFile(std::filesystem::path const& path, std::string& error);
        void clearMacro();
        void setMode(Mode mode);
        std::string macroInfoLine() const;
        std::string levelMismatchWarning() const;

        // ---- state ----
        /// True while inputs/stats are being controlled by the mod.
        bool locksStats() const { return inLevel && engine.active() && PlayLayer::get() != nullptr; }

        // ---- hooks ----
        void onEnterLevel(PlayLayer* pl);
        void onExitLevel();
        void onResetLevel(PlayLayer* pl);
        /// Returns true if the level completion was swallowed (results shown instead).
        bool onLevelComplete(PlayLayer* pl);
        void onStep(GJBaseGameLayer* layer);
        void onInjected(GJBaseGameLayer* layer, Due const& due);

        ResultsData makeResults(bool finished) const;

        // ---- click pack (one pack for every macro) ----
        std::string clickPackPath;                 // empty = built-in sounds
        std::vector<std::filesystem::path> packClicks;
        std::vector<std::filesystem::path> packReleases;
        void setClickPack(std::filesystem::path const& folder);
        void resetClickPack();
        std::string clickPackLabel() const;

        // ---- audio (only macro-driven inputs make sound) ----
        void playClick(bool down);

    private:
        void scanClickPack();
        void rememberMacroForLevel();
        void tryAutoLoadForLevel(GJGameLevel* level);
        int m_levelId = 0;
    };

}
