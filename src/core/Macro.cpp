#include "Macro.hpp"

#include <gdr/gdr.hpp>

#include <algorithm>
#include <exception>
#include <span>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace mpr {

    namespace {
        // We don't need any bot-specific extension data, only the plain inputs.
        struct PlainReplay : gdr::Replay<PlainReplay, gdr::Input<>> {
            PlainReplay() : Replay("", 1) {}
        };
    }

    static LoadResult loadGdr2(std::vector<uint8_t> data, std::string fileName) {
        LoadResult out;


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

    // ---------------------------------------------------------------------------------
    // Minimal JSON reader (only what the bot formats need)
    // ---------------------------------------------------------------------------------
    namespace {
        struct J {
            enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
            bool b = false;
            double n = 0;
            std::string s;
            std::vector<J> a;
            std::vector<std::pair<std::string, J>> o;
            J const* get(char const* k) const {
                for (auto const& [kk, v] : o) if (kk == k) return &v;
                return nullptr;
            }
            double num(double d = 0) const { return t == Num ? n : (t == Bool ? (b ? 1 : 0) : d); }
            bool flag(bool d = false) const { return t == Bool ? b : (t == Num ? n != 0 : d); }
        };

        struct JP {
            std::string_view in;
            size_t i = 0;
            int depth = 0;
            bool fail = false;

            void ws() { while (i < in.size() && (in[i] == ' ' || in[i] == '\n' || in[i] == '\r' || in[i] == '\t')) i++; }
            bool lit(char const* w) {
                size_t l = std::strlen(w);
                if (in.substr(i, l) == w) { i += l; return true; }
                return false;
            }
            std::string str() {
                std::string r;
                i++; // opening quote
                while (i < in.size() && in[i] != '"') {
                    if (in[i] == '\\' && i + 1 < in.size()) {
                        i++;
                        switch (in[i]) {
                            case 'n': r += '\n'; break;
                            case 't': r += '\t'; break;
                            case 'u': i += 4; r += '?'; break;
                            default: r += in[i];
                        }
                        i++;
                    }
                    else r += in[i++];
                }
                if (i >= in.size()) fail = true;
                i++;
                return r;
            }
            J val() {
                J v;
                ws();
                if (fail || i >= in.size() || depth > 64) { fail = true; return v; }
                char c = in[i];
                if (c == '{') {
                    v.t = J::Obj; i++; depth++;
                    ws();
                    if (i < in.size() && in[i] == '}') { i++; depth--; return v; }
                    while (!fail) {
                        ws();
                        if (i >= in.size() || in[i] != '"') { fail = true; break; }
                        auto k = str();
                        ws();
                        if (i >= in.size() || in[i] != ':') { fail = true; break; }
                        i++;
                        v.o.emplace_back(std::move(k), val());
                        ws();
                        if (i < in.size() && in[i] == ',') { i++; continue; }
                        if (i < in.size() && in[i] == '}') { i++; break; }
                        fail = true;
                    }
                    depth--;
                }
                else if (c == '[') {
                    v.t = J::Arr; i++; depth++;
                    ws();
                    if (i < in.size() && in[i] == ']') { i++; depth--; return v; }
                    while (!fail) {
                        v.a.push_back(val());
                        ws();
                        if (i < in.size() && in[i] == ',') { i++; continue; }
                        if (i < in.size() && in[i] == ']') { i++; break; }
                        fail = true;
                    }
                    depth--;
                }
                else if (c == '"') { v.t = J::Str; v.s = str(); }
                else if (lit("true")) { v.t = J::Bool; v.b = true; }
                else if (lit("false")) { v.t = J::Bool; v.b = false; }
                else if (lit("null")) { v.t = J::Null; }
                else {
                    char* end = nullptr;
                    std::string tmp(in.substr(i, 40));
                    double d = std::strtod(tmp.c_str(), &end);
                    if (end == tmp.c_str()) { fail = true; return v; }
                    i += static_cast<size_t>(end - tmp.c_str());
                    v.t = J::Num; v.n = d;
                }
                return v;
            }
        };

        std::string lower(std::string s) {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        bool endsWith(std::string const& s, char const* suf) {
            size_t l = std::strlen(suf);
            return s.size() >= l && s.compare(s.size() - l, l, suf) == 0;
        }

        struct Builder {
            std::shared_ptr<Macro> m = std::make_shared<Macro>();
            Builder(std::string const& fileName, char const* bot, double fps) {
                m->fileName = fileName;
                m->botName = bot;
                m->fps = fps > 0 ? fps : 240.0;
            }
            void add(int64_t frame, bool down, bool p2, int button = 1) {
                MacroInput mi;
                mi.frame = frame; mi.down = down; mi.p2 = p2; mi.button = button;
                m->inputs.push_back(mi);
            }
            LoadResult finish() {
                LoadResult out;
                std::stable_sort(m->inputs.begin(), m->inputs.end(), [](MacroInput const& a, MacroInput const& b) {
                    return a.frame < b.frame;
                });
                if (m->inputs.empty()) { out.error = "The macro has no inputs."; return out; }
                out.macro = std::move(m);
                return out;
            }
        };

        LoadResult fail(std::string e) { LoadResult r; r.error = std::move(e); return r; }

        uint32_t rd32(uint8_t const* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

        // --- MegaHack (.mhr) binary
        LoadResult loadMhrBinary(std::vector<uint8_t> const& d, std::string const& name) {
            if (d.size() < 0x20) return fail("MegaHack macro is truncated.");
            uint32_t metaSize = rd32(&d[8]);
            double fps = static_cast<int32_t>(rd32(&d[12]));
            size_t pos = 12 + static_cast<size_t>(metaSize) + 8; // meta, then 8 reserved bytes
            if (metaSize < 4 || pos + 8 > d.size()) return fail("MegaHack macro is corrupt.");
            uint32_t evSize = rd32(&d[pos]);
            uint32_t evCount = rd32(&d[pos + 4]);
            pos += 8;
            if (evSize < 8) return fail("MegaHack macro is corrupt.");
            Builder b(name, "MegaHack Replay", fps);
            for (uint32_t k = 0; k < evCount; k++) {
                if (pos + evSize > d.size()) break;
                bool down = d[pos + 2] == 1;
                bool p2 = d[pos + 3] == 1;
                b.add(static_cast<int32_t>(rd32(&d[pos + 4])), down, p2);
                pos += evSize;
            }
            return b.finish();
        }

        // --- JSON based bots
        LoadResult loadJson(std::string_view text, std::string const& name) {
            JP p{text};
            J root = p.val();
            if (p.fail || root.t != J::Obj) return fail("Could not read macro: invalid JSON.");

            // MegaHack JSON: { "_": ..., "events": [{frame, down, p2}], "meta": {"fps"} }
            if (auto ev = root.get("events"); ev && ev->t == J::Arr) {
                double fps = 240;
                if (auto meta = root.get("meta")) if (auto f = meta->get("fps")) fps = f->num(240);
                Builder b(name, "MegaHack Replay", fps);
                for (auto const& e : ev->a) {
                    auto fr = e.get("frame");
                    if (!fr) continue;
                    auto dn = e.get("down");
                    if (!dn) continue; // position-only events
                    auto p2 = e.get("p2");
                    b.add(static_cast<int64_t>(fr->num()), dn->flag(), p2 && p2->flag());
                }
                return b.finish();
            }
            // TASBot: { "fps", "macro": [{frame, player_1:{click}, player_2:{click}}] } (1 = down, 2 = up)
            if (auto mac = root.get("macro"); mac && mac->t == J::Arr) {
                double fps = root.get("fps") ? root.get("fps")->num(240) : 240;
                Builder b(name, "TASBot", fps);
                for (auto const& e : mac->a) {
                    auto fr = e.get("frame");
                    if (!fr) continue;
                    for (int pl = 0; pl < 2; pl++) {
                        auto a = e.get(pl == 0 ? "player_1" : "player_2");
                        auto c = a ? a->get("click") : nullptr;
                        if (!c) continue;
                        int v = static_cast<int>(c->num());
                        if (v == 1 || v == 2) b.add(static_cast<int64_t>(fr->num()), v == 1, pl == 1);
                    }
                }
                return b.finish();
            }
            // Echo (new): { "fps", "inputs": [{frame, holding, player_2}] }
            if (auto in = root.get("inputs"); in && in->t == J::Arr) {
                double fps = root.get("fps") ? root.get("fps")->num(240) : 240;
                Builder b(name, "Echo", fps);
                for (auto const& e : in->a) {
                    auto fr = e.get("frame");
                    auto h = e.get("holding");
                    if (!fr || !h) continue;
                    auto p2 = e.get("player_2");
                    b.add(static_cast<int64_t>(fr->num()), h->flag(), p2 && p2->flag());
                }
                return b.finish();
            }
            // Echo (old)
            if (auto in = root.get("Echo Replay"); in && in->t == J::Arr) {
                double fps = root.get("FPS") ? root.get("FPS")->num(240) : 240;
                Builder b(name, "Echo", fps);
                for (auto const& e : in->a) {
                    auto fr = e.get("Frame");
                    auto h = e.get("Hold");
                    if (!fr || !h) continue;
                    auto p2 = e.get("Player 2");
                    b.add(static_cast<int64_t>(fr->num()), h->flag(), p2 && p2->flag());
                }
                return b.finish();
            }
            return fail("Unknown JSON macro format.");
        }

        // --- text based bots: first line = fps, then one input per line
        //   plain text:  "frame hold p2"
        //   xdBot:       "frame|hold|button|player1|..."
        LoadResult loadText(std::string_view text, std::string const& name) {
            std::istringstream ss{std::string(text)};
            std::string line;
            if (!std::getline(ss, line)) return fail("Empty macro file.");
            char* end = nullptr;
            double fps = std::strtod(line.c_str(), &end);
            if (end == line.c_str() || fps <= 0 || fps > 100000) return fail("Unknown macro format.");

            bool xd = false;
            std::vector<std::string> lines;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                if (line.find('|') != std::string::npos) xd = true;
                lines.push_back(line);
            }
            Builder b(name, xd ? "xdBot" : "Plain text", fps);
            for (auto const& l : lines) {
                std::vector<std::string> f;
                std::string cur;
                for (char c : l) {
                    if ((xd && c == '|') || (!xd && (c == ' ' || c == '\t'))) {
                        if (!xd && cur.empty()) continue;
                        f.push_back(cur); cur.clear();
                    }
                    else cur += c;
                }
                if (!cur.empty() || xd) f.push_back(cur);
                if (f.size() < 3) return fail("Unknown macro format (bad line: " + l.substr(0, 30) + ").");
                char* e2 = nullptr;
                long long fr = std::strtoll(f[0].c_str(), &e2, 10);
                if (e2 == f[0].c_str()) return fail("Unknown macro format (bad line: " + l.substr(0, 30) + ").");
                bool down = f[1] == "1";
                if (xd) {
                    int btn = std::atoi(f[2].c_str());
                    if (btn < 1 || btn > 3) continue;
                    bool p1 = f.size() < 4 || f[3] == "1";
                    b.add(fr, down, !p1, btn);
                }
                else {
                    b.add(fr, down, f[2] == "1");
                }
            }
            return b.finish();
        }

        // --- zBot (.zbf): f32 delta, f32 speedhack, then {i32 frame, u8 down('1'), u8 player1('1')}
        LoadResult loadZbot(std::vector<uint8_t> const& d, std::string const& name) {
            if (d.size() < 14) return fail("zBot macro is truncated.");
            float delta, speed;
            std::memcpy(&delta, &d[0], 4);
            std::memcpy(&speed, &d[4], 4);
            double fps = std::round(1.0 / (static_cast<double>(delta) * speed));
            if (!(fps >= 1 && fps <= 100000)) return fail("Unknown macro format.");
            Builder b(name, "zBot", fps);
            for (size_t pos = 8; pos + 6 <= d.size(); pos += 6) {
                b.add(static_cast<int32_t>(rd32(&d[pos])), d[pos + 4] == 0x31, d[pos + 5] != 0x31);
            }
            return b.finish();
        }

        bool looksLikeJson(std::vector<uint8_t> const& d) {
            for (auto c : d) {
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0xEF || c == 0xBB || c == 0xBF) continue;
                return c == '{';
            }
            return false;
        }
    }

    LoadResult loadMacro(std::vector<uint8_t> data, std::string fileName) {
        if (data.size() < 4) return fail("File is too small to be a macro.");
        auto ext = lower(fileName);

        try {
            if (data[0] == 'G' && data[1] == 'D' && data[2] == 'R') return loadGdr2(std::move(data), std::move(fileName));

            static constexpr uint8_t mhr[8] = {0x48, 0x41, 0x43, 0x4B, 0x50, 0x52, 0x4F, 0x07};
            if (data.size() >= 8 && std::memcmp(data.data(), mhr, 8) == 0) return loadMhrBinary(data, fileName);

            if (looksLikeJson(data)) {
                size_t off = (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) ? 3 : 0;
                return loadJson(std::string_view(reinterpret_cast<char const*>(data.data()) + off, data.size() - off), fileName);
            }

            bool zbf = endsWith(ext, ".zbf");
            if (!zbf && data.size() >= 14 && (data.size() - 8) % 6 == 0) {
                // headerless guess: sane delta, text would not look like this
                float delta, speed;
                std::memcpy(&delta, &data[0], 4);
                std::memcpy(&speed, &data[4], 4);
                double fps = 1.0 / (static_cast<double>(delta) * speed);
                zbf = fps >= 30 && fps <= 10000 && std::fabs(fps - std::round(fps)) < 0.01 && data[12] < 0x80;
                // text macros start with digits; a valid zbf float would not be printable ASCII
                bool printable = true;
                for (int k = 0; k < 4; k++) if (data[k] < 0x20 || data[k] > 0x7E) printable = false;
                if (printable) zbf = false;
            }
            if (zbf) return loadZbot(data, fileName);

            return loadText(std::string_view(reinterpret_cast<char const*>(data.data()), data.size()), fileName);
        }
        catch (std::exception const& e) {
            return fail(std::string("Could not read macro: ") + e.what());
        }
        catch (...) {
            return fail("Could not read macro (corrupt file?).");
        }
    }

}
