#include "Session.hpp"

#include "ui/Hud.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;

namespace mpr {


    Session& Session::get() {
        static Session instance;
        return instance;
    }

    // ---------------------------------------------------------------- settings

    void Session::loadSettings() {
        auto* mod = Mod::get();

        Config cfg;
        cfg.maxEarly = std::clamp(mod->getSavedValue<int>("max-early", 8), 1, 60);
        cfg.window = std::clamp(mod->getSavedValue<int>("window", 8), 1, 60);
        cfg.frameOffset = std::clamp(mod->getSavedValue<int>("frame-offset", 0), -10, 10);
        cfg.gameTps = static_cast<double>(std::clamp(mod->getSavedValue<int>("game-tps", 0), 0, 2000));
        engine.setConfig(cfg);
        buffer.setWindow(cfg.maxEarly);

        ui.enabled = mod->getSavedValue<bool>("enabled", true);
        ui.indicators = mod->getSavedValue<bool>("indicators", true);
        ui.indicatorsWhenIdle = mod->getSavedValue<bool>("indicators-idle", true);
        ui.buffer = mod->getSavedValue<bool>("buffer", false);
        ui.bufferUsesMacro = mod->getSavedValue<bool>("buffer-uses-macro", true);
        ui.bufferCorner = static_cast<Corner>(std::clamp(mod->getSavedValue<int>("buffer-corner", 1), 0, 3));
        ui.startAdjust = std::clamp(mod->getSavedValue<int>("start-adjust", 0), -10, 10);
        ui.indicatorType = static_cast<IndicatorType>(std::clamp(mod->getSavedValue<int>("indicator-type", 2), 0, 2));
        ui.dotSize = std::clamp(mod->getSavedValue<int>("dot-size", 6), 2, 14);
        ui.lineWidth = std::clamp(mod->getSavedValue<int>("line-width", 2), 1, 8);
        ui.macroVolume = std::clamp(mod->getSavedValue<int>("macro-volume", 60), 0, 250);

        auto readColor = [&](char const* key, ccColor3B def) {
            int packed = mod->getSavedValue<int>(key, (def.r << 16) | (def.g << 8) | def.b);
            return ccColor3B{
                static_cast<GLubyte>((packed >> 16) & 0xff),
                static_cast<GLubyte>((packed >> 8) & 0xff),
                static_cast<GLubyte>(packed & 0xff)
            };
        };
        ui.pressColor = readColor("press-color", ui.pressColor);
        ui.releaseColor = readColor("release-color", ui.releaseColor);
        ui.holdColor = readColor("hold-color", ui.holdColor);
        ui.indicatorOpacity = std::clamp(mod->getSavedValue<int>("indicator-opacity", 90), 1, 100);
        ui.holdOpacity = std::clamp(mod->getSavedValue<int>("hold-opacity", 55), 1, 100);
        ui.holdLine = mod->getSavedValue<bool>("hold-line", true);

        auto pack = mod->getSavedValue<std::string>("click-pack", "");
        if (!pack.empty()) setClickPack(std::filesystem::u8path(pack));
    }

    void Session::saveSettings() {
        auto* mod = Mod::get();
        auto const& cfg = engine.config();
        mod->setSavedValue<int>("max-early", cfg.maxEarly);
        mod->setSavedValue<int>("window", cfg.window);
        mod->setSavedValue<int>("frame-offset", cfg.frameOffset);
        mod->setSavedValue<int>("game-tps", static_cast<int>(cfg.gameTps));
        mod->setSavedValue<bool>("enabled", ui.enabled);
        mod->setSavedValue<bool>("indicators", ui.indicators);
        mod->setSavedValue<bool>("indicators-idle", ui.indicatorsWhenIdle);
        mod->setSavedValue<bool>("buffer", ui.buffer);
        mod->setSavedValue<bool>("buffer-uses-macro", ui.bufferUsesMacro);
        mod->setSavedValue<int>("buffer-corner", static_cast<int>(ui.bufferCorner));
        mod->setSavedValue<int>("start-adjust", ui.startAdjust);
        mod->setSavedValue<int>("indicator-type", static_cast<int>(ui.indicatorType));
        mod->setSavedValue<int>("dot-size", ui.dotSize);
        mod->setSavedValue<int>("line-width", ui.lineWidth);
        mod->setSavedValue<int>("macro-volume", ui.macroVolume);
        auto packColor = [](ccColor3B c) { return (c.r << 16) | (c.g << 8) | c.b; };
        mod->setSavedValue<int>("press-color", packColor(ui.pressColor));
        mod->setSavedValue<int>("release-color", packColor(ui.releaseColor));
        mod->setSavedValue<int>("hold-color", packColor(ui.holdColor));
        mod->setSavedValue<int>("indicator-opacity", ui.indicatorOpacity);
        mod->setSavedValue<int>("hold-opacity", ui.holdOpacity);
        mod->setSavedValue<bool>("hold-line", ui.holdLine);
        mod->setSavedValue<std::string>("click-pack", clickPackPath);
    }

    void Session::pushConfig(Config const& cfg) {
        engine.setConfig(cfg);
        buffer.setWindow(cfg.maxEarly);
        if (inLevel) {
            if (auto* pl = PlayLayer::get()) syncStartPos(pl);
        }
        saveSettings();
    }

    void Session::settingsChanged() {
        this->refreshMode();
        if (inLevel) {
            if (auto* pl = PlayLayer::get()) syncStartPos(pl);
        }
        this->saveSettings();
    }

    std::string Session::clickPackName() const {
        if (clickPackPath.empty()) return "Built-in";
        if (packClicks.empty() && packReleases.empty()) return "Empty folder";
        return geode::utils::string::pathToString(std::filesystem::u8path(clickPackPath).filename());
    }

    void Session::beginAttemptNow() {
        if (!inLevel) return;
        if (auto* pl = PlayLayer::get()) {
            engine.beginAttempt(tickOf(pl) - 1, m_shift);
        }
    }

    // ------------------------------------------------------------------- macro

    bool Session::loadMacroFile(std::filesystem::path const& path, std::string& error) {
        auto read = geode::utils::file::readBinary(path);
        if (read.isErr()) {
            error = "Could not open the file.";
            return false;
        }
        std::vector<uint8_t> bytes = std::move(read).unwrap();

        auto result = loadMacro(std::move(bytes), geode::utils::string::pathToString(path.filename()));
        if (!result.ok()) {
            error = result.error;
            return false;
        }

        macroPath = path;
        this->loadOrigin();
        engine.setMacro(result.macro);
        lastResults.reset();
        completeHandled = false;
        m_platformer = result.macro->platformer;

        this->refreshMode();
        if (inLevel) {
            if (auto* pl = PlayLayer::get()) syncStartPos(pl);
        }
        this->loadMacroPath();
        rememberMacroForLevel();
        return true;
    }

    void Session::clearMacro() {
        userMode = Mode::Off;
        engine.setMacro(nullptr);
        this->refreshMode();
        macroPath.clear();
        this->loadMacroPath();
        originX.reset();
        originMode = OriginMode::Auto;
        m_autoLevelStart = false;
        autoNote.clear();
        m_waitOrigin = false;
        lastResults.reset();
        startSync = StartSync::None;
        m_shift = 0;
    }

    void Session::setUserMode(Mode mode) {
        if (mode != Mode::Off && !engine.macro()) mode = Mode::Off;
        userMode = (mode == Mode::Watch || mode == Mode::Trace) ? mode : Mode::Off;
        completeHandled = false;
        this->refreshMode();
        if (inLevel) {
            if (auto* pl = PlayLayer::get()) syncStartPos(pl);
        }
    }

    void Session::refreshMode() {
        Mode eff = Mode::Off;
        if (ui.enabled && engine.macro()) {
            if (userMode == Mode::Watch || userMode == Mode::Trace) eff = userMode;
            else if (ui.buffer && ui.bufferUsesMacro) eff = Mode::EarlyFix;
        }
        engine.setMode(eff);
        buffer.reset();
    }

    bool Session::stateBufferActive() const {
        if (!ui.enabled || !ui.buffer) return false;
        if (engine.macroDrives()) return false;
        // with a macro loaded the buffer aims at it (Mode::EarlyFix) unless told otherwise
        return !(engine.macro() && ui.bufferUsesMacro);
    }

    bool Session::assistActive() const {
        return ui.enabled && (engine.active() || stateBufferActive());
    }

    bool Session::toolUsed() const {
        return ui.enabled && (assistActive() || (ui.indicators && engine.macro() != nullptr));
    }

    bool Session::indicatorsVisible() const {
        if (!ui.enabled || !ui.indicators || !engine.macro()) return false;
        if (engine.macroDrives()) return true;
        return ui.indicatorsWhenIdle;
    }

    int64_t Session::tickOf(GJBaseGameLayer* layer) const {
        return static_cast<int64_t>(std::floor(layer->m_gameState.m_levelTime * tickRate() + 1e-4)) + 1;
    }

    int64_t Session::pathIndexOf(GJBaseGameLayer* layer) {
        return static_cast<int64_t>(std::floor(layer->m_gameState.m_levelTime * 240.0 + 1e-4)) + 1;
    }

    bool Session::playerReady(GJBaseGameLayer* layer, int button, bool p2) const {
        if (button != 1) return true; // platformer left / right never need a target
        PlayerObject* p = (p2 && layer->m_player2) ? layer->m_player2 : layer->m_player1;
        if (!p) return true;
        // these gamemodes act from anywhere
        if (p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing) return true;
        if (p->m_isOnGround || p->m_isOnGround2) return true;
        if (p->m_touchingRings && p->m_touchingRings->count() > 0) return true;
        return false;
    }

    std::string Session::macroInfoLine() const {
        auto const& m = engine.macro();
        if (!m) return "No macro loaded";
        std::string bot = m->botName.empty() ? "unknown bot" : m->botName;
        std::string line = fmt::format("{:.0f} TPS  -  {} inputs  -  {}", m->fps, m->inputs.size(), bot);
        if (m->platformer) line += "  -  platformer";
        return line;
    }

    std::string Session::levelMismatchWarning() const {
        auto const& m = engine.macro();
        if (!m || !inLevel || m_levelId == 0 || m->levelId == 0) return "";
        if (static_cast<int>(m->levelId) != m_levelId) {
            return fmt::format("Warning: macro was recorded on level {} ({}).", m->levelId, m->levelName);
        }
        return "";
    }

    void Session::rememberMacroForLevel() {
        if (m_levelId <= 0 || macroPath.empty()) return;
        Mod::get()->setSavedValue<std::string>(
            fmt::format("macro-for-level-{}", m_levelId),
            geode::utils::string::pathToString(macroPath)
        );
    }

    void Session::tryAutoLoadForLevel(GJGameLevel* level) {
        m_levelId = level ? level->m_levelID.value() : 0;
        if (m_levelId <= 0) return;

        // a macro that is already loaded stays loaded (even across level changes) unless
        // there is a macro remembered for this specific level.
        auto key = fmt::format("macro-for-level-{}", m_levelId);
        auto saved = Mod::get()->getSavedValue<std::string>(key, "");
        if (saved.empty()) return;

        std::filesystem::path path = std::filesystem::u8path(saved);
        if (path == macroPath && engine.macro()) return;

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;

        std::string err;
        if (!loadMacroFile(path, err)) {
            log::warn("Could not auto-load macro {}: {}", saved, err);
        }
    }

    std::string Session::startPosHint() const {
        if (!inLevel || !engine.macro()) return "";
        if (!autoNote.empty()) return autoNote;
        switch (startSync) {
            case StartSync::Uncalibrated:
                return "Play this level from the start once (any mode) so the macro can be synced.";
            case StartSync::Waiting:
                return "The macro starts when you reach its StartPos.";
            case StartSync::Synced:
                return originX ? std::string("Macro synced to its StartPos.")
                               : fmt::format("StartPos synced (macro frame {}).", engine.startProgress());
            default:
                return "";
        }
    }

    // ------------------------------------------------------------------- hooks

    void Session::syncStartPos(PlayLayer* pl, bool atReset) {
        m_shift = 0;
        m_waitOrigin = false;
        startSync = StartSync::None;
        int64_t progress = tickOf(pl) - 1; // what the first step after the reset will count from
        float x = pl->m_player1 ? pl->m_player1->getPositionX()
                                : (pl->m_startPosObject ? pl->m_startPosObject->getPositionX() : 0.f);
        if (atReset) {
            m_hasStartPos = pl->m_startPosObject != nullptr;
            m_spawnX = x;
            if (m_hasStartPos && !m_pathKey.empty()) Mod::get()->setSavedValue<float>("startpos-" + m_pathKey, x);
        }
        bool startPosLevel = pl->m_startPosObject != nullptr;
        this->refreshOriginIdx();

        if (engine.macro() && !m_platformer && ui.enabled) {
            // converts "steps between two path points" into the macro's tick rate
            auto toMacroTicks = [&](int64_t from, int64_t to) {
                return static_cast<int64_t>(std::llround(static_cast<double>(to - from) * tickRate() / 240.0));
            };

            if (!originX) {
                // Macro recorded from the start of the level. Only a StartPos needs a correction:
                // the path (measured while the level was played from the start) says which step the
                // StartPos x corresponds to.
                if (startPosLevel) {
                    if (auto g = path.findIndexAtX(x)) {
                        // the first step after the reset sees the player at x with tick progress + 1
                        m_shift = toMacroTicks(1, *g) + 1 - (progress + 1) + ui.startAdjust;
                        startSync = StartSync::Synced;
                    }
                    else if (originMode == OriginMode::Auto && !m_autoLevelStart && atReset) {
                        // nothing to sync a level-start macro with: assume it was recorded from here
                        originX = x;
                        this->saveOrigin();
                        this->refreshOriginIdx();
                        autoNote = "Auto: macro assumed to start at this StartPos.";
                        m_shift = 1 - (progress + 1) + ui.startAdjust;
                        startSync = StartSync::Synced;
                    }
                    else {
                        startSync = StartSync::Uncalibrated;
                    }
                }
            }
            else {
                // Macro recorded from a StartPos: its frame 0 is the step where the player is at originX.
                float ox = *originX;
                if (std::fabs(x - ox) <= 10.f) {
                    // started from that very StartPos
                    m_shift = 1 - (progress + 1) + ui.startAdjust;
                    startSync = StartSync::Synced;
                }
                else if (x < ox) {
                    // starting before the origin: the macro begins when the player gets there
                    m_waitOrigin = true;
                    m_shift = kNeverShift;
                    startSync = StartSync::Waiting;
                }
                else if (auto gNow = path.findIndexAtX(x); gNow && m_originIdx) {
                    m_shift = toMacroTicks(*m_originIdx, *gNow) + 1 - (progress + 1) + ui.startAdjust;
                    startSync = StartSync::Synced;
                }
                else {
                    startSync = StartSync::Uncalibrated;
                }
            }
        }
        m_lastProgress = progress;
        engine.beginAttempt(progress, m_shift);
    }

    void Session::refreshOriginIdx() {
        m_originIdx.reset();
        if (originX) m_originIdx = path.findIndexAtX(*originX, 12.f);
    }

    void Session::loadOrigin() {
        auto key = geode::utils::string::pathToString(macroPath);
        auto* mod = Mod::get();
        originMode = static_cast<OriginMode>(std::clamp(mod->getSavedValue<int>("origin-mode-" + key, 0), 0, 2));
        float saved = mod->getSavedValue<float>("origin-" + key, -1.f);
        if (saved >= 0.f) originX = saved;
        else originX.reset();
        m_autoLevelStart = mod->getSavedValue<bool>("autols-" + key, false);
        autoNote.clear();
    }

    void Session::saveOrigin() {
        if (macroPath.empty()) return;
        auto key = geode::utils::string::pathToString(macroPath);
        auto* mod = Mod::get();
        mod->setSavedValue<float>("origin-" + key, originX ? *originX : -1.f);
        mod->setSavedValue<int>("origin-mode-" + key, static_cast<int>(originMode));
        mod->setSavedValue<bool>("autols-" + key, m_autoLevelStart);
    }

    void Session::setOriginAuto() {
        originMode = OriginMode::Auto;
        originX.reset();
        m_autoLevelStart = false;
        autoNote.clear();
        this->saveOrigin();
        this->settingsChanged();
    }

    // Auto mode noticed that the current assumption does not fit: switch to the other one.
    void Session::autoCorrect() {
        if (originMode != OriginMode::Auto || macroPath.empty() || !engine.macro()) return;
        auto* mod = Mod::get();
        if (!originX && !m_autoLevelStart) {
            // assumed "starts at the level start" and it does not fit -> a StartPos
            float cand = m_hasStartPos ? m_spawnX : mod->getSavedValue<float>("startpos-" + m_pathKey, -1.f);
            if (cand >= 0.f) {
                originX = cand;
                this->saveOrigin();
                autoNote = "Auto: the macro seems to start at a StartPos. Restart the level.";
            }
        }
        else if (originX && m_hasStartPos && std::fabs(m_spawnX - *originX) <= 10.f && !m_autoLevelStart) {
            // assumed that StartPos and it does not fit -> level start
            originX.reset();
            m_autoLevelStart = true;
            this->saveOrigin();
            autoNote = "Auto: the macro seems to start at the level start. Restart the level.";
        }
    }

    void Session::onDeath() {
        if (!ui.enabled || originMode != OriginMode::Auto || !engine.macroDrives()) return;
        if (engine.shift() <= mpr::kNeverShift / 2) return;
        // a macro that is in sync does not die right after it starts
        if (reached - engine.startProgress() < engine.tps() * 2.5) this->autoCorrect();
    }

    bool Session::setOriginHere() {
        if (!m_hasStartPos || !engine.macro()) return false;
        originMode = OriginMode::StartPos;
        originX = m_spawnX;
        this->saveOrigin();
        this->settingsChanged();
        return true;
    }

    void Session::setOriginLevelStart() {
        originMode = OriginMode::LevelStart;
        m_autoLevelStart = false;
        originX.reset();
        this->saveOrigin();
        this->settingsChanged();
    }

    void Session::onEnterLevel(PlayLayer* pl) {
        inLevel = true;
        completeHandled = false;
        reached = 0;
        m_platformer = pl->m_levelSettings && pl->m_levelSettings->m_platformerMode;
        {
            auto c = engine.config();
            c.ignoreP2 = !(pl->m_levelSettings && pl->m_levelSettings->m_twoPlayerMode);
            engine.setConfig(c);
        }
        tryAutoLoadForLevel(pl->m_level);
        // a macro never starts driving the game by itself
        userMode = Mode::Off;
        this->refreshMode();
        this->loadPath(pl);
        this->syncStartPos(pl, true);

        auto* node = MacroHud::create();
        if (node) {
            hud = node;
            pl->m_uiLayer->addChild(node, 1000);
            node->attachWorld(pl->m_objectLayer);
        }
    }

    void Session::onExitLevel() {
        this->savePath();
        inLevel = false;
        userMode = Mode::Off;
        this->refreshMode();
        hud = nullptr;
    }

    void Session::onResetLevel(PlayLayer* pl) {
        completeHandled = false;
        reached = 0;
        outOfSync = false;
        farMisses = 0;
        autoNote.clear();
        buffer.reset();
        this->syncStartPos(pl, true);
        if (hud) hud->onNewAttempt();
    }

    ResultsData Session::makeResults(bool finished) const {
        ResultsData r;
        r.mode = engine.mode();
        r.summary = engine.summarize(reached, finished);
        r.tps = engine.tps();
        r.macroName = engine.macro() ? engine.macro()->fileName : "";
        r.finished = finished;
        return r;
    }

    void Session::onLevelComplete(PlayLayer*) {
        if (completeHandled) return;
        completeHandled = true;
        if (engine.active()) lastResults = makeResults(true);
        this->savePath();
    }

    void Session::onStep(GJBaseGameLayer* layer) {
        int64_t progress = tickOf(layer);
        m_lastProgress = progress;
        reached = progress - 1 + engine.shift();

        // macro recorded from a StartPos, level started before it: begin once the player gets there
        if (m_waitOrigin && originX && layer->m_player1 && layer->m_player1->getPositionX() >= *originX - 1.f) {
            m_waitOrigin = false;
            m_shift = 1 - progress + ui.startAdjust; // this step is macro progress 1
            startSync = StartSync::Synced;
            engine.beginAttempt(progress - 1, m_shift);
            reached = progress - 1 + engine.shift();
        }

        if (!ui.enabled || m_platformer || !layer->m_player1) return;
        if (layer->m_player1->m_isDead) return;
        float nan = std::numeric_limits<float>::quiet_NaN();
        float x2 = nan, y2 = nan;
        if (layer->m_gameState.m_isDualMode && layer->m_player2) {
            x2 = layer->m_player2->getPositionX();
            y2 = layer->m_player2->getPositionY();
        }

        // Where the macro takes the player, by macro tick: works from a StartPos too.
        if (engine.macroDrives() && engine.shift() > mpr::kNeverShift / 2 && !m_mpathKey.empty()) {
            mpath.record(
                progress + engine.shift(), layer->m_player1->getPositionX(), layer->m_player1->getPositionY(),
                x2, y2, true
            );
        }

        // Measure the path while the level is played from its start (nothing to do with the mode).
        if (layer->m_startPosObject) return;
        bool authoritative = engine.macroDrives() && engine.shift() > mpr::kNeverShift / 2;
        path.record(
            pathIndexOf(layer), layer->m_player1->getPositionX(), layer->m_player1->getPositionY(), x2, y2,
            authoritative
        );

        // do not lose a long first run if the game is closed abruptly
        if ((path.dirty() || mpath.dirty()) && ++m_sinceSave >= 3000) this->savePath();
    }

    void Session::onInjected(GJBaseGameLayer*, Due const& due) {
        // click sound for macro-driven inputs (Watch / Trace)
        if (due.fromMacro) playClick(due.down);
    }

    // ------------------------------------------------------------------ path

    std::filesystem::path Session::pathFile() const {
        return Mod::get()->getSaveDir() / "paths" / (m_pathKey + ".bin");
    }

    void Session::loadPath(PlayLayer* pl) {
        path.clear();
        path.markClean();
        m_sinceSave = 0;
        m_pathKey.clear();
        if (!pl->m_level) return;

        int id = pl->m_level->m_levelID.value();
        if (id > 0) {
            m_pathKey = fmt::format("L{}", id);
        }
        else {
            // local / editor levels: the name is all there is
            uint64_t h = 1469598103934665603ull;
            for (unsigned char c : std::string(pl->m_level->m_levelName)) h = (h ^ c) * 1099511628211ull;
            m_pathKey = fmt::format("N{:016x}", h);
        }

        auto read = geode::utils::file::readBinary(pathFile());
        if (read.isOk()) {
            if (!path.deserialize(read.unwrap())) path.clear();
            path.markClean();
        }
        this->loadMacroPath();
    }

    std::filesystem::path Session::macroPathFile() const {
        return Mod::get()->getSaveDir() / "paths" / (m_mpathKey + ".bin");
    }

    void Session::loadMacroPath() {
        if (!m_mpathKey.empty() && mpath.dirty() && mpath.size() > 0) {
            std::error_code ec;
            std::filesystem::create_directories(macroPathFile().parent_path(), ec);
            (void)geode::utils::file::writeBinary(macroPathFile(), mpath.serialize());
        }
        mpath.clear();
        mpath.markClean();
        m_mpathKey.clear();
        if (m_pathKey.empty() || macroPath.empty()) return;

        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : geode::utils::string::pathToString(macroPath)) h = (h ^ c) * 1099511628211ull;
        m_mpathKey = fmt::format("{}.m{:016x}", m_pathKey, h);

        auto read = geode::utils::file::readBinary(macroPathFile());
        if (read.isOk()) {
            if (!mpath.deserialize(read.unwrap())) mpath.clear();
            mpath.markClean();
        }
    }

    void Session::savePath() {
        if (!m_mpathKey.empty() && mpath.dirty() && mpath.size() > 0) {
            std::error_code ec;
            std::filesystem::create_directories(macroPathFile().parent_path(), ec);
            if (geode::utils::file::writeBinary(macroPathFile(), mpath.serialize()).isOk()) mpath.markClean();
        }
        if (m_pathKey.empty() || !path.dirty() || path.size() == 0) return;
        std::error_code ec;
        std::filesystem::create_directories(pathFile().parent_path(), ec);
        auto bytes = path.serialize();
        auto res = geode::utils::file::writeBinary(pathFile(), bytes);
        if (res.isOk()) path.markClean();
        m_sinceSave = 0;
    }

    std::optional<cocos2d::CCPoint> Session::pathPointFor(int64_t macroFrame, bool p2) const {
        // an input with frame F is applied by the processCommands call whose progress is F + 1
        if (auto pt = mpath.nearest(macroFrame - engine.config().frameOffset + 1, 6)) {
            if (p2 && !std::isnan(pt->x2)) return cocos2d::CCPoint(pt->x2, pt->y2);
            return cocos2d::CCPoint(pt->x1, pt->y1);
        }
        int64_t base = 1;
        if (originX) {
            if (!m_originIdx) return std::nullopt;
            base = *m_originIdx;
        }
        auto idx = base + static_cast<int64_t>(std::llround(static_cast<double>(macroFrame) * 240.0 / tickRate()));
        auto pt = path.nearest(idx);
        if (!pt) return std::nullopt;
        if (p2 && !std::isnan(pt->x2)) return cocos2d::CCPoint(pt->x2, pt->y2);
        return cocos2d::CCPoint(pt->x1, pt->y1);
    }

    // -------------------------------------------------------------- click pack

    void Session::scanClickPack() {
        packClicks.clear();
        packReleases.clear();
        if (clickPackPath.empty()) return;

        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path root = fs::u8path(clickPackPath);
        if (!fs::is_directory(root, ec)) return;

        auto lower = [](std::string s) {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };

        size_t seen = 0;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && seen < 5000; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            seen++;

            auto ext = lower(geode::utils::string::pathToString(it->path().extension()));
            if (ext != ".wav" && ext != ".mp3" && ext != ".ogg" && ext != ".flac") continue;

            // judge by the path inside the pack: "release", "releases", "rel_3.wav"...
            auto rel = lower(geode::utils::string::pathToString(fs::relative(it->path(), root, e2)));
            bool isRelease = rel.find("release") != std::string::npos;
            if (!isRelease) {
                auto stem = lower(geode::utils::string::pathToString(it->path().stem()));
                isRelease = stem.rfind("rel", 0) == 0 && stem.rfind("relax", 0) != 0;
            }
            // hold / loop sounds are not single clicks
            if (rel.find("hold") != std::string::npos && !isRelease) continue;
            (isRelease ? packReleases : packClicks).push_back(it->path());
        }
    }

    void Session::setClickPack(std::filesystem::path const& folder) {
        clickPackPath = geode::utils::string::pathToString(folder);
        scanClickPack();
        Mod::get()->setSavedValue<std::string>("click-pack", clickPackPath);
    }

    void Session::resetClickPack() {
        clickPackPath.clear();
        packClicks.clear();
        packReleases.clear();
        Mod::get()->setSavedValue<std::string>("click-pack", "");
    }

    std::string Session::clickPackLabel() const {
        if (clickPackPath.empty()) return "Click pack: built-in";
        if (packClicks.empty() && packReleases.empty()) return "Click pack: no sounds found (using built-in)";
        auto name = geode::utils::string::pathToString(std::filesystem::u8path(clickPackPath).filename());
        return fmt::format("Click pack: {} ({} clicks, {} releases)", name, packClicks.size(), packReleases.size());
    }

    // ------------------------------------------------------------------- audio

    void Session::playClick(bool down) {
        if (ui.macroVolume <= 0) return;
        float volume = static_cast<float>(ui.macroVolume) / 100.f;

        auto& list = down ? packClicks : packReleases;
        std::string path;
        if (!list.empty()) {
            path = geode::utils::string::pathToString(list[static_cast<size_t>(rand()) % list.size()]);
        }
        else if (down || packClicks.empty()) {
            // a pack without releases is fine: releases are just silent then
            if (!down && !packClicks.empty()) return;
            path = geode::utils::string::pathToString(
                Mod::get()->getResourcesDir() / (down ? "click.wav" : "release.wav")
            );
        }
        else {
            return;
        }
        FMODAudioEngine::sharedEngine()->playEffect(path, 1.f, 0.f, volume);
    }

}
