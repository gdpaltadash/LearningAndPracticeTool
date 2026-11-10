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
        ui.bufferWatermark = mod->getSavedValue<bool>("buffer-watermark", true);
        ui.bufferCorner = static_cast<Corner>(std::clamp(mod->getSavedValue<int>("buffer-corner", 1), 0, 3));
        ui.startAdjust = std::clamp(mod->getSavedValue<int>("start-adjust", 0), -10, 10);
        ui.showLane = mod->getSavedValue<bool>("show-lane", true);
        ui.showDots = mod->getSavedValue<bool>("show-dots", true);
        ui.macroVolume = std::clamp(mod->getSavedValue<int>("macro-volume", 60), 0, 100);

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
        ui.indicatorOpacity = std::clamp(mod->getSavedValue<int>("indicator-opacity", 90), 5, 100);
        ui.holdOpacity = std::clamp(mod->getSavedValue<int>("hold-opacity", 55), 5, 100);
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
        mod->setSavedValue<bool>("buffer-watermark", ui.bufferWatermark);
        mod->setSavedValue<int>("buffer-corner", static_cast<int>(ui.bufferCorner));
        mod->setSavedValue<int>("start-adjust", ui.startAdjust);
        mod->setSavedValue<bool>("show-lane", ui.showLane);
        mod->setSavedValue<bool>("show-dots", ui.showDots);
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
            engine.beginAttempt(static_cast<int64_t>(pl->m_gameState.m_currentProgress), m_shift);
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
        engine.setMacro(result.macro);
        lastResults.reset();
        completeHandled = false;
        m_platformer = result.macro->platformer;

        this->refreshMode();
        if (inLevel) {
            if (auto* pl = PlayLayer::get()) syncStartPos(pl);
        }
        rememberMacroForLevel();
        return true;
    }

    void Session::clearMacro() {
        userMode = Mode::Off;
        engine.setMacro(nullptr);
        this->refreshMode();
        macroPath.clear();
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
        switch (startSync) {
            case StartSync::Uncalibrated:
                return "StartPos: play this level from the start once (any mode) so the macro can be synced.";
            case StartSync::Synced:
                return fmt::format("StartPos synced (macro frame {}).", engine.startProgress());
            default:
                return "";
        }
    }

    // ------------------------------------------------------------------- hooks

    void Session::syncStartPos(PlayLayer* pl) {
        m_shift = 0;
        startSync = StartSync::None;
        int64_t progress = static_cast<int64_t>(pl->m_gameState.m_currentProgress);

        if (pl->m_startPosObject && engine.macro() && !m_platformer && ui.enabled) {
            // The macro was recorded from the start of the level, so its frames are "ahead" of the
            // game's own step counter when starting from a StartPos. The player path (measured while
            // the level was played from the start) says which step the StartPos x corresponds to.
            float x = pl->m_player1 ? pl->m_player1->getPositionX() : pl->m_startPosObject->getPositionX();
            if (auto g = path.findIndexAtX(x)) {
                // the first step after the reset sees the player at x with progress + 1
                m_shift = *g - (progress + 1) + ui.startAdjust;
                startSync = StartSync::Synced;
            }
            else {
                startSync = StartSync::Uncalibrated;
            }
        }
        engine.beginAttempt(progress, m_shift);
    }

    void Session::onEnterLevel(PlayLayer* pl) {
        inLevel = true;
        completeHandled = false;
        reached = 0;
        m_platformer = pl->m_levelSettings && pl->m_levelSettings->m_platformerMode;
        tryAutoLoadForLevel(pl->m_level);
        // a macro never starts driving the game by itself
        userMode = Mode::Off;
        this->refreshMode();
        this->loadPath(pl);
        this->syncStartPos(pl);

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
        buffer.reset();
        this->syncStartPos(pl);
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
        int64_t progress = static_cast<int64_t>(layer->m_gameState.m_currentProgress);
        reached = progress - 1 + engine.shift();

        // Measure the path while the level is played from its start (nothing to do with the mode).
        if (!ui.enabled || m_platformer || layer->m_startPosObject || !layer->m_player1) return;
        if (layer->m_player1->m_isDead) return;
        float nan = std::numeric_limits<float>::quiet_NaN();
        float x2 = nan, y2 = nan;
        if (layer->m_gameState.m_isDualMode && layer->m_player2) {
            x2 = layer->m_player2->getPositionX();
            y2 = layer->m_player2->getPositionY();
        }
        path.record(progress, layer->m_player1->getPositionX(), layer->m_player1->getPositionY(), x2, y2);

        // do not lose a long first run if the game is closed abruptly
        if (path.dirty() && ++m_sinceSave >= 3000) this->savePath();
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
    }

    void Session::savePath() {
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
        auto pt = path.at(macroFrame + 1);
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
