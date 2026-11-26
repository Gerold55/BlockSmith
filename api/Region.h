#pragma once
// A region: a box of blocks copied out of the world (their ids, metadata and block text), to
// write back somewhere else, turn, mirror, keep for undo, or save as text (a schematic).
// No graphics and no world: World reads and writes them, StructureAPI.h gives them to Lua.
//
// Cells are kept x-major: index = (x * size.y + y) * size.z + z, with 0..size-1 on each axis.
// Block ids are this game's ids (they can differ between games and servers): text made by
// toText() names the blocks instead, so it can be loaded anywhere the same blocks exist.

#include "ChunkData.h"     // BlockText
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Region {
    glm::ivec3 size{ 0 };
    std::vector<uint8_t> ids, metas;
    std::map<uint32_t, BlockText> texts;   // by cell index

    // The most cells a region may have (a 160 x 160 x 160 box): enough for big builds, small
    // enough that nothing runs out of memory (2 bytes a cell: about 8 MB)
    static constexpr size_t kMaxCells = 4096000;
    static constexpr int kMaxSizeX = 4096;
    static constexpr int kMaxSizeY = 256;
    static constexpr int kMaxSizeZ = 4096;

    static bool validSize(const glm::ivec3& s) {
        if (s.x <= 0 || s.y <= 0 || s.z <= 0 ||
            s.x > kMaxSizeX || s.y > kMaxSizeY || s.z > kMaxSizeZ) return false;
        const size_t xy = static_cast<size_t>(s.x) * static_cast<size_t>(s.y);
        return xy <= kMaxCells && static_cast<size_t>(s.z) <= kMaxCells / xy;
    }

    size_t cells() const { return static_cast<size_t>(size.x) * size.y * size.z; }
    bool valid() const { return validSize(size) && ids.size() == cells() && metas.size() == cells(); }
    bool empty() const { return cells() == 0; }
    bool inside(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < size.x && y < size.y && z < size.z; }
    size_t index(int x, int y, int z) const { return (static_cast<size_t>(x) * size.y + y) * size.z + z; }
    void resize(const glm::ivec3& s) {
        if (!validSize(s)) throw std::invalid_argument("region size is invalid or too big");
        size = s;
        ids.assign(cells(), 0);
        metas.assign(cells(), 0);
        texts.clear();
    }

    // Turned clockwise (seen from above) by quarter turns (any whole number; negative turns go the
    // other way), around the vertical axis. Blocks that face somewhere turn with it: stairs,
    // doors, signs, mirrors, logs lying down.
    Region rotated(int quarterTurns) const;
    // Mirrored along an axis: 'x' (east and west swap), 'z' (north and south), 'y' (upside down:
    // slabs and stairs turn over too)
    Region flipped(char axis) const;

    // As JSON text (a schematic: { "format": "blocksmith-region", "version": 1, "size": [x, y, z],
    // "palette": [block names], "cells": [[palette, meta, count], ...] runs in x, y, z order,
    // "texts": [...] }): data only. fromText reads that (or the older "BSREGION 1" text) and
    // refuses (false + why) anything malformed or too big; blocks this game doesn't have become
    // air, and are listed in `missing`.
    std::string toText() const;
    static bool fromText(const std::string& text, Region& out, std::string& why, std::vector<std::string>* missing = nullptr);
};
