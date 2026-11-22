#pragma once
#include <cstdint>

class ChunkData;

// Minecraft-style caves, carved into chunks after terrain generation. The game and the
// dedicated server both use this, with the world seed, so they always make the same caves.
//
//   - Tunnels  : Minecraft's classic winding caves. Long tunnels that wander, change width,
//                split into branches and open into rooms; some break through the surface as
//                cave entrances. Flat-ish floors, like Minecraft's.
//   - Ravines  : rare deep, narrow canyons with jagged walls.
//   - Caverns  : big open "cheese" caves deep down, with natural stone pillars.
//   - Noodles  : thin twisty tunnels in some regions, joining everything up.
//   - Lava     : open cave space at the bottom of the world fills with lava (if a "lava" block exists).
// Caves never cut through water (no holes in the sea floor), trees, or the bedrock layers.
// When a tunnel removes grass, the dirt below becomes grass, so entrances look natural.
struct CaveSettings {
    bool  tunnels = true;          // classic winding caves
    float tunnelFrequency = 1.0f;  // 2 = twice as many, 0.5 = half
    float tunnelWidth = 1.0f;      // 1.5 = 50% wider
    bool  ravines = true;
    float ravineFrequency = 1.0f;
    bool  caverns = true;          // big deep caves with pillars
    float cavernSize = 1.0f;       // bigger = more and larger caverns
    bool  noodles = true;          // thin tunnels
    int   lavaLevel = 10;          // carved space at or below this y becomes lava (-1 = none)
    int   maxY = 200;              // nothing is carved above this y
};

class CaveGenerator {
public:
    static void init();                       // kept for compatibility (does nothing)
    static void setSeed(uint32_t seed);
    static void setEnabled(bool enabled);
    static bool isEnabled();
    static void setSettings(const CaveSettings& settings);
    static CaveSettings settings();
    // Looks up the block types caves use. carveChunk does this itself the first time, but
    // call it once before generating on several threads so they don't all do it at once.
    static void prepare();
    // The blocks changed completely (the mods were loaded again): forget the block types and the
    // settings mods gave
    static void reset();

    // Carves caves into a freshly generated chunk.
    static void carveChunk(ChunkData& chunk, int chunkX, int chunkZ);

    // Per-block query for the noise caves only (slower; kept for old code).
    static bool isCarvedOut(int x, int y, int z);
};
