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
    for (int i = 0; i < threads; ++i) m_threads.emplace_back(&ChunkGenerator::workerLoop, this, i);
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
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.push_back({cx, cz, epoch, seed});
    }
    m_wake.notify_one();
}

void ChunkGenerator::clearQueue() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.clear();
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

std::vector<ChunkGenerator::Result> ChunkGenerator::takeFinished() {
    std::vector<Result> out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.swap(m_done);
    }
    // Rate for 'tps'
    const auto now = std::chrono::steady_clock::now();
    const double span = std::chrono::duration<double>(now - m_rateWindow).count();
    if (span >= 2.0) {
        m_rate = m_generatedThisWindow.exchange(0) / span;
        m_rateWindow = now;
    }
    return out;
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
        std::cerr << "[Generator " << index << "] Lua setup failed (using built-in terrain): " << e.what() << std::endl;
        lua.reset();
    }

    bool seeded = false;
    uint32_t luaSeed = 0;
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_stop) return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }
        if (lua && (!seeded || luaSeed != job.seed)) {
            sol::protected_function setSeed = (*lua)["set_world_seed"];
            if (setSeed.valid()) {
                auto r = setSeed(job.seed);
                if (!r.valid()) {
                    sol::error err = r;
                    std::cerr << "[Generator " << index << "] set_world_seed: " << err.what() << std::endl;
                }
            }
            seeded = true;
            luaSeed = job.seed;
        }
        auto chunk = std::make_shared<ChunkData>();
        World::generateNewChunk(*chunk, job.cx, job.cz, lua.get(), job.seed);
        ++m_generatedThisWindow;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done.push_back({job.cx, job.cz, job.epoch, std::move(chunk)});
    }
}
