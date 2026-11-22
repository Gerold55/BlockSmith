// Diagnostics.cpp - see Diagnostics.h
#include "Diagnostics.h"
#include "Version.h"

#include <sol/sol.hpp>   // (the Lua headers, as the rest of the engine includes them)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <thread>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#  include <process.h>
#else
#  include <cxxabi.h>
#  include <dlfcn.h>
#  if !defined(BLOCKSMITH_BACKTRACE) || BLOCKSMITH_BACKTRACE
#    include <execinfo.h>
#  else
// No backtrace() on this system (CMakeLists.txt: FindBacktrace found nothing): no call stacks
static int backtrace(void**, int) { return 0; }
static void backtrace_symbols_fd(void* const*, int, int) {}
#  endif
#  include <fcntl.h>
#  include <sys/utsname.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <sys/sysctl.h>
#  endif
#endif

namespace fs = std::filesystem;

namespace diag {
namespace {

std::string g_role = "?";
std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();
std::time_t g_startWall = std::time(nullptr);
std::mutex g_mutex;                       // the trace buffer, the warning counts, DbgHelp
std::vector<std::string> g_extraReport;
fs::path g_logDir;

// Where std::cerr really goes (console, and the log file once there is one), before the
// warning traces were put in front of it: traces and Lua tracebacks are written here directly
std::streambuf* g_rawErr = nullptr;

// Paths for the crash handler, made in advance (it may not allocate)
char g_crashPath[1024] = "";
char g_latestPath[1024] = "";
std::atomic<bool> g_crashing{false};

struct Seen { size_t count = 0; std::string first; };
std::map<std::string, Seen> g_seen;

std::string timeString(std::time_t t, const char* fmt, bool utc = false) {
    std::tm tmv{};
#ifdef _WIN32
    if (utc) gmtime_s(&tmv, &t); else localtime_s(&tmv, &t);
#else
    if (utc) gmtime_r(&t, &tmv); else localtime_r(&t, &tmv);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt, &tmv);
    return buf;
}

std::string threadName() {
    std::ostringstream s;
    s << std::this_thread::get_id();
    return s.str();
}

// The same warning, whatever numbers are in it ("chunk 3,4" and "chunk 5,6" count as one)
std::string normalize(const std::string& line) {
    std::string out;
    bool inNum = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        const bool digit = (c >= '0' && c <= '9') ||
                           (inNum && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == 'x'));
        if (digit) { if (!inNum) out += '#'; inNum = true; }
        else { out += c; inNum = false; }
    }
    return out;
}

// Full trace the 1st, 10th, 100th, 1000th... time
bool traceDue(size_t n) {
    if (n == 1) return true;
    while (n % 10 == 0) n /= 10;
    return n == 1;
}

void rawWrite(const std::string& text) {
    std::streambuf* out = g_rawErr ? g_rawErr : std::cerr.rdbuf();
    out->sputn(text.data(), static_cast<std::streamsize>(text.size()));
    out->pubsync();
}

// ---------------------------------------------------------------------------
// Native call stacks
// ---------------------------------------------------------------------------
#ifdef _WIN32
std::once_flag g_symOnce;
void symInit() {
    std::call_once(g_symOnce, [] {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    });
}

std::string describeAddress(DWORD64 addr) {
    symInit();
    HANDLE proc = GetCurrentProcess();
    char modPath[MAX_PATH] = "?";
    const DWORD64 base = SymGetModuleBase64(proc, addr);
    if (base) GetModuleFileNameA(reinterpret_cast<HMODULE>(base), modPath, MAX_PATH);
    std::string mod = fs::path(modPath).filename().string();
    char buf[sizeof(SYMBOL_INFO) + 512] = {};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    DWORD64 disp = 0;
    std::ostringstream s;
    if (SymFromAddr(proc, addr, &disp, sym)) s << sym->Name << "+0x" << std::hex << disp << std::dec;
    else s << "??";
    s << "  (" << mod << "+0x" << std::hex << (base ? addr - base : addr) << std::dec << ")";
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD ldisp = 0;
    if (SymGetLineFromAddr64(proc, addr, &ldisp, &line) && line.FileName) {
        s << "  " << fs::path(line.FileName).filename().string() << ":" << line.LineNumber;
    }
    return s.str();
}
#else
std::string describeAddress(void* addr) {
    Dl_info info{};
    std::ostringstream s;
    if (dladdr(addr, &info) && info.dli_fname) {
        std::string name = "??";
        if (info.dli_sname) {
            int status = 0;
            char* dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            name = (status == 0 && dem) ? dem : info.dli_sname;
            std::free(dem);
            if (name.size() > 160) name = name.substr(0, 157) + "...";
            s << name << "+0x" << std::hex
              << (reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(info.dli_saddr)) << std::dec;
        } else {
            s << "??";
        }
        s << "  (" << fs::path(info.dli_fname).filename().string() << "+0x" << std::hex
          << (reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(info.dli_fbase)) << std::dec << ")";
    } else {
        s << "?? (" << addr << ")";
    }
    return s.str();
}

// The frames with their exact names: this program's frames looked up by addr2line (Linux) or
// atos (macOS) - the names dladdr gives are only the nearest exported symbol, often wrong
// (code put inline, functions not exported). Run once per place a warning comes from.
// ok = false when the tool isn't there (then the frames have dladdr's names).
std::vector<std::string> exactFrames(void* const* frames, int n, bool& ok) {
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i) out.push_back(describeAddress(frames[i]));
    ok = false;
    Dl_info self{};
    if (!dladdr(reinterpret_cast<void*>(&init), &self) || !self.dli_fbase || !self.dli_fname) return out;
    std::error_code ec;
#if defined(__APPLE__)
    std::string exe = fs::absolute(self.dli_fname, ec).string();
#else
    std::string exe = fs::read_symlink("/proc/self/exe", ec).string();
    if (exe.empty()) exe = fs::absolute(self.dli_fname, ec).string();
#endif
    if (exe.empty() || exe.find('\'') != std::string::npos) return out;
    std::vector<int> mine;   // which frames are this program's
    std::ostringstream cmd;
#if defined(__APPLE__)
    cmd << "atos -o '" << exe << "' -l 0x" << std::hex << reinterpret_cast<uintptr_t>(self.dli_fbase);
#else
    cmd << "addr2line -a -f -i -C -e '" << exe << "'";   // (-i -a: each address, then its inline chain)
#endif
    for (int i = 0; i < n; ++i) {
        Dl_info info{};
        if (!dladdr(frames[i], &info) || info.dli_fbase != self.dli_fbase) continue;
        // (a return address points after the call: one byte back is the call itself)
        const uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]) - 1;
#if defined(__APPLE__)
        cmd << " 0x" << std::hex << a;
#else
        cmd << " 0x" << std::hex << (a - reinterpret_cast<uintptr_t>(self.dli_fbase));
#endif
        mine.push_back(i);
    }
    if (mine.empty()) return out;
    cmd << " 2>/dev/null";
    FILE* p = popen(cmd.str().c_str(), "r");
    if (!p) return out;
    std::vector<std::string> lines;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), p)) {
        std::string l = buf;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        lines.push_back(l);
    }
    pclose(p);
    const std::string mod = fs::path(exe).filename().string();
    auto where = [&](int i) {
        std::ostringstream m;
        m << "  (" << mod << "+0x" << std::hex
          << (reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(self.dli_fbase)) << ")";
        return m.str();
    };
#if defined(__APPLE__)
    // "function (in BlockSmithServer) (File.cpp:123)" or "function (in BlockSmithServer) + 45"
    if (lines.size() != mine.size()) return out;
    for (size_t k = 0; k < mine.size(); ++k) {
        const std::string& l = lines[k];
        const size_t in = l.find(" (in ");
        if (in == std::string::npos || l.rfind("0x", 0) == 0) continue;   // (atos couldn't name it)
        std::string text = l.substr(0, in) + where(mine[k]);
        const size_t open = l.rfind(" (");
        if (open != std::string::npos && open > in && l.back() == ')') text += "  " + l.substr(open + 2, l.size() - open - 3);
        out[mine[k]] = text;
    }
#else
    // each address: "0x...", then function / "path/File.cpp:123" pairs, innermost inline code
    // first - the last pair is the real function and the line in it ("??:?" without debug info)
    std::vector<std::pair<std::string, std::string>> last;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind("0x", 0) == 0) { last.push_back({ "??", "??" }); continue; }
        if (last.empty() || i + 1 >= lines.size()) return out;
        last.back() = { lines[i], lines[i + 1] };
        ++i;
    }
    if (last.size() != mine.size()) return out;
    for (size_t k = 0; k < mine.size(); ++k) {
        const std::string& fn = last[k].first;
        std::string loc = last[k].second;
        if (fn == "??") continue;
        const size_t disc = loc.find(" (discriminator");
        if (disc != std::string::npos) loc.erase(disc);
        std::string text = fn + where(mine[k]);
        if (loc.rfind("??", 0) != 0) text += "  " + fs::path(loc).filename().string();
        out[mine[k]] = text;
    }
#endif
    ok = true;
    return out;
}
#endif

}  // namespace
std::vector<std::string> stackTraceResolved(int skip, int max, std::string& resolve);
namespace {

// Frames that are only the printing machinery (iostreams, this file)
bool boring(const std::string& frame) {
    static const char* skip[] = { "diag::", "std::basic_ostream", "std::__ostream_insert", "std::basic_streambuf",
                                  "std::endl", "std::operator<<", "std::ostream::", "basic_ostream<", "__ostream_insert" };
    for (const char* s : skip) if (frame.find(s) != std::string::npos) return true;
    return false;
}

// The last time this thread printed a Lua error with its Lua stack: the lines right after it
// (sol's "an error occurred", "Failed to load ...") are the same error, and get no native trace
thread_local double t_lastLuaError = -100.0;

// A frame, short: "loadTextureToLayer  main.cpp:1714" (no template arguments, no offsets;
// the module and offset only when there's no file:line)
std::string shortFrame(const std::string& f) {
    const size_t sep = f.find("  (");
    std::string name = f.substr(0, sep);
    for (const char* junk : { "(anonymous namespace)::", "`anonymous namespace'::" }) {
        for (size_t at; (at = name.find(junk)) != std::string::npos;) name.erase(at, std::strlen(junk));
    }
    for (size_t at; (at = name.find("{lambda")) != std::string::npos;) {   // GCC / Clang: {lambda(int)#1}
        int d = 0; size_t e = at;
        for (; e < name.size(); ++e) { if (name[e] == '{') ++d; else if (name[e] == '}' && --d == 0) break; }
        name.replace(at, e - at + 1, "(lambda)");
    }
    for (const char* junk : { "::_FUN", "::operator()" }) {
        const size_t at = name.rfind(junk);
        if (at != std::string::npos && at + std::strlen(junk) == name.size()) name.erase(at);
    }
    std::string rest = sep == std::string::npos ? "" : f.substr(sep + 2);
    const size_t plus = name.rfind("+0x");
    if (plus != std::string::npos) name.erase(plus);
    std::string n;
    int depth = 0;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name.compare(i, 8, "<lambda_") == 0 && depth == 0) { n += "(lambda)"; }
        if (name[i] == '<' && !(i >= 8 && name.compare(i - 8, 8, "operator") == 0)) { ++depth; continue; }
        if (name[i] == '>' && depth > 0) { --depth; continue; }
        if (name[i] == '(' && (depth > 0 || (name.compare(i, 8, "(lambda)") != 0 &&
            name.compare(i, 11, "(anonymous ") != 0))) { ++depth; continue; }   // (the parameter list)
        if (name[i] == ')' && depth > 0) { --depth; continue; }
        if (depth == 0) n += name[i];
    }
    if (n == "_FUN" || n.empty()) n = "(lambda)";
    if (n.size() > 90) n = n.substr(0, 87) + "...";
    const size_t close = rest.find(')');
    std::string module = close == std::string::npos ? rest : rest.substr(0, close + 1);
    std::string line = close == std::string::npos ? "" : rest.substr(close + 1);
    const size_t a = line.find_first_not_of(' ');
    line = a == std::string::npos ? "" : line.substr(a);
    return n + "  " + (line.empty() ? module : line);
}

// Frames of Lua itself / sol (where the engine was called from a mod): the trace stops there
bool luaFrame(const std::string& f) {
    // (the name without its parameters or template arguments: those mention sol:: in engine
    // functions that take Lua values too)
    const std::string sf = shortFrame(f);
    const std::string name = sf.substr(0, sf.find("  "));
    if (name.rfind("sol::", 0) == 0 || name.find(" sol::") != std::string::npos) return true;
    static const char* mods[] = { "(lua", "(liblua", "(Lua" };
    for (const char* m : mods) if (f.find(m) != std::string::npos) return true;
    return false;
}

// The system's own frames (thread start, C runtime, exception unwinding): left out
bool systemFrame(const std::string& f) {
    static const char* s[] = { "ntdll", "KERNEL32", "KERNELBASE", "VCRUNTIME", "ucrtbase", "libc.so", "libsystem_",
                               "__scrt_common_main", "_CxxFrameHandler", "start_thread", "__libc_start", "libstdc++",
                               "_start  (", "dyld" };
    for (const char* x : s) if (f.find(x) != std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------------------
// The warning trace: every line std::cerr prints, with its trace
// ---------------------------------------------------------------------------
class WarnTraceBuf : public std::streambuf {
public:
    explicit WarnTraceBuf(std::streambuf* out) : m_out(out) {}
protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        const char c = static_cast<char>(ch);
        xsputn(&c, 1);
        return ch;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        thread_local std::string line;   // (each thread's own line: they don't mix)
        for (std::streamsize i = 0; i < n; ++i) {
            line.push_back(s[i]);
            if (s[i] == '\n') { finish(line); line.clear(); }
        }
        return n;
    }
    int sync() override { m_out->pubsync(); return 0; }
private:
    // (never inlined: the stack trace leaves out exactly this and xsputn)
#if defined(_MSC_VER)
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    void finish(const std::string& line) {
        std::lock_guard<std::mutex> lock(g_mutex);
        // sol's own copy of a Lua error that was just printed with its Lua stack: not again
        if (line.rfind("[sol2] An error occurred and has been passed to an error handler", 0) == 0 &&
            uptime() - t_lastLuaError < 1.0) return;
        m_out->sputn(line.data(), static_cast<std::streamsize>(line.size()));
        std::string text = line;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (text.find_first_not_of(" \t") == std::string::npos || text.rfind("    |", 0) == 0) { m_out->pubsync(); return; }
        Seen& seen = g_seen[normalize(text)];
        if (seen.first.empty()) seen.first = text.substr(0, 200);
        const size_t count = ++seen.count;
        if (count > 1) {   // the same warning again: just the count, now and then
            if (traceDue(count)) {
                const std::string c = "    | (seen " + std::to_string(count) + " times)\n";
                m_out->sputn(c.data(), static_cast<std::streamsize>(c.size()));
            }
            m_out->pubsync(); return;
        }
        if (uptime() - t_lastLuaError < 1.0) { m_out->pubsync(); return; }   // (the Lua stack said where)
        // Where it was printed from: the engine's own frames, up to where a mod called in
        std::string resolve;
        std::vector<std::string> frames;
        bool fromLua = false;
#ifdef _WIN32
        const std::vector<std::string> stack = stackTrace(1, 40);   // (DbgHelp: exact names already)
#else
        // The same place again (other text, same call site): nothing more to show
        void* raw[48];
        const int rawN = backtrace(raw, 48);
        {
            static std::set<std::string> rawSites;
            std::ostringstream key;
            for (int i = 0; i < rawN && i < 12; ++i) key << raw[i] << ",";
            if (!rawSites.insert(key.str()).second) { m_out->pubsync(); return; }
        }
        bool exact = false;
        const std::vector<std::string> stack = exactFrames(raw, rawN, exact);
        if (!exact) stackTraceResolved(0, 40, resolve);   // (no addr2line / atos here: say how to get them)
#endif
        for (const auto& f : stack) {
            if (boring(f) || systemFrame(f)) continue;
            if (luaFrame(f)) { if (!frames.empty()) { fromLua = true; break; } continue; }
            std::string sf = shortFrame(f);
            if (sf.find("Diagnostics.cpp") != std::string::npos) continue;   // (this trace itself)
#ifndef _WIN32
            // (without addr2line / atos the names are dladdr's guesses: the misleading ones,
            // "std::basic_string" for code put inline, are left out; the command gives them all)
            if (!exact && (sf.rfind("std::", 0) == 0 || sf.rfind("??", 0) == 0)) continue;
            if (!frames.empty() && sf.substr(0, sf.find("  ")) == frames.back().substr(0, frames.back().find("  "))) continue;
#endif
            frames.push_back(sf);
            if (frames.size() >= 6) break;
        }
        // Several warnings from the same place (one per missing texture...): the trace once
        std::string site;
        for (size_t i = 0; i < frames.size() && i < 3; ++i) site += frames[i] + "|";
        static std::set<std::string> sitesShown;
        if (frames.empty() || !sitesShown.insert(site).second) { m_out->pubsync(); return; }
        std::ostringstream t;
        t << "    | at ";
        for (size_t i = 0; i < frames.size(); ++i) t << (i ? "\n    |    < " : "") << frames[i];
        if (fromLua) t << "\n    |    < (called from a mod's Lua)";
        t << "\n";
        if (!resolve.empty()) t << "    |    exact names: " << resolve << "\n";   // (Linux / macOS)
        const std::string s = t.str();
        m_out->sputn(s.data(), static_cast<std::streamsize>(s.size()));
        m_out->pubsync();
    }
    std::streambuf* m_out;
};

// ---------------------------------------------------------------------------
// The log file (logs/latest.log)
// ---------------------------------------------------------------------------
class LogTee : public std::streambuf {
public:
    LogTee(std::streambuf* console, std::ofstream* file) : m_console(console), m_file(file) {}
protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        const char c = static_cast<char>(ch);
        xsputn(&c, 1);
        return ch;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        m_console->sputn(s, n);
        std::lock_guard<std::mutex> lock(fileMutex());
        for (std::streamsize i = 0; i < n; ++i) {
            if (m_atLineStart) { (*m_file) << timeString(std::time(nullptr), "[%H:%M:%S] "); m_atLineStart = false; }
            m_file->put(s[i]);
            if (s[i] == '\n') { m_atLineStart = true; m_file->flush(); }
        }
        return n;
    }
    int sync() override { m_console->pubsync(); return 0; }
private:
    static std::mutex& fileMutex() { static std::mutex m; return m; }
    std::streambuf* m_console;
    std::ofstream* m_file;
    bool m_atLineStart = true;
};

// ---------------------------------------------------------------------------
// Crashes
// ---------------------------------------------------------------------------
void writeCrashText(const std::string& text) {
    // console (straight to the error stream), the crash file and the end of latest.log
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
    if (g_crashPath[0]) { std::ofstream f(g_crashPath, std::ios::app); f << text; }
    if (g_latestPath[0]) { std::ofstream f(g_latestPath, std::ios::app); f << text; }
}

std::string crashHeader(const std::string& what) {
    std::ostringstream s;
    s << "\n==================== CRASH ====================\n"
      << "BlockSmith " << g_role << " " << BLOCKSMITH_VERSION << " (commit " << BLOCKSMITH_COMMIT << ")\n"
      << "What:   " << what << "\n"
      << "When:   " << timeString(std::time(nullptr), "%Y-%m-%d %H:%M:%S") << ", " << uptime() << " s after start\n"
      << "Thread: " << threadName() << "\n";
    for (const auto& l : g_extraReport) s << "        " << l << "\n";
    s << "Stack:\n";
    return s.str();
}

void onTerminate() {
    if (g_crashing.exchange(true)) std::_Exit(3);
    std::string what = "std::terminate (an uncaught C++ exception, or a destroyed running thread)";
    if (auto e = std::current_exception()) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& ex) { what = std::string("uncaught exception: ") + ex.what(); }
        catch (...) { what = "uncaught exception of an unknown type"; }
    }
    std::string text = crashHeader(what);
    int k = 0;
    std::string resolve;
    for (const auto& f : stackTraceResolved(1, 40, resolve)) text += "  #" + std::to_string(k++) + " " + f + "\n";
    if (!resolve.empty()) text += "Exact names and lines: " + resolve + "\n";
    text += "===============================================\n";
    writeCrashText(text);
#ifndef _WIN32
    std::signal(SIGABRT, SIG_DFL);
#endif
    std::abort();
}

#ifdef _WIN32
LONG WINAPI onWindowsCrash(EXCEPTION_POINTERS* ep) {
    if (g_crashing.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const char* name = code == EXCEPTION_ACCESS_VIOLATION ? "access violation (bad memory access)"
                     : code == EXCEPTION_STACK_OVERFLOW ? "stack overflow"
                     : code == EXCEPTION_INT_DIVIDE_BY_ZERO ? "integer divide by zero"
                     : code == EXCEPTION_ILLEGAL_INSTRUCTION ? "illegal instruction"
                     : "exception";
    std::ostringstream what;
    what << name << " (code 0x" << std::hex << code << ") at " << describeAddress(reinterpret_cast<DWORD64>(ep->ExceptionRecord->ExceptionAddress));
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        what << ", " << (ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading") << " address 0x"
             << std::hex << ep->ExceptionRecord->ExceptionInformation[1];
    }
    std::string text = crashHeader(what.str());
    int k = 0;
#if defined(_M_X64) || defined(__x86_64__)
    {
        std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);   // (it may have crashed holding it)
        symInit();
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = ctx.Rip;    frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
        while (k < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &ctx,
                                     nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            if (!frame.AddrPC.Offset) break;
            text += "  #" + std::to_string(k++) + " " + describeAddress(frame.AddrPC.Offset) + "\n";
        }
    }
#endif
    if (k == 0) for (const auto& f : stackTrace(0, 40)) text += "  #" + std::to_string(k++) + " " + f + "\n";
    text += "===============================================\n";
    writeCrashText(text);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
// Async-signal-safe output: only write(), and numbers formatted by hand
void sigWrite(int fd, const char* s) { if (fd >= 0) { ssize_t r = write(fd, s, std::strlen(s)); (void)r; } }
void sigWriteNum(int fd, unsigned long long v, int base) {
    char buf[32];
    int i = 31;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v && i > 0) { const int d = static_cast<int>(v % base); buf[--i] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10); v /= base; }
    sigWrite(fd, buf + i);
}
alignas(16) char g_altStack[64 * 1024];

void onSignal(int sig, siginfo_t* info, void*) {
    if (g_crashing.exchange(true)) { std::signal(sig, SIG_DFL); raise(sig); return; }
    const char* name = sig == SIGSEGV ? "SIGSEGV (bad memory access)" : sig == SIGBUS ? "SIGBUS (bad memory access)"
                     : sig == SIGFPE ? "SIGFPE (arithmetic error, e.g. divide by zero)"
                     : sig == SIGILL ? "SIGILL (illegal instruction)" : sig == SIGABRT ? "SIGABRT (aborted)" : "signal";
    int fds[3] = { 2, g_crashPath[0] ? open(g_crashPath, O_WRONLY | O_CREAT | O_APPEND, 0644) : -1,
                   g_latestPath[0] ? open(g_latestPath, O_WRONLY | O_APPEND) : -1 };
    void* frames[64];
    const int n = backtrace(frames, 64);
    for (int fd : fds) {
        if (fd < 0) continue;
        sigWrite(fd, "\n==================== CRASH ====================\nBlockSmith ");
        sigWrite(fd, g_role.c_str());
        sigWrite(fd, " " BLOCKSMITH_VERSION " (commit " BLOCKSMITH_COMMIT ")\nWhat:   ");
        sigWrite(fd, name);
        if (info && info->si_code <= 0) {          // sent by a program (kill), not a fault
            sigWrite(fd, ", sent by process ");
            sigWriteNum(fd, static_cast<unsigned long long>(info->si_pid), 10);
        } else if (info && (sig == SIGSEGV || sig == SIGBUS)) {
            sigWrite(fd, " at address 0x");
            sigWriteNum(fd, reinterpret_cast<uintptr_t>(info->si_addr), 16);
        }
        sigWrite(fd, "\nAfter:  ");
        sigWriteNum(fd, static_cast<unsigned long long>(std::time(nullptr) - g_startWall), 10);
        sigWrite(fd, " s\nStack (module(function+offset) [address] - addr2line -e <module> <offset> gives file:line):\n");
        if (n > 0) backtrace_symbols_fd(frames, n, fd);
        else sigWrite(fd, "(no call stack: this build has no backtrace())\n");
        sigWrite(fd, "===============================================\n");
    }
    if (fds[1] >= 0) close(fds[1]);
    if (fds[2] >= 0) close(fds[2]);
    std::signal(sig, SIG_DFL);
    raise(sig);
}
#endif

// ---------------------------------------------------------------------------
// Lua tracebacks: sol's default error handler (every sol::protected_function call uses it)
// ---------------------------------------------------------------------------
void logLuaError(lua_State* L, const char* label);

int luaTraceHandler(lua_State* L) {
    logLuaError(L, "Error");
    lua_pushvalue(L, 1);   // the error itself, unchanged (messages shown to players stay the same)
    return 1;
}

// pcall in mods: the same as Lua's (same results, yields work), but an error it catches is
// logged with its Lua stack first - a mod catching another mod's error no longer hides where
// it came from
int caughtHandler(lua_State* L) {
    logLuaError(L, "Error caught by pcall");
    lua_pushvalue(L, 1);
    return 1;
}
int finishTracedPcall(lua_State* L, int status, lua_KContext) {
    if (status != LUA_OK && status != LUA_YIELD) {   // [true, handler, error] -> false, error
        lua_pushboolean(L, 0);
        lua_pushvalue(L, -2);
        return 2;
    }
    lua_remove(L, 2);                                // [true, handler, results...] -> true, results...
    return lua_gettop(L);
}
int tracedPcall(lua_State* L) {
    luaL_checkany(L, 1);
    lua_pushboolean(L, 1);
    lua_insert(L, 1);
    lua_pushcfunction(L, caughtHandler);
    lua_insert(L, 2);
    const int status = lua_pcallk(L, lua_gettop(L) - 3, LUA_MULTRET, 2, 0, finishTracedPcall);
    return finishTracedPcall(L, status, 0);
}

void logLuaError(lua_State* L, const char* label) {
    std::string msg;
    if (lua_type(L, 1) == LUA_TSTRING || lua_type(L, 1) == LUA_TNUMBER) msg = lua_tostring(L, 1);
    else msg = std::string("(a ") + luaL_typename(L, 1) + " value)";
    luaL_traceback(L, L, msg.c_str(), 1);
    std::string tb = lua_tostring(L, -1) ? lua_tostring(L, -1) : msg;
    lua_pop(L, 1);
    t_lastLuaError = uptime();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Seen& seen = g_seen[std::string("[Lua] ") + label + ": " + normalize(msg)];
        if (seen.first.empty()) seen.first = std::string("[Lua] ") + label + ": " + msg.substr(0, 180);
        const size_t count = ++seen.count;
        if (traceDue(count)) {
            std::string out = std::string("[Lua] ") + label + (count > 1 ? " (seen " + std::to_string(count) + " times)" : std::string()) + ", with its Lua stack:\n";
            std::istringstream lines(tb);
            std::string l;
            while (std::getline(lines, l)) {
                for (char& c : l) if (c == '\t') c = ' ';
                out += "    | " + l + "\n";
            }
            rawWrite(out);
        }
    }
}

} // namespace

// ===========================================================================

double uptime() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

void init(const char* role) {
    g_role = role ? role : "?";
    g_start = std::chrono::steady_clock::now();
    g_startWall = std::time(nullptr);
    std::set_terminate(onTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onWindowsCrash);
#else
    { void* warm[4]; backtrace(warm, 4); }   // (loads what backtrace needs now, not in a crash)
    stack_t ss{};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof(g_altStack);
    sigaltstack(&ss, nullptr);                // (so a stack overflow can still be reported)
    struct sigaction sa{};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT }) sigaction(sig, &sa, nullptr);
#endif
}

void startLogFile(const fs::path& dir) {
    std::error_code ec;
    const fs::path logs = dir / "logs";
    fs::create_directories(logs, ec);
    g_logDir = logs;
    const fs::path latest = logs / "latest.log";
    if (fs::exists(latest, ec)) {                      // keep the last one: logs/2026-11-19-1.log ...
        const std::string day = timeString(std::time(nullptr), "%Y-%m-%d");
        for (int n = 1; n < 1000; ++n) {
            const fs::path old = logs / (day + "-" + std::to_string(n) + ".log");
            if (!fs::exists(old, ec)) { fs::rename(latest, old, ec); break; }
        }
    }
    const std::string crash = (logs / ("crash-" + timeString(g_startWall, "%Y-%m-%d_%H-%M-%S") + ".txt")).string();
    std::snprintf(g_crashPath, sizeof(g_crashPath), "%s", crash.c_str());
    std::snprintf(g_latestPath, sizeof(g_latestPath), "%s", latest.string().c_str());
    static std::ofstream file(latest, std::ios::trunc);
    if (!file) { std::cerr << "[Log] Could not write " << latest.string() << std::endl; return; }
    static LogTee outTee(std::cout.rdbuf(), &file);
    static LogTee errTee(std::cerr.rdbuf(), &file);
    std::cout.rdbuf(&outTee);
    std::cerr.rdbuf(&errTee);
}

void enableWarningTraces() {
    static bool on = false;
    if (on) return;
    on = true;
    g_rawErr = std::cerr.rdbuf();
    static WarnTraceBuf buf(g_rawErr);
    std::cerr.rdbuf(&buf);
}

void addReportLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_extraReport.push_back(line);
}

std::vector<std::string> systemReport() {
    std::vector<std::string> r;
    r.push_back(std::string("BlockSmith ") + g_role + " " + BLOCKSMITH_VERSION + " (commit " + BLOCKSMITH_COMMIT + ")");
    // how it was built
    std::string compiler =
#if defined(__clang__)
        std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
        std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
        "MSVC " + std::to_string(_MSC_FULL_VER);
#else
        "unknown compiler";
#endif
    std::string arch =
#if defined(__x86_64__) || defined(_M_X64)
        "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
        "arm64";
#elif defined(__i386__) || defined(_M_IX86)
        "x86";
#else
        std::to_string(sizeof(void*) * 8) + "-bit";
#endif
#ifdef NDEBUG
    const char* type = "release";
#else
    const char* type = "debug";
#endif
    r.push_back("Build: " + std::string(type) + ", " + arch + ", " + compiler + ", C++ " + std::to_string(__cplusplus) +
                ", built " + __DATE__ + " " + __TIME__);
    // where it runs
    std::string os = "unknown OS";
#ifdef _WIN32
    {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        OSVERSIONINFOEXW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion")));
        if (fn && fn(&v) == 0) {
            os = "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + " build " +
                 std::to_string(v.dwBuildNumber);
        }
    }
#else
    {
        utsname u{};
        if (uname(&u) == 0) os = std::string(u.sysname) + " " + u.release + " (" + u.machine + ")";
    }
#endif
    r.push_back("System: " + os);
    std::string cpu = "?";
    unsigned long long ramMB = 0;
#ifdef _WIN32
    {
        HKEY key;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS) {
            char buf[256] = {};
            DWORD size = sizeof(buf) - 1;
            if (RegQueryValueExA(key, "ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) cpu = buf;
            RegCloseKey(key);
        }
        MEMORYSTATUSEX m{};
        m.dwLength = sizeof(m);
        if (GlobalMemoryStatusEx(&m)) ramMB = m.ullTotalPhys / (1024 * 1024);
    }
#elif defined(__APPLE__)
    {
        char buf[256] = {};
        size_t size = sizeof(buf) - 1;
        if (sysctlbyname("machdep.cpu.brand_string", buf, &size, nullptr, 0) == 0) cpu = buf;
        uint64_t mem = 0;
        size = sizeof(mem);
        if (sysctlbyname("hw.memsize", &mem, &size, nullptr, 0) == 0) ramMB = mem / (1024 * 1024);
    }
#else
    {
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("model name", 0) == 0) { const size_t c = line.find(':'); if (c != std::string::npos) cpu = line.substr(c + 2); break; }
        }
        const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && page > 0) ramMB = static_cast<unsigned long long>(pages) * page / (1024 * 1024);
    }
#endif
    r.push_back("CPU: " + cpu + ", " + std::to_string(std::thread::hardware_concurrency()) + " threads; memory " +
                (ramMB ? std::to_string(ramMB) + " MB" : std::string("?")));
    r.push_back(std::string("Lua: ") + LUA_RELEASE);
    std::error_code ec;
#ifdef _WIN32
    const unsigned long pid = static_cast<unsigned long>(_getpid());
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    r.push_back("Process " + std::to_string(pid) + ", folder " + fs::current_path(ec).string());
    r.push_back("Started " + timeString(g_startWall, "%Y-%m-%d %H:%M:%S") + " (" + timeString(g_startWall, "%Y-%m-%d %H:%M:%S UTC", true) +
                "), up " + std::to_string(static_cast<long long>(uptime())) + " s");
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& l : g_extraReport) r.push_back(l);
    return r;
}

void printSystemReport() {
    for (const auto& l : systemReport()) std::cout << "[Diag] " << l << "\n";
    std::cout.flush();
}

std::vector<std::string> warningSummary(size_t max) {
    std::vector<std::pair<size_t, std::string>> list;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& [k, v] : g_seen) list.push_back({ v.count, v.first });
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < list.size() && i < max; ++i) out.push_back(std::to_string(list[i].first) + " x " + list[i].second);
    return out;
}

// The stack (as stackTrace), and a command that turns its frames in this program into exact
// function, file and line: addr2line (Linux) or atos (macOS). The names above come from the
// program's exported symbols; a function that isn't exported shows as the nearest one that is,
// which can mislead - the command never does (with debug info: BLOCKSMITH_DEBUG_INFO).
std::vector<std::string> stackTraceResolved(int skip, int max, std::string& resolve) {
    resolve.clear();
#ifdef _WIN32
    return stackTrace(skip + 1, max);   // (Windows: DbgHelp already reads the .pdb)
#else
    std::vector<std::string> out;
    void* frames[64];
    const int n = backtrace(frames, std::min(64, max + skip + 1));
    Dl_info self{};
    dladdr(reinterpret_cast<void*>(&init), &self);   // this program: where it's loaded, its file
    std::string addrs;
    for (int i = skip + 1; i < n; ++i) {
        out.push_back(describeAddress(frames[i]));
        Dl_info info{};
        if (self.dli_fbase && dladdr(frames[i], &info) && info.dli_fbase == self.dli_fbase) {
            std::ostringstream a;
#if defined(__APPLE__)
            a << " 0x" << std::hex << reinterpret_cast<uintptr_t>(frames[i]);
#else
            a << " 0x" << std::hex << (reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(self.dli_fbase));
#endif
            addrs += a.str();
        }
    }
    if (!addrs.empty() && self.dli_fname) {
        const std::string exe = fs::path(self.dli_fname).filename().string();
        std::ostringstream c;
#if defined(__APPLE__)
        c << "atos -o " << exe << " -l 0x" << std::hex << reinterpret_cast<uintptr_t>(self.dli_fbase) << addrs;
#else
        c << "addr2line -f -C -i -e " << exe << addrs;
#endif
        resolve = c.str();
    }
    return out;
#endif
}

std::vector<std::string> stackTrace(int skip, int max) {
    std::vector<std::string> out;
#ifdef _WIN32
    void* frames[64];
    const USHORT n = CaptureStackBackTrace(static_cast<DWORD>(skip + 1), static_cast<DWORD>(std::min(max, 62)), frames, nullptr);
    for (USHORT i = 0; i < n; ++i) out.push_back(describeAddress(reinterpret_cast<DWORD64>(frames[i])));
#else
    void* frames[64];
    const int n = backtrace(frames, std::min(64, max + skip + 1));
    for (int i = skip + 1; i < n; ++i) out.push_back(describeAddress(frames[i]));
#endif
    return out;
}

void installLuaTraceback(lua_State* L) {
    if (!L) return;
    lua_pushcfunction(L, luaTraceHandler);
    lua_setglobal(L, sol::detail::default_handler_name());
    lua_pushcfunction(L, tracedPcall);
    lua_setglobal(L, "pcall");
}

} // namespace diag
