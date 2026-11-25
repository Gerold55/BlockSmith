#include "BlockDefs.h"
#include "Block.h"
#include "Types.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
uint8_t g_NextBlockId = 1;   // 0 is air
}

void resetBlockDefinitions() {
    g_NextBlockId = 1;
    BlockRegistry::getInstance().reset();
}

void applyDigProperties(BlockProperty& block, const sol::table& props) {
    sol::optional<float> hardness = props["hardness"];
    if (hardness) block.hardness = *hardness < 0.0f ? -1.0f : *hardness;
    sol::optional<std::string> tool = props["tool"];
    if (tool) block.tool = *tool;
    sol::optional<bool> needsTool = props["needs_tool"];
    if (needsTool) block.needsTool = *needsTool;
    sol::optional<std::string> drops = props["drops"];
    if (drops) { block.drops = *drops; block.dropsSet = true; }
    sol::optional<std::string> pick = props["pick"];
    if (pick) block.pick = *pick;
    sol::optional<std::string> toolType = props["tool_type"];
    if (toolType) block.toolType = *toolType;
    sol::optional<float> toolSpeed = props["tool_speed"];
    if (toolSpeed) block.toolSpeed = *toolSpeed > 0.0f ? *toolSpeed : 1.0f;
}

namespace {
// { r, g, b } as 0..1 (numbers above 1 are taken as 0..255), or "#rrggbb"
bool readColor(const sol::object& o, float out[3]) {
    if (o.is<std::string>()) {
        std::string hex = o.as<std::string>();
        if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
        if (hex.size() != 6) return false;
        const unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
        out[0] = ((v >> 16) & 255) / 255.0f; out[1] = ((v >> 8) & 255) / 255.0f; out[2] = (v & 255) / 255.0f;
        return true;
    }
    if (!o.is<sol::table>()) return false;
    sol::table t = o.as<sol::table>();
    float c[3];
    for (int i = 0; i < 3; ++i) {
        sol::optional<float> v = t[i + 1];
        if (!v) return false;
        c[i] = *v;
    }
    const bool bytes = c[0] > 1.0f || c[1] > 1.0f || c[2] > 1.0f;
    for (int i = 0; i < 3; ++i) out[i] = std::max(0.0f, std::min(1.0f, bytes ? c[i] / 255.0f : c[i]));
    return true;
}
}

void applyLightProperties(BlockProperty& block, const sol::table& props) {
    sol::object lightObj = props["light"];
    sol::object colorObj = props["light_color"];
    if (lightObj.is<sol::table>() || lightObj.is<double>()) {
        block.lightSet = true;
        if (lightObj.is<sol::table>()) {
            sol::table t = lightObj.as<sol::table>();
            for (int i = 0; i < 3; ++i) {
                block.lightEmit[i] = static_cast<uint8_t>(std::max(0, std::min(15, t.get_or(i + 1, 0))));
            }
        } else if (lightObj.is<double>()) {
            const int level = std::max(0, std::min(15, static_cast<int>(std::lround(lightObj.as<double>()))));
            float c[3] = {1.0f, 1.0f, 1.0f};
            readColor(colorObj, c);
            const float top = std::max(c[0], std::max(c[1], c[2]));
            for (int i = 0; i < 3; ++i) {
                block.lightEmit[i] = static_cast<uint8_t>(top > 0.0f ? std::lround(level * c[i] / top) : 0);
            }
        }
    }
    sol::optional<int> absorb = props["light_absorb"];
    if (absorb) block.lightAbsorb = std::max(0, std::min(15, *absorb));
    float f[3];
    sol::object filterObj = props["light_filter"];
    if (readColor(filterObj, f)) {
        for (int i = 0; i < 3; ++i) block.lightFilter[i] = f[i];
        block.tintView = true;              // stained glass tints the view too, unless told not to
    }
    sol::optional<bool> tintView = props["tint_view"];
    if (tintView) block.tintView = *tintView;
}

void applyTankProperties(BlockProperty& block, const sol::table& props) {
    sol::optional<sol::table> tank = props["tank"];
    if (!tank) return;
    block.tankFluid = tank->get_or("fluid", std::string());
    if (sol::optional<sol::table> box = (*tank)["box"]) {
        float v[6];
        bool ok = true;
        for (int i = 0; i < 6; ++i) {
            sol::optional<float> n = (*box)[i + 1];
            if (!n || !std::isfinite(*n)) { ok = false; break; }
            v[i] = std::max(0.0f, std::min(16.0f, *n)) / 16.0f;
        }
        if (ok && v[0] < v[3] && v[1] < v[4] && v[2] < v[5]) for (int i = 0; i < 6; ++i) block.tankBox[i] = v[i];
    }
    sol::optional<float> inset = (*tank)["inset"];
    if (inset && std::isfinite(*inset)) block.tankInset = std::max(0.0f, std::min(7.0f, *inset)) / 16.0f;
    block.tankStream = tank->get_or("stream", false);
    sol::optional<float> floorPx = (*tank)["floor"];
    if (floorPx && std::isfinite(*floorPx)) block.tankFloor = std::max(0.0f, std::min(15.0f, *floorPx)) / 16.0f;
    block.tankShell = tank->get_or("shell", true);
    block.tankFluid2 = tank->get_or("fluid2", std::string());
}

void applySoundProperties(BlockProperty& block, const sol::table& props) {
    sol::optional<sol::table> sounds = props["sounds"];
    if (!sounds) return;
    auto read = [&sounds](const char* key, BlockProperty::SoundSpec& out) {
        sol::object o = (*sounds)[key];
        if (o.is<std::string>()) { out = BlockProperty::SoundSpec{o.as<std::string>(), 1.0f, 1.0f}; return; }
        if (o.is<sol::table>()) {
            sol::table t = o.as<sol::table>();
            out.name = t.get_or("name", std::string());
            out.gain = t.get_or("gain", 1.0f);
            out.pitch = t.get_or("pitch", 1.0f);
        }
    };
    read("footstep", block.soundFootstep);
    read("dig", block.soundDig);
    read("dug", block.soundDug);
    read("place", block.soundPlace);
}

namespace {
bool nameHas(const std::string& name, std::initializer_list<const char*> parts) {
    for (const char* p : parts) if (name.find(p) != std::string::npos) return true;
    return false;
}
}

// Guesses for games that never set hardness, roughly Minecraft's times by hand
float blockHardness(const BlockProperty& b) {
    if (b.hardness >= 0.0f || b.hardness == -1.0f) return b.hardness;
    const std::string& n = b.name;
    if (nameHas(n, {"bedrock", "barrier"})) return -1.0f;
    if (b.pattern == "plantlike" || nameHas(n, {"flower", "short_grass", "sapling", "mushroom", "torch"})) return 0.0f;
    if (nameHas(n, {"leaves"})) return 0.0f;   // leaves break instantly
    if (nameHas(n, {"glass", "ice"})) return 0.45f;
    if (nameHas(n, {"obsidian"})) return 50.0f;
    if (nameHas(n, {"ore"})) return 3.0f;
    if (nameHas(n, {"stone", "cobble", "brick", "andesite", "granite", "diorite", "deepslate"})) return 1.5f;
    if (nameHas(n, {"log", "wood", "planks", "plank"})) return 2.0f;
    if (nameHas(n, {"sand", "gravel", "dirt", "grass", "clay", "snow", "farmland"})) return 0.6f;
    return 1.0f;
}

std::string blockTool(const BlockProperty& b) {
    if (!b.tool.empty()) return b.tool;
    if (b.hardness != -2.0f) return "";   // the game set its own values
    const std::string& n = b.name;
    if (nameHas(n, {"stone", "cobble", "brick", "ore", "obsidian", "andesite", "granite", "diorite", "deepslate"})) return "pickaxe";
    if (nameHas(n, {"log", "wood", "planks", "plank"})) return "axe";
    if (nameHas(n, {"sand", "gravel", "dirt", "grass", "clay", "snow", "farmland"})) return "shovel";
    return "";
}

bool blockNeedsTool(const BlockProperty& b) {
    if (b.needsTool || b.hardness != -2.0f) return b.needsTool;
    return blockTool(b) == "pickaxe";   // like Minecraft: stone and ores need a pickaxe
}

// Moved here unchanged from main.cpp (registerBlockFromLua), except that textures are
// loaded through the function passed in.
void registerBlockDefinition(const std::string& name, float r, float g, float b,
                             sol::object texObj, sol::object overlayObj,
                             sol::optional<sol::table> propsTable, const TextureLoader& loadTextureToLayer) {
    if (r > 1.0f || g > 1.0f || b > 1.0f) {
        r = glm::clamp(r / 255.0f, 0.0f, 1.0f);
        g = glm::clamp(g / 255.0f, 0.0f, 1.0f);
        b = glm::clamp(b / 255.0f, 0.0f, 1.0f);
    }

    // Registering a name twice keeps its first id (a second id would leave blocks in the
    // world, and anything that looked the id up earlier, pointing at the wrong block).
    // Ids are one byte: past 255 blocks a new one would overwrite block 0, 1, 2...
    const uint8_t existing = BlockRegistry::getInstance().getBlockId(name);
    if (existing != 0 && name != "air") {   // (air is the engine's own block 0: a game setting it up is normal)
        std::cerr << "[Block] '" << name << "' is registered more than once - keeping id "
                  << static_cast<int>(existing) << " and updating its settings" << std::endl;
    } else if (name != "air" && g_NextBlockId == 0) {
        std::cerr << "[Block] Too many blocks (255 max) - '" << name << "' was not registered" << std::endl;
        return;
    }
    BlockProperty block;
    block.id = (existing != 0 || name == "air") ? existing : g_NextBlockId++;
    block.name = name;
    block.defaultTint = glm::vec3(r, g, b);

    bool shouldTint = (name == "grass" || name == "short_grass");

    if (propsTable.has_value()) {
        sol::table props = propsTable.value();
        block.isTransparent = props.get_or("transparent", false);
        block.isCutout      = props.get_or("cutout", false);
        block.isWalkable    = props.get_or("walkable", (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red"));
        block.isSelectable  = props.get_or("selectable", (name != "water" && name != "flowing_water" && name != "air" && name != "short_grass" && name != "flower_red"));
        {
            const std::string pat = props.get_or("pattern", std::string("cube"));
            block.holdsText = props.get_or("text", pat == "sign" || pat == "wall_sign");
        }
        block.hasGravity    = props.get_or("gravity", false);
        block.pattern       = props.get_or("pattern", std::string("cube"));
        // mirror = true (MirrorShape.h): a thin panel on the wall whose front reflects the room;
        // the block's picture is its frame (see-through pixels are the glass)
        {
            const sol::object m = props["mirror"];
            if ((m.is<bool>() && m.as<bool>()) || m.is<sol::table>()) block.pattern = "mirror";
            if (m.is<sol::table>()) {
                const sol::table t = m.as<sol::table>();
                float c[3];
                if (readColor(t["tint"], c)) block.mirrorTint = glm::vec3(c[0], c[1], c[2]);
                if (readColor(t["glass"], c)) block.mirrorGlass = glm::vec3(c[0], c[1], c[2]);
                const sol::optional<float> clarity = t["clarity"];
                if (clarity) block.mirrorClarity = std::max(0.0f, std::min(1.0f, *clarity));
            }
        }
        if (block.pattern == "mirror") {
            block.isTransparent = true;
            block.isWalkable = props.get_or("walkable", false);
        }
        {
            // rotation = "axis" (BlockShape.h): a log, lying along the face it's put against
            const std::string rot = props.get_or("rotation", std::string());
            block.rotation = rot == "axis" ? 3 : 0;
            block.waterloggable = props.get_or("waterloggable", true);
            block.stairsConnect = props.get_or("stairs_connect", true);
            if (!rot.empty() && block.rotation == 0)
                std::cerr << "[Block] " << name << ": rotation \"" << rot << "\" isn't known (only \"axis\", for logs; "
                          << "slabs and stairs turn by their pattern)" << std::endl;
        }
        shouldTint          = props.get_or("tint", shouldTint);
        // in_creative = false hides the block from the creative inventory (it still exists in the world)
        block.in_creative   = props.get_or("in_creative", true);
        // description = "Oak Log": the name players see (pointing at it, in the inventory)
        block.description   = props.get_or("description", std::string());
        applyDigProperties(block, props);
        applySoundProperties(block, props);
        applyLightProperties(block, props);
        applyTankProperties(block, props);
        // Tanks: see-through (the liquid shows through the glass), drawn by their own shape
        if (block.pattern == "tank") block.isTransparent = true;
        // Doors (DoorShape.h): a thin panel, so light and the blocks beside them show through,
        // and see-through pixels in the picture (windows) are cut out
        if (block.pattern == "door") {
            block.isTransparent = true;
            block.isCutout = true;
        }
        sol::optional<std::string> biome = props["biome_tint"];
        if (biome) block.biomeTint = (*biome == "grass") ? 0 : (*biome == "foliage") ? 1 : -2;   // -2 = "none" (chosen)

        // inventory_image = "stick.png" shows a flat picture in the inventory/hotbar instead of a cube
        sol::optional<std::string> invImage = props["inventory_image"];
        if (invImage.has_value() && !invImage.value().empty()) {
            block.inventoryImage = invImage.value();
            block.inventoryLayer = loadTextureToLayer(block.inventoryImage);
        }
    } else {
        block.isTransparent = (name == "water" || name == "flowing_water" || name == "leaves" || name == "birch_leaves" || name == "short_grass" || name == "flower_red");
        block.isCutout      = (name.find("leaves") != std::string::npos);
        block.isWalkable    = (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red");
        block.isSelectable  = (name != "water" && name != "flowing_water" && name != "air" && name != "short_grass" && name != "flower_red");
        block.hasGravity    = (name == "sand" || name == "gravel");
        block.pattern       = (name == "short_grass" || name == "flower_red") ? "plantlike" : "cube";
    }

    if (texObj.is<std::string>()) {
        int layer = loadTextureToLayer(texObj.as<std::string>());
        for (int f = 0; f < 6; ++f) block.faces[f].baseLayer = layer;
        if (shouldTint) {
            if (name == "grass") block.faces[2].tintBase = true;
            else { for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true; }
        }
    } else if (texObj.is<sol::table>()) {
        sol::table t = texObj.as<sol::table>();
        sol::optional<std::string> top = t["top"];
        sol::optional<std::string> bottom = t["bottom"];
        sol::optional<std::string> sides = t["sides"];

        int topL = top.has_value() ? loadTextureToLayer(top.value()) : -1;
        int botL = bottom.has_value() ? loadTextureToLayer(bottom.value()) : -1;
        int sideL = sides.has_value() ? loadTextureToLayer(sides.value()) : -1;

        block.faces[2].baseLayer = topL;
        block.faces[3].baseLayer = botL;
        block.faces[0].baseLayer = block.faces[1].baseLayer = block.faces[4].baseLayer = block.faces[5].baseLayer = sideL;

        if (shouldTint) {
            if (name == "grass") block.faces[2].tintBase = true;
            else { for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true; }
        }
    }

    if (overlayObj.is<std::string>()) {
        int overlayLayer = loadTextureToLayer(overlayObj.as<std::string>());
        for (int f = 0; f < 6; ++f) {
            if (f != 2 && f != 3) {
                block.faces[f].overlayLayer = overlayLayer;
                block.faces[f].tintOverlay = true;
            }
        }
    } else if (overlayObj.is<sol::table>()) {
        sol::table ot = overlayObj.as<sol::table>();
        sol::optional<std::string> sideOverlay = ot["sides"];
        if (sideOverlay.has_value()) {
            int sideL = loadTextureToLayer(sideOverlay.value());
            for (int f = 0; f < 6; ++f) {
                if (f != 2 && f != 3) {
                    block.faces[f].overlayLayer = sideL;
                    block.faces[f].tintOverlay = true;
                }
            }
        }
    }

    // Biome colour when the game didn't say: tinted grass blocks follow the grass colours,
    // tinted leaves the foliage colours (like Minecraft)
    if (block.biomeTint == -1 && shouldTint) {
        if (name.find("grass") != std::string::npos) block.biomeTint = 0;
        else if (name.find("leaves") != std::string::npos) block.biomeTint = 1;
    }
    if (block.biomeTint == -2) block.biomeTint = -1;
    BlockRegistry::getInstance().registerBlock(block);
}
