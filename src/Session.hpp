#pragma once

#include <Geode/Geode.hpp>

#include "core/Buffer.hpp"
#include "core/Engine.hpp"
#include "core/PathTable.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class MacroHud;

namespace mpr {

    enum class Corner { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3 };

    struct UiSettings {
        bool enabled = true;          // master switch of the whole mod

        // ---- click indicators ----
        bool indicators = true;       // feature on/off (needs a loaded macro)
        bool showLane = true;
        bool showDots = true;
        bool indicatorsWhenIdle = true; // also show them while no mode (Watch / Trace) is active
        cocos2d::ccColor3B pressColor = {80, 242, 128};
        cocos2d::ccColor3B releaseColor = {255, 115, 102};
        int indicatorOpacity = 90;    // percent
        bool holdLine = true;         // line from press to release (shows how long to hold)
        cocos2d::ccColor3B holdColor = {255, 255, 255};
        int holdOpacity = 55;         // percent

        // ---- predictive input buffer ----
        bool buffer = false;
        bool bufferUsesMacro = true;  // with a macro loaded, aim at the macro instead of the player state
        bool bufferWatermark = true;  // show it on the end screen
        Corner bufferCorner = Corner::TopRight;

        // ---- Watch / Trace ----
        int macroVolume = 60;         // percent, 0 = no click sounds
        int startAdjust = 0;          // fine tune of the StartPos sync, in frames
    };

    /// Data shown by the results popup.
    struct ResultsData {
        Mode mode = Mode::Off;
        Summary summary;
        double tps = 240.0;
        std::string macroName;
        bool finished = false;
    };

    enum class StartSync { None, Synced, Uncalibrated };

    /// Global state of the mod. Everything runs on the main thread.
    class Session {
    public:
        static Session& get();

        Engine engine;
        InputBuffer buffer;
        PathTable path;
        UiSettings ui;
        MacroHud* hud = nullptr;

        Mode userMode = Mode::Off;       // what the player picked: Off / Watch / Trace
        std::filesystem::path macroPath;
        bool inLevel = false;
        bool completeHandled = false;
        int64_t reached = 0;             // last event frame of the current attempt (macro space)
        StartSync startSync = StartSync::None;

        std::optional<ResultsData> lastResults;

        // ---- settings / persistence ----
        void loadSettings();
        void saveSettings();
        void pushConfig(Config const& cfg);

        // ---- macro ----
        bool loadMacroFile(std::filesystem::path const& path, std::string& error);
        void clearMacro();
        void setUserMode(Mode mode);
        /// Works out the mode the engine really runs from: picked mode, buffer, master switch.
        void refreshMode();
        std::string macroInfoLine() const;
        std::string levelMismatchWarning() const;
        std::string startPosHint() const;

        // ---- state ----
        bool hasMacro() const { return engine.macro() != nullptr; }
        /// The predictive buffer works from the player's state (no macro to aim at).
        bool stateBufferActive() const;
        /// Something that changes how the game plays is running.
        bool assistActive() const;
        /// Something of the mod is in use (also guidance only): drives the end screen watermark.
        bool toolUsed() const;
        bool indicatorsVisible() const;
        /// True while inputs/stats are being controlled by the mod.
        bool locksStats() const { return inLevel && assistActive() && PlayLayer::get() != nullptr; }
        bool playerReady(GJBaseGameLayer* layer, int button, bool p2) const;

        // ---- hooks ----
        void onEnterLevel(PlayLayer* pl);
        void onExitLevel();
        void onResetLevel(PlayLayer* pl);
        void onLevelComplete(PlayLayer* pl);
        void onStep(GJBaseGameLayer* layer);
        void onInjected(GJBaseGameLayer* layer, Due const& due);

        ResultsData makeResults(bool finished) const;

        // ---- player path (measured passively from the level start) ----
        std::optional<cocos2d::CCPoint> pathPointFor(int64_t macroFrame, bool p2) const;

        // ---- click pack (one pack for every macro) ----
        std::string clickPackPath;                 // empty = built-in sounds
        std::vector<std::filesystem::path> packClicks;
        std::vector<std::filesystem::path> packReleases;
        void setClickPack(std::filesystem::path const& folder);
        void resetClickPack();
        std::string clickPackLabel() const;

        // ---- audio (only macro-driven inputs make sound) ----
        void playClick(bool down);

        /// Restart the engine's attempt from the game's current progress.
        void beginAttemptNow();

        /// Call after changing anything in `ui` that affects behaviour (enable, buffer...).
        void settingsChanged();
        std::string clickPackName() const;

    private:
        void scanClickPack();
        void rememberMacroForLevel();
        void tryAutoLoadForLevel(GJGameLevel* level);
        void syncStartPos(PlayLayer* pl);
        void loadPath(PlayLayer* pl);
        void savePath();
        std::filesystem::path pathFile() const;

        int m_levelId = 0;
        std::string m_pathKey;
        bool m_platformer = false;
        int64_t m_shift = 0;
        int m_sinceSave = 0;
    };

}
