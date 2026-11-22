#pragma once
// UpdateCheck: is there a newer BlockSmith to install?
//
// The engine downloads one small JSON file - the "version manifest" - and compares it with the
// version it was built as (Version.h). It is DATA ONLY, like all remote content (RemoteContent.h):
// fetched over HTTPS from a whitelisted host, checked to really be JSON, and only a few plain
// fields are read from it. Nothing is ever downloaded or installed by the engine itself: it
// says a new version exists, and where to get it (the player's browser opens that page when
// they click it; a server prints it in its console).
//
// The manifest (version.json):
//   {
//     "latest": "0.2-alpha",                  the newest version
//     "min_supported": "0.1-alpha",           optional: older than this must update (it can't
//                                             play with up-to-date games and servers any more)
//     "commit": "1a2b3c4d5e",                 optional: the newest version's commit
//     "download": "https://github.com/Gerold55/BlockSmith/releases/latest",
//                                             optional: where to get it (https, github.com or
//                                             *.github.io, or the manifest's own site)
//     "notes": "New biomes and menus"         optional: one line about it (up to 300 characters)
//   }
//
// Settings: update_check = true|false and update_url = <manifest URL> (settings.txt for the
// game, server.conf for a server). The URL must be allowed like any remote content: GitHub raw
// files / GitHub Pages, or a domain approved in remote_domains.

#include "RemoteContent.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace update {

inline const char* kDefaultUrl = "https://raw.githubusercontent.com/Gerold55/BlockSmith/main/version.json";

struct Info {
    bool checked = false;     // a manifest has been read (the fields below are from it)
    bool available = false;   // a newer version than this one exists
    bool required = false;    // this version is older than min_supported: it has to be updated
    std::string current = BLOCKSMITH_VERSION;
    std::string latest, minSupported, commit, download, notes;
    std::string error;        // why the last check failed ("" = it worked)
};

// ---------------------------------------------------------------------------
// Versions: "0.1", "0.1.0", "0.1-alpha", "0.2-beta". Numbers compare one by one (missing ones are 0); a
// version with a "-something" after the numbers comes before the same numbers without it.
// ---------------------------------------------------------------------------
struct Version {
    std::vector<int> parts;
    std::string pre;
};

inline bool parseVersion(const std::string& text, Version& out) {
    out = Version{};
    if (text.empty() || text.size() > 40) return false;
    size_t i = 0;
    if (text[0] == 'v' || text[0] == 'V') i = 1;
    while (true) {
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        long v = 0;
        size_t digits = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            if (++digits > 6) return false;
            v = v * 10 + (text[i] - '0');
            ++i;
        }
        out.parts.push_back(static_cast<int>(v));
        if (out.parts.size() > 4) return false;
        if (i == text.size()) return true;
        if (text[i] == '.') { ++i; continue; }
        if (text[i] == '-') break;
        return false;
    }
    out.pre = text.substr(i + 1);
    if (out.pre.empty()) return false;
    for (char c : out.pre) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
    }
    return true;
}

// < 0: a is older, 0: the same, > 0: a is newer
inline int compare(const Version& a, const Version& b) {
    const size_t n = std::max(a.parts.size(), b.parts.size());
    for (size_t k = 0; k < n; ++k) {
        const int x = k < a.parts.size() ? a.parts[k] : 0;
        const int y = k < b.parts.size() ? b.parts[k] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (a.pre.empty() != b.pre.empty()) return a.pre.empty() ? 1 : -1;   // 0.1-alpha < 0.1
    return a.pre == b.pre ? 0 : (a.pre < b.pre ? -1 : 1);
}

// Releases only: "0.1-alpha" counts as 0.1 (who may join whom: builds of the same release
// play together, a newer release can join an older one, an older one can't join a newer one)
inline int compareRelease(const Version& a, const Version& b) {
    Version x = a, y = b;
    x.pre.clear();
    y.pre.clear();
    return compare(x, y);
}

// Plain text for a console or the screen: printable characters only, at most `max` bytes
// (cut at a whole UTF-8 character)
inline std::string plainText(const std::string& s, size_t max) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) c = ' ';
        out.push_back(static_cast<char>(c));
    }
    if (out.size() > max) {
        size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out = out.substr(0, cut) + "...";
    }
    return out;
}

// Only https pages on GitHub (github.com, *.github.io) or the manifest's own site, written
// with ordinary URL characters: nothing a browser or the system could read as more than an
// address. "" if it isn't one.
inline std::string safeDownload(const std::string& url, const std::string& manifestHost) {
    if (url.size() > 300 || url.rfind("https://", 0) != 0) return "";
    for (unsigned char c : url) {
        if (!(std::isalnum(c) || std::string("-._~:/?#[]@!&()*+,;=%").find(static_cast<char>(c)) != std::string::npos)) return "";
    }
    remote::Url u;
    std::string why;
    if (!remote::parseUrl(url, u, why) || u.scheme != "https") return "";
    std::string host = u.host;
    for (char& c : host) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto endsWith = [](const std::string& s, const std::string& end) {
        return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
    };
    const bool ok = host == "github.com" || endsWith(host, ".github.io") || (!manifestHost.empty() && host == manifestHost);
    return ok ? url : "";
}

// Reads a manifest. False + why if it isn't one.
inline bool readManifest(const std::string& text, const std::string& manifestUrl, Info& info, std::string& why) {
    remote::Json root;
    if (!remote::parseJson(text, root, why)) return false;
    if (root.type != remote::Json::Object) { why = "the version file isn't a JSON object"; return false; }
    auto field = [&root](const char* name) -> const remote::Json* {
        for (const auto& f : root.fields) if (f.first == name) return &f.second;
        return nullptr;
    };
    auto str = [&](const char* name) {
        const remote::Json* j = field(name);
        return (j && j->type == remote::Json::String) ? j->s : std::string();
    };
    Info out;
    out.latest = str("latest");
    Version latest, current, minimum;
    if (!parseVersion(out.latest, latest)) { why = "the version file has no valid \"latest\" version"; return false; }
    if (!parseVersion(out.current, current)) { why = "this build's version isn't readable"; return false; }
    out.minSupported = str("min_supported");
    if (!out.minSupported.empty() && !parseVersion(out.minSupported, minimum)) out.minSupported.clear();
    out.commit = str("commit");
    if (out.commit.size() > 40 || !std::all_of(out.commit.begin(), out.commit.end(),
                                               [](unsigned char c) { return std::isxdigit(c) != 0; })) out.commit.clear();
    remote::Url mu;
    std::string ignore;
    const std::string mhost = remote::parseUrl(manifestUrl, mu, ignore) ? mu.host : "";
    out.download = safeDownload(str("download"), mhost);
    out.notes = plainText(str("notes"), 300);
    out.available = compare(latest, current) > 0;
    out.required = !out.minSupported.empty() && compare(current, minimum) < 0;
    out.checked = true;
    info = out;
    return true;
}

// ---------------------------------------------------------------------------
// The checker: asks now and then (in the background), keeps the last answer.
// ---------------------------------------------------------------------------
class Checker {
public:
    // enabled = false: never asks. interval: seconds between checks.
    Checker(const remote::fs::path& cacheDir, std::string url, const remote::Policy& policy, bool enabled,
            double interval = 6 * 3600.0)
        : m_url(std::move(url)), m_enabled(enabled), m_interval(interval) {
        if (!m_enabled) return;
        if (!remote::Client::httpsAvailable()) {
            m_info.error = "this build can't download (no HTTPS support)";
            m_enabled = false;
            m_news = true;
            return;
        }
        remote::Policy p = policy;
        p.enabled = true;          // (mods' remote_content setting doesn't turn this off: update_check does)
        m_client = std::make_unique<remote::Client>(cacheDir, p, 1);
    }

    // Call every frame / tick with the time in seconds
    void update(double now) {
        if (!m_enabled || !m_client) return;
        if (!m_asking && (m_next < 0 || now >= m_next)) {
            m_client->fetch(m_url, true, 0.0);
            m_asking = true;
            m_next = now + m_interval;
        }
        for (auto& r : m_client->takeFinished()) {
            m_asking = false;
            std::string why;
            Info info;
            m_lastStatus = r.status;
            m_lastFromCache = r.fromCache;
            m_lastStale = r.stale;
            m_lastResultOk = r.ok;
            if (r.ok && r.kind == remote::Kind::Json && readManifest(r.text, m_url, info, why)) {
                m_info = info;
            } else {
                // keep what an earlier check found; say why this one didn't work
                m_info.error = !r.ok ? r.error : (r.kind != remote::Kind::Json ? "the version file isn't JSON" : why);
                if (m_info.error.empty()) m_info.error = "unknown error";
            }
            m_news = true;
        }
    }

    // True once after each check finished (or failed): time to tell someone
    bool takeNews() { const bool n = m_news; m_news = false; return n; }
    const Info& info() const { return m_info; }
    bool enabled() const { return m_enabled; }
    const std::string& url() const { return m_url; }

    // A warning to show: a new version, one that must be installed, or a check that failed
    bool warning() const {
        const Info& i = m_info;
        return i.available || i.required || (!i.error.empty());
    }

    // The details behind the last answer, one line each (for the console / log, under the
    // warning): what was asked, what came back, and why it decided what it did
    std::vector<std::string> trace() const {
        const Info& i = m_info;
        std::vector<std::string> t;
        t.push_back("version file: " + m_url);
        std::string http = m_lastStatus ? "HTTP " + std::to_string(m_lastStatus) : std::string("no HTTP answer");
        if (m_lastStatus == 304) http += " (not changed since the last check)";
        if (m_lastFromCache) http += ", using the cached copy";
        if (m_lastStale) http += " - the network failed, this is the last good copy";
        t.push_back("result: " + http + (m_lastResultOk ? "" : ", download failed"));
        t.push_back(std::string("this build: ") + BLOCKSMITH_VERSION + " (commit " + BLOCKSMITH_COMMIT + ")");
        if (i.checked) {
            t.push_back("latest: " + i.latest + (i.commit.empty() ? "" : " (commit " + i.commit + ")") +
                        ", min_supported: " + (i.minSupported.empty() ? "none" : i.minSupported));
            if (i.required) t.push_back("decision: " + i.current + " is older than min_supported " + i.minSupported + " -> must update");
            else if (i.available) t.push_back("decision: " + i.latest + " is newer than " + i.current + " -> update available");
            else t.push_back("decision: " + i.current + " is the latest or newer -> up to date");
            t.push_back("download: " + (i.download.empty() ? std::string("(none given, or not an allowed https page)") : i.download));
        }
        if (!i.error.empty()) t.push_back("error: " + i.error);
        return t;
    }

    // One line for a console or a message: "", or what's new / that it's up to date
    std::string summary() const {
        const Info& i = m_info;
        if (!i.checked) return i.error.empty() ? "" : "Couldn't check for updates: " + i.error;
        std::string where = i.download.empty() ? "" : " Download: " + i.download;
        if (i.required) {
            return "This version (" + i.current + ") is too old and must be updated: install " + i.latest +
                   " (" + i.minSupported + " or newer is needed)." + where;
        }
        if (i.available) {
            return "A new version is available: " + i.latest + (i.commit.empty() ? "" : " (commit " + i.commit + ")") +
                   ", you have " + i.current + " (commit " + BLOCKSMITH_COMMIT + ")." +
                   (i.notes.empty() ? "" : " " + i.notes) + where;
        }
        return "Up to date (" + i.current + ", latest " + i.latest + ")";
    }

private:
    std::string m_url;
    bool m_enabled = false;
    double m_interval;
    double m_next = -1.0;
    bool m_asking = false;
    bool m_news = false;
    int m_lastStatus = 0;
    bool m_lastFromCache = false, m_lastStale = false, m_lastResultOk = false;
    Info m_info;
    std::unique_ptr<remote::Client> m_client;
};

} // namespace update
