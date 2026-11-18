#pragma once
// Generates new chunks on background threads, so the server (or game) never stalls while
// the world generator runs. Each thread has its own Lua state with the game's world
// generator loaded (Lua can't be shared between threads).
//
// World asks for chunks with request() and collects them with takeFinished(); it decides
// which results are still wanted. Saved chunks are still read on the main thread (that's fast).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>
#include <sol/sol.hpp>
#include "ChunkData.h"

class ChunkGenerator {
public:
    // Builds a Lua state with the world generator (generate_chunk / set_world_seed) loaded.
    // Called once on each worker thread. May return nullptr (then the built-in terrain is used).
    using LuaFactory = std::function<std::unique_ptr<sol::state>()>;

    struct Result {
        int cx, cz;
        unsigned epoch;                    // World::resetCount() when requested
        std::shared_ptr<ChunkData> chunk;  // nullptr: the job failed (set_world_seed errored) -
                                           // nothing was generated; request it again later
    };

    // threads <= 0: one per CPU core, minus `reserveCores` left free for the main thread
    // (and the graphics driver on the client); at least 1, at most 8.
    ChunkGenerator(int threads, LuaFactory factory, uint32_t seed, int reserveCores = 1);
    ~ChunkGenerator();
    ChunkGenerator(const ChunkGenerator&) = delete;
    ChunkGenerator& operator=(const ChunkGenerator&) = delete;

    // `seed`: the world seed for this chunk (the game can switch worlds; each thread
    // re-seeds its Lua when it changes). Defaults to the seed given to the constructor.
    void request(int cx, int cz, unsigned epoch);
    void request(int cx, int cz, unsigned epoch, uint32_t seed);
    // Drops every request that hasn't started (switching or leaving a world).
    void clearQueue();
    // Removes a request that hasn't started yet (the player moved away). False if already running.
    bool cancel(int cx, int cz);
    std::vector<Result> takeFinished();

    int threadCount() const { return static_cast<int>(m_threads.size()); }
    size_t queuedCount() const;
    // Chunks generated per second over the last few seconds (for the 'tps' command).
    double chunksPerSecond() const { return m_rate.load(); }

private:
    void workerLoop(int index);

    LuaFactory m_factory;
    uint32_t m_seed;
    std::vector<std::thread> m_threads;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    struct Job { int cx, cz; unsigned epoch; uint32_t seed; };
    std::deque<Job> m_jobs;
    std::vector<Result> m_done;
    bool m_stop = false;

    std::atomic<int> m_generatedThisWindow{0};
    std::atomic<double> m_rate{0.0};
    std::chrono::steady_clock::time_point m_rateWindow = std::chrono::steady_clock::now();
};
