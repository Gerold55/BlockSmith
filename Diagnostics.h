#pragma once
// Diagnostics: as much information as possible about what went wrong, for the game and the
// dedicated server alike.
//
//   - A system report at start: which BlockSmith (version, commit, protocol), how it was built
//     (compiler, build type, architecture), and where it runs (OS, CPU threads, memory, Lua).
//   - logs/latest.log: everything printed, each line with the time (the previous log is kept as
//     logs/<date>-<n>.log). The server always had one; the game has one too now.
//   - Warning traces: every line printed as a warning or error (std::cerr) is followed by a
//     trace: the build, the thread, how long it had been running, and the native call stack that
//     printed it. The same warning over and over gets the full trace the 1st, 10th, 100th...
//     time, with the count, so a warning every frame can't flood the log.
//   - Lua tracebacks: every Lua error the engine catches is printed with the Lua stack it came
//     from (which mod, which file, which line, called from where).
//   - Crash reports: a crash (segfault, abort, illegal instruction...) or an uncaught C++
//     exception prints what happened with the call stack, to the console and to
//     logs/crash-<date>_<time>.txt, before the program ends.
//
// Native stacks name functions where the platform can (Linux / macOS: exported symbols, so
// the build exports them; Windows: DbgHelp, best with a .pdb). Every frame also shows its module
// and offset ("BlockSmithServer+0x1a2b3c"), which addr2line / a debugger turns into file:line.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct lua_State;

namespace diag {

// Call first thing in main(): role is "client" or "server". Remembers the start time, installs
// the crash handlers and std::terminate handler.
void init(const char* role);

// The log file: logs/latest.log in `dir` (everything std::cout / std::cerr print from now on,
// with the time on each line). Crash reports go to the same folder.
void startLogFile(const std::filesystem::path& dir);

// From now on, every line printed to std::cerr is followed by its trace (see above). Call after
// startLogFile, so the traces go to the log too.
void enableWarningTraces();

// The system report, one line each (also printed by printSystemReport)
std::vector<std::string> systemReport();
void printSystemReport();

// Extra lines for the system report (the game adds its graphics card and OpenGL version)
void addReportLine(const std::string& line);

// The warnings seen so far, most frequent first: "count x message" (for the server's diag command)
std::vector<std::string> warningSummary(size_t max = 20);

// The native call stack here, one line per frame (skip: frames to leave out at the top)
std::vector<std::string> stackTrace(int skip = 0, int max = 24);

// Every Lua error the engine catches (sol::protected_function) is printed with its Lua traceback.
// Call once for each Lua state, after its libraries are open.
void installLuaTraceback(lua_State* L);

// Seconds since init()
double uptime();

} // namespace diag
