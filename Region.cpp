#include "Region.h"
#include "Block.h"
#include "BlockShape.h"
#include "DoorShape.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace {

enum class Facing { None, Four, Sixteen, Axis };   // what a block's low metadata bits say about its turn

Facing facingKind(const BlockProperty& p) {
    if (shape::isStairs(p) || p.pattern == "door" || p.pattern == "wall_sign" || p.pattern == "mirror") return Facing::Four;
    if (p.pattern == "sign") return Facing::Sixteen;
    if (p.rotation == shape::kRotAxis) return Facing::Axis;
    return Facing::None;
}

// Facings 0 north, 1 east, 2 south, 3 west: a quarter turn clockwise (seen from above) is +1.
// Standing signs: 0..15 in steps of 22.5 degrees, 0 = south, 4 = east: clockwise is -4.
uint8_t turnMeta(uint8_t id, uint8_t meta, int turns) {
    if (id == 0 || turns == 0) return meta;
    const BlockProperty& p = BlockRegistry::getInstance().getBlock(id);
    switch (facingKind(p)) {
    case Facing::Four:    return static_cast<uint8_t>((meta & ~3) | (((meta & 3) + turns) & 3));
    case Facing::Sixteen: return static_cast<uint8_t>((meta & ~15) | (((meta & 15) - 4 * turns) & 15));
    case Facing::Axis: {
        const int a = meta & 3;
        if ((turns & 1) && (a == 1 || a == 2)) return static_cast<uint8_t>((meta & ~3) | (a == 1 ? 2 : 1));
        return meta;
    }
    default: return meta;
    }
}

uint8_t flipMeta(uint8_t id, uint8_t meta, char axis) {
    if (id == 0) return meta;
    const BlockProperty& p = BlockRegistry::getInstance().getBlock(id);
    const Facing kind = facingKind(p);
    if (axis == 'y') {
        if (shape::isSlab(p)) {
            const int h = meta & 3;
            if (h == shape::kSlabBottom || h == shape::kSlabTop)
                return static_cast<uint8_t>((meta & ~3) | (h == shape::kSlabBottom ? shape::kSlabTop : shape::kSlabBottom));
            return meta;
        }
        if (shape::isStairs(p)) return static_cast<uint8_t>(meta ^ shape::kStairUpsideDown);
        if (p.pattern == "door") return static_cast<uint8_t>(meta ^ door::kUpper);
        return meta;
    }
    // x: east and west swap; z: north and south swap. Mirroring swaps left and right too.
    uint8_t out = meta;
    if (kind == Facing::Four) {
        int f = meta & 3;
        if (axis == 'x' && (f == 1 || f == 3)) f = 4 - f;
        if (axis == 'z' && (f == 0 || f == 2)) f = 2 - f;
        out = static_cast<uint8_t>((meta & ~3) | f);
    } else if (kind == Facing::Sixteen) {
        const int r = meta & 15;
        const int nr = axis == 'x' ? (16 - r) & 15 : (8 - r) & 15;
        out = static_cast<uint8_t>((meta & ~15) | nr);
    }
    if (shape::isStairs(p)) {
        const int s = (out & shape::kStairShapeMask) >> shape::kStairShapeShift;
        int ns = s;
        if (s == shape::kShapeInnerLeft) ns = shape::kShapeInnerRight;
        else if (s == shape::kShapeInnerRight) ns = shape::kShapeInnerLeft;
        else if (s == shape::kShapeOuterLeft) ns = shape::kShapeOuterRight;
        else if (s == shape::kShapeOuterRight) ns = shape::kShapeOuterLeft;
        out = static_cast<uint8_t>((out & ~shape::kStairShapeMask) | (ns << shape::kStairShapeShift));
    }
    if (p.pattern == "door") out = static_cast<uint8_t>(out ^ door::kHingeRight);
    return out;
}

std::string hexOf(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}
bool unhex(const std::string& h, std::string& out) {
    if (h.size() % 2) return false;
    out.clear();
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (size_t i = 0; i < h.size(); i += 2) {
        const int a = v(h[i]), b = v(h[i + 1]);
        if (a < 0 || b < 0) return false;
        out += static_cast<char>(a * 16 + b);
    }
    return true;
}

// Block text as one line of words
std::string textToLine(const BlockText& t) {
    std::ostringstream o;
    o << (t.waxed ? 1 : 0);
    for (const BlockTextSide* s : { &t.front, &t.back }) {
        o << ' ' << s->color << ' ' << (s->glowing ? 1 : 0) << ' ' << s->lines.size();
        for (const auto& l : s->lines) o << ' ' << (l.empty() ? std::string("-") : hexOf(l));
    }
    return o.str();
}
bool textFromLine(std::istringstream& in, BlockText& t) {
    int waxed = 0;
    if (!(in >> waxed)) return false;
    t.waxed = waxed != 0;
    for (BlockTextSide* s : { &t.front, &t.back }) {
        uint32_t color = 0; int glow = 0; size_t n = 0;
        if (!(in >> color >> glow >> n) || n > BlockText::kMaxLines) return false;
        s->color = color & 0xFFFFFF;
        s->glowing = glow != 0;
        s->lines.clear();
        for (size_t i = 0; i < n; ++i) {
            std::string h, line;
            if (!(in >> h)) return false;
            if (h != "-" && !unhex(h, line)) return false;
            if (line.size() > BlockText::kMaxLineBytes) line.resize(BlockText::kMaxLineBytes);
            s->lines.push_back(line);
        }
    }
    return true;
}

// ---- A small JSON reader and writer (only what a schematic needs; no library, so the dedicated
// server builds without one). Values: null, bool, number, string, array, object.
struct JVal {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;
    const JVal* get(const std::string& k) const { for (const auto& [kk, v] : o) if (kk == k) return &v; return nullptr; }
};
struct JParser {
    const std::string& t; size_t p = 0; int depth = 0; std::string err;
    explicit JParser(const std::string& text) : t(text) {}
    void ws() { while (p < t.size() && std::isspace(static_cast<unsigned char>(t[p]))) ++p; }
    bool fail(const std::string& e) { if (err.empty()) err = e; return false; }
    bool str(std::string& out) {
        if (p >= t.size() || t[p] != '"') return fail("a string expected");
        ++p;
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c == '\\') {
                if (p >= t.size()) return fail("bad string");
                const char e = t[p++];
                if (e == 'n') out += '\n'; else if (e == 't') out += '\t'; else if (e == 'r') out += '\r';
                else if (e == 'b') out += '\b'; else if (e == 'f') out += '\f';
                else if (e == 'u') {
                    if (p + 4 > t.size()) return fail("bad string");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = t[p++];
                        cp = cp * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                    }
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63)); }
                    else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
                } else out += e;
            } else out += c;
            if (out.size() > 1024) return fail("a string is too long");
        }
        if (p >= t.size()) return fail("bad string");
        ++p;
        return true;
    }
    bool value(JVal& v) {
        ws();
        if (p >= t.size()) return fail("unexpected end");
        if (++depth > 32) return fail("too deep");
        const char c = t[p];
        bool ok = true;
        if (c == '{') {
            v.kind = JVal::Obj; ++p; ws();
            if (p < t.size() && t[p] == '}') ++p;
            else for (;;) {
                ws(); std::string k; JVal x;
                if (!str(k)) { ok = false; break; }
                ws(); if (p >= t.size() || t[p] != ':') { ok = fail("':' expected"); break; } ++p;
                if (!value(x)) { ok = false; break; }
                v.o.emplace_back(std::move(k), std::move(x));
                ws(); if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == '}') { ++p; break; }
                ok = fail("',' or '}' expected"); break;
            }
        } else if (c == '[') {
            v.kind = JVal::Arr; ++p; ws();
            if (p < t.size() && t[p] == ']') ++p;
            else for (;;) {
                JVal x;
                if (!value(x)) { ok = false; break; }
                v.a.push_back(std::move(x));
                if (v.a.size() > 8000000) { ok = fail("too many values"); break; }
                ws(); if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == ']') { ++p; break; }
                ok = fail("',' or ']' expected"); break;
            }
        } else if (c == '"') { v.kind = JVal::Str; ok = str(v.s); }
        else if (t.compare(p, 4, "true") == 0) { v.kind = JVal::Bool; v.b = true; p += 4; }
        else if (t.compare(p, 5, "false") == 0) { v.kind = JVal::Bool; p += 5; }
        else if (t.compare(p, 4, "null") == 0) { p += 4; }
        else {
            const size_t start = p;
            while (p < t.size() && (std::isdigit(static_cast<unsigned char>(t[p])) || t[p] == '-' || t[p] == '+' || t[p] == '.' || t[p] == 'e' || t[p] == 'E')) ++p;
            if (p == start) ok = fail("unexpected character");
            else { v.kind = JVal::Num; try { v.n = std::stod(t.substr(start, p - start)); } catch (...) { ok = fail("bad number"); } }
        }
        --depth;
        return ok;
    }
};
std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n"; else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o + "\"";
}
std::string hexColor(uint32_t c) { char b[8]; std::snprintf(b, sizeof(b), "#%06x", c & 0xFFFFFF); return b; }
uint32_t colorOf(const JVal* v) {
    if (!v || v->kind != JVal::Str || v->s.size() != 7 || v->s[0] != '#') return 0;
    return static_cast<uint32_t>(std::strtoul(v->s.c_str() + 1, nullptr, 16)) & 0xFFFFFF;
}

} // namespace

Region Region::rotated(int quarterTurns) const {
    const int turns = ((quarterTurns % 4) + 4) % 4;
    if (turns == 0) return *this;
    Region out;
    out.resize(turns % 2 ? glm::ivec3(size.z, size.y, size.x) : size);
    for (int x = 0; x < size.x; ++x)
        for (int y = 0; y < size.y; ++y)
            for (int z = 0; z < size.z; ++z) {
                // clockwise seen from above: north (-z) goes east (+x)
                int nx = x, nz = z;
                for (int t = 0; t < turns; ++t) {
                    const int w = (t % 2 == 0) ? size.z : size.x;     // the depth before this quarter turn
                    const int px = nx;
                    nx = w - 1 - nz;
                    nz = px;
                }
                const size_t from = index(x, y, z), to = out.index(nx, y, nz);
                out.ids[to] = ids[from];
                out.metas[to] = turnMeta(ids[from], metas[from], turns);
                const auto it = texts.find(static_cast<uint32_t>(from));
                if (it != texts.end()) out.texts[static_cast<uint32_t>(to)] = it->second;
            }
    return out;
}

Region Region::flipped(char axis) const {
    Region out;
    out.resize(size);
    for (int x = 0; x < size.x; ++x)
        for (int y = 0; y < size.y; ++y)
            for (int z = 0; z < size.z; ++z) {
                const int nx = axis == 'x' ? size.x - 1 - x : x;
                const int ny = axis == 'y' ? size.y - 1 - y : y;
                const int nz = axis == 'z' ? size.z - 1 - z : z;
                const size_t from = index(x, y, z), to = out.index(nx, ny, nz);
                out.ids[to] = ids[from];
                out.metas[to] = flipMeta(ids[from], metas[from], axis);
                const auto it = texts.find(static_cast<uint32_t>(from));
                if (it != texts.end()) out.texts[static_cast<uint32_t>(to)] = it->second;
            }
    return out;
}

std::string Region::toText() const {
    auto& reg = BlockRegistry::getInstance();
    // the blocks used, by name (air first), and the cells as runs of [palette, meta, count]
    std::vector<int> paletteOf(256, -1);
    std::vector<std::string> names;
    paletteOf[0] = 0;
    names.push_back("air");
    for (uint8_t id : ids) {
        if (paletteOf[id] < 0) { paletteOf[id] = static_cast<int>(names.size()); names.push_back(reg.getBlock(id).name); }
    }
    std::ostringstream o;
    o << "{\n  \"format\": \"blocksmith-region\",\n  \"version\": 1,\n";
    o << "  \"size\": [" << size.x << ", " << size.y << ", " << size.z << "],\n";
    o << "  \"order\": \"x, then y, then z: cell (x, y, z) is number (x * sy + y) * sz + z\",\n";
    o << "  \"palette\": [";
    for (size_t i = 0; i < names.size(); ++i) o << (i ? ", " : "") << jsonStr(names[i]);
    o << "],\n  \"cells\": [";
    size_t i = 0, n = 0;
    while (i < ids.size()) {
        size_t j = i + 1;
        while (j < ids.size() && ids[j] == ids[i] && metas[j] == metas[i]) ++j;
        o << (n ? "," : "") << (n % 8 == 0 ? "\n    " : " ") << '[' << paletteOf[ids[i]] << ", " << static_cast<int>(metas[i]) << ", " << (j - i) << ']';
        ++n;
        i = j;
    }
    o << "\n  ],\n  \"texts\": [";
    n = 0;
    for (const auto& [idx, t] : texts) {
        o << (n++ ? "," : "") << "\n    { \"cell\": " << idx << ", \"waxed\": " << (t.waxed ? "true" : "false");
        for (const auto& [label, side] : { std::make_pair("front", &t.front), std::make_pair("back", &t.back) }) {
            o << ", \"" << label << "\": { \"color\": \"" << hexColor(side->color) << "\", \"glowing\": " << (side->glowing ? "true" : "false") << ", \"lines\": [";
            for (size_t k = 0; k < side->lines.size(); ++k) o << (k ? ", " : "") << jsonStr(side->lines[k]);
            o << "] }";
        }
        o << " }";
    }
    o << (n ? "\n  ]\n}\n" : "]\n}\n");
    return o.str();
}

// A schematic in JSON (toText)
static bool fromJson(const std::string& text, Region& out, std::string& why, std::vector<std::string>* missing) {
    JParser jp(text);
    JVal root;
    if (!jp.value(root)) { why = "not valid JSON: " + jp.err; return false; }
    if (root.kind != JVal::Obj) { why = "not a region"; return false; }
    const JVal* fmt = root.get("format");
    if (!fmt || fmt->kind != JVal::Str || fmt->s != "blocksmith-region") { why = "not a region (\"format\": \"blocksmith-region\")"; return false; }
    const JVal* sz = root.get("size");
    if (!sz || sz->kind != JVal::Arr || sz->a.size() != 3) { why = "no size"; return false; }
    glm::ivec3 s(static_cast<int>(sz->a[0].n), static_cast<int>(sz->a[1].n), static_cast<int>(sz->a[2].n));
    if (s.x <= 0 || s.y <= 0 || s.z <= 0 || s.x > 4096 || s.y > 256 || s.z > 4096 ||
        static_cast<size_t>(s.x) * s.y * s.z > Region::kMaxCells) { why = "too big (or no size)"; return false; }
    const JVal* pal = root.get("palette");
    if (!pal || pal->kind != JVal::Arr || pal->a.empty() || pal->a.size() > 256) { why = "bad palette"; return false; }
    auto& reg = BlockRegistry::getInstance();
    std::vector<uint8_t> ids;
    for (const auto& v : pal->a) {
        if (v.kind != JVal::Str || v.s.size() > 128) { why = "bad palette"; return false; }
        const uint8_t id = v.s == "air" ? 0 : reg.getBlockId(v.s);
        if (id == 0 && v.s != "air" && missing) missing->push_back(v.s);
        ids.push_back(id);
    }
    Region r;
    r.resize(s);
    const JVal* cells = root.get("cells");
    if (!cells || cells->kind != JVal::Arr) { why = "no cells"; return false; }
    size_t at = 0;
    for (const auto& run : cells->a) {
        if (run.kind != JVal::Arr || run.a.size() != 3) { why = "bad cell run"; return false; }
        const double pi = run.a[0].n, mi = run.a[1].n, ci = run.a[2].n;
        if (pi < 0 || pi >= ids.size() || mi < 0 || mi > 255 || ci < 1 || ci > static_cast<double>(r.cells() - at)) { why = "bad cell run"; return false; }
        const uint8_t id = ids[static_cast<size_t>(pi)];
        for (size_t k = 0; k < static_cast<size_t>(ci); ++k, ++at) { r.ids[at] = id; r.metas[at] = id ? static_cast<uint8_t>(mi) : 0; }
    }
    if (at != r.cells()) { why = "too few cells"; return false; }
    if (const JVal* texts = root.get("texts"); texts && texts->kind == JVal::Arr) {
        for (const auto& tv : texts->a) {
            const JVal* cell = tv.get("cell");
            if (!cell || cell->kind != JVal::Num || cell->n < 0 || cell->n >= static_cast<double>(r.cells())) continue;
            const uint32_t idx = static_cast<uint32_t>(cell->n);
            if (r.ids[idx] == 0) continue;
            BlockText t;
            if (const JVal* w = tv.get("waxed")) t.waxed = w->kind == JVal::Bool && w->b;
            for (const auto& [label, side] : { std::make_pair("front", &t.front), std::make_pair("back", &t.back) }) {
                const JVal* sv = tv.get(label);
                if (!sv || sv->kind != JVal::Obj) continue;
                side->color = colorOf(sv->get("color"));
                if (const JVal* g = sv->get("glowing")) side->glowing = g->kind == JVal::Bool && g->b;
                if (const JVal* lines = sv->get("lines"); lines && lines->kind == JVal::Arr) {
                    for (const auto& l : lines->a) {
                        if (side->lines.size() >= BlockText::kMaxLines) break;
                        std::string line = l.kind == JVal::Str ? l.s : std::string();
                        if (line.size() > BlockText::kMaxLineBytes) line.resize(BlockText::kMaxLineBytes);
                        side->lines.push_back(line);
                    }
                }
            }
            r.texts[idx] = t;
        }
    }
    out = std::move(r);
    return true;
}

bool Region::fromText(const std::string& text, Region& out, std::string& why, std::vector<std::string>* missing) {
    // JSON (what toText writes now), or the older "BSREGION 1" text
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '{') return fromJson(text, out, why, missing);
        break;
    }
    std::istringstream in(text);
    std::string word;
    int version = 0;
    if (!(in >> word >> version) || word != "BSREGION" || version != 1) { why = "not a region (it should start with BSREGION 1)"; return false; }
    glm::ivec3 s;
    if (!(in >> word >> s.x >> s.y >> s.z) || word != "size") { why = "no size"; return false; }
    if (s.x <= 0 || s.y <= 0 || s.z <= 0 || s.x > 4096 || s.y > 256 || s.z > 4096 ||
        static_cast<size_t>(s.x) * s.y * s.z > kMaxCells) { why = "too big (or no size)"; return false; }
    size_t n = 0;
    if (!(in >> word >> n) || word != "palette" || n == 0 || n > 256) { why = "bad palette"; return false; }
    auto& reg = BlockRegistry::getInstance();
    std::vector<uint8_t> pal(n, 0);
    for (size_t i = 0; i < n; ++i) {
        std::string name;
        if (!(in >> name) || name.size() > 128) { why = "bad palette"; return false; }
        pal[i] = name == "air" ? 0 : reg.getBlockId(name);
        if (pal[i] == 0 && name != "air" && missing) missing->push_back(name);
    }
    Region r;
    r.resize(s);
    if (!(in >> word) || word != "cells") { why = "no cells"; return false; }
    size_t at = 0;
    while (at < r.cells()) {
        std::string tok;
        if (!(in >> tok)) { why = "too few cells"; return false; }
        size_t p = 0, meta = 0, count = 1;
        const size_t colon = tok.find(':'), star = tok.find('*');
        try {
            p = std::stoul(tok.substr(0, std::min(colon, star)));
            if (colon != std::string::npos) meta = std::stoul(tok.substr(colon + 1, star == std::string::npos ? std::string::npos : star - colon - 1));
            if (star != std::string::npos) count = std::stoul(tok.substr(star + 1));
        } catch (...) { why = "bad cell: " + tok.substr(0, 20); return false; }
        if (p >= n || meta > 255 || count == 0 || count > r.cells() - at) { why = "bad cell: " + tok.substr(0, 20); return false; }
        for (size_t k = 0; k < count; ++k, ++at) {
            r.ids[at] = pal[p];
            r.metas[at] = pal[p] ? static_cast<uint8_t>(meta) : 0;
        }
    }
    size_t nt = 0;
    if (!(in >> word >> nt) || word != "texts" || nt > r.cells()) { why = "bad texts"; return false; }
    std::string line;
    std::getline(in, line);
    for (size_t i = 0; i < nt; ++i) {
        if (!std::getline(in, line)) { why = "too few texts"; return false; }
        std::istringstream li(line);
        uint32_t idx = 0;
        BlockText t;
        if (!(li >> idx) || idx >= r.cells() || !textFromLine(li, t)) { why = "bad text"; return false; }
        if (r.ids[idx] != 0) r.texts[idx] = t;
    }
    out = std::move(r);
    return true;
}
