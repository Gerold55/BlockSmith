#include "CaveGenerator.h"
#include "ChunkData.h"
#include "Block.h"
#include "Types.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

// ===========================================================================
// Tunables
// ===========================================================================

constexpr int kBedrockTop = 4;        // y <= this is never carved

// Tunnels and ravines are worked out from every chunk within this many chunks, so a cave
// that starts in one chunk carves the same path through all the chunks it reaches
constexpr int kCarverRange = 8;

// Noise caves (caverns, noodles) - sampled every 4 blocks and blended in between
constexpr int   kSurfaceCrust     = 6;          // noodles stay this far below the surface
constexpr float kNoodleScale      = 1.0f / 30.0f;
constexpr float kNoodleRadius     = 0.060f;
constexpr float kNoodleRegion     = 0.20f;      // higher = noodle regions are rarer
constexpr float kCheeseScaleXZ    = 1.0f / 80.0f;
constexpr float kCheeseScaleY     = 1.0f / 40.0f;
constexpr float kCheeseThreshold  = 0.45f;      // higher = smaller/fewer caverns
constexpr int   kCheeseMinDepth   = 24;         // caverns stay at least this far below the surface
constexpr int   kCheeseTopY       = 48;         // ...and below this height
constexpr float kCheeseTaper      = 0.025f;     // they shrink towards that limit
constexpr int   kStepXZ = 4;
constexpr int   kStepY  = 4;

// ===========================================================================
// State
// ===========================================================================

std::atomic<uint32_t> g_seed{0};
std::atomic<bool>     g_enabled{true};
std::mutex            g_settingsMutex;
CaveSettings          g_settings;

uint32_t fieldSeed(uint32_t seed, int field) {
    return seed ^ (0x9E3779B9u * static_cast<uint32_t>(field + 1));
}

// ===========================================================================
// Random numbers: the same on every computer and build (no std:: distributions,
// whose results differ between compilers)
// ===========================================================================

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        s += 0x9E3779B97F4A7C15ull;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int nextInt(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint64_t>(n)); }
    double nextDouble() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
};

uint64_t chunkSeed(uint32_t seed, int cx, int cz, uint64_t salt) {
    uint64_t h = (static_cast<uint64_t>(seed) << 32) ^ salt;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(cx)) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(cz)) * 0xC2B2AE3D27D4EB4Full;
    return Rng(h).next();
}

// ===========================================================================
// Seeded 3D Perlin noise (roughly -1..1)
// ===========================================================================

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float t, float a, float b) { return a + t * (b - a); }

inline float grad(uint32_t hash, float x, float y, float z) {
    const uint32_t h = hash & 15u;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1u) == 0 ? u : -u) + ((h & 2u) == 0 ? v : -v);
}

inline uint32_t hash3D(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x27D4EB2Du; h = (h ^ (h >> 15)) * 0x85EBCA6Bu;
    h ^= static_cast<uint32_t>(y) * 0x165667B1u; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
    h ^= static_cast<uint32_t>(z) * 0x9E3779B1u; h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
    return h ^ (h >> 13);
}

float noise3D(float x, float y, float z, uint32_t seed) {
    const int X = static_cast<int>(std::floor(x));
    const int Y = static_cast<int>(std::floor(y));
    const int Z = static_cast<int>(std::floor(z));
    const float fx = x - X, fy = y - Y, fz = z - Z;
    const float u = fade(fx), v = fade(fy), w = fade(fz);
    const float g000 = grad(hash3D(X,     Y,     Z,     seed), fx,        fy,        fz);
    const float g100 = grad(hash3D(X + 1, Y,     Z,     seed), fx - 1.0f, fy,        fz);
    const float g010 = grad(hash3D(X,     Y + 1, Z,     seed), fx,        fy - 1.0f, fz);
    const float g110 = grad(hash3D(X + 1, Y + 1, Z,     seed), fx - 1.0f, fy - 1.0f, fz);
    const float g001 = grad(hash3D(X,     Y,     Z + 1, seed), fx,        fy,        fz - 1.0f);
    const float g101 = grad(hash3D(X + 1, Y,     Z + 1, seed), fx - 1.0f, fy,        fz - 1.0f);
    const float g011 = grad(hash3D(X,     Y + 1, Z + 1, seed), fx,        fy - 1.0f, fz - 1.0f);
    const float g111 = grad(hash3D(X + 1, Y + 1, Z + 1, seed), fx - 1.0f, fy - 1.0f, fz - 1.0f);
    const float x00 = lerp(u, g000, g100);
    const float x10 = lerp(u, g010, g110);
    const float x01 = lerp(u, g001, g101);
    const float x11 = lerp(u, g011, g111);
    return lerp(w, lerp(v, x00, x10), lerp(v, x01, x11));
}

// Noodles: where TWO independent noise fields are both near zero - a long thin twisty line.
// Only inside "noodle regions". Negative = inside.
float noodleField(float x, float y, float z, uint32_t seed) {
    const float region = noise3D(x / 200.0f, 0.0f, z / 200.0f, fieldSeed(seed, 3));
    if (region <= kNoodleRegion) return 1.0f;
    const float nx = x * kNoodleScale, ny = y * kNoodleScale, nz = z * kNoodleScale;
    const float c = noise3D(nx, ny, nz, fieldSeed(seed, 4));
    const float d = noise3D(nx, ny, nz, fieldSeed(seed, 5));
    return (c * c + d * d) - kNoodleRadius * kNoodleRadius;
}

// Caverns ("cheese"): big blobs where the noise is high; pillars are left between them.
float cheeseField(float x, float y, float z, uint32_t seed, float size) {
    const float e = noise3D(x * kCheeseScaleXZ, y * kCheeseScaleY, z * kCheeseScaleXZ, fieldSeed(seed, 6))
                  + 0.5f * noise3D(x * kCheeseScaleXZ * 2.0f, y * kCheeseScaleY * 2.0f,
                                   z * kCheeseScaleXZ * 2.0f, fieldSeed(seed, 7));
    return (kCheeseThreshold - 0.08f * (size - 1.0f)) - e;
}

// ===========================================================================
// Which blocks can be carved
// ===========================================================================

struct BlockClasses {
    bool ready = false;
    bool carvable[256] = {};
    bool liquid[256] = {};
    bool anchor[256] = {};         // tree trunks and bushes: never left hanging over a cave
    bool needsSupport[256] = {};   // plants and other non-solid decorations: they go when the
                                   // ground under them is carved away (no floating flowers)
    uint8_t lavaId = 0, grassId = 0, dirtId = 0;
};
BlockClasses g_blocks;
std::mutex g_blocksMutex;

bool containsAny(const std::string& s, std::initializer_list<const char*> parts) {
    for (const char* p : parts) if (s.find(p) != std::string::npos) return true;
    return false;
}

void classifyBlocks() {
    std::lock_guard<std::mutex> lock(g_blocksMutex);
    if (g_blocks.ready) return;
    auto& reg = BlockRegistry::getInstance();
    for (int id = 1; id < 256; ++id) {
        const auto& b = reg.getBlock(static_cast<uint8_t>(id));
        if (b.name.empty() || b.name == "air") continue;
        if (containsAny(b.name, { "water", "lava" })) {
            g_blocks.liquid[id] = true;
            continue;
        }
        // Grass tufts, flowers, mushrooms, crops...: not solid, so they need ground under them
        g_blocks.needsSupport[id] = !b.isWalkable;
        g_blocks.anchor[id] = b.isWalkable && containsAny(b.name, { "log", "leaves", "trunk", "cactus" });
        // Solid terrain only - leave trees, plants, and placed building blocks alone
        g_blocks.carvable[id] = b.isWalkable &&
            !containsAny(b.name, { "log", "leaves", "planks", "wood", "bedrock", "door", "glass" });
    }
    g_blocks.lavaId = reg.getBlockId("lava");
    g_blocks.grassId = reg.getBlockId("grass_block");
    if (g_blocks.grassId == 0) g_blocks.grassId = reg.getBlockId("grass");
    g_blocks.dirtId = reg.getBlockId("dirt");
    g_blocks.ready = true;
}

// ===========================================================================
// Carving one chunk
// ===========================================================================

struct Target {
    ChunkData& chunk;
    int baseX, baseZ;
    int maxY;
    std::vector<uint8_t> carved;   // what we carved there (0 = not carved), so lava and grass can
                                   // be fixed up after and ground under trees put back
    bool carvedGrass = false;

    Target(ChunkData& c, int cx, int cz, int top)
        : chunk(c), baseX(cx * CHUNK_WIDTH), baseZ(cz * CHUNK_WIDTH), maxY(top),
          carved(static_cast<size_t>(CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH), 0) {}
    static int index(int x, int y, int z) { return (x * CHUNK_HEIGHT + y) * CHUNK_WIDTH + z; }

    void carve(int x, int y, int z) {
        const uint8_t id = chunk.getLocalBlock(x, y, z);
        if (!g_blocks.carvable[id]) return;
        if (id == g_blocks.grassId && g_blocks.grassId != 0) carvedGrass = true;
        chunk.setBlock(x, y, z, 0);
        carved[index(x, y, z)] = id;
    }

    // Is there water or lava in this box (or right next to it)? Caves stop there, as in
    // Minecraft, so they never drain the sea or a lake into a hole
    bool liquidNear(int x0, int x1, int y0, int y1, int z0, int z1) const {
        x0 = std::max(x0 - 1, 0); x1 = std::min(x1 + 1, CHUNK_WIDTH - 1);
        z0 = std::max(z0 - 1, 0); z1 = std::min(z1 + 1, CHUNK_WIDTH - 1);
        y0 = std::max(y0 - 1, 0); y1 = std::min(y1 + 1, CHUNK_HEIGHT - 1);
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z)
                for (int y = y1; y >= y0; --y)
                    if (g_blocks.liquid[chunk.getLocalBlock(x, y, z)]) return true;
        return false;
    }

    // Carves an ellipsoid at world position (cx, cy, cz). floorCut: how much of the bottom is
    // left (Minecraft cuts at -0.7, giving caves flat-ish floors). yScale (ravines): extra
    // width per height for jagged walls.
    void ellipsoid(double cx, double cy, double cz, double rh, double rv, const float* yScale = nullptr) {
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - rh)) - baseX - 1);
        const int x1 = std::min(CHUNK_WIDTH - 1, static_cast<int>(std::floor(cx + rh)) - baseX + 1);
        const int z0 = std::max(0, static_cast<int>(std::floor(cz - rh)) - baseZ - 1);
        const int z1 = std::min(CHUNK_WIDTH - 1, static_cast<int>(std::floor(cz + rh)) - baseZ + 1);
        const int y0 = std::max(kBedrockTop + 1, static_cast<int>(std::floor(cy - rv)) - 1);
        const int y1 = std::min(maxY, static_cast<int>(std::floor(cy + rv)) + 1);
        if (x0 > x1 || z0 > z1 || y0 > y1) return;
        if (liquidNear(x0, x1, y0, y1, z0, z1)) return;

        for (int x = x0; x <= x1; ++x) {
            const double dx = (baseX + x + 0.5 - cx) / rh;
            for (int z = z0; z <= z1; ++z) {
                const double dz = (baseZ + z + 0.5 - cz) / rh;
                const double flat = dx * dx + dz * dz;
                if (flat >= 1.0) continue;
                for (int y = y1; y >= y0; --y) {
                    const double dy = (y + 0.5 - cy) / rv;
                    if (yScale) {
                        if (flat * yScale[y] + dy * dy / 6.0 < 1.0) carve(x, y, z);
                    } else if (dy > -0.7 && flat + dy * dy < 1.0) {
                        carve(x, y, z);
                    }
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Tunnels (Minecraft's classic cave carver)
// ---------------------------------------------------------------------------

struct CarveContext {
    Target& t;
    double centerX, centerZ;   // middle of the chunk being carved
    float width;               // tunnelWidth setting
};

void tunnel(CarveContext& ctx, uint64_t seed, double x, double y, double z, double width,
            double yaw, double pitch, int step, int length, double heightScale) {
    Rng rng(seed);
    const int maxLength = kCarverRange * 16 - 16;
    if (length <= 0) length = maxLength - rng.nextInt(maxLength / 4);
    bool room = false;
    if (step == -1) { step = length / 2; room = true; }
    const int splitAt = rng.nextInt(length / 2) + length / 4;
    const bool steep = rng.nextInt(6) == 0;
    double yawChange = 0.0, pitchChange = 0.0;

    for (; step < length; ++step) {
        const double rh = (1.5 + std::sin(step * kPi / length) * width) * ctx.width;
        const double rv = rh * heightScale;

        const double cp = std::cos(pitch);
        x += std::cos(yaw) * cp;
        y += std::sin(pitch);
        z += std::sin(yaw) * cp;
        pitch *= steep ? 0.92 : 0.7;
        pitch += pitchChange * 0.1;
        yaw += yawChange * 0.1;
        pitchChange *= 0.9;
        yawChange *= 0.75;
        // One draw per statement: C++ leaves the order of calls inside one expression open, so
        // writing them in one line let different compilers (MSVC, GCC, Clang) draw them in a
        // different order and carve different caves from the same seed. Order: source order.
        const double pitchA = rng.nextDouble();
        const double pitchB = rng.nextDouble();
        const double pitchC = rng.nextDouble();
        pitchChange += (pitchA - pitchB) * pitchC * 2.0;
        const double yawA = rng.nextDouble();
        const double yawB = rng.nextDouble();
        const double yawC = rng.nextDouble();
        yawChange += (yawA - yawB) * yawC * 4.0;

        // Split into two branches going left and right
        if (!room && step == splitAt && width > 1.0) {
            // (each branch's seed, then its width: drawn before the call - function arguments
            // are evaluated in no fixed order)
            const uint64_t leftSeed = rng.next();
            const double leftWidth = rng.nextDouble() * 0.5 + 0.5;
            tunnel(ctx, leftSeed, x, y, z, leftWidth, yaw - kPi / 2, pitch / 3.0, step, length, 1.0);
            const uint64_t rightSeed = rng.next();
            const double rightWidth = rng.nextDouble() * 0.5 + 0.5;
            tunnel(ctx, rightSeed, x, y, z, rightWidth, yaw + kPi / 2, pitch / 3.0, step, length, 1.0);
            return;
        }
        if (!room && rng.nextInt(4) == 0) continue;   // some steps carve nothing: lumpier walls

        // Too far away to ever reach the chunk being carved: stop
        const double dx = x - ctx.centerX, dz = z - ctx.centerZ;
        const double remaining = length - step;
        const double reach = width + 2.0 + 16.0;
        if (dx * dx + dz * dz - remaining * remaining > reach * reach) return;

        if (x >= ctx.centerX - 16.0 - rh * 2.0 && z >= ctx.centerZ - 16.0 - rh * 2.0 &&
            x <= ctx.centerX + 16.0 + rh * 2.0 && z <= ctx.centerZ + 16.0 + rh * 2.0) {
            ctx.t.ellipsoid(x, y, z, rh, rv);
        }
        if (room) break;
    }
}

void tunnelsFrom(CarveContext& ctx, uint32_t seed, int ox, int oz, float frequency, int maxY) {
    Rng rng(chunkSeed(seed, ox, oz, 0x7C1A9E5Bull));
    int count = rng.nextInt(rng.nextInt(rng.nextInt(15) + 1) + 1);
    // Minecraft: caves start in about 1 chunk in 7 (frequency scales that)
    const double chance = std::min(1.0, frequency / 7.0);
    if (rng.nextDouble() >= chance) count = 0;

    const int startTop = std::max(16, std::min(maxY, 128) - 8);
    for (int i = 0; i < count; ++i) {
        const double x = ox * 16 + rng.nextInt(16);
        const double y = rng.nextInt(rng.nextInt(startTop) + 8);
        const double z = oz * 16 + rng.nextInt(16);
        int branches = 1;
        if (rng.nextInt(4) == 0) {
            // A room: a big round chamber the tunnels come out of
            const uint64_t roomSeed = rng.next();                 // seed, then size (not as two arguments)
            const double roomWidth = 1.0 + rng.nextDouble() * 6.0;
            tunnel(ctx, roomSeed, x, y, z, roomWidth, 0.0, 0.0, -1, -1, 0.5);
            branches += rng.nextInt(4);
        }
        for (int b = 0; b < branches; ++b) {
            const double yaw = rng.nextDouble() * kPi * 2.0;
            const double pitch = (rng.nextDouble() - 0.5) * 2.0 / 8.0;
            const double widthA = rng.nextDouble();
            const double widthB = rng.nextDouble();
            double width = widthA * 2.0 + widthB;
            if (rng.nextInt(10) == 0) {                           // a big one
                const double bigA = rng.nextDouble();
                const double bigB = rng.nextDouble();
                width *= bigA * bigB * 3.0 + 1.0;
            }
            tunnel(ctx, rng.next(), x, y, z, width, yaw, pitch, 0, 0, 1.0);
        }
    }
}

// ---------------------------------------------------------------------------
// Ravines
// ---------------------------------------------------------------------------

void ravine(CarveContext& ctx, uint64_t seed, double x, double y, double z, double width,
            double yaw, double pitch, double heightScale) {
    Rng rng(seed);
    const int maxLength = kCarverRange * 16 - 16;
    const int length = maxLength - rng.nextInt(maxLength / 4);
    double yawChange = 0.0, pitchChange = 0.0;

    // Jagged walls: each height gets its own width
    float yScale[CHUNK_HEIGHT];
    float f = 1.0f;
    for (int i = 0; i < CHUNK_HEIGHT; ++i) {
        if (i == 0 || rng.nextInt(3) == 0) {
            const double wallA = rng.nextDouble();
            const double wallB = rng.nextDouble();
            f = 1.0f + static_cast<float>(wallA * wallB);
        }
        yScale[i] = f * f;
    }

    for (int step = 0; step < length; ++step) {
        double rh = (1.5 + std::sin(step * kPi / length) * width) * ctx.width;
        double rv = rh * heightScale;
        rh *= rng.nextDouble() * 0.25 + 0.75;
        rv *= rng.nextDouble() * 0.25 + 0.75;

        const double cp = std::cos(pitch);
        x += std::cos(yaw) * cp;
        y += std::sin(pitch);
        z += std::sin(yaw) * cp;
        pitch *= 0.7;
        pitch += pitchChange * 0.05;
        yaw += yawChange * 0.05;
        pitchChange *= 0.8;
        yawChange *= 0.5;
        // One draw per statement: C++ leaves the order of calls inside one expression open, so
        // writing them in one line let different compilers (MSVC, GCC, Clang) draw them in a
        // different order and carve different caves from the same seed. Order: source order.
        const double pitchA = rng.nextDouble();
        const double pitchB = rng.nextDouble();
        const double pitchC = rng.nextDouble();
        pitchChange += (pitchA - pitchB) * pitchC * 2.0;
        const double yawA = rng.nextDouble();
        const double yawB = rng.nextDouble();
        const double yawC = rng.nextDouble();
        yawChange += (yawA - yawB) * yawC * 4.0;
        if (rng.nextInt(4) == 0) continue;

        const double dx = x - ctx.centerX, dz = z - ctx.centerZ;
        const double remaining = length - step;
        const double reach = width + 2.0 + 16.0;
        if (dx * dx + dz * dz - remaining * remaining > reach * reach) return;

        if (x >= ctx.centerX - 16.0 - rh * 2.0 && z >= ctx.centerZ - 16.0 - rh * 2.0 &&
            x <= ctx.centerX + 16.0 + rh * 2.0 && z <= ctx.centerZ + 16.0 + rh * 2.0) {
            ctx.t.ellipsoid(x, y, z, rh, rv, yScale);
        }
    }
}

void ravinesFrom(CarveContext& ctx, uint32_t seed, int ox, int oz, float frequency) {
    Rng rng(chunkSeed(seed, ox, oz, 0x3B8F21D7ull));
    // Minecraft: about 1 chunk in 50
    if (rng.nextDouble() >= std::min(1.0, frequency / 50.0)) return;
    const double x = ox * 16 + rng.nextInt(16);
    const double y = rng.nextInt(rng.nextInt(40) + 8) + 20;
    const double z = oz * 16 + rng.nextInt(16);
    const double yaw = rng.nextDouble() * kPi * 2.0;
    const double pitch = (rng.nextDouble() - 0.5) * 2.0 / 8.0;
    const double widthA = rng.nextDouble();
    const double widthB = rng.nextDouble();
    const double width = (widthA * 2.0 + widthB) * 2.0;
    ravine(ctx, rng.next(), x, y, z, width, yaw, pitch, 3.0);
}

} // namespace

// ===========================================================================
// CaveGenerator
// ===========================================================================

void CaveGenerator::init() {}
void CaveGenerator::setSeed(uint32_t seed) { g_seed = seed; }
void CaveGenerator::setEnabled(bool enabled) { g_enabled = enabled; }
bool CaveGenerator::isEnabled() { return g_enabled; }
void CaveGenerator::prepare() { classifyBlocks(); }

void CaveGenerator::setSettings(const CaveSettings& s) {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = s;
    g_settings.maxY = std::clamp(g_settings.maxY, kBedrockTop + 1, CHUNK_HEIGHT - 1);
    g_settings.tunnelFrequency = std::max(0.0f, g_settings.tunnelFrequency);
    g_settings.ravineFrequency = std::max(0.0f, g_settings.ravineFrequency);
    g_settings.tunnelWidth = std::clamp(g_settings.tunnelWidth, 0.3f, 4.0f);
    g_settings.cavernSize = std::clamp(g_settings.cavernSize, 0.0f, 4.0f);
}

CaveSettings CaveGenerator::settings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
}

bool CaveGenerator::isCarvedOut(int x, int y, int z) {
    if (!g_enabled || y <= kBedrockTop) return false;
    const CaveSettings s = settings();
    if (y > s.maxY) return false;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
    return (s.noodles && noodleField(fx, fy, fz, g_seed) < 0.0f)
        || (s.caverns && y < kCheeseTopY && cheeseField(fx, fy, fz, g_seed, s.cavernSize) < 0.0f);
}

void CaveGenerator::carveChunk(ChunkData& chunk, int chunkX, int chunkZ) {
    if (!g_enabled) return;
    classifyBlocks();
    const CaveSettings s = settings();
    const uint32_t seed = g_seed;

    // Surface height of each column (highest carvable block; ignores trees)
    int surface[CHUNK_WIDTH][CHUNK_WIDTH];
    int topY = 0;
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            surface[x][z] = -1;
            for (int y = CHUNK_HEIGHT - 1; y > kBedrockTop; --y) {
                if (g_blocks.carvable[chunk.getLocalBlock(x, y, z)]) { surface[x][z] = y; break; }
            }
            topY = std::max(topY, surface[x][z]);
        }
    }
    if (topY <= kBedrockTop) return;   // empty chunk

    Target t(chunk, chunkX, chunkZ, std::min(s.maxY, CHUNK_HEIGHT - 1));

    // 1. Caverns and noodles (noise caves), sampled on a coarse grid that's the same in
    //    every chunk, so they join up seamlessly
    if (s.caverns || s.noodles) {
        const int nx = CHUNK_WIDTH / kStepXZ + 1;
        const int nz = CHUNK_WIDTH / kStepXZ + 1;
        const int ny = std::min(topY, t.maxY) / kStepY + 2;
        std::vector<float> noodle(static_cast<size_t>(nx * ny * nz), 1.0f);
        std::vector<float> cheese(static_cast<size_t>(nx * ny * nz), 1.0f);
        auto idx = [ny, nz](int ix, int iy, int iz) { return (ix * ny + iy) * nz + iz; };
        for (int ix = 0; ix < nx; ++ix) {
            for (int iy = 0; iy < ny; ++iy) {
                for (int iz = 0; iz < nz; ++iz) {
                    const float wx = static_cast<float>(t.baseX + ix * kStepXZ);
                    const float wy = static_cast<float>(iy * kStepY);
                    const float wz = static_cast<float>(t.baseZ + iz * kStepXZ);
                    if (s.noodles) noodle[idx(ix, iy, iz)] = noodleField(wx, wy, wz, seed);
                    if (s.caverns && iy * kStepY <= kCheeseTopY + kStepY) cheese[idx(ix, iy, iz)] = cheeseField(wx, wy, wz, seed, s.cavernSize);
                }
            }
        }
        auto sample = [&](const std::vector<float>& f, int x, int y, int z) {
            const int ix = x / kStepXZ, iy = y / kStepY, iz = z / kStepXZ;
            const float tx = static_cast<float>(x % kStepXZ) / kStepXZ;
            const float ty = static_cast<float>(y % kStepY) / kStepY;
            const float tz = static_cast<float>(z % kStepXZ) / kStepXZ;
            const float c00 = lerp(tx, f[idx(ix, iy,     iz)],     f[idx(ix + 1, iy,     iz)]);
            const float c10 = lerp(tx, f[idx(ix, iy + 1, iz)],     f[idx(ix + 1, iy + 1, iz)]);
            const float c01 = lerp(tx, f[idx(ix, iy,     iz + 1)], f[idx(ix + 1, iy,     iz + 1)]);
            const float c11 = lerp(tx, f[idx(ix, iy + 1, iz + 1)], f[idx(ix + 1, iy + 1, iz + 1)]);
            return lerp(tz, lerp(ty, c00, c10), lerp(ty, c01, c11));
        };
        auto touchesLiquid = [&](int x, int y, int z) {
            if (y + 1 < CHUNK_HEIGHT && g_blocks.liquid[chunk.getLocalBlock(x, y + 1, z)]) return true;
            if (x > 0               && g_blocks.liquid[chunk.getLocalBlock(x - 1, y, z)]) return true;
            if (x < CHUNK_WIDTH - 1 && g_blocks.liquid[chunk.getLocalBlock(x + 1, y, z)]) return true;
            if (z > 0               && g_blocks.liquid[chunk.getLocalBlock(x, y, z - 1)]) return true;
            if (z < CHUNK_WIDTH - 1 && g_blocks.liquid[chunk.getLocalBlock(x, y, z + 1)]) return true;
            return false;
        };
        for (int x = 0; x < CHUNK_WIDTH; ++x) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                const int surf = std::min(surface[x][z], t.maxY);
                if (surf <= kBedrockTop) continue;
                const int noodleTop = surf - kSurfaceCrust;
                const int cheeseTop = std::min(surf - kCheeseMinDepth, kCheeseTopY);
                for (int y = surf; y > kBedrockTop; --y) {
                    bool carve = false;
                    if (s.noodles && y <= noodleTop && sample(noodle, x, y, z) < 0.0f) carve = true;
                    if (!carve && s.caverns && y <= cheeseTop) {
                        const float taper = std::max(0, y - (cheeseTop - 12)) * kCheeseTaper;
                        if (sample(cheese, x, y, z) + taper < 0.0f) carve = true;
                    }
                    if (carve && !touchesLiquid(x, y, z)) t.carve(x, y, z);
                }
            }
        }
    }

    // 2. Tunnels and ravines started in this chunk or any chunk near enough to reach it
    CarveContext ctx{t, t.baseX + 8.0, t.baseZ + 8.0, s.tunnelWidth};
    for (int ox = chunkX - kCarverRange; ox <= chunkX + kCarverRange; ++ox) {
        for (int oz = chunkZ - kCarverRange; oz <= chunkZ + kCarverRange; ++oz) {
            if (s.tunnels && s.tunnelFrequency > 0.0f) tunnelsFrom(ctx, seed, ox, oz, s.tunnelFrequency, t.maxY);
            if (s.ravines && s.ravineFrequency > 0.0f) ravinesFrom(ctx, seed, ox, oz, s.ravineFrequency);
        }
    }

    // 3. Trees would hang in the air where a cave passed under their trunk (logs and leaves are
    //    never carved): put back the ground under each undermined trunk, down to the cave
    //    floor, so the tree stands on a pillar of the original dirt and stone.
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            for (int y = CHUNK_HEIGHT - 1; y > kBedrockTop + 1; --y) {
                if (!g_blocks.anchor[chunk.getLocalBlock(x, y, z)]) continue;
                int yy = y - 1;
                while (yy > kBedrockTop && chunk.getLocalBlock(x, yy, z) == 0 && t.carved[Target::index(x, yy, z)]) {
                    chunk.setBlock(x, yy, z, t.carved[Target::index(x, yy, z)]);
                    t.carved[Target::index(x, yy, z)] = 0;
                    --yy;
                }
                y = yy + 1;                 // carry on below what we filled (the loop's --y)
            }
        }
    }

    // 4. Plants left standing on ground that was carved away would float: remove them (bottom
    //    up, so a tall plant's top goes with its bottom). Only above space we carved, so plants
    //    the mapgen put on other non-solid blocks on purpose are left alone.
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            bool removedBelow = false;
            for (int y = kBedrockTop + 1; y < CHUNK_HEIGHT; ++y) {
                const uint8_t id = chunk.getLocalBlock(x, y, z);
                const bool unsupported = id != 0 && g_blocks.needsSupport[id] &&
                    chunk.getLocalBlock(x, y - 1, z) == 0 &&
                    (t.carved[Target::index(x, y - 1, z)] || removedBelow);
                if (unsupported) {
                    chunk.setBlock(x, y, z, 0);
                    t.carved[Target::index(x, y, z)] = id;  // counts as opened up (for grass below)
                }
                removedBelow = unsupported;
            }
        }
    }

    // 5. Where grass was carved away, the dirt left underneath becomes grass (cave entrances
    //    look like Minecraft's instead of showing a bare dirt hole)
    if (t.carvedGrass && g_blocks.dirtId != 0) {
        for (int x = 0; x < CHUNK_WIDTH; ++x) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                for (int y = CHUNK_HEIGHT - 2; y > kBedrockTop; --y) {
                    if (!t.carved[Target::index(x, y + 1, z)]) continue;
                    if (chunk.getLocalBlock(x, y, z) != g_blocks.dirtId) continue;
                    // only if open to the sky through what was carved (an entrance)
                    bool open = true;
                    for (int yy = y + 1; yy < CHUNK_HEIGHT && open; ++yy) {
                        const uint8_t id = chunk.getLocalBlock(x, yy, z);
                        if (id != 0) open = false;
                    }
                    if (open) chunk.setBlock(x, y, z, g_blocks.grassId);
                    break;
                }
            }
        }
    }

    // 6. Low cave space fills with lava (done last, so the lava we add doesn't stop caves
    //    being carved next to it - only water/lava that was already there should)
    if (g_blocks.lavaId != 0 && s.lavaLevel > kBedrockTop) {
        for (int x = 0; x < CHUNK_WIDTH; ++x) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                for (int y = kBedrockTop + 1; y <= s.lavaLevel && y < CHUNK_HEIGHT; ++y) {
                    if (t.carved[Target::index(x, y, z)]) chunk.setBlock(x, y, z, g_blocks.lavaId);
                }
            }
        }
    }
}
