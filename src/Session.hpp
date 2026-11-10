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
        bool macroSounds = true;
        bool userSounds = true;
        bool showLane = true;
        bool showDots = true;
        int macroVolume = 60; // percent
        int userVolume = 50;  // percent
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

        // ---- audio ----
        void playClick(bool down, bool user);

    private:
        void rememberMacroForLevel();
        void tryAutoLoadForLevel(GJGameLevel* level);
        int m_levelId = 0;
    };

}
