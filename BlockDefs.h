#pragma once
// engine.register_block(...) - shared by the game and the headless server, so both agree
// on every block's properties (solid, transparent, walkable...). Movement prediction
// depends on that: if the server thought a block was solid and the game didn't, players
// would keep getting pulled back.
//
// Textures are loaded through `loadTexture` (the game uploads them to the GPU; the server
// passes a function that does nothing and returns -1).

#include <functional>
#include <string>
#include <sol/sol.hpp>

using TextureLoader = std::function<int(const std::string& path)>;   // -> texture layer, or -1

struct BlockProperty;

// Survival digging fields from a Lua table (hardness, tool, needs_tool, drops, tool_type,
// tool_speed). Used by register_block's properties and engine.override_block.
void applyDigProperties(BlockProperty& block, const sol::table& props);

// Seconds to dig a block by hand: its hardness, or a sensible guess from its name for
// blocks registered without one (stone 1.5, logs 2, dirt 0.5, leaves 0.2, plants 0...).
// -1 = can't be dug.
float blockHardness(const BlockProperty& block);
// sounds = { footstep = "...", dig = {name=..., gain=..., pitch=...}, dug = ..., place = ... }
void applySoundProperties(BlockProperty& block, const sol::table& props);
// Light: light = 0..15 (or { r, g, b } levels), light_color = { r, g, b } (0..255 or 0..1) or
// "#rrggbb", light_absorb = extra light lost passing through, light_filter = { r, g, b } colour
// let through (stained glass). See Light.h.
void applyLightProperties(BlockProperty& block, const sol::table& props);
// The tool type a block wants when it didn't say ("pickaxe" for stone, "axe" for wood...)
std::string blockTool(const BlockProperty& block);
bool blockNeedsTool(const BlockProperty& block);

void registerBlockDefinition(const std::string& name, float r, float g, float b,
                             sol::object texObj, sol::object overlayObj,
                             sol::optional<sol::table> propsTable, const TextureLoader& loadTexture);
