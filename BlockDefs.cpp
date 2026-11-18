#include "BlockDefs.h"
#include "Block.h"
#include "Types.h"
#include <glm/glm.hpp>
#include <iostream>

namespace {
uint8_t g_NextBlockId = 1;   // 0 is air
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
    sol::optional<std::string> toolType = props["tool_type"];
    if (toolType) block.toolType = *toolType;
    sol::optional<float> toolSpeed = props["tool_speed"];
    if (toolSpeed) block.toolSpeed = *toolSpeed > 0.0f ? *toolSpeed : 1.0f;
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
    if (nameHas(n, {"leaves"})) return 0.3f;
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
    if (existing != 0 || name == "air") {
        std::cerr << "[Block] '" << name << "' is registered more than once - keeping id "
                  << static_cast<int>(existing) << " and updating its settings" << std::endl;
    } else if (g_NextBlockId == 0) {
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
        block.hasGravity    = props.get_or("gravity", false);
        block.pattern       = props.get_or("pattern", std::string("cube"));
        shouldTint          = props.get_or("tint", shouldTint);
        // in_creative = false hides the block from the creative inventory (it still exists in the world)
        block.in_creative   = props.get_or("in_creative", true);
        applyDigProperties(block, props);

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

    BlockRegistry::getInstance().registerBlock(block);
}
