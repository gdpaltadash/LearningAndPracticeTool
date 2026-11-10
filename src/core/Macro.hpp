#pragma once

// Pure C++ (no Geode / Cocos headers) so it can be unit-tested off-game.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mpr {

    /// One button event of a macro. `frame` is in the macro's own tick rate.
    struct MacroInput {
        int64_t frame = 0;
        int button = 1;      // 1 = jump, 2 = left, 3 = right (PlayerButton)
        bool p2 = false;
        bool down = false;
    };

    struct Macro {
        std::string fileName;
        std::string author;
        std::string botName;
        int botVersion = 0;
        std::string levelName;
        uint32_t levelId = 0;
        double fps = 240.0;   // ticks per second the macro was recorded at
        bool platformer = false;
        int gameVersion = 0;
        std::vector<MacroInput> inputs; // sorted by frame (stable)

        int64_t lastFrame() const {
            return inputs.empty() ? 0 : inputs.back().frame;
        }
    };

    struct LoadResult {
        std::shared_ptr<Macro> macro; // null on failure
        std::string error;
        bool ok() const { return macro != nullptr; }
    };

    /// Parses a GDR2 replay (the format used by zBot and other GDR bots).
    LoadResult loadMacro(std::vector<uint8_t> data, std::string fileName);

}
