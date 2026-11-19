#pragma once
// LuaSandbox: what a mod's Lua may load, and how long one call into a mod may run.
// Used for every mod state in the game (main state and world generator threads) and on the
// dedicated server. Client mods (ClientMods.cpp) have their own, stricter sandbox.
//
// install(lua, roots, libraries):
//   require("name")       loads a Lua module once (cached in package.loaded). Looked for in:
//                           1. the folder of the file that calls require (your mod):
//                              <that folder>/name.lua, <that folder>/name/init.lua
//                           2. the shared library folders (`libraries`, e.g. lualib/):
//                              lualib/name.lua, lualib/name/init.lua
//                           3. another mod's files, by "mod.file": require("doors.api") ->
//                              <mods folder>/doors/api.lua  (never that mod's init.lua again)
//                         Dots are folder separators: require("util.vec") -> util/vec.lua
//   dofile / loadfile     only files inside `roots` (mods, games, libraries); never stdin
//   load                  text only (no precompiled bytecode: it can crash the VM)
//   package.loadlib, C modules (package.cpath) and package.searchpath are removed: mods can't
//   load native DLLs/.so files.
//   Every path is resolved to where it really is (".." and symlinks followed) and must be
//   inside one of the roots.
//
// Time limit: TimeLimit guard(seconds) around a call into Lua; if that call runs longer, it
// stops with an error ("... took longer than N s") instead of freezing the program. Needs
// installTimeLimitHook(lua) once per state. Per thread, so generator threads are separate.

#include <sol/sol.hpp>
#include "Sha256.h"
#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace luasandbox {

namespace fs = std::filesystem;

constexpr uintmax_t kMaxSourceBytes = 8u * 1024u * 1024u;   // a single Lua file, at most

struct Roots {
    std::vector<fs::path> allowed;     // canonical: files may be read inside these
    std::vector<fs::path> modRoots;    // canonical: folders holding mods (for "mod.file")
    std::vector<fs::path> libraries;   // canonical: shared module folders (lualib/)
};

inline fs::path canonicalOrEmpty(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::canonical(p, ec);
    return ec ? fs::path() : c;
}

// Is `path` inside `root`? Part by part, so "mods2" isn't inside "mods".
inline bool inside(const fs::path& path, const fs::path& root) {
    auto r = root.begin(), p = path.begin();
    for (; r != root.end(); ++r, ++p) {
        if (r->empty()) continue;
        if (p == path.end() || *p != *r) return false;
    }
    return true;
}

inline bool allowedFile(const Roots& roots, const fs::path& real) {
    for (const auto& r : roots.allowed) if (inside(real, r)) return true;
    return false;
}

// "util.vec" -> "util/vec"; empty if the name has anything but letters, digits, _ - and dots
inline std::string moduleRelPath(const std::string& name) {
    if (name.empty() || name.size() > 200 || name.front() == '.' || name.back() == '.') return "";
    std::string out;
    char prev = 0;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok || (c == '.' && prev == '.')) return "";
        out += (c == '.') ? '/' : c;
        prev = c;
    }
    return out;
}

// Reads a Lua source file, if it is a regular file inside the roots (after following links)
inline bool readSource(const Roots& roots, const std::string& path, std::string& src, std::string& error) {
    if (path.empty()) { error = "no file name given"; return false; }
    const fs::path real = canonicalOrEmpty(path);
    std::error_code ec;
    if (real.empty() || !fs::is_regular_file(real, ec)) { error = "cannot open " + path + " (no such file)"; return false; }
    if (!allowedFile(roots, real)) {
        error = "cannot open " + path + ": mods may only load files from the mods, games and lualib folders";
        return false;
    }
    const uintmax_t size = fs::file_size(real, ec);
    if (ec || size > kMaxSourceBytes) { error = "cannot open " + path + ": too big for a Lua file"; return false; }
    std::ifstream in(real, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!in && !in.eof()) { error = "cannot read " + path; return false; }
    src = ss.str();
    if (!src.empty() && src[0] == '\x1b') { error = path + " is precompiled Lua - only source files can be loaded"; return false; }
    return true;
}

// The folder of the Lua file that called the function `level` levels up (empty if unknown)
inline fs::path callerFolder(lua_State* L, int level) {
    lua_Debug ar;
    for (int l = level; l < level + 4; ++l) {             // skip C functions / tail calls
        if (!lua_getstack(L, l, &ar)) break;
        if (!lua_getinfo(L, "S", &ar) || !ar.source) continue;
        if (ar.source[0] == '@') return fs::path(ar.source + 1).parent_path();
    }
    return fs::path();
}

// Where require(name) finds its file. Empty + `tried` filled if nowhere.
inline std::string resolveModule(const Roots& roots, const std::string& name, const fs::path& callerDir,
                                 std::string& tried) {
    const std::string rel = moduleRelPath(name);
    if (rel.empty()) { tried = "'" + name + "' isn't a valid module name (letters, digits, _ - and dots)"; return ""; }
    std::vector<fs::path> candidates;
    auto add = [&](const fs::path& base) {
        candidates.push_back(base / (rel + ".lua"));
        candidates.push_back(base / rel / "init.lua");
    };
    if (!callerDir.empty()) add(callerDir);
    for (const auto& lib : roots.libraries) add(lib);
    const size_t slash = rel.find('/');
    if (slash != std::string::npos) {                      // "mod.file": a file of another mod
        const std::string mod = rel.substr(0, slash), rest = rel.substr(slash + 1);
        for (const auto& m : roots.modRoots) {
            candidates.push_back(m / mod / (rest + ".lua"));
            candidates.push_back(m / mod / rest / "init.lua");
        }
    }
    std::error_code ec;
    for (const auto& c : candidates) {
        const fs::path real = canonicalOrEmpty(c);
        if (real.empty() || !fs::is_regular_file(real, ec) || !allowedFile(roots, real)) continue;
        return c.lexically_normal().generic_string();
    }
    tried = "module '" + name + "' not found. Looked for:";
    for (const auto& c : candidates) tried += "\n    " + c.lexically_normal().generic_string();
    return "";
}

// ---------------------------------------------------------------------------
// Downloaded content is data only (RemoteContent.h): every text and JSON string handed to a
// mod is remembered (by SHA-256), and load() refuses to compile any of it.
// ---------------------------------------------------------------------------
inline std::mutex& remoteMutex() { static std::mutex m; return m; }
inline std::unordered_set<std::string>& remoteHashes() { static std::unordered_set<std::string> s; return s; }
inline void markRemoteText(const std::string& text) {
    if (text.empty()) return;
    const std::string h = sha256Hex(text);
    std::lock_guard<std::mutex> lock(remoteMutex());
    auto& set = remoteHashes();
    if (set.size() > 200000) set.clear();   // (bounded; the newest are what matters)
    set.insert(h);
}
inline bool isRemoteText(const std::string& text) {
    if (text.empty()) return false;
    const std::string h = sha256Hex(text);
    std::lock_guard<std::mutex> lock(remoteMutex());
    return remoteHashes().count(h) > 0;
}

// The Lua half: replaces require / dofile / loadfile / load with checked versions. The C++
// helpers are only reachable from these functions (they're removed from the globals).
inline const char* kSandboxLua = R"LUA(
local resolve, read, is_remote = __sandbox_resolve, __sandbox_read, __sandbox_is_remote
__sandbox_resolve, __sandbox_read, __sandbox_is_remote = nil, nil, nil
local _load, _type, _error, _select, _concat = load, type, error, select, table.concat
local loaded = package.loaded

package.loadlib = nil
package.searchpath = nil
package.cpath = ""
package.path = ""
package.searchers = {}

local function load_source(src, chunkname, env, has_env)
    if has_env then return _load(src, chunkname, "t", env) end
    return _load(src, chunkname, "t")
end

local refused = "refusing to run downloaded content: online content is data only"
load = function(chunk, chunkname, mode, ...)
    if _type(chunk) == "function" then          -- a reader: collect the pieces, then check them
        local parts, n = {}, 0
        while true do
            local piece = chunk()
            if piece == nil or piece == "" then break end
            if _type(piece) ~= "string" then return nil, "reader function must return strings" end
            n = n + 1
            parts[n] = piece
        end
        chunk = _concat(parts)
    end
    if _type(chunk) == "string" and is_remote(chunk) then return nil, refused end
    if _select("#", ...) > 0 then return _load(chunk, chunkname, "t", (...)) end
    return _load(chunk, chunkname, "t")
end

loadfile = function(path, mode, ...)
    if _type(path) ~= "string" then return nil, "loadfile needs a file name" end
    local src, err = read(path)
    if not src then return nil, err end
    return load_source(src, "@" .. path, (...), _select("#", ...) > 0)
end

dofile = function(path)
    if _type(path) ~= "string" then _error("dofile needs a file name", 2) end
    local src, err = read(path)
    if not src then _error(err, 2) end
    local f, e = _load(src, "@" .. path, "t")
    if not f then _error(e, 2) end
    return f()
end

require = function(name)
    if _type(name) ~= "string" then _error("bad argument #1 to 'require' (string expected)", 2) end
    local v = loaded[name]
    if v ~= nil then return v end
    local path, err = resolve(name)
    if not path then _error(err, 2) end
    local src, rerr = read(path)
    if not src then _error(rerr, 2) end
    local f, e = _load(src, "@" .. path, "t")
    if not f then _error(e, 2) end
    local r = f(name, path)
    if loaded[name] == nil then loaded[name] = (r == nil) and true or r end
    return loaded[name], path
end
)LUA";

// Sets up the sandbox on a state that has base and package open. `allowedRoots`: folders
// files may be read from; `modRoots`: folders holding mods; `libraries`: shared module folders.
// Folders that don't exist are skipped.
inline void install(sol::state& lua, const std::vector<fs::path>& allowedRoots,
                    const std::vector<fs::path>& modRoots, const std::vector<fs::path>& libraries) {
    auto roots = std::make_shared<Roots>();
    auto addTo = [](std::vector<fs::path>& v, const fs::path& p) {
        const fs::path c = canonicalOrEmpty(p);
        if (!c.empty() && std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
    };
    for (const auto& p : allowedRoots) addTo(roots->allowed, p);
    for (const auto& p : modRoots) { addTo(roots->modRoots, p); addTo(roots->allowed, p); }
    for (const auto& p : libraries) { addTo(roots->libraries, p); addTo(roots->allowed, p); }

    lua["__sandbox_read"] = [roots](const std::string& path, sol::this_state ts) {
        sol::state_view lv(ts);
        std::string src, err;
        if (!readSource(*roots, path, src, err)) {
            return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, err));
        }
        return std::make_tuple(sol::make_object(lv, src), sol::make_object(lv, sol::lua_nil));
    };
    lua["__sandbox_is_remote"] = [](const std::string& chunk) { return isRemoteText(chunk); };
    lua["__sandbox_resolve"] = [roots](const std::string& name, sol::this_state ts) {
        sol::state_view lv(ts);
        // level 0 = this function, 1 = require, 2 = whoever called require
        const fs::path caller = callerFolder(ts.L, 2);
        std::string tried;
        const std::string path = resolveModule(*roots, name, caller, tried);
        if (path.empty()) return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, tried));
        return std::make_tuple(sol::make_object(lv, path), sol::make_object(lv, sol::lua_nil));
    };
    lua.script(kSandboxLua, "=sandbox");
}

// ---------------------------------------------------------------------------
// Time limit
// ---------------------------------------------------------------------------
struct Deadline {
    bool active = false;
    std::chrono::steady_clock::time_point at;
    double seconds = 0.0;
    const char* what = "";
};
inline Deadline& deadline() { thread_local Deadline d; return d; }

inline void timeLimitHook(lua_State* L, lua_Debug*) {
    Deadline& d = deadline();
    if (!d.active || std::chrono::steady_clock::now() < d.at) return;
    d.active = false;                          // once: the error must be able to unwind
    char msg[200];   // (lua_pushfstring has no %.1f)
    std::snprintf(msg, sizeof(msg), "%s took longer than %.1f s and was stopped (a loop that never ends?)",
                  d.what, d.seconds);
    luaL_error(L, "%s", msg);
}

// Checks the clock every 100 000 Lua instructions (cheap). Coroutines made later inherit it.
inline void installTimeLimitHook(sol::state& lua) {
    lua_sethook(lua.lua_state(), timeLimitHook, LUA_MASKCOUNT, 100000);
}

// While it exists, Lua calls on this thread must finish within `seconds` (<= 0: no limit).
// Nested guards keep the outer (earlier) deadline.
class TimeLimit {
public:
    TimeLimit(double seconds, const char* what) {
        Deadline& d = deadline();
        m_saved = d;
        if (seconds <= 0.0 || d.active) return;
        d.active = true;
        d.seconds = seconds;
        d.what = what;
        d.at = std::chrono::steady_clock::now() +
               std::chrono::microseconds(static_cast<long long>(seconds * 1e6));
    }
    ~TimeLimit() { deadline() = m_saved; }
    TimeLimit(const TimeLimit&) = delete;
    TimeLimit& operator=(const TimeLimit&) = delete;
private:
    Deadline m_saved;
};

} // namespace luasandbox
