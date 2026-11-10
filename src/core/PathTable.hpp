#pragma once

// Pure C++ (no Geode / Cocos headers) so it can be unit-tested off-game.
//
// Where the players were at every physics step, measured passively while the level is played
// from its start. Used to
//   * place click indicators inside the level without needing a Watch pass, and
//   * find which macro frame a StartPos corresponds to (so macros stay in sync from a StartPos).
// Index = m_currentProgress as seen by processCommands.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace mpr {

    struct PathPoint {
        float x1 = std::numeric_limits<float>::quiet_NaN();
        float y1 = std::numeric_limits<float>::quiet_NaN();
        float x2 = std::numeric_limits<float>::quiet_NaN();
        float y2 = std::numeric_limits<float>::quiet_NaN();
        bool valid() const { return !std::isnan(x1); }
    };

    class PathTable {
    public:
        static constexpr int64_t kMaxSteps = 4'000'000;

        void clear() {
            m_pts.clear();
            m_mismatch = 0;
            m_dirty = true;
        }
        size_t size() const { return m_pts.size(); }
        bool dirty() const { return m_dirty; }
        void markClean() { m_dirty = false; }

        /// First write wins, so a run where the player drifted off the route cannot damage what was
        /// measured before. `authoritative` writes (the macro itself was driving the player) always
        /// replace what is stored: that is the route the indicators have to show.
        /// The table is never thrown away on its own.
        void record(int64_t index, float x1, float y1, float x2, float y2, bool authoritative = false) {
            if (index < 0 || index > kMaxSteps || std::isnan(x1)) return;
            if (static_cast<size_t>(index) >= m_pts.size()) m_pts.resize(static_cast<size_t>(index) + 1);
            PathPoint& p = m_pts[static_cast<size_t>(index)];
            if (!p.valid()) {
                p = {x1, y1, x2, y2};
                m_dirty = true;
                return;
            }
            if (authoritative && (std::fabs(p.x1 - x1) > 0.01f || std::fabs(p.y1 - y1) > 0.01f)) {
                p = {x1, y1, x2, y2};
                m_dirty = true;
            }
        }

        /// The point at `index`, or the closest known one within `radius` steps (a hole in the
        /// table must not make an indicator vanish).
        std::optional<PathPoint> nearest(int64_t index, int64_t radius = 24) const {
            if (auto p = at(index)) return p;
            for (int64_t d = 1; d <= radius; ++d) {
                if (auto p = at(index - d)) return p;
                if (auto p = at(index + d)) return p;
            }
            return std::nullopt;
        }

        std::optional<PathPoint> at(int64_t index) const {
            if (index < 0 || static_cast<size_t>(index) >= m_pts.size()) return std::nullopt;
            if (!m_pts[static_cast<size_t>(index)].valid()) return std::nullopt;
            return m_pts[static_cast<size_t>(index)];
        }

        /// Index whose player 1 x is closest to `x` (the lowest one on ties), or nothing when `x`
        /// is further than `maxDist` from anything known.
        std::optional<int64_t> findIndexAtX(float x, float maxDist = 30.f) const {
            std::optional<int64_t> best;
            float bestDist = maxDist;
            for (size_t i = 0; i < m_pts.size(); ++i) {
                if (!m_pts[i].valid()) continue;
                float d = std::fabs(m_pts[i].x1 - x);
                if (d < bestDist) {
                    bestDist = d;
                    best = static_cast<int64_t>(i);
                }
            }
            return best;
        }

        // ---- persistence -------------------------------------------------------------
        std::vector<uint8_t> serialize() const {
            std::vector<uint8_t> out(8 + m_pts.size() * sizeof(float) * 4);
            std::memcpy(out.data(), "MPT1", 4);
            uint32_t n = static_cast<uint32_t>(m_pts.size());
            std::memcpy(out.data() + 4, &n, 4);
            if (n) std::memcpy(out.data() + 8, m_pts.data(), static_cast<size_t>(n) * sizeof(PathPoint));
            return out;
        }

        bool deserialize(std::vector<uint8_t> const& data) {
            static_assert(sizeof(PathPoint) == sizeof(float) * 4, "PathPoint must be tightly packed");
            if (data.size() < 8 || std::memcmp(data.data(), "MPT1", 4) != 0) return false;
            uint32_t n = 0;
            std::memcpy(&n, data.data() + 4, 4);
            if (n > kMaxSteps || data.size() != 8 + static_cast<size_t>(n) * sizeof(PathPoint)) return false;
            m_pts.assign(n, PathPoint{});
            if (n) std::memcpy(m_pts.data(), data.data() + 8, static_cast<size_t>(n) * sizeof(PathPoint));
            m_mismatch = 0;
            m_dirty = false;
            return true;
        }

    private:
        std::vector<PathPoint> m_pts;
        int m_mismatch = 0;
        bool m_dirty = false;
    };

}
