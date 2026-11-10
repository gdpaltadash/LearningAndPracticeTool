#pragma once

// Pure C++ (no Geode / Cocos headers) so it can be unit-tested off-game.
//
// Predictive input buffer for when there is no macro to aim at. A press that arrives while the
// player cannot act yet (cube in the air, no orb under it...) is held back for up to `window`
// frames and released into the game the moment the player CAN act. Everything the user does on
// that button afterwards keeps its original spacing (a 5 frame tap stays a 5 frame tap).
//
// Frame convention is the same as Engine: an input seen while currentProgress == p has frame p - 1.

#include <cstdint>
#include <deque>
#include <map>
#include <utility>

namespace mpr {

    class InputBuffer {
    public:
        struct Out {
            int button = 1;
            bool p2 = false;
            bool down = false;
        };

        void setWindow(int frames) { m_window = frames < 1 ? 1 : frames; }
        int window() const { return m_window; }

        void reset() { m_lanes.clear(); }

        /// Returns true if the input was taken over by the buffer (do NOT forward it now).
        bool onInput(int64_t frame, int button, bool p2, bool down, bool readyNow) {
            Lane& lane = m_lanes[{button, p2}];
            if (lane.q.empty()) {
                if (!down || readyNow) return false;       // nothing waiting: pass through
                lane.known = false;
                lane.shift = 0;
            }
            lane.q.push_back({frame, down});
            return true;
        }

        /// Call once per physics step, after the original processCommands.
        /// `ready(button, p2)` tells whether a press would do something right now.
        template <class Ready, class Emit>
        void step(int64_t progress, Ready&& ready, Emit&& emit) {
            int64_t now = progress - 1;
            for (auto& [key, lane] : m_lanes) {
                while (!lane.q.empty()) {
                    Ev const& head = lane.q.front();
                    if (!lane.known) {
                        // head is the press we are waiting to place
                        if (head.frame > now) break;
                        bool go = ready(key.first, key.second) || now >= head.frame + m_window;
                        if (!go) break;
                        lane.shift = now - head.frame;
                        lane.known = true;
                        emit(Out{key.first, key.second, true});
                        lane.q.pop_front();
                    }
                    else {
                        if (head.frame + lane.shift > now) break;
                        emit(Out{key.first, key.second, head.down});
                        lane.q.pop_front();
                    }
                }
                if (lane.q.empty()) {
                    lane.known = false;
                    lane.shift = 0;
                }
            }
        }

        bool anyWaiting() const {
            for (auto const& [k, l] : m_lanes) if (!l.q.empty()) return true;
            return false;
        }

    private:
        struct Ev {
            int64_t frame;
            bool down;
        };
        struct Lane {
            std::deque<Ev> q;
            int64_t shift = 0;
            bool known = false;
        };
        std::map<std::pair<int, bool>, Lane> m_lanes;
        int m_window = 6;
    };

}
