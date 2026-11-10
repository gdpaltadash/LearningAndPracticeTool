#include "Session.hpp"

#include "ui/Hud.hpp"
#include "ui/ResultsPopup.hpp"

#include <cctype>
#include <cstdlib>

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
        positions.assign(engine.inputs().size(), std::nullopt);
        if (inLevel) {
            if (auto* pl = PlayLayer::get()) {
                engine.beginAttempt(static_cast<int64_t>(pl->m_gameState.m_currentProgress));
            }
        }
        saveSettings();
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
        positions.assign(engine.inputs().size(), std::nullopt);
        lastResults.reset();
        completeHandled = false;

        if (inLevel) {
            if (auto* pl = PlayLayer::get()) {
                engine.beginAttempt(static_cast<int64_t>(pl->m_gameState.m_currentProgress));
            }
        }
        rememberMacroForLevel();
        return true;
    }

    void Session::clearMacro() {
        engine.setMode(Mode::Off);
        engine.setMacro(nullptr);
        positions.clear();
        macroPath.clear();
        lastResults.reset();
    }

    void Session::setMode(Mode mode) {
        if (mode != Mode::Off && !engine.macro()) mode = Mode::Off;
        engine.setMode(mode);
        completeHandled = false;
        if (inLevel) {
            if (auto* pl = PlayLayer::get()) {
                engine.beginAttempt(static_cast<int64_t>(pl->m_gameState.m_currentProgress));
            }
        }
    }

    std::string Session::macroInfoLine() const {
        auto const& m = engine.macro();
        if (!m) return "Load a macro to start.";
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

    // ------------------------------------------------------------------- hooks

    void Session::onEnterLevel(PlayLayer* pl) {
        inLevel = true;
        completeHandled = false;
        reached = 0;
        tryAutoLoadForLevel(pl->m_level);
        // a macro for another level never keeps driving the game by accident
        engine.setMode(Mode::Off);
        engine.beginAttempt(0);

        auto* node = MacroHud::create();
        if (node) {
            hud = node;
            pl->m_uiLayer->addChild(node, 1000);
            node->attachWorld(pl->m_objectLayer);
        }
    }

    void Session::onExitLevel() {
        inLevel = false;
        engine.setMode(Mode::Off);
        hud = nullptr;
    }

    void Session::onResetLevel(PlayLayer* pl) {
        completeHandled = false;
        reached = 0;
        engine.beginAttempt(static_cast<int64_t>(pl->m_gameState.m_currentProgress));
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

    bool Session::onLevelComplete(PlayLayer* pl) {
        if (!locksStats()) return false;
        if (completeHandled) return true;
        completeHandled = true;

        auto results = makeResults(true);
        lastResults = results;

        // Stop the game behind the popup so nothing keeps running (and nothing gets saved).
        pl->pauseGame(false);
        ResultsPopup::create(results)->show();
        return true;
    }

    void Session::onStep(GJBaseGameLayer* layer) {
        reached = static_cast<int64_t>(layer->m_gameState.m_currentProgress) - 1;
    }

    void Session::onInjected(GJBaseGameLayer* layer, Due const& due) {
        // click sound for macro-driven inputs (Watch / Trace)
        if (due.fromMacro) {
            playClick(due.down);

            if (due.macroIdx >= 0 && static_cast<size_t>(due.macroIdx) < positions.size()) {
                PlayerObject* p = (due.p2 && layer->m_player2) ? layer->m_player2 : layer->m_player1;
                if (p) positions[due.macroIdx] = p->getPosition();
            }
        }
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
