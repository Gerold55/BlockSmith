// RemoteContent: see RemoteContent.h. Data only - nothing downloaded is ever run.
#include "RemoteContent.h"
#include "SafeReplace.h"
#include "Sha256.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#elif defined(BLOCKSMITH_HAS_CURL)
#include <curl/curl.h>
#endif

namespace remote {

namespace {

constexpr size_t kMaxDownload = 16u * 1024u * 1024u;   // the largest kind (audio)
constexpr size_t kMaxText = 4u * 1024u * 1024u;        // JSON / text
constexpr size_t kMaxImage = 8u * 1024u * 1024u;
constexpr size_t kMaxAudio = 16u * 1024u * 1024u;
constexpr int kMaxImageSide = 4096;
constexpr int kMaxRedirects = 3;
constexpr uintmax_t kMaxCacheBytes = 256u * 1024u * 1024u;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}
double nowUnix() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// The file extension of a URL's path (lower case, no query), "" if none
std::string urlExtension(const std::string& url) {
    std::string path = url;
    const size_t q = path.find_first_of("?#");
    if (q != std::string::npos) path.resize(q);
    const size_t slash = path.rfind('/');
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return lower(path.substr(dot));
}

bool validUtf8(const std::string& s) {
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0) return false;                                   // no NUL bytes in text
        int extra;
        if (c < 0x80) extra = 0;
        else if ((c >> 5) == 0x6) extra = 1;
        else if ((c >> 4) == 0xE) extra = 2;
        else if ((c >> 3) == 0x1E) extra = 3;
        else return false;
        if (i + static_cast<size_t>(extra) >= n && extra > 0) return false;   // cut off
        for (int k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) return false;
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return true;
}

// Programs and things that run: refused whatever the file is called
const char* executableSignature(const std::string& b) {
    auto starts = [&b](const char* sig, size_t n) { return b.size() >= n && std::memcmp(b.data(), sig, n) == 0; };
    if (starts("MZ", 2)) return "a Windows program (EXE/DLL)";
    if (starts("\x7f" "ELF", 4)) return "a Linux program";
    if (starts("\xfe\xed\xfa\xce", 4) || starts("\xfe\xed\xfa\xcf", 4) || starts("\xce\xfa\xed\xfe", 4) ||
        starts("\xcf\xfa\xed\xfe", 4) || starts("\xca\xfe\xba\xbe", 4)) return "a macOS program or Java class";
    if (starts("#!", 2)) return "a script (#!)";
    if (starts("\x1bLua", 4)) return "compiled Lua";
    if (starts("PK\x03\x04", 4)) return "a ZIP/JAR archive";
    if (starts("\0asm", 4)) return "WebAssembly";
    if (starts("\xd0\xcf\x11\xe0", 4)) return "an Office document (can hold macros)";
    return nullptr;
}

const std::set<std::string>& blockedExtensions() {
    static const std::set<std::string> s = {
        ".lua", ".luac", ".js", ".mjs", ".cjs", ".ts", ".wasm", ".exe", ".dll", ".so", ".dylib", ".bat", ".cmd",
        ".com", ".ps1", ".psm1", ".sh", ".bash", ".zsh", ".py", ".pyc", ".rb", ".pl", ".php", ".jar", ".class",
        ".msi", ".scr", ".vbs", ".vbe", ".wsf", ".hta", ".apk", ".app", ".bin", ".elf", ".lnk", ".reg", ".html",
        ".htm", ".xhtml", ".svg", ".zip", ".7z", ".rar", ".gz", ".tar", ".cab", ".iso", ".docm", ".xlsm"};
    return s;
}

bool pngSize(const std::string& b, int& w, int& h) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 33 || std::memcmp(b.data(), sig, 8) != 0 || b.compare(12, 4, "IHDR") != 0) return false;
    auto be32 = [&b](size_t p) {
        return (uint32_t(uint8_t(b[p])) << 24) | (uint32_t(uint8_t(b[p + 1])) << 16) |
               (uint32_t(uint8_t(b[p + 2])) << 8) | uint8_t(b[p + 3]);
    };
    const uint32_t ww = be32(16), hh = be32(20);
    if (ww == 0 || hh == 0 || ww > 1u << 24 || hh > 1u << 24) return false;
    w = static_cast<int>(ww); h = static_cast<int>(hh);
    return true;
}

bool jpegSize(const std::string& b, int& w, int& h) {
    if (b.size() < 4 || uint8_t(b[0]) != 0xFF || uint8_t(b[1]) != 0xD8) return false;
    size_t p = 2;
    while (p + 9 < b.size()) {
        if (uint8_t(b[p]) != 0xFF) return false;
        const uint8_t marker = uint8_t(b[p + 1]);
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { p += 2; continue; }
        const size_t len = (size_t(uint8_t(b[p + 2])) << 8) | uint8_t(b[p + 3]);
        if (len < 2) return false;
        if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
            (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF)) {
            h = (int(uint8_t(b[p + 5])) << 8) | uint8_t(b[p + 6]);
            w = (int(uint8_t(b[p + 7])) << 8) | uint8_t(b[p + 8]);
            return w > 0 && h > 0;
        }
        p += 2 + len;
    }
    return false;
}

bool oggVorbis(const std::string& b) {
    // First Ogg page, then the Vorbis identification header ("\x01vorbis")
    if (b.size() < 64 || b.compare(0, 4, "OggS") != 0) return false;
    const size_t segs = uint8_t(b[26]);
    const size_t body = 27 + segs;
    return body + 7 <= b.size() && b[body] == '\x01' && b.compare(body + 1, 6, "vorbis") == 0;
}

bool wavPcm(const std::string& b) {
    if (b.size() < 44 || b.compare(0, 4, "RIFF") != 0 || b.compare(8, 4, "WAVE") != 0) return false;
    return b.find("fmt ", 12) != std::string::npos && b.find("data", 12) != std::string::npos;
}

// ---- JSON ----
struct JsonParser {
    const std::string& s;
    size_t p = 0;
    int values = 0;
    std::string why;
    explicit JsonParser(const std::string& text) : s(text) {}

    void ws() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p; }
    bool fail(const std::string& msg) { if (why.empty()) why = msg + " (at byte " + std::to_string(p) + ")"; return false; }
    bool lit(const char* word) {
        const size_t n = std::strlen(word);
        if (s.compare(p, n, word) != 0) return false;
        p += n;
        return true;
    }
    static void utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    bool hex4(uint32_t& v) {
        if (p + 4 > s.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool str(std::string& out) {
        if (p >= s.size() || s[p] != '"') return fail("expected a string");
        ++p;
        while (p < s.size()) {
            const char c = s[p++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in a string");
            if (c != '\\') { out += c; continue; }
            if (p >= s.size()) break;
            const char e = s[p++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return fail("bad \\u escape");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t lo;
                        if (p + 6 > s.size() || s[p] != '\\' || s[p + 1] != 'u') return fail("lone surrogate");
                        p += 2;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("lone surrogate");
                    }
                    if (cp == 0) return fail("NUL in a string");
                    utf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return fail("nested too deeply (more than 64 levels)");
        if (++values > 200000) return fail("too many values (more than 200 000)");
        ws();
        if (p >= s.size()) return fail("unexpected end");
        const char c = s[p];
        if (c == '{') {
            v.type = Json::Object;
            ++p; ws();
            if (p < s.size() && s[p] == '}') { ++p; return true; }
            while (true) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (p >= s.size() || s[p] != ':') return fail("expected ':'");
                ++p;
                Json child;
                if (!value(child, depth + 1)) return false;
                v.fields.emplace_back(std::move(k), std::move(child));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == '}') { ++p; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Json::Array;
            ++p; ws();
            if (p < s.size() && s[p] == ']') { ++p; return true; }
            while (true) {
                Json child;
                if (!value(child, depth + 1)) return false;
                v.items.push_back(std::move(child));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == ']') { ++p; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = Json::String; return str(v.s); }
        if (lit("true")) { v.type = Json::Bool; v.b = true; return true; }
        if (lit("false")) { v.type = Json::Bool; v.b = false; return true; }
        if (lit("null")) { v.type = Json::Null; return true; }
        // number
        const size_t start = p;
        if (p < s.size() && s[p] == '-') ++p;
        if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) return fail("unexpected character");
        if (s[p] == '0') ++p; else while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
        if (p < s.size() && s[p] == '.') {
            ++p;
            if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) return fail("bad number");
            while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
            ++p;
            if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p;
            if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) return fail("bad number");
            while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
        }
        v.type = Json::Number;
        try { v.n = std::stod(s.substr(start, p - start)); } catch (...) { return fail("bad number"); }
        if (!std::isfinite(v.n)) return fail("number out of range");
        return true;
    }
};

} // namespace

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Json: return "json";
        case Kind::Text: return "text";
        case Kind::Image: return "image";
        case Kind::Audio: return "audio";
        default: return "none";
    }
}

bool parseJson(const std::string& text, Json& out, std::string& why) {
    JsonParser jp(text);
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) jp.p = 3;   // UTF-8 BOM
    if (!jp.value(out, 0)) { why = jp.why; return false; }
    jp.ws();
    if (jp.p != text.size()) { why = "extra data after the JSON value (at byte " + std::to_string(jp.p) + ")"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// URL rules
// ---------------------------------------------------------------------------
bool parseUrl(const std::string& text, Url& out, std::string& why) {
    if (text.size() > 2000) { why = "the address is too long"; return false; }
    for (unsigned char c : text) {
        if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '"' || c == '<' || c == '>' || c == '`' || c == '{' ||
            c == '}' || c == '|' || c == '^') {
            why = "the address has characters that aren't allowed (spaces, backslashes, non-ASCII...)";
            return false;
        }
    }
    const std::string prefix = "https://";
    if (lower(text.substr(0, prefix.size())) != prefix) { why = "only https:// addresses are allowed"; return false; }
    std::string rest = text.substr(prefix.size());
    const size_t hash = rest.find('#');
    if (hash != std::string::npos) rest.resize(hash);
    const size_t slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (authority.find('@') != std::string::npos) { why = "user names and passwords in addresses aren't allowed"; return false; }
    if (authority.find('[') != std::string::npos) { why = "IP addresses aren't allowed - use a domain name"; return false; }
    out.port = 443;
    const size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        if (authority.substr(colon + 1) != "443") { why = "only the normal HTTPS port (443) is allowed"; return false; }
        authority.resize(colon);
    }
    out.host = lower(authority);
    out.scheme = "https";
    if (out.host.empty() || out.host.size() > 253 || out.host.find('.') == std::string::npos ||
        out.host.front() == '.' || out.host.back() == '.' || out.host.find("..") != std::string::npos) {
        why = "that isn't a valid domain name";
        return false;
    }
    for (char c : out.host) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')) { why = "that isn't a valid domain name"; return false; }
    }
    const bool numeric = std::all_of(out.host.begin(), out.host.end(),
                                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; });
    if (numeric) { why = "IP addresses aren't allowed - use a domain name"; return false; }
    if (out.host == "localhost" || endsWith(out.host, ".localhost") || endsWith(out.host, ".local") ||
        endsWith(out.host, ".internal") || endsWith(out.host, ".lan")) {
        why = "local network addresses aren't allowed";
        return false;
    }
    // Path: no "." / ".." segments and no encoded dots, slashes or backslashes, so a URL can't
    // climb out of the folder a mod declared
    const std::string pathOnly = out.path.substr(0, out.path.find('?'));
    const std::string lp = lower(pathOnly);
    if (lp.find("%2e") != std::string::npos || lp.find("%2f") != std::string::npos || lp.find("%5c") != std::string::npos ||
        lp.find("%00") != std::string::npos) {
        why = "encoded dots or slashes in addresses aren't allowed";
        return false;
    }
    std::stringstream ss(pathOnly);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg == "." || seg == "..") { why = "'.' and '..' in addresses aren't allowed"; return false; }
    }
    return true;
}

bool hostAllowed(const std::string& host, const Policy& policy, std::string& why) {
    if (host == "raw.githubusercontent.com" || host == "gist.githubusercontent.com") return true;
    // GitHub Pages: <user>.github.io (one name in front)
    if (endsWith(host, ".github.io")) {
        const std::string user = host.substr(0, host.size() - std::string(".github.io").size());
        if (!user.empty() && user.find('.') == std::string::npos) return true;
    }
    for (const auto& d : policy.extraDomains) {
        if (d.empty()) continue;
        if (host == d || endsWith(host, "." + d)) return true;
    }
    why = "'" + host + "' isn't an approved site (GitHub raw, GitHub Pages, or remote_domains in the settings)";
    return false;
}

bool underPrefix(const std::string& url, const std::vector<std::string>& prefixes) {
    for (const auto& pre : prefixes) {
        if (pre.empty() || url.compare(0, pre.size(), pre) != 0) continue;
        if (url.size() == pre.size() || pre.back() == '/' || url[pre.size()] == '/' || url[pre.size()] == '?') return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Content rules
// ---------------------------------------------------------------------------
bool validateContent(const std::string& url, const std::string& body, Kind& kind, std::string& why) {
    const std::string ext = urlExtension(url);
    if (blockedExtensions().count(ext)) {
        why = "'" + ext + "' files can't be downloaded (online content is data only: JSON, text, PNG/JPEG, OGG/WAV)";
        return false;
    }
    if (const char* what = executableSignature(body)) {
        why = std::string("refused: the file is ") + what + " - online content is data only";
        return false;
    }
    int w = 0, h = 0;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        const bool ok = (ext == ".png") ? pngSize(body, w, h) : jpegSize(body, w, h);
        if (!ok) { why = "it isn't a real " + ext.substr(1) + " picture"; return false; }
        if (w > kMaxImageSide || h > kMaxImageSide) { why = "the picture is too big (up to 4096 x 4096)"; return false; }
        if (body.size() > kMaxImage) { why = "the picture file is too big (up to 8 MB)"; return false; }
        kind = Kind::Image;
        return true;
    }
    if (ext == ".ogg" || ext == ".wav") {
        const bool ok = (ext == ".ogg") ? oggVorbis(body) : wavPcm(body);
        if (!ok) { why = "it isn't a real " + ext.substr(1) + " sound"; return false; }
        if (body.size() > kMaxAudio) { why = "the sound file is too big (up to 16 MB)"; return false; }
        kind = Kind::Audio;
        return true;
    }
    const bool jsonExt = ext == ".json";
    const bool textExt = ext == ".txt" || ext == ".md" || ext == ".csv" || ext == ".tsv";
    if (!jsonExt && !textExt && !ext.empty()) {
        why = "'" + ext + "' files aren't supported (JSON, text, PNG/JPEG, OGG/WAV)";
        return false;
    }
    // JSON, text, or no extension (a GitHub Pages address like /news/): must be UTF-8 text
    if (body.size() > kMaxText) { why = "the text is too big (up to 4 MB)"; return false; }
    if (!validUtf8(body)) { why = "it isn't text (not valid UTF-8, or has NUL bytes)"; return false; }
    if (jsonExt || (ext.empty() && !body.empty() && (body.find_first_not_of(" \t\r\n") != std::string::npos) &&
                    (body[body.find_first_not_of(" \t\r\n")] == '{' || body[body.find_first_not_of(" \t\r\n")] == '['))) {
        Json j;
        std::string err;
        if (!parseJson(body, j, err)) {
            if (jsonExt) { why = "it isn't valid JSON: " + err; return false; }
        } else {
            kind = Kind::Json;
            return true;
        }
    }
    kind = Kind::Text;
    return true;
}

// ---------------------------------------------------------------------------
// HTTPS (no redirects followed here: the caller checks each one)
// ---------------------------------------------------------------------------
namespace {

struct Response {
    int status = 0;
    std::string body, etag, lastModified, location, error;
    bool tooBig = false;
};

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
std::string queryHeader(HINTERNET req, DWORD what) {
    DWORD size = 0;
    WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return "";
    std::wstring buf(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &size, WINHTTP_NO_HEADER_INDEX)) return "";
    buf.resize(size / sizeof(wchar_t));
    return narrow(buf);
}

bool httpGet(const Url& url, const std::string& etag, const std::string& lastModified, Response& r) {
    struct Handle { HINTERNET h = nullptr; ~Handle() { if (h) WinHttpCloseHandle(h); } };
    Handle session, connect, request;
    session.h = WinHttpOpen(L"BlockSmith/1.0 (online mod content)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { r.error = "couldn't start HTTPS"; return false; }
    WinHttpSetTimeouts(session.h, 10000, 10000, 20000, 20000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    connect.h = WinHttpConnect(session.h, widen(url.host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect.h) { r.error = "couldn't connect to " + url.host; return false; }
    request.h = WinHttpOpenRequest(connect.h, L"GET", widen(url.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request.h) { r.error = "couldn't make the request"; return false; }
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;      // redirects are checked by hand
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
#ifdef WINHTTP_OPTION_DECOMPRESSION
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));   // (Windows 8.1+)
#endif
    std::wstring headers;
    if (!etag.empty()) headers += L"If-None-Match: " + widen(etag) + L"\r\n";
    if (!lastModified.empty()) headers += L"If-Modified-Since: " + widen(lastModified) + L"\r\n";
    if (!WinHttpSendRequest(request.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = "couldn't reach " + url.host + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    r.etag = queryHeader(request.h, WINHTTP_QUERY_ETAG);
    r.lastModified = queryHeader(request.h, WINHTTP_QUERY_LAST_MODIFIED);
    r.location = queryHeader(request.h, WINHTTP_QUERY_LOCATION);
    if (r.status != 200) return true;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) { r.error = "the download broke off"; return false; }
        if (avail == 0) break;
        if (r.body.size() + avail > kMaxDownload) { r.tooBig = true; return true; }
        const size_t at = r.body.size();
        r.body.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, &r.body[at], avail, &got)) { r.error = "the download broke off"; return false; }
        r.body.resize(at + got);
    }
    return true;
}
#elif defined(BLOCKSMITH_HAS_CURL)
size_t onBody(char* data, size_t size, size_t n, void* user) {
    auto* r = static_cast<Response*>(user);
    const size_t bytes = size * n;
    if (r->body.size() + bytes > kMaxDownload) { r->tooBig = true; return 0; }   // stops the transfer
    r->body.append(data, bytes);
    return bytes;
}
size_t onHeader(char* data, size_t size, size_t n, void* user) {
    auto* r = static_cast<Response*>(user);
    const std::string line(data, size * n);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
        const std::string name = lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.pop_back();
        if (name == "etag") r->etag = value;
        else if (name == "last-modified") r->lastModified = value;
        else if (name == "location") r->location = value;
    }
    return size * n;
}
bool httpGet(const Url& url, const std::string& etag, const std::string& lastModified, Response& r) {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* c = curl_easy_init();
    if (!c) { r.error = "couldn't start HTTPS"; return false; }
    const std::string full = "https://" + url.host + url.path;
    curl_easy_setopt(c, CURLOPT_URL, full.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);          // redirects are checked by hand
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "BlockSmith/1.0 (online mod content)");
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(kMaxDownload));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &r);
    struct curl_slist* hdrs = nullptr;
    if (!etag.empty()) hdrs = curl_slist_append(hdrs, ("If-None-Match: " + etag).c_str());
    if (!lastModified.empty()) hdrs = curl_slist_append(hdrs, ("If-Modified-Since: " + lastModified).c_str());
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    r.status = static_cast<int>(status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (r.tooBig || rc == CURLE_FILESIZE_EXCEEDED) { r.tooBig = true; return true; }
    if (rc != CURLE_OK) { r.error = std::string("couldn't download: ") + curl_easy_strerror(rc); return false; }
    return true;
}
#else
bool httpGet(const Url&, const std::string&, const std::string&, Response& r) {
    r.error = "this build of the game can't download (no HTTPS support)";
    return false;
}
#endif

} // namespace

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------
bool Client::httpsAvailable() {
#if defined(_WIN32) || defined(BLOCKSMITH_HAS_CURL)
    return true;
#else
    return false;
#endif
}

Client::Client(fs::path cacheDir, Policy policy, int threads) : m_dir(std::move(cacheDir)), m_policy(std::move(policy)) {
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    pruneCache();
    for (int i = 0; i < std::max(1, threads); ++i) m_threads.emplace_back(&Client::worker, this);
}

Client::~Client() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_jobs.clear();
    }
    m_wake.notify_all();
    for (auto& t : m_threads) if (t.joinable()) t.join();
}

void Client::setPolicy(const Policy& p) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_policy = p;
}

std::string Client::key(const std::string& url) const { return sha256Hex(url).substr(0, 32); }

bool Client::readMeta(const std::string& url, Meta& m) const {
    std::ifstream in(m_dir / (key(url) + ".meta"));
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "url") m.url = v;
        else if (k == "etag") m.etag = v;
        else if (k == "last_modified") m.lastModified = v;
        else if (k == "file") m.file = v;
        else if (k == "sha256") m.sha256 = v;
        else if (k == "checked") { try { m.checked = std::stod(v); } catch (...) {} }
        else if (k == "kind") m.kind = v == "json" ? Kind::Json : v == "text" ? Kind::Text : v == "image" ? Kind::Image
                                     : v == "audio" ? Kind::Audio : Kind::None;
    }
    std::error_code ec;
    // The cached file must be ours: a plain name in the cache folder that still exists
    if (m.url != url || m.file.empty() || m.file.find_first_of("/\\") != std::string::npos ||
        !fs::is_regular_file(m_dir / m.file, ec)) return false;
    return true;
}

void Client::writeMeta(const Meta& m) const {
    auto clean = [](std::string s) { s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '\n' || c == '\r'; }), s.end()); return s; };
    const fs::path file = m_dir / (key(m.url) + ".meta"), tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "url=" << clean(m.url) << "\netag=" << clean(m.etag) << "\nlast_modified=" << clean(m.lastModified)
            << "\nfile=" << m.file << "\nsha256=" << m.sha256 << "\nkind=" << kindName(m.kind)
            << "\nchecked=" << static_cast<long long>(m.checked) << "\n";
    }
    std::error_code ec;
    replaceFile(tmp, file, ec);
}

// Oldest files go first once the cache is over its size limit
void Client::pruneCache() {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    uintmax_t total = 0;
    for (const auto& e : fs::directory_iterator(m_dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        total += e.file_size(ec);
        files.emplace_back(e.last_write_time(ec), e.path());
    }
    if (total <= kMaxCacheBytes) return;
    std::sort(files.begin(), files.end());
    for (const auto& [t, p] : files) {
        if (total <= kMaxCacheBytes * 3 / 4) break;
        total -= fs::file_size(p, ec);
        fs::remove(p, ec);
    }
}

bool Client::cached(const std::string& url, Result& out) {
    Meta m;
    if (!readMeta(url, m)) return false;
    out = Result{};
    out.url = url;
    out.ok = true;
    out.kind = m.kind;
    out.file = (m_dir / m.file).string();
    out.sha256 = m.sha256;
    out.fromCache = true;
    if (m.kind == Kind::Json || m.kind == Kind::Text) {
        std::ifstream in(m_dir / m.file, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        out.text = ss.str();
    }
    return true;
}

uint64_t Client::fetch(const std::string& url, bool revalidate, double maxAge) {
    const uint64_t id = m_nextId++;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& waiting = m_inFlight[url];
        waiting.push_back(id);
        if (waiting.size() == 1) m_jobs.push_back(Job{id, url, revalidate, maxAge});   // else: joins that download
    }
    m_wake.notify_one();
    return id;
}

std::vector<Result> Client::takeFinished() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Result> out;
    out.swap(m_done);
    return out;
}

size_t Client::pending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inFlight.size();
}

void Client::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_stop) return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }
        Result r = run(job);
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_inFlight.find(job.url);
        const std::vector<uint64_t> ids = it == m_inFlight.end() ? std::vector<uint64_t>{job.id} : it->second;
        if (it != m_inFlight.end()) m_inFlight.erase(it);
        for (uint64_t id : ids) { Result copy = r; copy.id = id; m_done.push_back(std::move(copy)); }
    }
}

Result Client::run(const Job& job) {
    Result r;
    r.url = job.url;
    Policy policy;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        policy = m_policy;
    }
    auto fail = [&r](const std::string& why) { r.ok = false; r.error = why; return r; };
    if (!policy.enabled) return fail("online content is switched off (remote_content in the settings)");

    Url url;
    std::string why;
    if (!parseUrl(job.url, url, why) || !hostAllowed(url.host, policy, why)) return fail(why);
    {                                                                // refused before connecting
        static const std::set<std::string> supported = {"", ".json", ".txt", ".md", ".csv", ".tsv",
                                                        ".png", ".jpg", ".jpeg", ".ogg", ".wav"};
        const std::string ext = urlExtension(job.url);
        if (!supported.count(ext)) {
            return fail("'" + ext + "' files can't be downloaded (online content is data only: "
                        "JSON, text, PNG/JPEG, OGG/WAV)");
        }
    }

    Meta meta;
    const bool haveCache = readMeta(job.url, meta);
    if (haveCache && !job.revalidate && job.maxAge > 0.0 && nowUnix() - meta.checked < job.maxAge) {
        Result c;
        cached(job.url, c);
        return c;                                   // fresh enough: no network at all
    }

    // Download, following at most kMaxRedirects redirects - each one checked again, and only
    // to the same (whitelisted) host
    Response resp;
    Url current = url;
    for (int hop = 0; ; ++hop) {
        resp = Response{};
        const bool sameAsCached = hop == 0 && haveCache;
        if (!httpGet(current, sameAsCached ? meta.etag : "", sameAsCached ? meta.lastModified : "", resp)) {
            if (haveCache) {                        // offline: the last good copy, marked stale
                Result c;
                cached(job.url, c);
                c.stale = true;
                c.error = resp.error;
                return c;
            }
            return fail(resp.error);
        }
        if (resp.status >= 300 && resp.status < 400 && resp.status != 304) {
            if (hop >= kMaxRedirects) return fail("too many redirects");
            std::string target = resp.location;
            if (!target.empty() && target[0] == '/') target = "https://" + current.host + target;
            Url next;
            if (!parseUrl(target, next, why)) return fail("redirected to an address that isn't allowed: " + why);
            if (next.host != current.host) return fail("redirected to another site (" + next.host + ") - not followed");
            current = next;
            continue;
        }
        break;
    }
    r.status = resp.status;

    if (resp.status == 304 && haveCache) {          // unchanged: nothing downloaded
        meta.checked = nowUnix();
        writeMeta(meta);
        Result c;
        cached(job.url, c);
        c.status = 304;
        return c;
    }
    if (resp.status != 200) return fail("the site answered " + std::to_string(resp.status));
    if (resp.tooBig) return fail("the file is too big (up to 16 MB)");

    Kind kind = Kind::None;
    if (!validateContent(job.url, resp.body, kind, why)) return fail(why);

    // Stored under a hashed name (never the site's file name); a new version gets a new name, so
    // the engine's picture and sound caches pick it up
    const std::string sha = sha256Hex(resp.body);
    const char* ext = kind == Kind::Json ? ".json" : kind == Kind::Text ? ".txt"
                    : kind == Kind::Audio ? (resp.body.compare(0, 4, "OggS") == 0 ? ".ogg" : ".wav")
                    : (resp.body.size() > 1 && uint8_t(resp.body[0]) == 0x89 ? ".png" : ".jpg");
    const std::string fileName = key(job.url) + "-" + sha.substr(0, 12) + ext;
    std::error_code ec;
    if (!haveCache || meta.file != fileName) {
        const fs::path tmp = m_dir / (fileName + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
            if (!out) return fail("couldn't save it in the cache");
        }
        if (!replaceFile(tmp, m_dir / fileName, ec)) return fail("couldn't save it in the cache");
        if (haveCache && meta.file != fileName) fs::remove(m_dir / meta.file, ec);   // the old version
    }
    const bool changed = !haveCache || meta.sha256 != sha;
    meta.url = job.url;
    meta.etag = resp.etag;
    meta.lastModified = resp.lastModified;
    meta.file = fileName;
    meta.sha256 = sha;
    meta.kind = kind;
    meta.checked = nowUnix();
    writeMeta(meta);

    r.ok = true;
    r.kind = kind;
    r.file = (m_dir / fileName).string();
    r.sha256 = sha;
    r.changed = changed;
    if (kind == Kind::Json || kind == Kind::Text) {
        r.text = resp.body;
        if (r.text.size() >= 3 && r.text.compare(0, 3, "\xEF\xBB\xBF") == 0) r.text.erase(0, 3);
    }
    return r;
}

} // namespace remote
