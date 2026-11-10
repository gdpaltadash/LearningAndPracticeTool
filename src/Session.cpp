#include "Session.hpp"

#include "ui/Hud.hpp"
#include "ui/ResultsPopup.hpp"

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

        ui.macroSounds = mod->getSavedValue<bool>("macro-sounds", true);
        ui.userSounds = mod->getSavedValue<bool>("user-sounds", true);
        ui.showLane = mod->getSavedValue<bool>("show-lane", true);
        ui.showDots = mod->getSavedValue<bool>("show-dots", true);
        ui.macroVolume = std::clamp(mod->getSavedValue<int>("macro-volume", 60), 0, 100);
        ui.userVolume = std::clamp(mod->getSavedValue<int>("user-volume", 50), 0, 100);
    }

    void Session::saveSettings() {
        auto* mod = Mod::get();
        auto const& cfg = engine.config();
        mod->setSavedValue<int>("max-early", cfg.maxEarly);
        mod->setSavedValue<int>("window", cfg.window);
        mod->setSavedValue<int>("frame-offset", cfg.frameOffset);
        mod->setSavedValue<int>("game-tps", static_cast<int>(cfg.gameTps));
        mod->setSavedValue<bool>("macro-sounds", ui.macroSounds);
        mod->setSavedValue<bool>("user-sounds", ui.userSounds);
        mod->setSavedValue<bool>("show-lane", ui.showLane);
        mod->setSavedValue<bool>("show-dots", ui.showDots);
        mod->setSavedValue<int>("macro-volume", ui.macroVolume);
        mod->setSavedValue<int>("user-volume", ui.userVolume);
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
        if (!m) return "Import a GDR2 macro to start.";
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
            playClick(due.down, false);

            if (due.macroIdx >= 0 && static_cast<size_t>(due.macroIdx) < positions.size()) {
                PlayerObject* p = (due.p2 && layer->m_player2) ? layer->m_player2 : layer->m_player1;
                if (p) positions[due.macroIdx] = p->getPosition();
            }
        }
    }

    // ------------------------------------------------------------------- audio

    void Session::playClick(bool down, bool user) {
        if (user ? !ui.userSounds : !ui.macroSounds) return;
        float volume = static_cast<float>(user ? ui.userVolume : ui.macroVolume) / 100.f;
        if (volume <= 0.f) return;

        auto path = geode::utils::string::pathToString(
            Mod::get()->getResourcesDir() / (down ? "click.wav" : "release.wav")
        );
        float speed = user ? 1.12f : 1.f;
        FMODAudioEngine::sharedEngine()->playEffect(path, speed, 0.f, volume);
    }

}
