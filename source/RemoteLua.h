#pragma once
// The `remote` Lua table: mods' online content (RemoteContent.h). Data only.
//
//   remote.fetch(url, callback [, { refresh = true, max_age = 30 }])  -> id | nil, error
//   remote.watch(url, seconds, callback)                           -> id | nil, error
//   remote.unwatch(id)
//   remote.refresh(url)                 ask the site now; watchers hear about changes
//   remote.get_cached(url)              -> result | nil   (no network)
//   remote.check_url(url)               -> true | false, why
//   remote.is_available()               -> true | false, why
//   remote.null                         stands for JSON null inside arrays
//
// callback(result): result = { ok, url, kind = "json"|"text"|"image"|"audio", data (json: tables,
// text: string), image (path for ui.draw_image), sound (path for sound.play), changed,
// from_cache, stale, status, error }
//
// A mod may only use URLs below the prefixes in its mod.conf:  remote_sources = https://...

#include "LuaSandbox.h"
#include "RemoteContent.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

class RemoteLua {
public:
    RemoteLua(sol::state& lua, std::filesystem::path cacheDir, remote::Policy policy,
              std::vector<std::filesystem::path> modRoots, double timeLimit = 5.0)
        : m_lua(lua), m_client(std::make_unique<remote::Client>(std::move(cacheDir), policy)),
          m_policy(std::move(policy)), m_timeLimit(timeLimit) {
        for (const auto& r : modRoots) {
            const auto c = luasandbox::canonicalOrEmpty(r);
            if (!c.empty()) m_modRoots.push_back(c);
        }
        install();
    }
    ~RemoteLua() {
        m_callbacks.clear();      // Lua references go before the state does
        m_watches.clear();
        m_client.reset();
        sol::object t = m_lua["remote"];
        if (t.valid()) m_lua["remote"] = sol::lua_nil;
    }
    RemoteLua(const RemoteLua&) = delete;
    RemoteLua& operator=(const RemoteLua&) = delete;

    // Every frame / tick, on the thread that owns the Lua state
    void update() {
        const double now = clock();
        // Watches that are due: ask the site (only changes reach the callback)
        for (auto& [id, w] : m_watches) {
            if (w.busy || now < w.next) continue;
            w.busy = true;
            w.request = m_client->fetch(w.url, true, 0.0);
        }
        for (auto& r : m_client->takeFinished()) deliver(r);
    }

private:
    struct ModInfo {
        std::string name;
        std::vector<std::string> prefixes;
        std::string problem;            // why it can't go online ("" = fine)
        double windowStart = 0.0;
        int requests = 0;
    };
    struct Callback { sol::protected_function fn; std::string mod; };
    struct Watch {
        std::string url, mod;
        double interval = 60.0, next = 0.0;
        bool busy = false, first = true;
        uint64_t request = 0;
        std::string sha;            // content it last delivered
        sol::protected_function fn;
    };

    sol::state& m_lua;
    std::unique_ptr<remote::Client> m_client;
    remote::Policy m_policy;
    double m_timeLimit;
    std::vector<std::filesystem::path> m_modRoots;
    std::map<std::string, ModInfo> m_mods;            // by mod folder
    std::map<uint64_t, Callback> m_callbacks;         // request id -> callback
    std::map<uint64_t, Watch> m_watches;              // watch id -> watch
    uint64_t m_nextWatch = 1;
    sol::table m_null;

    static constexpr int kRequestsPerMinute = 120;
    static constexpr double kMinWatchSeconds = 10.0;

    static double clock() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // The mod a call comes from: the folder of the calling Lua file, up to its mod folder
    ModInfo* modOf(lua_State* L, std::string& why) {
        const auto callerDir = luasandbox::canonicalOrEmpty(luasandbox::callerFolder(L, 1));
        if (callerDir.empty()) { why = "remote.* must be called from a mod's file"; return nullptr; }
        std::filesystem::path modDir;
        for (std::filesystem::path p = callerDir; !p.empty() && p != p.parent_path(); p = p.parent_path()) {
            for (const auto& root : m_modRoots) if (p.parent_path() == root) modDir = p;
            if (!modDir.empty()) break;
        }
        if (modDir.empty()) { why = "remote.* is only for mods in the mods folders"; return nullptr; }
        const std::string key = modDir.string();
        auto it = m_mods.find(key);
        if (it != m_mods.end()) return &it->second;
        ModInfo info;
        info.name = modDir.filename().string();
        std::ifstream in(modDir / "mod.conf");
        std::string line, sources;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            k.erase(0, k.find_first_not_of(" \t"));
            k.erase(k.find_last_not_of(" \t\r") + 1);
            if (k == "remote_sources") sources += "," + line.substr(eq + 1);
        }
        std::stringstream ss(sources);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t\r") + 1);
            if (item.empty()) continue;
            remote::Url u;
            std::string err;
            if (!remote::parseUrl(item, u, err)) { info.problem = "remote_sources '" + item + "': " + err; continue; }
            // A prefix must name an owner and a repository on shared GitHub hosts, so a mod
            // can't claim all of GitHub
            if ((u.host == "raw.githubusercontent.com" || u.host == "gist.githubusercontent.com")) {
                int segments = 0;
                std::stringstream ps(u.path);
                std::string seg;
                while (std::getline(ps, seg, '/')) if (!seg.empty()) ++segments;
                if (segments < 2) { info.problem = "remote_sources '" + item + "' must include the owner and repository"; continue; }
            }
            info.prefixes.push_back(item);
        }
        if (info.prefixes.empty() && info.problem.empty()) {
            info.problem = "mod '" + info.name + "' has no remote_sources in its mod.conf "
                           "(e.g. remote_sources = https://raw.githubusercontent.com/you/repo/main/)";
        }
        return &m_mods.emplace(key, std::move(info)).first->second;
    }

    // Can this mod use this URL? (prefix, site, settings)
    bool allowed(ModInfo& mod, const std::string& url, std::string& why) {
        if (!m_policy.enabled) { why = "online content is switched off (remote_content in the settings)"; return false; }
        if (!remote::Client::httpsAvailable()) { why = "this build of the game can't download (no HTTPS support)"; return false; }
        remote::Url u;
        if (!remote::parseUrl(url, u, why) || !remote::hostAllowed(u.host, m_policy, why)) return false;
        if (!remote::underPrefix(url, mod.prefixes)) {
            why = mod.prefixes.empty() ? mod.problem
                                       : "'" + url + "' isn't under this mod's remote_sources (mod.conf)" +
                                         (mod.problem.empty() ? std::string() : " - also: " + mod.problem);
            return false;
        }
        return true;
    }
    bool countRequest(ModInfo& mod, std::string& why) {
        const double now = clock();
        if (now - mod.windowStart > 60.0) { mod.windowStart = now; mod.requests = 0; }
        if (++mod.requests > kRequestsPerMinute) {
            why = "too many requests from mod '" + mod.name + "' (at most " + std::to_string(kRequestsPerMinute) + " a minute)";
            return false;
        }
        return true;
    }

    sol::object toLua(const remote::Json& j, int depth = 0) {
        switch (j.type) {
            case remote::Json::Bool: return sol::make_object(m_lua, j.b);
            case remote::Json::Number: return sol::make_object(m_lua, j.n);
            case remote::Json::String:
                if (j.s.size() >= 8) luasandbox::markRemoteText(j.s);   // never load()-able
                return sol::make_object(m_lua, j.s);
            case remote::Json::Array: {
                sol::table t = m_lua.create_table(static_cast<int>(j.items.size()), 0);
                int i = 1;
                for (const auto& v : j.items) t[i++] = v.type == remote::Json::Null ? sol::object(m_null) : toLua(v, depth + 1);
                return t;
            }
            case remote::Json::Object: {
                sol::table t = m_lua.create_table(0, static_cast<int>(j.fields.size()));
                for (const auto& [k, v] : j.fields) if (v.type != remote::Json::Null) t[k] = toLua(v, depth + 1);
                return t;
            }
            default: return sol::make_object(m_lua, sol::lua_nil);
        }
    }

    sol::table resultTable(const remote::Result& r) {
        sol::table t = m_lua.create_table();
        t["ok"] = r.ok;
        t["url"] = r.url;
        t["changed"] = r.changed;
        t["from_cache"] = r.fromCache;
        t["stale"] = r.stale;
        t["status"] = r.status;
        if (!r.error.empty()) t["error"] = r.error;
        if (!r.ok) return t;
        t["kind"] = remote::kindName(r.kind);
        if (r.kind == remote::Kind::Json) {
            remote::Json j;
            std::string why;
            if (remote::parseJson(r.text, j, why)) t["data"] = toLua(j);
            else { t["ok"] = false; t["error"] = "cached JSON is damaged: " + why; }
            luasandbox::markRemoteText(r.text);
        } else if (r.kind == remote::Kind::Text) {
            luasandbox::markRemoteText(r.text);
            t["data"] = r.text;
        } else if (r.kind == remote::Kind::Image) {
            t["image"] = r.file;
        } else if (r.kind == remote::Kind::Audio) {
            t["sound"] = r.file;
        }
        return t;
    }

    void runCallback(sol::protected_function& fn, const std::string& mod, const remote::Result& r) {
        sol::table t = resultTable(r);
        luasandbox::TimeLimit limit(m_timeLimit, "a remote.* callback");
        auto res = fn(t);
        if (!res.valid()) {
            sol::error err = res;
            std::cerr << "[Remote] " << mod << ": callback for " << r.url << ": " << err.what() << std::endl;
        }
    }

    void deliver(const remote::Result& r) {
        if (!r.ok) std::cerr << "[Remote] " << r.url << ": " << r.error << std::endl;
        // One-off fetches
        auto cb = m_callbacks.find(r.id);
        if (cb != m_callbacks.end()) {
            Callback c = std::move(cb->second);
            m_callbacks.erase(cb);
            runCallback(c.fn, c.mod, r);
        }
        // Watches: the first answer, then only when the content really differs from what that
        // watch last saw (several requests for one URL share a download, so "changed" alone
        // could reach a watch more than once)
        const double now = clock();
        for (auto& [id, w] : m_watches) {
            const bool mine = w.request == r.id;
            if (!mine && w.url != r.url) continue;
            if (mine) { w.busy = false; w.next = now + w.interval; }
            const bool newContent = r.ok && r.sha256 != w.sha;
            const bool firstFailure = mine && w.first && !r.ok;
            if (!newContent && !firstFailure) continue;
            w.first = false;
            if (r.ok) w.sha = r.sha256;
            sol::protected_function fn = w.fn;
            runCallback(fn, w.mod, r);
        }
    }

    void install() {
        sol::table api = m_lua.create_named_table("remote");
        m_null = m_lua.create_table();
        api["null"] = m_null;
        api["is_available"] = [this](sol::this_state ts) {
            sol::state_view lv(ts);
            if (!remote::Client::httpsAvailable()) return std::make_tuple(false, sol::make_object(lv, "this build can't download (no HTTPS support)"));
            if (!m_policy.enabled) return std::make_tuple(false, sol::make_object(lv, "online content is switched off (remote_content in the settings)"));
            return std::make_tuple(true, sol::make_object(lv, sol::lua_nil));
        };
        api["check_url"] = [this](const std::string& url, sol::this_state ts) {
            sol::state_view lv(ts);
            std::string why;
            ModInfo* mod = modOf(ts.L, why);
            if (mod && allowed(*mod, url, why)) return std::make_tuple(true, sol::make_object(lv, sol::lua_nil));
            return std::make_tuple(false, sol::make_object(lv, why));
        };
        api["fetch"] = [this](const std::string& url, sol::protected_function callback, sol::optional<sol::table> options,
                              sol::this_state ts) {
            sol::state_view lv(ts);
            std::string why;
            ModInfo* mod = modOf(ts.L, why);
            if (!mod || !allowed(*mod, url, why) || !countRequest(*mod, why)) {
                return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, why));
            }
            const bool refresh = options && options->get_or("refresh", false);
            const double maxAge = options ? options->get_or("max_age", 30.0) : 30.0;
            const uint64_t id = m_client->fetch(url, refresh, refresh ? 0.0 : maxAge);
            m_callbacks[id] = Callback{std::move(callback), mod->name};
            return std::make_tuple(sol::make_object(lv, static_cast<double>(id)), sol::make_object(lv, sol::lua_nil));
        };
        api["watch"] = [this](const std::string& url, double seconds, sol::protected_function callback, sol::this_state ts) {
            sol::state_view lv(ts);
            std::string why;
            ModInfo* mod = modOf(ts.L, why);
            if (!mod || !allowed(*mod, url, why)) {
                return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, why));
            }
            Watch w;
            w.url = url;
            w.mod = mod->name;
            w.interval = std::max(kMinWatchSeconds, seconds);
            w.fn = std::move(callback);
            w.busy = true;
            w.request = m_client->fetch(url, false, 0.0);   // first answer straight away (cached or fresh)
            const uint64_t id = m_nextWatch++;
            m_watches.emplace(id, std::move(w));
            return std::make_tuple(sol::make_object(lv, static_cast<double>(id)), sol::make_object(lv, sol::lua_nil));
        };
        api["unwatch"] = [this](double id) { return m_watches.erase(static_cast<uint64_t>(id)) > 0; };
        api["refresh"] = [this](const std::string& url, sol::this_state ts) {
            std::string why;
            ModInfo* mod = modOf(ts.L, why);
            if (!mod || !allowed(*mod, url, why) || !countRequest(*mod, why)) return false;
            m_client->fetch(url, true, 0.0);   // watchers of this URL hear if it changed
            return true;
        };
        api["get_cached"] = [this](const std::string& url, sol::this_state ts) -> sol::object {
            std::string why;
            ModInfo* mod = modOf(ts.L, why);
            remote::Result r;
            if (!mod || !allowed(*mod, url, why) || !m_client->cached(url, r)) return sol::make_object(m_lua, sol::lua_nil);
            return sol::make_object(m_lua, resultTable(r));
        };
    }
};
