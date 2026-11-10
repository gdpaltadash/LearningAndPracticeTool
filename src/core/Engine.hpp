#pragma once

// Pure C++ (no Geode / Cocos headers) so it can be unit-tested off-game.
//
// Frame convention (same as zBot / GDR bots):
//   * an input recorded while currentProgress == p has frame p - 1
//   * an input with frame F is applied by a bot at the first processCommands
//     call where F < currentProgress.
// Everything in here works in those "event frames".

#include "Macro.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace mpr {

    enum class Mode { Off = 0, EarlyFix = 1, Watch = 2, Trace = 3 };

    inline const char* modeName(Mode m) {
        switch (m) {
            case Mode::Off: return "Off";
            case Mode::EarlyFix: return "Early Fix";
            case Mode::Watch: return "Watch";
            case Mode::Trace: return "Trace";
        }
        return "?";
    }

    struct Config {
        int maxEarly = 8;        // Early Fix: max frames an early input can be delayed
        int window = 8;          // match / scoring window, in frames (both directions)
        int frameOffset = 0;     // shifts the macro in time (compat with other bots' conventions)
        double gameTps = 0.0;    // physics ticks per second of the running game (0 = same as the macro)
    };

    /// An input that must now be forwarded to the game.
    struct Due {
        int64_t frame = 0;
        int button = 1;
        bool p2 = false;
        bool down = false;
        int macroIdx = -1;      // index into the macro (Watch / Trace), -1 for delayed user inputs
        bool fromMacro = false;
    };

    /// What happened to one real user input.
    struct UserResult {
        bool swallow = false;   // do NOT forward it to the game right now
        bool delayed = false;   // Early Fix: the engine will forward it later
        bool matched = false;   // paired with a macro input
        bool extra = false;     // no macro input to pair with
        int macroIdx = -1;
        int offset = 0;         // user frame - macro frame (negative = early, positive = late)
        int applyDelay = 0;     // frames the input was delayed by (Early Fix)
        int64_t userFrame = 0;
        int button = 1;
        bool p2 = false;
        bool down = false;
    };

    struct Summary {
        int total = 0;      // matched + missed
        int matched = 0;
        int missed = 0;
        int extra = 0;
        int perfect = 0;    // offset == 0
        int early = 0;      // offset < 0
        int late = 0;       // offset > 0
        int fixed = 0;      // Early Fix: inputs that were delayed
        double fixedAvgFrames = 0.0;
        double accuracy = 0.0;   // 0..100
        double meanOffset = 0.0; // signed, frames
        double meanAbs = 0.0;    // frames
        int worstEarly = 0;      // most negative offset
        int worstLate = 0;       // most positive offset
    };

    class Engine {
    public:
        struct Eff {
            int64_t frame = 0;
            int button = 1;
            bool p2 = false;
            bool down = false;
            bool skipped = false;  // happened before the attempt's start point
            bool matched = false;
            bool injected = false;
            int offset = 0;
            bool fixed = false;
            int fixedBy = 0;
        };

        // ---- setup -------------------------------------------------------

        void setMacro(std::shared_ptr<Macro const> macro) {
            m_macro = std::move(macro);
            rebuild();
        }

        std::shared_ptr<Macro const> const& macro() const { return m_macro; }

        void setConfig(Config const& cfg) {
            bool changed = cfg.frameOffset != m_cfg.frameOffset || cfg.gameTps != m_cfg.gameTps;
            m_cfg = cfg;
            if (changed) rebuild();
        }

        Config const& config() const { return m_cfg; }

        void setMode(Mode mode) { m_mode = mode; }
        Mode mode() const { return m_mode; }

        bool active() const { return m_mode != Mode::Off && m_macro != nullptr; }
        bool macroDrives() const { return active() && (m_mode == Mode::Watch || m_mode == Mode::Trace); }

        // ---- attempt lifecycle ------------------------------------------

        /// Call whenever the level (re)starts or a checkpoint is respawned.
        /// `progress` is m_currentProgress right after the reset.
        void beginAttempt(int64_t progress) {
            m_startProgress = progress;
            for (auto& e : m_eff) {
                e.matched = false;
                e.injected = false;
                e.offset = 0;
                e.fixed = false;
                e.fixedBy = 0;
                e.skipped = e.frame < progress;
            }
            auto it = std::lower_bound(m_eff.begin(), m_eff.end(), progress, [](Eff const& e, int64_t p) {
                return e.frame < p;
            });
            m_next = static_cast<size_t>(it - m_eff.begin());
            m_pending.clear();
            m_extra = 0;
            m_events.clear();
        }

        // ---- real user input --------------------------------------------

        UserResult onUserInput(int64_t frame, int button, bool p2, bool down) {
            UserResult r;
            r.userFrame = frame;
            r.button = button;
            r.p2 = p2;
            r.down = down;

            if (!active()) return r;

            switch (m_mode) {
                case Mode::Off:
                    return r;
                case Mode::Watch:
                    r.swallow = true; // the macro is in control, user input is ignored
                    return r;
                case Mode::Trace: {
                    r.swallow = true;
                    int idx = findMatch(frame, button, p2, down, m_cfg.window, m_cfg.window);
                    applyMatch(r, idx, frame);
                    m_events.push_back(r);
                    return r;
                }
                case Mode::EarlyFix: {
                    int idx = findMatch(frame, button, p2, down, m_cfg.maxEarly, m_cfg.window);
                    applyMatch(r, idx, frame);

                    int64_t plan = frame;
                    if (idx >= 0) {
                        int64_t target = m_eff[idx].frame;
                        if (target > frame) plan = target; // early -> delay it, late -> leave it alone
                    }

                    // never reorder events of the same button: if something for this key is
                    // still waiting, this one has to wait for it too.
                    int64_t last = lastPendingFrame(button, p2);
                    if (last >= plan && last > frame) plan = last + 1;

                    if (plan > frame) {
                        r.swallow = true;
                        r.delayed = true;
                        r.applyDelay = static_cast<int>(plan - frame);
                        if (idx >= 0) {
                            m_eff[idx].fixed = true;
                            m_eff[idx].fixedBy = r.applyDelay;
                        }
                        Due d;
                        d.frame = plan;
                        d.button = button;
                        d.p2 = p2;
                        d.down = down;
                        d.fromMacro = false;
                        insertPending(d);
                    }
                    m_events.push_back(r);
                    return r;
                }
            }
            return r;
        }

        // ---- per physics step -------------------------------------------

        /// Call once per processCommands (after the original) with m_currentProgress.
        /// Idempotent: each input is delivered exactly once.
        template <class F>
        void dispatchDue(int64_t progress, F&& fn) {
            if (!active()) return;

            if (macroDrives()) {
                while (m_next < m_eff.size() && m_eff[m_next].frame < progress) {
                    auto& e = m_eff[m_next];
                    if (!e.skipped) {
                        Due d;
                        d.frame = e.frame;
                        d.button = e.button;
                        d.p2 = e.p2;
                        d.down = e.down;
                        d.macroIdx = static_cast<int>(m_next);
                        d.fromMacro = true;
                        e.injected = true;
                        fn(d);
                    }
                    ++m_next;
                }
            }
            else if (m_mode == Mode::EarlyFix) {
                while (!m_pending.empty() && m_pending.front().frame < progress) {
                    Due d = m_pending.front();
                    m_pending.pop_front();
                    fn(d);
                }
            }
        }

        // ---- results ------------------------------------------------------

        /// `reached` = last event frame the attempt got to. `finished` = level was completed
        /// (everything unmatched counts as missed).
        Summary summarize(int64_t reached, bool finished) const {
            Summary s;
            double scoreSum = 0.0;
            double offSum = 0.0, absSum = 0.0;
            int64_t decidedUpTo = finished ? INT64_MAX : reached - m_cfg.window;
            double denom = static_cast<double>(m_cfg.window) + 1.0;
            double fixedFrames = 0.0;

            for (auto const& e : m_eff) {
                if (e.skipped) continue;
                if (e.matched) {
                    s.matched++;
                    s.total++;
                    if (e.offset == 0) s.perfect++;
                    else if (e.offset < 0) s.early++;
                    else s.late++;
                    s.worstEarly = std::min(s.worstEarly, e.offset);
                    s.worstLate = std::max(s.worstLate, e.offset);
                    offSum += e.offset;
                    absSum += std::abs(e.offset);
                    scoreSum += std::max(0.0, 1.0 - std::abs(static_cast<double>(e.offset)) / denom);
                    if (e.fixed) {
                        s.fixed++;
                        fixedFrames += e.fixedBy;
                    }
                }
                else if (e.frame <= decidedUpTo) {
                    s.missed++;
                    s.total++;
                }
            }
            s.extra = m_extra;
            if (s.matched > 0) {
                s.meanOffset = offSum / s.matched;
                s.meanAbs = absSum / s.matched;
            }
            if (s.fixed > 0) s.fixedAvgFrames = fixedFrames / s.fixed;
            int denomCount = s.total + s.extra;
            s.accuracy = denomCount > 0 ? 100.0 * scoreSum / denomCount : 0.0;
            return s;
        }

        // ---- read-only views (HUD) --------------------------------------

        std::vector<Eff> const& inputs() const { return m_eff; }
        std::vector<UserResult> const& events() const { return m_events; }
        int64_t startProgress() const { return m_startProgress; }

        /// Effective ticks per second used for time conversions.
        double tps() const {
            return m_cfg.gameTps > 0.0 ? m_cfg.gameTps : macroFps();
        }

        double framesToMs(double frames) const {
            return frames * 1000.0 / tps();
        }

    private:
        double macroFps() const {
            return (m_macro && m_macro->fps > 0.0) ? m_macro->fps : 240.0;
        }

        void rebuild() {
            m_eff.clear();
            if (!m_macro) return;
            double scale = tps() / macroFps();
            m_eff.reserve(m_macro->inputs.size());
            for (auto const& in : m_macro->inputs) {
                Eff e;
                e.frame = static_cast<int64_t>(std::llround(static_cast<double>(in.frame) * scale)) + m_cfg.frameOffset;
                e.button = in.button;
                e.p2 = in.p2;
                e.down = in.down;
                m_eff.push_back(e);
            }
            std::stable_sort(m_eff.begin(), m_eff.end(), [](Eff const& a, Eff const& b) {
                return a.frame < b.frame;
            });
            beginAttempt(m_startProgress);
        }

        /// Nearest unmatched macro input of the same kind within [frame - lateWin, frame + earlyWin].
        int findMatch(int64_t frame, int button, bool p2, bool down, int earlyWin, int lateWin) const {
            int best = -1;
            int64_t bestDist = INT64_MAX;
            auto it = std::lower_bound(m_eff.begin(), m_eff.end(), frame - lateWin, [](Eff const& e, int64_t f) {
                return e.frame < f;
            });
            for (size_t i = static_cast<size_t>(it - m_eff.begin()); i < m_eff.size(); ++i) {
                auto const& e = m_eff[i];
                if (e.frame > frame + earlyWin) break;
                if (e.skipped || e.matched) continue;
                if (e.button != button || e.p2 != p2 || e.down != down) continue;
                int64_t dist = std::abs(e.frame - frame);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = static_cast<int>(i);
                }
            }
            return best;
        }

        void applyMatch(UserResult& r, int idx, int64_t frame) {
            if (idx < 0) {
                r.extra = true;
                m_extra++;
                return;
            }
            auto& e = m_eff[idx];
            e.matched = true;
            e.offset = static_cast<int>(frame - e.frame);
            r.matched = true;
            r.macroIdx = idx;
            r.offset = e.offset;
        }

        int64_t lastPendingFrame(int button, bool p2) const {
            int64_t last = INT64_MIN;
            for (auto const& d : m_pending) {
                if (d.button == button && d.p2 == p2) last = std::max(last, d.frame);
            }
            return last;
        }

        void insertPending(Due const& d) {
            auto it = std::upper_bound(m_pending.begin(), m_pending.end(), d.frame, [](int64_t f, Due const& x) {
                return f < x.frame;
            });
            m_pending.insert(it, d);
        }

        std::shared_ptr<Macro const> m_macro;
        Config m_cfg;
        Mode m_mode = Mode::Off;
        std::vector<Eff> m_eff;
        size_t m_next = 0;
        std::deque<Due> m_pending;
        std::vector<UserResult> m_events;
        int m_extra = 0;
        int64_t m_startProgress = 0;
    };

}
