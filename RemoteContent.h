#pragma once
// RemoteContent: mods' online content - DATA ONLY.
//
// Mods can show content that lives on GitHub (raw files or GitHub Pages) or on domains the
// player / server owner approved, and pick up changes without a new mod download (like the
// in-game browser of some Minecraft mods). The engine downloads, checks and caches it; mods get
// plain data back (JSON as tables, text as strings, pictures for ui.draw_image, sounds for
// sound.play). Nothing downloaded is ever run:
//
//   - Only HTTPS, to whitelisted hosts: raw.githubusercontent.com, gist.githubusercontent.com,
//     <name>.github.io, plus domains approved in settings.txt / server.conf (remote_domains).
//     No IP addresses, localhost, other ports, user:password@, "..", or encoded slashes/dots.
//   - A mod may only fetch below the URL prefixes it declares in mod.conf (remote_sources).
//   - Redirects are followed by hand (at most 3), and only to the same whitelisted host.
//   - Kinds: JSON (.json), text (.txt .md .csv .tsv), images (.png .jpg .jpeg), audio (.ogg .wav).
//     The bytes must really be that kind (checked by content, not just the name), and anything
//     that looks like a program (EXE/DLL, ELF, Mach-O, scripts with #!, Lua bytecode, ZIP/JAR,
//     WebAssembly) or has a code extension (.lua .js .exe .dll ...) is refused.
//   - Size limits per kind; JSON depth and size limits; text must be valid UTF-8.
//   - Downloads are stored in a cache folder outside every mod folder (require/dofile can't
//     reach it), named by a hash - never by the server's file name.
//
// Updates: each file is cached with its ETag / Last-Modified; checking again asks the server
// "changed since?" (HTTP 304 = no, nothing is downloaded). Results say whether it changed.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace remote {

namespace fs = std::filesystem;

enum class Kind { None, Json, Text, Image, Audio };
const char* kindName(Kind k);

// A parsed JSON value (kept simple: the Lua side turns it into tables)
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Json> items;                              // Array
    std::vector<std::pair<std::string, Json>> fields;     // Object (in file order)
};
// Strict JSON: depth <= 64, at most 200 000 values. False + why on anything else.
bool parseJson(const std::string& text, Json& out, std::string& why);

struct Policy {
    bool enabled = true;
    std::vector<std::string> extraDomains;   // "example.com" (that host and its subdomains), lower case
};

struct Url {
    std::string scheme, host, path;   // path includes the query (no fragment)
    int port = 443;
};
// Parses and checks a URL against the safety rules above (not the mod's prefixes)
bool parseUrl(const std::string& text, Url& out, std::string& why);
bool hostAllowed(const std::string& host, const Policy& policy, std::string& why);
// Is `url` inside one of the prefixes (on a path-segment boundary)?
bool underPrefix(const std::string& url, const std::vector<std::string>& prefixes);

// Is this data acceptable, and of what kind? (extension from the URL + the bytes themselves)
bool validateContent(const std::string& url, const std::string& body, Kind& kind, std::string& why);

struct Result {
    uint64_t id = 0;
    std::string url;
    bool ok = false;
    std::string error;
    Kind kind = Kind::None;
    std::string file;      // the cached copy (images / audio: hand this path to the engine)
    std::string text;      // JSON / text: the content
    std::string sha256;    // of the content
    bool changed = false;  // different from what was cached before
    bool fromCache = false;
    bool stale = false;    // the network failed: this is the last good copy
    int status = 0;        // HTTP status (200, 304...) or 0
};

class Client {
public:
    Client(fs::path cacheDir, Policy policy, int threads = 2);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    static bool httpsAvailable();   // false: this build has no HTTPS backend
    const Policy& policy() const { return m_policy; }
    void setPolicy(const Policy& p);

    // Queues a download. revalidate: ask the server even if the cache is fresh. maxAge: a
    // cached copy checked less than this many seconds ago is used as it is (no network).
    uint64_t fetch(const std::string& url, bool revalidate, double maxAge);
    // The cached copy, if any (no network)
    bool cached(const std::string& url, Result& out);
    std::vector<Result> takeFinished();
    size_t pending() const;

private:
    struct Job { uint64_t id; std::string url; bool revalidate; double maxAge; };
    struct Meta {
        std::string url, etag, lastModified, file, sha256;
        Kind kind = Kind::None;
        double checked = 0.0;   // unix time of the last successful check
    };
    void worker();
    Result run(const Job& job);
    bool readMeta(const std::string& url, Meta& m) const;
    void writeMeta(const Meta& m) const;
    std::string key(const std::string& url) const;
    void pruneCache();

    fs::path m_dir;
    Policy m_policy;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_jobs;
    std::map<std::string, std::vector<uint64_t>> m_inFlight;   // url -> request ids waiting for it
    std::vector<Result> m_done;
    std::vector<std::thread> m_threads;
    std::atomic<uint64_t> m_nextId{1};
    bool m_stop = false;
};

} // namespace remote
