#include "Macro.hpp"

#include <gdr/gdr.hpp>

#include <algorithm>
#include <exception>
#include <span>

namespace mpr {

    namespace {
        // We don't need any bot-specific extension data, only the plain inputs.
        struct PlainReplay : gdr::Replay<PlainReplay, gdr::Input<>> {
            PlainReplay() : Replay("", 1) {}
        };
    }

    LoadResult loadMacro(std::vector<uint8_t> data, std::string fileName) {
        LoadResult out;

        if (data.size() < 4) {
            out.error = "File is too small to be a macro.";
            return out;
        }
        if (data[0] != 'G' || data[1] != 'D' || data[2] != 'R') {
            out.error = "Unsupported macro format. Only GDR2 macros (.gdr / .gdr2) are supported.";
            return out;
        }

        try {
            auto res = PlainReplay::importData(std::span<uint8_t>(data.data(), data.size()));
            if (res.isErr()) {
                out.error = "Could not read macro: " + res.unwrapErr();
                return out;
            }
            auto const& r = res.unwrap();

            auto macro = std::make_shared<Macro>();
            macro->fileName = std::move(fileName);
            macro->author = r.author;
            macro->botName = r.botInfo.name;
            macro->botVersion = r.botInfo.version;
            macro->levelName = r.levelInfo.name;
            macro->levelId = r.levelInfo.id;
            macro->fps = r.framerate > 0.0 ? r.framerate : 240.0;
            macro->platformer = r.platformer;
            macro->gameVersion = r.gameVersion;

            macro->inputs.reserve(r.inputs.size());
            for (auto const& in : r.inputs) {
                MacroInput mi;
                mi.frame = static_cast<int64_t>(in.frame);
                mi.button = in.button == 0 ? 1 : static_cast<int>(in.button);
                mi.p2 = in.player2;
                mi.down = in.down;
                macro->inputs.push_back(mi);
            }
            std::stable_sort(macro->inputs.begin(), macro->inputs.end(), [](MacroInput const& a, MacroInput const& b) {
                return a.frame < b.frame;
            });

            if (macro->inputs.empty()) {
                out.error = "The macro has no inputs.";
                return out;
            }

            out.macro = std::move(macro);
            return out;
        }
        catch (std::exception const& e) {
            out.error = std::string("Could not read macro: ") + e.what();
        }
        catch (...) {
            out.error = "Could not read macro (corrupt file?).";
        }
        return out;
    }

}
