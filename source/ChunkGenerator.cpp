#include "ChunkGenerator.h"
#include "World.h"
#include <algorithm>
#include <iostream>

ChunkGenerator::ChunkGenerator(int threads, LuaFactory factory, uint32_t seed, int reserveCores)
    : m_factory(std::move(factory)), m_seed(seed) {
    if (threads <= 0) {
        const int cores = static_cast<int>(std::thread::hardware_concurrency());
        threads = std::clamp(cores - reserveCores, 1, 8);
    }
    threads = std::clamp(threads, 1, 8);
    m_threads.reserve(static_cast<size_t>(threads));   // adding a started thread can't reallocate (and throw)
    try {
        for (int i = 0; i < threads; ++i) m_threads.emplace_back(&ChunkGenerator::workerLoop, this, i);
    } catch (...) {
        // A thread couldn't be started (std::system_error: the OS refused). The workers already
        // running must be stopped and joined here, while the mutex and condition variable they
        // wait on are still alive - leaving the constructor with joinable std::threads would
        // call std::terminate. Then the original error goes to the caller.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
            m_jobs.clear();
        }
        m_wake.notify_all();
        for (auto& t : m_threads) {
            if (t.joinable()) t.join();
        }
        m_threads.clear();
        throw;
    }
}

ChunkGenerator::~ChunkGenerator() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_jobs.clear();
    }
    m_wake.notify_all();
    for (auto& t : m_threads) t.join();
}

void ChunkGenerator::request(int cx, int cz, unsigned epoch) { request(cx, cz, epoch, m_seed); }

void ChunkGenerator::request(int cx, int cz, unsigned epoch, uint32_t seed) {
    request(cx, cz, epoch, seed, std::string(), true);
}

void ChunkGenerator::request(int cx, int cz, unsigned epoch, uint32_t seed, const std::string& dimension, bool caves) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Job j;
        j.cx = cx; j.cz = cz; j.epoch = epoch; j.seed = seed;
        j.dimension = dimension == "overworld" ? std::string() : dimension;
        j.caves = caves;
        m_jobs.push_back(std::move(j));
    }
    m_wake.notify_one();
}

void ChunkGenerator::clearQueue() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.clear();
}

void ChunkGenerator::clearQueue(const std::string& dimensionIn) {
    const std::string dimension = dimensionIn == "overworld" ? std::string() : dimensionIn;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [&](const Job& j) { return j.dimension == dimension; }),
                 m_jobs.end());
}

bool ChunkGenerator::cancel(int cx, int cz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        if (it->cx == cx && it->cz == cz) {
            m_jobs.erase(it);
            return true;
        }
    }
    return false;
}

bool ChunkGenerator::cancel(int cx, int cz, const std::string& dimensionIn) {
    const std::string dimension = dimensionIn == "overworld" ? std::string() : dimensionIn;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        if (it->cx == cx && it->cz == cz && it->dimension == dimension) {
            m_jobs.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<ChunkGenerator::Result> ChunkGenerator::takeFinished(const std::string& dimensionIn) {
    const std::string dimension = dimensionIn == "overworld" ? std::string() : dimensionIn;
    std::vector<Result> out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto keep = std::stable_partition(m_done.begin(), m_done.end(),
                                          [&](const Result& r) { return r.dimension != dimension; });
        out.assign(std::make_move_iterator(keep), std::make_move_iterator(m_done.end()));
        m_done.erase(keep, m_done.end());
    }
    updateRate();
    return out;
}

std::vector<ChunkGenerator::Result> ChunkGenerator::takeFinished() {
    std::vector<Result> out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.swap(m_done);
    }
    updateRate();
    return out;
}

void ChunkGenerator::updateRate() {
    // Rate for 'tps'
    const auto now = std::chrono::steady_clock::now();
    const double span = std::chrono::duration<double>(now - m_rateWindow).count();
    if (span >= 2.0) {
        m_rate = m_generatedThisWindow.exchange(0) / span;
        m_rateWindow = now;
    }
}

size_t ChunkGenerator::queuedCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_jobs.size();
}

void ChunkGenerator::workerLoop(int index) {
    // This thread's own Lua with the world generator
    std::unique_ptr<sol::state> lua;
    try {
        if (m_factory) lua = m_factory();
    } catch (const std::exception& e) {
        std::cerr << "[Generator " << index << "] Lua setup failed (chunks will be empty): " << e.what() << std::endl;
        lua.reset();
    }

    // The Lua had a world generator (the factory worked) but couldn't be rebuilt after a failed
    // set_world_seed: jobs fail instead of quietly switching to empty chunks.
    bool generatorLost = false;
    std::string lastSeedError;
    bool seeded = false;          // luaSeed is what set_world_seed last ran with successfully
    uint32_t luaSeed = 0;
    bool reportedFailure = false; // set_world_seed errors are logged once per seed, not per chunk
    uint32_t reportedSeed = 0;
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_stop) return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }
        if (generatorLost) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done.push_back({job.cx, job.cz, job.dimension, job.epoch, nullptr,
                              "the world generator couldn't be restarted after set_world_seed failed: " + lastSeedError});
            continue;
        }
        if (lua && seeded && luaSeed != job.seed && m_factory) {
            // Another world: start from a fresh Lua state, so nothing a world generator worked
            // out for the old seed (caches of where things are, and so on) carries over into
            // this world's chunks - every chunk comes from the same, cleanly seeded generator
            try {
                auto fresh = m_factory();
                if (fresh) { lua = std::move(fresh); seeded = false; }
            } catch (const std::exception& e) {
                std::cerr << "[Generator " << index << "] couldn't start fresh for the new world: " << e.what() << std::endl;
            }
        }
        if (lua && (!seeded || luaSeed != job.seed)) {
            bool ok = true;
            {   // (everything referring to this Lua state ends here - it may be replaced below)
                sol::protected_function setSeed = (*lua)["set_world_seed"];
                if (setSeed.valid()) {
                    auto r = setSeed(job.seed);
                    if (!r.valid()) {
                        ok = false;
                        sol::error err = r;
                        lastSeedError = err.what();
                        if (!reportedFailure || reportedSeed != job.seed) {
                            std::cerr << "[Generator " << index << "] set_world_seed: " << lastSeedError
                                      << " (chunks wait until it works)" << std::endl;
                            reportedFailure = true;
                            reportedSeed = job.seed;
                        }
                    }
                }
            }
            if (!ok) {
                // The terrain is never generated unseeded: this job fails with the Lua error, and
                // World decides when to ask again (and when to give up and stop opening the world).
                // The hook may have half-changed this state before failing, so it is thrown away:
                // a retry starts from a fresh state, exactly like the first attempt (set up by the
                // same factory) - see set_world_seed in docs/MODDING.md.
                seeded = false;
                std::string error = lastSeedError;
                lua.reset();
                try {
                    if (m_factory) lua = m_factory();
                } catch (const std::exception& e) {
                    lua.reset();
                    error += std::string(" (and the world generator couldn't be restarted: ") + e.what() + ")";
                }
                if (!lua) generatorLost = true;   // no Lua to retry with: every job fails from now on
                std::lock_guard<std::mutex> lock(m_mutex);
                m_done.push_back({job.cx, job.cz, job.dimension, job.epoch, nullptr, error});
                continue;
            }
            seeded = true;                     // only now: set_world_seed really ran with it
            luaSeed = job.seed;
            reportedFailure = false;
        }
        auto chunk = std::make_shared<ChunkData>();
        World::generateNewChunk(*chunk, job.cx, job.cz, lua.get(), job.seed, job.dimension, job.caves);
        ++m_generatedThisWindow;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done.push_back({job.cx, job.cz, job.dimension, job.epoch, std::move(chunk), std::string()});
    }
}
