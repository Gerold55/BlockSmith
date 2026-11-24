#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Use the best graphics card. On PCs and laptops with both a built-in (integrated) GPU and
// a dedicated NVIDIA/AMD one, Windows starts programs on the built-in one unless the
// program asks otherwise. The NVIDIA and AMD drivers look for these two values in the
// game's .exe and switch to the dedicated GPU when they find them. (They must be in the
// .exe itself, not a DLL - which is why they're here in main.cpp.)
#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <exception>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include "ModOrder.h"
#include "SafeReplace.h"
#include "ClientMods.h"
#include "Auth.h"
#include <cstdlib>
#include <iomanip>
#include <random>

#include "Types.h"
#include "Block.h"
#include "ChunkData.h"
#include "WorldRenderer.h"
#include "DimensionsLua.h"
#include "SignShape.h"
#include "SignText.h"
#include "BlockTextLua.h"
#include "BlockStateLua.h"
#include "VoxelVolume.h"
#include "Spotlights.h"
#include "ArtNet.h"
#include "GpuCheck.h"
#include "Bloom.h"
#include "SunShafts.h"
#include "SceneCopy.h"
#include "DimensionWorlds.h"
#include "Net.h"
#include "BlockDefs.h"
#include "PlayerMovement.h"
#include "PlayerAvatars.h"
#include "World.h"
#include "Player.h"
#include "Raycast.h"
#include "DoorShape.h"
#include "FirstPerson.h"
#include "Hotbar.h"
#include "FontRenderer.h"
#include "ChatSystem.h"
#include "Lighting.h"
#include "Skybox.h"
#include "Light.h"
#include "ChunkMesh.h"
#include "Model.h"
#include "Clouds.h"
#include "Fog.h"
#include "MainMenu.h"
#include "Inventory.h" // Include your new dedicated Inventory module
#include "CaveGenerator.h"
#include "CaveLua.h"
#include "ChunkGenerator.h"
#include "Fluids.h"
#include "WeatherEffects.h"
#include "Audio.h"
#include "Firewall.h"
#include "LuaSandbox.h"
#include "UiDraw.h"
#include "ServerLuaApi.h"
#include "EntityLua.h"
#include "BlockModifiers.h"
#include "RemoteLua.h"
#include "UpdateCheck.h"
#include "Diagnostics.h"
#include "WebLink.h"
#include "Settings.h"
#include "ParticleSystem.h"

namespace fs = std::filesystem;

namespace {
    constexpr Dimensions2D baseResolution{1920, 1080};

    void scaleCursorToFramebuffer(GLFWwindow* window, double& x, double& y,
                                  int framebufferWidth, int framebufferHeight) {
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        if (windowWidth == 0 || windowHeight == 0) return;

        x *= static_cast<double>(framebufferWidth) / windowWidth;
        y *= static_cast<double>(framebufferHeight) / windowHeight;
    }
}

// Underwater look. Clear, Minecraft-like water by default: you can see a good distance,
// with a light blue-green fog that only gets dark deep down. Games change these with
// underwater.set_shallow_color / set_deep_color / set_visibility / set_tint_strength.
struct UnderwaterSettings {
    glm::vec3 shallowColor      = glm::vec3(0.22f, 0.45f, 0.62f);   // soft blue-green, not deep blue
    glm::vec3 deepColor         = glm::vec3(0.05f, 0.14f, 0.28f);
    float       shallowVisibility = 48.0f;   // blocks until things fade fully into the fog
    float       deepVisibility    = 20.0f;
    float       fullDepth         = 48.0f;   // depth where it's fully "deep"
    float       fadeInSeconds     = 0.6f;
    float       fadeOutSeconds    = 0.4f;
    // How much everything is coloured like the water, however close: this is what makes a small
    // pool look underwater too (the fog alone only shows on things far away, as in the ocean)
    float       tintStrength      = 0.42f;
    float       fogStartFraction  = 0.04f;   // fog starts this far (of the visibility) away
    // Colours set from Lua (underwater.set_shallow_color / set_deep_color). If not, they come
    // from the water block itself (its texture and tint), so being underwater looks like the water.
    bool        colorsFromLua     = false;
};

std::string g_CurrentModPath = "";
std::string g_ActiveGameFolder = "craft_and_ruin";
std::string g_ActiveWorldName = "world_alpha";
std::string g_LoadedGameFolder;   // the game whose mods were loaded at startup (worlds are tied to it)
std::map<std::string, std::string> g_ModFileStatus;   // mod .lua file (canonical path) -> "" if it ran OK, else the error
Hotbar* g_Hotbar = nullptr;
FontRenderer* g_FontRenderer = nullptr;
ChatSystem* g_ChatSystem = nullptr;
LightingSystem* g_LightingSystem = nullptr;
// Light traced through the blocks around the camera (VoxelVolume.h) and mods' spotlights
// (Spotlights.h: lights.add_spot...)
VoxelVolume g_Voxels;
Spotlights g_Spotlights;
// What the graphics card can take (GpuCheck.h, at startup) and the lighting the player chose:
// quality 0 off (no traced shadows), 1 low, 2 high; bloom on/off (Bloom.h)
GpuCaps g_GpuCaps;
// True only while the Settings screen handles the player's own mouse click: lighting and bloom
// are the player's choice, never a mod's or a server's (setVideoSetting refuses them otherwise)
bool g_PlayerSettingsClick = false;
int g_LightQuality = 0;
bool g_BloomOn = false;
bool g_FilmicOn = true;          // filmic exposure for Lighting Low/High (Settings; the player's choice)
SceneCopy g_SceneCopy;           // the frame before water is drawn: water reflections (High)
float g_BloomStrength = 0.6f, g_BloomThreshold = 0.75f;   // (a game can tune them: sky.set_bloom)
Bloom g_Bloom;
// Sun shafts in the air (SunShafts.h; lighting quality High), and how strong a game wants them
SunShafts g_SunShafts;
float g_SunShaftStrength = 0.5f;
Skybox* g_Skybox = nullptr;
Clouds* g_Clouds = nullptr;
WeatherEffects g_Weather;   // rain/snow/sky tint/flashes - decided in Lua (mods/weather)
AudioSystem g_Audio;        // OpenAL: block sounds, Lua's sound.*, music

// A block's sound (footstep, dig, dug, place - see BlockProperty), in the world at `pos`.
// Falls back like Luanti: no dig sound -> footstep; no dug sound -> dig, then footstep.
void playBlockSound(uint8_t id, const char* kind, const glm::vec3& pos, float gainScale = 1.0f) {
    if (id == 0 || !g_Audio.available()) return;
    const BlockProperty& b = BlockRegistry::getInstance().getBlock(id);
    const BlockProperty::SoundSpec* spec = nullptr;
    const std::string k = kind;
    auto pick = [&spec](const BlockProperty::SoundSpec& s) { if (!spec && !s.name.empty()) spec = &s; };
    if (k == "footstep") pick(b.soundFootstep);
    else if (k == "dig") { pick(b.soundDig); pick(b.soundFootstep); }
    else if (k == "dug") { pick(b.soundDug); pick(b.soundDig); pick(b.soundFootstep); }
    else if (k == "place") { pick(b.soundPlace); pick(b.soundFootstep); }
    if (!spec) return;
    SoundParams p;
    p.positional = true;
    p.position = pos;
    p.gain = spec->gain * gainScale;
    p.pitch = spec->pitch * (k == "dig" ? 0.9f + 0.2f * (std::rand() / static_cast<float>(RAND_MAX)) : 1.0f);
    p.maxDistance = k == "footstep" ? 16.0f : 24.0f;
    g_Audio.play(spec->name, p);
}
glm::vec3 g_PlayerPosition(0.0f);   // feet, updated every frame (engine.get_player_position)
float g_FootstepDistance = 1.3f;    // blocks walked between footstep sounds (engine.set_footstep_distance)

// Survival / creative (engine.set_creative). Creative: blocks break at once and placing is
// free. Survival: hold to dig (block hardness and tools), placing uses up the stack.
bool g_Creative = true;
struct DigState {
    bool active = false;
    glm::ivec3 pos{0};
    float progress = 0.0f;   // 0..1
    float seconds = 1.0f;    // how long this block takes with the held item
    float cooldown = 0.0f;   // short pause after each block, like Minecraft
    float particleTimer = 0.0f;
} g_Dig;
// Crack picture for survival digging (engine.set_dig_crack_texture): a vertical strip of
// `stages` square pictures, first stage at the top. 0 = no picture: a darkening box instead.
GLuint g_CrackTexture = 0;
int g_CrackStages = 10;
// A mod's own inventory screen (survival): open/closed (toggle_main_inventory decides)
bool g_LuaInventoryOpen = false;
sol::state* g_Lua = nullptr;   // the game's Lua, for engine code that needs to call a hook

// Mod storage (engine.get_storage / set_storage): text values saved with the world,
// in saves/<world>/mod_storage.txt, so mods remember health, hunger, game mode...
std::unordered_map<std::string, std::string> g_ModStorage;
bool g_ModStorageDirty = false;
uint8_t g_LastMoveKeys = 0;   // movement keys held this frame (after engine.set_player_controls)

// Puts items in the hotbar: onto a stack of the same item, else the first empty slot.
void hotbarScreenRect(const Hotbar& hb, float W, float H, float& x, float& y, float& w, float& h);   // Hotbar.cpp
void hotbarSlotRect(const Hotbar& hb, int i, float W, float H, float& x, float& y, float& size);       // Hotbar.cpp

// The player's client mods (clientmods/ folder, see ClientMods.h). Lives next to the game's Lua
// state in main(); null before it's made and after it's gone.
ClientMods* g_ClientMods = nullptr;
bool g_FlyAllowed = true;   // creative players may fly (engine.set_fly_allowed)

// Pick block: puts the block at `pos` in the player's hand - or the item its `pick` property
// names (crops pick their seeds). A mod's on_pick_block(x, y, z, item, meta, block) goes first
// (return true = handled). Then, in creative only: select the item if it's in the hotbar;
// otherwise put it in the selected slot if that's empty, else in the next empty slot (and select
// that), else replace what's in the selected slot - Minecraft's rules.
void pickBlock(sol::state& lua, World& world, const glm::ivec3& pos) {
    if (!g_Hotbar) return;
    const uint8_t blockId = world.getBlockAt(pos);
    if (blockId == 0) return;
    auto& reg = BlockRegistry::getInstance();
    const BlockProperty& block = reg.getBlock(blockId);
    const std::string item = block.pick.empty() ? block.name : block.pick;
    const uint8_t id = reg.getBlockId(item);
    if (id == 0) return;                      // `pick` names something that doesn't exist

    sol::protected_function hook = lua["on_pick_block"];
    if (hook.valid()) {
        auto r = hook(pos.x, pos.y, pos.z, item, static_cast<int>(world.getMetadataAt(pos)), block.name);
        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_pick_block: " << err.what() << std::endl; }
        else if (r.get_type() == sol::type::boolean && r.get<bool>()) return;
    }
    if (!g_Creative) return;

    const int n = g_Hotbar->slotCount;
    for (int i = 0; i < n; ++i) {
        if (g_Hotbar->getSlot(i).blockId == id) { g_Hotbar->activeSlot = i; return; }
    }
    int slot = g_Hotbar->activeSlot;
    if (g_Hotbar->getSlot(slot).blockId != 0) {
        for (int k = 1; k < n; ++k) {
            const int i = (g_Hotbar->activeSlot + k) % n;
            if (g_Hotbar->getSlot(i).blockId == 0) { slot = i; break; }
        }
    }
    g_Hotbar->setSlot(slot, id, 64);
    g_Hotbar->activeSlot = slot;
}

bool giveHotbarItem(const std::string& name, int count) {
    if (!g_Hotbar) return false;
    const uint8_t id = BlockRegistry::getInstance().getBlockId(name);
    if (id == 0) return false;
    const int n = std::max(1, count);
    for (int i = 0; i < g_Hotbar->slotCount; ++i) {
        HotbarSlot s = g_Hotbar->getSlot(i);
        if (s.blockId == id && s.count + n <= 64) { g_Hotbar->setSlot(i, id, s.count + n); return true; }
    }
    for (int i = 0; i < g_Hotbar->slotCount; ++i) {
        if (g_Hotbar->getSlot(i).blockId == 0) { g_Hotbar->setSlot(i, id, n); return true; }
    }
    // Hotbar full: a mod's inventory may take it - on_hotbar_full(name, count) -> true if stored
    if (g_Lua) {
        sol::protected_function hook = (*g_Lua)["on_hotbar_full"];
        if (hook.valid()) {
            auto r = hook(name, n);
            if (r.valid() && r.get_type() == sol::type::boolean && r.get<bool>()) return true;
        }
    }
    return false;
}

std::string escapeStorage(const std::string& v) {
    std::string o;
    for (char ch : v) {
        if (ch == '\\') o += "\\\\";
        else if (ch == '\n') o += "\\n";
        else if (ch == '\t') o += "\\t";
        else o += ch;
    }
    return o;
}
std::string unescapeStorage(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const char n = v[++i];
            o += n == 'n' ? '\n' : n == 't' ? '\t' : n;
        } else {
            o += v[i];
        }
    }
    return o;
}   // rain/snow/sky tint/flashes - decided in Lua (mods/weather)
// Shared values (engine.set_shared): decided by whoever runs the world (singleplayer, the
// host, or the server) and seen by every player. Here: this game's own copy.
std::unordered_map<std::string, std::string> g_SharedValues;
std::vector<std::string> g_SharedChanged;   // keys to report to on_shared_changed
// Mod messages that stay in this game (singleplayer, or the host's own): for its own
// on_server_message (from engine.send_to_player / send_to_all), and for its own
// on_mod_message (engine.send_to_server while not on someone else's server). Delivered
// once a frame, so a mod's handler never runs inside the call that sent it.
struct LocalModMessage { std::string channel, data; };
std::vector<LocalModMessage> g_ModToOwnGame, g_ModToOwnServer;
World* g_WorldInstance = nullptr;
// Dimensions (Dimensions.h). The world on screen (g_WorldInstance) is the dimension this game's
// player is in; when hosting, other dimensions with guests in them run in the background here.
DimensionWorlds g_DimWorlds;
std::string g_WorldDir;           // the open world's folder (its dimensions are inside it)
bool g_DimensionSwitched = false; // the main loop throws the old dimension's meshes away
// The world mods' world functions act on: the dimension the code runs in (dims::current() - the
// player, block or entity it's about), which may be a background one when hosting
World* apiWorld();
float g_PlaceRepeatSeconds = 0.2f;   // holding right-click places again this often (0 = off)
FirstPersonView g_FirstPerson;        // the hand / held block in first person (FirstPerson.h)

// ---- Block light (Light.h) settings, from Lua (sky.set_min_light etc.) ----
struct LightSettings {
    float minLight = 0.02f;          // the darkest anything gets (a sealed cave)
    float brightness = 0.35f;        // the game's: 0 = moody (Minecraft's default), 1 = bright (sky.set_brightness)
    float playerBrightness = 0.5f;   // the player's slider (Settings > Video): 0.5 = the game's choice
    // What's drawn. Below the middle, the player's slider takes the game's brightness down
    // toward moody; above it, the game's look stays and a gamma lift is added on top of the
    // final light (u_Gamma), which also brightens caves and nights - like Minecraft's slider.
    float effectiveBrightness() const {
        if (playerBrightness <= 0.5f) return brightness * (playerBrightness / 0.5f);
        return brightness;
    }
    float gammaLift() const { return playerBrightness > 0.5f ? (playerBrightness - 0.5f) / 0.5f : 0.0f; }
    float blockLightScale = 1.0f;    // strength of torches and other light sources
    bool heldLight = true;           // holding a light source lights up around you
};
LightSettings g_LightSettings;

// The light of the item in the player's hand (a torch lights the cave as you walk), as one
// of LightingSystem's moving point lights
struct HeldLight {
    int id = -1;
    void update(const World& w);
};
HeldLight g_HeldLight;
WorldRenderer* g_WorldRenderer = nullptr;   // client-side drawing of g_WorldInstance

// Multiplayer: the server runs inside this game while hosting; the client while joined.
GameServer g_Server;
GameClient g_Client;

// ---- Entities (mobs, dropped items): Entities.h / EntityLua.h ----
// Run here in singleplayer and when hosting; joined to a server, the server runs them and this
// game only draws what it's told (g_Client.entities()).
EntityManager g_Entities;
EntityLua g_EntityLua;
BlockModifiers g_BlockMods;                      // ABMs and LBMs (BlockModifiers.h)
struct EntityVisual {                            // a registered type's looks (loaded once)
    std::shared_ptr<AnimatedModel> model;
    std::shared_ptr<AnimationSet> anims;
    float scale = 1.0f;
};
std::map<std::string, EntityVisual> g_EntityVisuals;
struct EntityLook {                              // per entity, this game only: animation clock, red flash
    uint8_t hurtCount = 0;
    double hurtUntil = 0.0;
    glm::vec3 pos{0.0f};                         // where it was last seen, and its size (for its "poof")
    float width = 0.6f, height = 1.0f;
    bool item = false;
    std::string anim;
    double animStart = 0.0;
    bool seen = false;
};
std::unordered_map<uint32_t, EntityLook> g_EntityLooks;
class ParticleSystem;
ParticleSystem* g_Particles = nullptr;   // the game's particles (made with the GL context)
bool entitiesHere() { return g_Client.state() != GameClient::State::Joined; }   // we run them
// The light a model gets where it stands (sunlight - dark in caves and at night - and block
// light), for the model shader's u_LocalLight. Defined below main's helpers.
glm::vec3 modelLightAt(const glm::vec3& pos);

// Drops items into the world: ours to make (singleplayer, hosting), or asked of the server.
// pickupDelay: seconds before anyone can pick it up (Minecraft: 2 for thrown items).
// May this game's player pick items up now? can_pick_up_item(item, count) -> false says no
// (the survival mod: not while dead). Defined further down (needs the Lua state).
bool canPickUpItem(const std::string& item, int count);

bool dropItemInWorld(const std::string& item, int count, const glm::vec3& pos, const glm::vec3& vel, float pickupDelay) {
    if (item.empty() || count <= 0 || BlockRegistry::getInstance().getBlockId(item) == 0) return false;
    if (entitiesHere()) {
        Entity* e = g_Entities.get(g_Entities.dropItem(item, count, pos, vel));
        if (!e) return false;
        e->pickupDelay = pickupDelay;
        return true;
    }
    if (g_Client.state() == GameClient::State::Joined) {
        g_Client.sendDropItem(item, count, pos, false, &vel, pickupDelay);
        return true;
    }
    return false;
}

// The other players in this game's dimension (the ones elsewhere aren't seen, like Minecraft;
// they're still in the player list)
const net::PlayerMap& playersHere() {
    const net::PlayerMap& all = g_Server.running() ? g_Server.players() : g_Client.players();
    const std::string mine = g_WorldInstance ? g_WorldInstance->dimension() : std::string();
    bool same = true;
    for (const auto& [id, v] : all) if (v.dimension != mine) { same = false; break; }
    if (same) return all;
    static net::PlayerMap here;
    here.clear();
    for (const auto& [id, v] : all) if (v.dimension == mine) here.emplace(id, v);
    return here;
}

// What to draw and click this frame: ours, or the server's (only this dimension's)
std::vector<EntityView> currentEntityViews() {
    std::vector<EntityView> out;
    if (entitiesHere()) {
        out.reserve(g_Entities.all().size());
        const std::string mine = g_WorldInstance ? g_WorldInstance->dimension() : std::string();
        for (const auto& [id, e] : g_Entities.all()) if (!e.removed && e.dimension == mine) out.push_back(g_Entities.view(e));
    } else {
        out.reserve(g_Client.entities().size());
        for (const auto& [id, v] : g_Client.entities()) out.push_back(v);
    }
    return out;
}
// The nearest entity (not a dropped item) the ray hits within maxDist, and how far; 0 = none
uint32_t raycastEntities(const glm::vec3& o, const glm::vec3& d, float maxDist, float& dist) {
    if (entitiesHere()) return g_Entities.raycast(o, d, maxDist, &dist);
    uint32_t best = 0;
    dist = maxDist;
    for (const auto& [id, v] : g_Client.entities()) {
        if (v.type == EntityManager::kItemType) continue;
        const float hw = v.width * 0.5f + 0.05f;
        const glm::vec3 mn(v.pos.x - hw, v.pos.y, v.pos.z - hw), mx(v.pos.x + hw, v.pos.y + v.height + 0.05f, v.pos.z + hw);
        float t0 = 0.0f, t1 = dist;
        bool hit = true;
        for (int a = 0; a < 3 && hit; ++a) {
            if (std::fabs(d[a]) < 1e-8f) { if (o[a] < mn[a] || o[a] > mx[a]) hit = false; continue; }
            float ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) hit = false;
        }
        if (hit && t0 < dist) { dist = t0; best = id; }
    }
    return best;
}
ServerPinger g_Pinger;    // server list: online? description? players?
LanScanner g_Lan;         // games hosted on the local network (Multiplayer screen)
std::string g_NetError;
// Why the last world couldn't be opened or was closed (world_select.lua shows it: engine.take_world_error)
std::string g_WorldError;
// LOADING timed out but the chunk under the player isn't loaded (e.g. its save file can't be
// read yet): the loading screen says so and offers Esc = back to the menu, while it keeps trying
bool g_SpawnChunkWaiting = false;
bool g_ShowDebugInfo = false;   // F3: FPS, ping and multiplayer info in the top-left corner
std::string g_GpuName;          // graphics card in use (shown on F3)
float g_Fps = 0.0f;       // frames per second, updated twice a second (shown by /netstats)   // why the last connection failed/ended (shown on the Multiplayer screen)
MainMenu* g_MainMenu = nullptr;
// Is there a newer BlockSmith? (UpdateCheck.h - one small version file, read as data). Made once,
// on the first session, and kept for the life of the game.
std::unique_ptr<update::Checker> g_Updates;
InventorySystem g_Inventory; // Dedicated C++ inventory system instance
float g_FieldOfView = 70.0f;
FogSettings g_Fog;
UnderwaterSettings g_Underwater;
float g_UnderwaterBlend = 0.0f;
bool g_Paused = false;   // pause menu open (drawn by pause_menu.lua)

// Loading screen state
bool g_SpawnPlaced = false;
double g_LoadStartTime = 0.0;
float g_LoadingProgress = 0.0f;
constexpr int kSpawnReadyRadius = 2;        // chunks around the spawn that must be ready
constexpr double kLoadTimeoutSec = 30.0;    // start anyway after this long

struct WaterColors {
    glm::vec3 shallow;
    glm::vec3 deep;
};

enum class GameState {
    MENU,
    WORLD_SELECT,   // singleplayer world list (drawn by world_select.lua)
    MODS,           // installed mods list (drawn by mods_menu.lua)
    TEXTURE_PACKS,  // texture pack picker (drawn by texture_packs.lua)
    SETTINGS,       // settings from the title screen (drawn by settings_menu.lua)
    MULTIPLAYER,    // server list / joining (drawn by multiplayer.lua)
    LOADING,
    PLAYING
};
GameState g_CurrentGameState = GameState::MENU;

struct GameInfo {
    std::string folderName;
    std::string title = "Unnamed Game";
    std::string author = "Unknown";
    std::string description = "";
};

GameInfo parseGameConfig(const fs::path& gameDirPath) {
    GameInfo info;
    info.folderName = gameDirPath.filename().string();

    fs::path confPath = gameDirPath / "game.conf";
    if (!fs::exists(confPath)) {
        confPath = gameDirPath / "mod.conf";
    }
    if (!fs::exists(confPath)) {
        info.title = info.folderName;
        return info;
    }

    std::ifstream file(confPath);
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        size_t delimiter = line.find('=');
        if (delimiter != std::string::npos) {
            std::string key = line.substr(0, delimiter);
            std::string val = line.substr(delimiter + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);

            if (key == "title" || key == "name") info.title = val;
            else if (key == "author") info.author = val;
            else if (key == "description") info.description = val;
        }
    }
    return info;
}

std::vector<GameInfo> getAvailableGames() {
    std::vector<GameInfo> games;
    std::string gamesDir = "games";
    if (!fs::exists(gamesDir) && fs::exists("../../games")) gamesDir = "../../games";

    if (fs::exists(gamesDir)) {
        for (const auto& entry : fs::directory_iterator(gamesDir)) {
            if (entry.is_directory()) {
                fs::path confPath = entry.path() / "game.conf";
                if (fs::exists(confPath) || fs::exists(entry.path() / "mod.conf")) {
                    games.push_back(parseGameConfig(entry.path()));
                }
            }
        }
    }

    if (games.empty()) {
        games.push_back({"craft_and_ruin", "Craft & Ruin", "Brandon", "Default Voxel Survival Game"});
    }
    return games;
}

// Is this game installed: a folder of that name in games/ (the same rule the title screen uses
// to list the games you can pick)
bool gameInstalled(const std::string& folder) {
    if (folder.empty() || folder == "." || folder == ".." || folder.find('/') != std::string::npos ||
        folder.find('\\') != std::string::npos || folder.find(':') != std::string::npos) return false;
    std::error_code ec;
    for (const char* root : { "games", "../../games" }) {
        if (fs::is_directory(fs::path(root) / folder, ec)) return true;
    }
    return false;
}

std::vector<std::string> getAvailableWorlds() {
    std::vector<std::string> worlds;
    std::string savesDir = "saves";
    if (!fs::exists(savesDir) && fs::exists("../../saves")) savesDir = "../../saves";

    if (fs::exists(savesDir)) {
        for (const auto& entry : fs::directory_iterator(savesDir)) {
            if (entry.is_directory()) {
                worlds.push_back(entry.path().filename().string());
            }
        }
    }
    if (worlds.empty()) worlds.push_back("world_alpha");
    return worlds;
}

// ===========================================================================
// World saves
//   saves/<name>/world.dat      seed, player position/look, hotbar (text key=value)
//   saves/<name>/chunks/*.bin   chunks that were changed (written by World)
// ===========================================================================

Player* g_PlayerInstance = nullptr;
// Server-side mod events and functions (ServerLuaApi.h): on_player_join, can_edit_block,
// engine.get_online_players... - the same as on the dedicated server
ServerLuaApi g_ServerApi;
bool g_PendingWorldOpen = false;          // open g_ActiveWorldName at the start of LOADING
// Playing a world that needs other mods (or another game) than the ones loaded: the session ends,
// everything mods made is thrown away and loaded again for that world, and it opens - all behind
// the loading screen, in the same window (see main)
struct ReloadRequest {
    bool pending = false;
    std::string world;   // open this world once the mods are loaded ("" = back to the title screen)
    std::string game;
};
ReloadRequest g_Reload;
int g_SessionGeneration = 0;              // goes up with every reload (caches of the old mods check it)
double g_LastAutosave = 0.0;
constexpr double kAutosaveSeconds = 120.0;

fs::path savesRoot() {
    if (!fs::exists("saves") && fs::exists("../../saves")) return fs::path("../../saves");
    return fs::path("saves");
}

uint32_t randomSeed() {
    std::random_device rd;
    return rd() ^ (rd() << 16);
}

// Empty -> random. A number -> that number. Any other text -> a hash of it.
uint32_t seedFromText(std::string t) {
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    if (t.empty()) return randomSeed();

    const bool numeric = t.size() <= 10 &&
        std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (numeric) {
        const unsigned long long v = std::stoull(t);
        if (v <= 0xFFFFFFFFull) return static_cast<uint32_t>(v);
    }

    uint32_t h = 2166136261u;   // FNV-1a
    for (unsigned char c : t) { h ^= c; h *= 16777619u; }
    return h;
}

// Keeps letters, digits, spaces, '-' and '_' so the name is a safe folder name.
std::string sanitizeWorldName(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = "New World";
    if (out.size() > 32) out.resize(32);
    return out;
}

struct WorldMeta {
    bool valid = false;
    uint32_t seed = 0;
    bool hasPlayer = false;
    glm::vec3 position{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    std::vector<std::pair<std::string, int>> hotbar;   // block name ("" = empty), count
    std::string game;                                  // games/<folder> this world belongs to ("" = older save)
    std::string timeLine;                              // "time=<days> <speed>" (DayTime.h), "" = new
    std::string dimension;                             // where the player is ("" = the overworld)
};

bool readWorldMeta(const fs::path& file, WorldMeta& meta) {
    std::ifstream in(file);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        std::istringstream val(line.substr(eq + 1));

        if (key == "seed") {
            val >> meta.seed;
            meta.valid = !val.fail();
        } else if (key == "player") {
            val >> meta.position.x >> meta.position.y >> meta.position.z;
            meta.hasPlayer = !val.fail();
        } else if (key == "look") {
            val >> meta.yaw >> meta.pitch;
        } else if (key == "game") {
            meta.game = line.substr(eq + 1);
        } else if (key == "time") {
            meta.timeLine = line;
        } else if (key == "dimension") {
            meta.dimension = line.substr(eq + 1);
            if (!dims::validName(meta.dimension) || meta.dimension == dims::kOverworld) meta.dimension.clear();
        } else if (key.rfind("hotbar", 0) == 0) {
            const int idx = std::atoi(key.c_str() + 6);
            std::string name;
            int count = 0;
            val >> name >> count;
            if (idx >= 1 && idx <= 64) {
                if (static_cast<int>(meta.hotbar.size()) < idx) meta.hotbar.resize(idx);
                meta.hotbar[idx - 1] = { name == "-" ? "" : name, count };
            }
        }
    }
    return meta.valid;
}

// withHotbar: write the hotbar that's on screen now. Only for the world being played - a world
// that is just being created must not get it (that was the hotbar of the last world played, so
// every new world started with the previous world's items).
bool writeWorldMeta(const fs::path& file, uint32_t seed, const Player* player, const std::string& game,
                    bool withHotbar = true) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << std::fixed << std::setprecision(3);
        out << "version=1\n";
        if (!game.empty()) out << "game=" << game << "\n";
        out << "seed=" << seed << "\n";
        if (player) {
            out << "player=" << player->position.x << " " << player->position.y << " " << player->position.z << "\n";
            out << "look=" << player->yaw << " " << player->pitch << "\n";
            // the dimension they're in (the overworld isn't written: older games read the file too)
            if (g_WorldInstance && !g_WorldInstance->dimension().empty()) out << "dimension=" << g_WorldInstance->dimension() << "\n";
        }
        // The time of day: only for the world being played (a new world starts in the morning)
        if (g_LightingSystem && withHotbar) out << g_LightingSystem->clock.saveLine() << "\n";
        if (g_Hotbar && withHotbar) {
            for (int i = 0; i < g_Hotbar->slotCount; ++i) {
                const HotbarSlot s = g_Hotbar->getSlot(i);
                const std::string name = s.blockId ? BlockRegistry::getInstance().getBlock(s.blockId).name : "-";
                out << "hotbar" << (i + 1) << "=" << name << " " << (s.blockId ? s.count : 0) << "\n";
            }
        }
        if (!out) return false;
    }

    if (!replaceFile(tmp, file, ec)) {   // the old file stays as it was
        std::cerr << "[Save] Could not replace " << file.string() << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

// Writes changed chunks + world.dat for the world that is currently open.
void saveCurrentWorld() {
    World* w = g_WorldInstance;
    if (!w || w->saveDirectory().empty() || g_WorldDir.empty()) return;

    // Every dimension's chunks (the one we're in, and any running in the background)
    const int written = w->saveAll() + g_DimWorlds.saveAll();
    g_BlockMods.saveDimension(w->dimension(), w->saveDirectory());
    g_DimWorlds.forEach([](const std::string& d, World& dw) { g_BlockMods.saveDimension(d, dw.saveDirectory()); });
    // (the world's own files are in its folder, whichever dimension we're in)
    if (entitiesHere() && !g_Entities.save(g_WorldDir, [](Entity& e) { g_EntityLua.saveData(e); })) {
        std::cerr << "[Save] Could not save entities.txt" << std::endl;
    }
    {
        std::ofstream f(fs::path(g_WorldDir) / "mod_storage.txt");
        for (const auto& [k, v] : g_ModStorage) f << escapeStorage(k) << '\t' << escapeStorage(v) << '\n';
        g_ModStorageDirty = false;
    }
    const bool metaOk = writeWorldMeta(fs::path(g_WorldDir) / "world.dat", w->seed(),
                                       g_PlayerInstance, g_LoadedGameFolder);
    std::cout << "[Save] Saved '" << g_ActiveWorldName << "' (" << written << " changed chunks"
              << (metaOk ? "" : ", world.dat FAILED") << ")" << std::endl;
}

// This game's own player goes to another dimension (singleplayer, or the host): the dimension
// it leaves is saved (and keeps running in the background while guests are there), the one it
// goes to is loaded into the world on screen. Like Minecraft's, the trip shows a short wait
// while the new dimension's chunks arrive. False: joined to someone else's server (that server
// decides), or no such dimension.
World* apiWorld() {
    if (!g_WorldInstance) return nullptr;
    const std::string& d = dims::current();
    if (d == g_WorldInstance->dimension() || g_Client.active()) return g_WorldInstance;   // (joined: only ours)
    if (World* w = g_DimWorlds.get(d)) return w;
    return g_WorldInstance;
}

extern GameServer g_Server;
extern ServerLuaApi g_ServerApi;
std::string playerName();
bool changeLocalDimension(const std::string& toIn, const glm::vec3& pos) {
    World* w = g_WorldInstance;
    if (!w || !g_PlayerInstance || g_Client.active()) return false;
    const std::string to = DimensionWorlds::norm(toIn);
    if (!to.empty() && !dims::Registry::get().has(to)) return false;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return false;
    const std::string from = w->dimension();
    g_PlayerInstance->velocity = glm::vec3(0.0f);
    if (to == from) { g_PlayerInstance->position = pos; return true; }

    // 1. The dimension we leave: everything changed in it goes to disk
    w->saveAll();
    if (!g_WorldDir.empty()) g_BlockMods.saveDimension(from, w->saveDirectory());
    // 2. The one we go to: if it was running in the background (guests there), it's saved and
    //    taken over by the world on screen (which reads those chunks back from disk)
    if (World* bg = g_DimWorlds.find(to); bg && bg != w) g_DimWorlds.unload(to);
    // 3. The world on screen becomes that dimension
    w->reset();
    w->setDimension(to);
    if (!g_WorldDir.empty()) {
        std::error_code ec;
        const fs::path dir = fs::path(g_WorldDir) / dims::folderOf(to);
        fs::create_directories(dir / "chunks", ec);
        w->setSaveDirectory(dir.string());
        g_BlockMods.loadDimension(to, dir.string());
    }
    if (g_Server.running()) w->setRecordChanges(true);
    // 4. Guests still in the dimension we left: it runs on in the background for them
    if (g_Server.running()) {
        for (const auto& p : g_Server.playerPlaces()) {
            if (p.dimension == from) { g_DimWorlds.get(from); break; }
        }
    }
    g_PlayerInstance->position = pos;
    g_Dig = DigState{};
    dims::currentRef() = to;
    g_DimensionSwitched = true;
    if (g_Server.running()) g_Server.setHostDimension(to);
    std::cout << "[Dimension] " << playerName() << ": " << (from.empty() ? std::string(dims::kOverworld) : from)
              << " -> " << (to.empty() ? std::string(dims::kOverworld) : to) << std::endl;
    g_ServerApi.dimensionChanged(playerName(), from, to);
    return true;
}

// Can this world be played with the game that is currently loaded?
// Older saves without a "game=" line belong to whichever game opens them first.
bool worldBelongsToLoadedGame(const WorldMeta& meta, std::string* why = nullptr) {
    if (meta.game.empty() || meta.game == g_LoadedGameFolder) return true;
    if (why) *why = "This world was made with '" + meta.game + "', but '" + g_LoadedGameFolder + "' is loaded.";
    return false;
}

std::string worldGame(const std::string& world) {
    WorldMeta meta;
    if (!world.empty() && readWorldMeta(savesRoot() / world / "world.dat", meta) && !meta.game.empty()) return meta.game;
    return g_ActiveGameFolder;
}

bool canPickUpItem(const std::string& item, int count) {
    if (!g_Lua) return true;
    sol::protected_function hook = (*g_Lua)["can_pick_up_item"];
    if (!hook.valid()) return true;
    auto r = hook(item, count);
    if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] can_pick_up_item: " << err.what() << std::endl; return true; }
    return !(r.get_type() == sol::type::boolean && !r.get<bool>());
}

glm::vec3 modelLightAt(const glm::vec3& pos) {
    if (!g_WorldInstance) return glm::vec3(1.0f);
    const uint16_t l = g_WorldInstance->getLightAt(glm::ivec3(glm::floor(pos)));
    auto curve = [](int level) {          // the world shader's brightness curve (lightCurve)
        const float x = std::clamp(level / 15.0f, 0.0f, 1.0f);
        const float v = x / (4.0f - 3.0f * x);
        const float inv = 1.0f - v;
        return 1.0f - inv * inv * inv * inv;
    };
    float daylight = 1.0f;                // as the world shader: dimmer at night
    if (g_LightingSystem) daylight = std::clamp(g_LightingSystem->getSunDirection().y * 0.6f + 0.4f, 0.28f, 1.0f);
    const glm::vec3 sun(curve(light::sky(l)) * daylight);
    const glm::vec3 block = glm::vec3(curve(light::red(l)), curve(light::green(l)), curve(light::blue(l))) * g_LightSettings.blockLightScale;
    glm::vec3 lit = glm::vec3(1.0f) - (glm::vec3(1.0f) - sun) * (glm::vec3(1.0f) - glm::min(block, glm::vec3(1.0f)));
    return glm::max(lit, glm::vec3(std::max(0.1f, g_LightSettings.minLight)));
}

// Opens g_ActiveWorldName: creates it if new, otherwise restores seed, player and hotbar.
// Returns false (and opens nothing) if the world belongs to a different game.
// ---------------------------------------------------------------------------
// World thumbnails: saves/<world>/thumbnail.png, a picture of the world for the world list.
// Taken from the game view (before the hand and HUD are drawn) when the game is paused, and
// a few seconds after a world first opens if it has none yet.
// ---------------------------------------------------------------------------
bool g_ThumbnailWanted = false;       // take one this frame
bool g_ThumbnailChecked = false;      // (the "has none yet" check, once per opened world)
double g_WorldOpenedAt = 0.0;
double g_LastThumbnailAt = 0.0;      // (while playing, the picture is refreshed every few minutes)

// A plain PNG (8-bit RGB, deflate "stored" blocks: no compression library needed)
bool writePngRGB(const fs::path& file, const std::vector<uint8_t>& rgb, int w, int h) {
    auto crc32 = [](const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
        for (size_t i = 0; i < n; ++i) {
            c ^= d[i];
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return c;
    };
    auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
    };
    std::vector<uint8_t> raw;                       // each row: filter byte 0, then the pixels
    raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * w * 3, rgb.begin() + static_cast<size_t>(y + 1) * w * 3);
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };        // zlib header, then stored blocks of up to 65535
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        const size_t len = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(len)); z.push_back(uint8_t(len >> 8));
        z.push_back(uint8_t(~len)); z.push_back(uint8_t(~len >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
        if (last) break;
    }
    uint32_t a = 1, b = 0;                          // adler32
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32(png, static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        png.insert(png.end(), td.begin(), td.end());
        be32(png, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
    };
    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(w)); be32(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 });    // 8 bits, RGB
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!out) return false;
    }
    std::error_code ec;
    return replaceFile(tmp, file, ec);
}

// Reads the frame being drawn (width x height pixels) and saves a 320x180 thumbnail of its
// middle (16:9) for the world being played
void saveWorldThumbnail(int width, int height) {
    if (g_ActiveWorldName.empty() || width < 16 || height < 16) return;
    const fs::path dir = savesRoot() / g_ActiveWorldName;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<uint8_t> frame(static_cast<size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, frame.data());
    // The biggest 16:9 area in the middle, shrunk to 320x180 by averaging
    const int TW = 320, TH = 180;
    int cw = width, ch = width * 9 / 16;
    if (ch > height) { ch = height; cw = height * 16 / 9; }
    const int ox = (width - cw) / 2, oy = (height - ch) / 2;
    std::vector<uint8_t> out(static_cast<size_t>(TW) * TH * 3);
    for (int ty = 0; ty < TH; ++ty) {
        const int y0 = oy + ty * ch / TH, y1 = std::max(y0 + 1, oy + (ty + 1) * ch / TH);
        for (int tx = 0; tx < TW; ++tx) {
            const int x0 = ox + tx * cw / TW, x1 = std::max(x0 + 1, ox + (tx + 1) * cw / TW);
            uint32_t sum[3] = { 0, 0, 0 }, n = 0;
            for (int y = y0; y < y1; y += std::max(1, (y1 - y0) / 3)) {
                for (int x = x0; x < x1; x += std::max(1, (x1 - x0) / 3)) {
                    const uint8_t* p = &frame[(static_cast<size_t>(y) * width + x) * 3];
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; ++n;
                }
            }
            uint8_t* o = &out[(static_cast<size_t>(TH - 1 - ty) * TW + tx) * 3];   // (GL reads bottom-up)
            for (int c = 0; c < 3; ++c) o[c] = static_cast<uint8_t>(sum[c] / std::max(1u, n));
        }
    }
    if (writePngRGB(dir / "thumbnail.png", out, TW, TH)) {
        std::cout << "[World] Thumbnail saved for '" << g_ActiveWorldName << "'" << std::endl;
    }
}

// "Just now", "5 minutes ago", "Yesterday", "3 days ago"...
std::string playedAgo(fs::file_time_type when) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - when).count();
    if (secs < 90) return "Just now";
    const long long mins = secs / 60, hours = mins / 60, days = hours / 24;
    if (mins < 60) return std::to_string(mins) + " minutes ago";
    if (hours < 24) return hours == 1 ? "An hour ago" : std::to_string(hours) + " hours ago";
    if (days < 2) return "Yesterday";
    if (days < 31) return std::to_string(days) + " days ago";
    if (days < 62) return "Last month";
    return std::to_string(days / 30) + " months ago";
}

bool openActiveWorld(World& world, Player& player) {
    const fs::path dir = savesRoot() / g_ActiveWorldName;

    WorldMeta meta;
    const bool existing = readWorldMeta(dir / "world.dat", meta);

    std::string why;
    if (existing && !worldBelongsToLoadedGame(meta, &why)) {
        std::cerr << "[World] Refusing to open '" << g_ActiveWorldName << "': " << why << std::endl;
        return false;
    }

    std::error_code ec;
    fs::create_directories(dir / "chunks", ec);

    world.reset();
    g_ThumbnailChecked = false;          // (a new world gets its first thumbnail after a few seconds)
    g_WorldOpenedAt = glfwGetTime();
    g_LastThumbnailAt = g_WorldOpenedAt;
    g_SharedValues.clear();   // shared values (weather...) belong to a world
    g_SharedChanged.clear();
    g_ModToOwnGame.clear();
    g_ModToOwnServer.clear();
    g_ModStorage.clear();     // so does mod storage
    {
        std::ifstream f(dir / "mod_storage.txt");
        std::string line;
        while (std::getline(f, line)) {
            const size_t tab = line.find('\t');
            if (tab != std::string::npos) g_ModStorage[unescapeStorage(line.substr(0, tab))] = unescapeStorage(line.substr(tab + 1));
        }
    }
    g_Dig = DigState{};
    g_DimWorlds.unloadAll();
    g_WorldDir = dir.string();
    g_DimWorlds.primary = &world;
    g_DimWorlds.worldDir = g_WorldDir;
    // The dimension the player was in (one no longer registered: back to the overworld spawn)
    std::string dimension = meta.dimension;
    if (!dimension.empty() && !dims::Registry::get().has(dimension)) {
        std::cout << "[World] The dimension '" << dimension << "' isn't there any more: back to the overworld" << std::endl;
        dimension.clear();
        meta.hasPlayer = false;
    }
    world.setDimension(dimension);
    dims::currentRef() = dimension;
    if (dimension.empty()) world.setSaveDirectory(dir.string());
    else {
        fs::create_directories(dir / dims::folderOf(dimension) / "chunks", ec);
        world.setSaveDirectory((dir / dims::folderOf(dimension)).string());
    }
    world.setSeed(existing ? meta.seed : randomSeed());

    // The time of day carries on where it was (a new world: morning)
    if (g_LightingSystem) {
        g_LightingSystem->clock = DayClock{};
        g_LightingSystem->customTimeOfDay = -1.0f;
        if (!meta.timeLine.empty()) g_LightingSystem->clock.loadLine(meta.timeLine);
    }

    // Hotbar: empty, then restore what was saved
    if (g_Hotbar) {
        for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
        for (size_t i = 0; i < meta.hotbar.size() && static_cast<int>(i) < g_Hotbar->slotCount; ++i) {
            const auto& entry = meta.hotbar[i];
            if (entry.first.empty()) continue;
            const uint8_t id = BlockRegistry::getInstance().getBlockId(entry.first);
            if (id != 0) g_Hotbar->setSlot(static_cast<int>(i), id, entry.second > 0 ? entry.second : 64);
        }
    }

    // Player: saved position, or a fresh spawn (LOADING finds the ground)
    if (existing && meta.hasPlayer) {
        player.position = meta.position;
        player.yaw = meta.yaw;
        player.pitch = meta.pitch;
        g_SpawnPlaced = true;
    } else {
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);
        g_SpawnPlaced = false;
    }

    // Which chunks the once-only LBMs have already been through
    g_BlockMods.loadWorld(dir.string());
    if (!dimension.empty()) g_BlockMods.loadDimension(dimension, world.saveDirectory());

    // Its mobs and dropped items (their mods' on_spawn gets what on_save gave them)
    g_EntityLua.reset();
    g_EntityLooks.clear();
    {
        const int n = g_Entities.load(dir.string());
        g_EntityLua.activateAll();
        if (n > 0) std::cout << "[World] " << n << " entities" << std::endl;
    }

    // Make sure the seed is on disk straight away
    // (also stamps older saves with the game that opened them)
    writeWorldMeta(dir / "world.dat", world.seed(), (existing && meta.hasPlayer) ? &player : nullptr,
                   g_LoadedGameFolder);
    g_LastAutosave = glfwGetTime();

    std::cout << "[World] " << (existing ? "Loaded" : "Created") << " world '" << g_ActiveWorldName
              << "' (seed " << world.seed() << ", game " << g_LoadedGameFolder << ")" << std::endl;
    return true;
}

// Switches to the loading screen; the world itself is opened from the main loop.
std::string worldGame(const std::string& world);
std::set<std::string> resolveGlobalMods(const std::string& game, const std::string& world);
extern std::set<std::string> g_LoadedGlobalMods;

extern bool g_HudHidden;

void beginLoadingWorld(GLFWwindow* win) {
    // Every world starts with the HUD showing (F1 hides it only until you leave that world)
    g_HudHidden = false;
    // This world runs other mods (or is another game's): load those first, then open it
    const std::string game = worldGame(g_ActiveWorldName);
    if (!g_Reload.pending && (game != g_LoadedGameFolder || resolveGlobalMods(game, g_ActiveWorldName) != g_LoadedGlobalMods)) {
        std::cout << "[Mods] '" << g_ActiveWorldName << "' needs other mods than the ones loaded: reloading for it" << std::endl;
        g_Reload = ReloadRequest{ true, g_ActiveWorldName, game };
    }
    g_CurrentGameState = GameState::LOADING;
    g_PendingWorldOpen = true;
    g_SpawnPlaced = false;
    g_LoadStartTime = glfwGetTime();
    g_LoadingProgress = 0.0f;
    g_Paused = false;
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

struct SavedWorldInfo {
    std::string name;
    std::string seed;
    std::string game;
    std::string gameMode;   // "world:game_mode" from its mod storage ("" = not set)
    fs::file_time_type lastPlayed;
};

// One value from a world's mod storage (saves/<world>/mod_storage.txt), "" if not there
std::string readWorldStorage(const fs::path& worldDir, const std::string& key) {
    std::ifstream f(worldDir / "mod_storage.txt");
    std::string line;
    while (std::getline(f, line)) {
        const size_t tab = line.find('\t');
        if (tab != std::string::npos && unescapeStorage(line.substr(0, tab)) == key) {
            return unescapeStorage(line.substr(tab + 1));
        }
    }
    return "";
}

// Worlds for the game currently selected on the title screen, most recently played first.
// Older saves with no game recorded are shown for every game until one opens them.
std::vector<SavedWorldInfo> listSavedWorlds() {
    std::vector<SavedWorldInfo> list;
    const fs::path root = savesRoot();
    std::error_code ec;
    if (!fs::exists(root, ec)) return list;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        SavedWorldInfo info;
        info.name = entry.path().filename().string();

        WorldMeta meta;
        const fs::path dat = entry.path() / "world.dat";
        info.seed = readWorldMeta(dat, meta) ? std::to_string(meta.seed) : "?";
        info.game = meta.game;
        if (!info.game.empty() && info.game != g_ActiveGameFolder) continue;   // another game's world

        info.gameMode = readWorldStorage(entry.path(), "world:game_mode");
        info.lastPlayed = fs::exists(dat, ec) ? fs::last_write_time(dat, ec) : fs::last_write_time(entry.path(), ec);
        list.push_back(info);
    }

    std::sort(list.begin(), list.end(),
              [](const SavedWorldInfo& a, const SavedWorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return list;
}

// Creates saves/<name>/ with a world.dat holding the seed. `options` (e.g. game_mode =
// "survival") go into the new world's mod storage as world:<key>, for mods to read with
// engine.get_storage("world:game_mode"). Returns the folder name used.
std::string createWorld(const std::string& rawName, const std::string& seedText,
                        const std::map<std::string, std::string>& options = {}) {
    const std::string base = sanitizeWorldName(rawName);
    std::string name = base;
    for (int n = 2; fs::exists(savesRoot() / name); ++n) {
        name = base + " (" + std::to_string(n) + ")";
    }

    std::error_code ec;
    fs::create_directories(savesRoot() / name / "chunks", ec);
    // (for the game selected on the title screen: playing it loads that game's mods)
    writeWorldMeta(savesRoot() / name / "world.dat", seedFromText(seedText), nullptr, g_ActiveGameFolder,
                   false);   // a new world starts with an empty hotbar
    if (!options.empty()) {
        std::ofstream f(savesRoot() / name / "mod_storage.txt");
        for (const auto& [k, v] : options) f << escapeStorage("world:" + k) << '\t' << escapeStorage(v) << '\n';
    }
    std::cout << "[World] Created save folder '" << name << "' for game '" << g_ActiveGameFolder << "'" << std::endl;
    return name;
}

// Can the world list be used right now? Always: playing a world of another game (or with other
// mods) reloads the mods for it first.
bool selectedGameIsLoaded() {
    return true;
}

// Permanently deletes saves/<name>. Refuses anything that isn't a world folder directly
// inside saves/, a world from another game, or the world that is currently open.
bool deleteWorld(const std::string& name, std::string& error) {
    if (name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        error = "Invalid world name.";
        return false;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(savesRoot(), ec);
    const fs::path dir = fs::weakly_canonical(savesRoot() / name, ec);
    if (ec || dir.parent_path() != root || !fs::is_directory(dir, ec)) {
        error = "World folder not found.";
        return false;
    }

    WorldMeta meta;
    readWorldMeta(dir / "world.dat", meta);
    if (!meta.game.empty() && meta.game != g_ActiveGameFolder) {
        error = "That world belongs to another game.";
        return false;
    }

    if (g_WorldInstance && !g_WorldInstance->saveDirectory().empty() &&
        fs::weakly_canonical(g_WorldInstance->saveDirectory(), ec) == dir) {
        error = "That world is currently open.";
        return false;
    }

    const auto removed = fs::remove_all(dir, ec);
    if (ec) {
        error = "Could not delete: " + ec.message();
        return false;
    }
    std::cout << "[World] Deleted '" << name << "' (" << removed << " files)" << std::endl;
    return true;
}

// ===========================================================================
// Installed mods (for the Mods screen)
// ===========================================================================

std::string modPathKey(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return (ec ? p : c).string();
}

struct ModRoot {
    fs::path path;
    std::string source;   // "Global" or "Game: <folder>"
};

// Same folders the startup loader reads, each listed once.
std::vector<ModRoot> modRoots() {
    std::vector<ModRoot> roots;
    std::set<std::string> seen;
    auto add = [&](const std::string& rel, const std::string& source) {
        std::string resolved = rel;
        if (!fs::exists(resolved) && fs::exists("../../" + rel)) resolved = "../../" + rel;
        std::error_code ec;
        if (!fs::is_directory(resolved, ec)) return;
        const std::string key = modPathKey(resolved);
        if (seen.insert(key).second) roots.push_back({ fs::path(resolved), source });
    };
    add("mods", "Global");
    add("../mods", "Global");
    add("../../mods", "Global");
    add("games/" + g_LoadedGameFolder + "/mods", "Game: " + g_LoadedGameFolder);
    return roots;
}

// key = value lines (mod.conf style)
std::map<std::string, std::string> readConfFile(const fs::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            const size_t e = s.find_last_not_of(" \t");
            s.erase(e == std::string::npos ? 0 : e + 1);
        };
        trim(key);
        trim(val);
        out[key] = val;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Turning mods in the mods/ folder on and off
//   Saved in mods/mod_settings.txt as lines like:   my_mod = false
//   Mods are on unless listed as false. Changes apply on the next start.
//   Mods in games/<game>/mods are part of the game and can't be switched off here.
// ---------------------------------------------------------------------------

std::map<std::string, bool> g_ModEnabledAtStartup;   // what the loader actually did this run

fs::path globalModsFolder() {
    for (const auto& r : modRoots()) {
        if (r.source == "Global") return r.path;
    }
    return fs::path("mods");
}

fs::path modSettingsFile() {
    return globalModsFolder() / "mod_settings.txt";
}

std::map<std::string, bool> loadModSettings() {
    std::map<std::string, bool> settings;
    for (const auto& [key, val] : readConfFile(modSettingsFile())) {
        settings[key] = !(val == "false" || val == "0" || val == "off" || val == "no");
    }
    return settings;
}

bool saveModSettings(const std::map<std::string, bool>& settings) {
    const fs::path file = modSettingsFile();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "# Mods switched on/off on the Mods screen. Changes apply when the game restarts.\n";
        for (const auto& [id, on] : settings) out << id << " = " << (on ? "true" : "false") << "\n";
        if (!out) return false;
    }
    std::error_code ec;
    if (!replaceFile(tmp, file, ec)) {   // the old file stays as it was
        std::cerr << "[Mods] Could not replace " << file.string() << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

// A mod that can't be switched off: the base mod (it contains the Mods screen itself),
// or any mod whose mod.conf says "required = true".
bool modIsRequired(const fs::path& modFolder, const std::string& id) {
    if (id == "base") return true;
    std::error_code ec;
    const fs::path conf = modFolder / "mod.conf";
    if (!fs::exists(conf, ec)) return false;
    auto kv = readConfFile(conf);
    return kv["required"] == "true";
}

bool modIsEnabled(const fs::path& rootFolder, const std::string& id) {
    if (modIsRequired(rootFolder / id, id)) return true;
    const auto settings = loadModSettings();
    const auto it = settings.find(id);
    return it == settings.end() || it->second;
}

struct ModInfo {
    std::string id;           // folder or file name
    std::string title, author, description, version, depends;
    std::string source;       // Global / Game: x
    std::string path;
    std::string status;       // "loaded", "error", "not_loaded", "disabled"
    std::string error;
    int luaFiles = 0;
    bool isFolder = false;
    bool enabled = true;      // switched on (takes effect on restart)
    bool required = false;    // can't be switched off
    bool restartNeeded = false;
};

// Fills status/error from what happened when the files were run at startup.
void applyLoadStatus(ModInfo& m, const std::vector<fs::path>& files) {
    bool ran = false;
    for (const auto& f : files) {
        const auto it = g_ModFileStatus.find(modPathKey(f));
        if (it == g_ModFileStatus.end()) continue;
        ran = true;
        if (!it->second.empty() && m.error.empty()) m.error = it->second;
    }
    m.status = !m.error.empty() ? "error" : (ran ? "loaded" : "not_loaded");
}

std::map<std::string, bool> loadGameModDefaults(const std::string& game);
bool writeOnOffFile(const fs::path& file, const std::map<std::string, bool>& values, const std::string& header);
fs::path gameModsFile(const std::string& game);
bool validFolderName(const std::string& name);
bool globalModOn(const std::string& game, const std::string& world, const std::string& id,
                 const std::map<std::string, bool>* gameDefaults = nullptr,
                 const std::map<std::string, bool>* worldChoices = nullptr);

// Every mod folder and loose .lua file in the mods/ folder (game mods are not listed).
std::vector<ModInfo> listInstalledMods() {
    std::vector<ModInfo> mods;
    // "enabled" = on by default for the selected game's worlds (the Mods screen sets that)
    const auto gameDefaults = loadGameModDefaults(g_ActiveGameFolder);
    const std::map<std::string, bool> noWorld;
    for (const auto& root : modRoots()) {
        if (root.source != "Global") continue;   // only the mods/ folder

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root.path, ec)) {
            if (entry.is_directory() && entry.path().filename() == "clientmods") continue;   // client mods: listed separately
            ModInfo m;
            m.source = root.source;
            m.path = entry.path().string();
            std::vector<fs::path> files;

            if (entry.is_directory()) {
                m.isFolder = true;
                m.id = entry.path().filename().string();
                for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
                    if (f.is_regular_file() && f.path().extension() == ".lua") files.push_back(f.path());
                }
                if (files.empty()) continue;   // not a mod (e.g. a textures folder)

                const fs::path conf = entry.path() / "mod.conf";
                if (fs::exists(conf, ec)) {
                    auto kv = readConfFile(conf);
                    m.title = kv.count("title") ? kv["title"] : kv["name"];
                    m.author = kv["author"];
                    m.description = kv["description"];
                    m.version = kv["version"];
                    m.depends = kv["depends"];
                }
            } else if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                m.id = entry.path().filename().string();
                files.push_back(entry.path());
            } else {
                continue;
            }

            if (m.title.empty()) m.title = m.id;
            m.luaFiles = static_cast<int>(files.size());
            m.required = modIsRequired(entry.path(), m.id);
            m.enabled = globalModOn(g_ActiveGameFolder, "", m.id, &gameDefaults, &noWorld);

            applyLoadStatus(m, files);
            const auto started = g_ModEnabledAtStartup.find(m.id);
            const bool wasOn = started == g_ModEnabledAtStartup.end() || started->second;
            if (!wasOn) m.status = "disabled";
            m.restartNeeded = false;   // applied when a world is opened (no restart)

            mods.push_back(m);
        }
    }
    std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
        return a.title < b.title;
    });
    return mods;
}

// Switches a mod in mods/ on or off for the selected game's worlds (they pick it up when opened).
bool setModEnabled(const std::string& id, bool on, std::string& error) {
    if (id.empty() || id.find('/') != std::string::npos || id.find('\\') != std::string::npos ||
        id.find("..") != std::string::npos) {
        error = "Invalid mod name.";
        return false;
    }
    const fs::path root = globalModsFolder();
    std::error_code ec;
    if (!fs::exists(root / id, ec)) {
        error = "Mod not found.";
        return false;
    }
    if (!on && modIsRequired(root / id, id)) {
        error = "This mod is required and can't be switched off.";
        return false;
    }

    // The selected game's default (worlds that haven't chosen for themselves follow it)
    auto defaults = loadGameModDefaults(g_ActiveGameFolder);
    defaults[id] = on;
    if (!validFolderName(g_ActiveGameFolder) ||
        !writeOnOffFile(gameModsFile(g_ActiveGameFolder), defaults,
                        "# Mods from the mods/ folder that worlds of '" + g_ActiveGameFolder + "' run, unless a world\n"
                        "# chooses otherwise (saves/<world>/world_mods.txt). Set on the Mods screen.\n")) {
        error = "Could not save " + gameModsFile(g_ActiveGameFolder).string();
        return false;
    }
    std::cout << "[Mods] " << id << " is now " << (on ? "on" : "off") << " for worlds of " << g_ActiveGameFolder << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Which mods a world runs (like Luanti's world.mt, with Minecraft-style game-wide defaults)
//   - A game's own mods (games/<game>/mods) always run with it.
//   - Mods in the mods/ folder run only where they're switched on:
//       mods/game_mods_<game>.txt      the game's defaults: "my_mod = true" (Mods screen)
//       saves/<world>/world_mods.txt   one world's own choice, which wins (world list > Mods)
//     A mod neither file mentions is off, so a newly installed mod changes no world until it's
//     switched on. "base" and mods with "required = true" always run.
//   - The first time a game's defaults are needed they start from the old mods/mod_settings.txt
//     (every mod on unless switched off there), so existing worlds keep the mods they had.
// Opening a world whose mods (or game) differ from the ones loaded reloads everything in place.
// ---------------------------------------------------------------------------

std::string g_SessionWorld;                 // the world whose mods this session loaded ("" = the game's defaults)
std::set<std::string> g_LoadedGlobalMods;   // mods/ folder mods running this session

bool validModId(const std::string& id) {
    return !id.empty() && id.size() <= 128 && id.find('/') == std::string::npos &&
           id.find('\\') == std::string::npos && id.find("..") == std::string::npos && id.find('=') == std::string::npos;
}
bool validFolderName(const std::string& name) {
    return validModId(name) && name != "." && name.find(':') == std::string::npos;
}

// The mods in the mods/ folder that are real mods (a folder with .lua files, or a loose .lua)
std::vector<std::string> installedGlobalModIds() {
    std::vector<std::string> ids;
    const fs::path root = globalModsFolder();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(ec)) {
            if (name == "clientmods") continue;
            bool hasLua = false;
            for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
                if (f.is_regular_file() && f.path().extension() == ".lua") { hasLua = true; break; }
            }
            if (hasLua) ids.push_back(name);
        } else if (entry.is_regular_file(ec) && entry.path().extension() == ".lua") {
            ids.push_back(name);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::map<std::string, bool> readOnOffFile(const fs::path& file) {
    std::map<std::string, bool> out;
    for (const auto& [key, val] : readConfFile(file)) {
        out[key] = !(val == "false" || val == "0" || val == "off" || val == "no");
    }
    return out;
}

bool writeOnOffFile(const fs::path& file, const std::map<std::string, bool>& values, const std::string& header) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << header;
        for (const auto& [id, on] : values) out << id << " = " << (on ? "true" : "false") << "\n";
        if (!out) return false;
    }
    if (!replaceFile(tmp, file, ec)) {
        std::cerr << "[Mods] Could not replace " << file.string() << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

fs::path gameModsFile(const std::string& game) {
    return globalModsFolder() / ("game_mods_" + game + ".txt");
}

std::map<std::string, bool> loadGameModDefaults(const std::string& game) {
    const fs::path file = gameModsFile(game);
    std::error_code ec;
    if (fs::exists(file, ec)) return readOnOffFile(file);
    // First time: what the old global on/off list said (on unless switched off), written down
    // so that mods installed from now on start off
    std::map<std::string, bool> seeded;
    const auto legacy = loadModSettings();
    for (const auto& id : installedGlobalModIds()) {
        const auto it = legacy.find(id);
        seeded[id] = it == legacy.end() || it->second;
    }
    if (validFolderName(game)) {
        writeOnOffFile(file, seeded, "# Mods from the mods/ folder that worlds of '" + game + "' run, unless a world\n"
                                     "# chooses otherwise (saves/<world>/world_mods.txt). Set on the Mods screen.\n");
    }
    return seeded;
}

fs::path worldModsFile(const std::string& world) {
    return savesRoot() / world / "world_mods.txt";
}

std::map<std::string, bool> loadWorldModChoices(const std::string& world) {
    if (world.empty() || !validFolderName(world)) return {};
    return readOnOffFile(worldModsFile(world));
}

// Is a mods/ folder mod on for this game and world? (world = "" -> the game's default)
bool globalModOn(const std::string& game, const std::string& world, const std::string& id,
                 const std::map<std::string, bool>* gameDefaults,
                 const std::map<std::string, bool>* worldChoices) {
    if (modIsRequired(globalModsFolder() / id, id)) return true;
    std::map<std::string, bool> w, g;
    if (!worldChoices) { w = loadWorldModChoices(world); worldChoices = &w; }
    if (auto it = worldChoices->find(id); it != worldChoices->end()) return it->second;
    if (!gameDefaults) { g = loadGameModDefaults(game); gameDefaults = &g; }
    if (auto it = gameDefaults->find(id); it != gameDefaults->end()) return it->second;
    return false;
}

std::set<std::string> resolveGlobalMods(const std::string& game, const std::string& world) {
    const auto g = loadGameModDefaults(game);
    const auto w = loadWorldModChoices(world);
    std::set<std::string> on;
    for (const auto& id : installedGlobalModIds()) {
        if (globalModOn(game, world, id, &g, &w)) on.insert(id);
    }
    return on;
}

// The game a saved world belongs to ("" if it has no world.dat yet: the selected game)
std::string worldGame(const std::string& world);

// Opens the main mods folder in Explorer / Finder / the desktop file manager.
void openModsFolder() {
    auto roots = modRoots();
    fs::path target;
    for (const auto& r : roots) {
        if (r.source == "Global") { target = r.path; break; }
    }
    if (target.empty()) {
        std::error_code ec;
        fs::create_directories("mods", ec);
        target = "mods";
    }
    std::error_code ec;
    std::string abs = fs::absolute(target, ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());   // keep the command well-formed

#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    std::cout << "[Mods] Opening " << abs << std::endl;
    (void)std::system(cmd.c_str());
}

// Calls a Lua UI function if it exists. Returns true only if it returned true.
template <typename... Args>
bool callLuaUI(sol::state* lua, const char* fn, Args&&... args) {
    if (!lua) return false;
    try {
        sol::protected_function f = (*lua)[fn];
        if (!f.valid()) return false;
        sol::protected_function_result r = f(std::forward<Args>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Lua] " << fn << ": " << err.what() << std::endl;
            return false;
        }
        if (r.return_count() > 0 && r.get_type() == sol::type::boolean) return r.get<bool>();
    } catch (...) {}
    return false;
}

struct EngineContext {
    Player* player = nullptr;
    sol::state* lua = nullptr;
};

const int TEX_SIZE = 16;
const int MAX_TEXTURE_LAYERS = 256;
GLuint g_TextureArrayID = 0;
int g_NextTextureLayer = 0;

void initTextureArray() {
    glGenTextures(1, &g_TextureArrayID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, MAX_TEXTURE_LAYERS, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// ===========================================================================
// Texture packs
//   texturepacks/<pack>/          one folder per pack
//   texturepacks/<pack>/pack.conf title, author, description (all optional)
//   Any image in a pack with the same file name as a game texture replaces it,
//   e.g. texturepacks/MyPack/stone.png (or .../textures/stone.png).
//   The chosen pack is saved in texturepacks/active_pack.txt.
// ===========================================================================

struct TextureSource {
    std::string requested;   // name the game asked for, e.g. "stone.png"
    std::string resolved;    // file it was found at without a pack ("" = not found)
};

std::vector<TextureSource> g_LayerSources;                 // block texture layer -> source
std::map<GLuint, TextureSource> g_StandardTextureSources;  // 2D UI textures (hotbar, etc.)
std::string g_ActiveTexturePack;                           // "" = default textures

fs::path texturePacksRoot() {
    if (!fs::exists("texturepacks") && fs::exists("../../texturepacks")) return fs::path("../../texturepacks");
    return fs::path("texturepacks");
}

bool isSafeFolderName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name.find(':') == std::string::npos && name.find("..") == std::string::npos;
}

// The file in `pack` that replaces `requested`, or "".
std::string packOverrideFor(const std::string& pack, const std::string& requested) {
    if (pack.empty() || requested.empty()) return "";
    const fs::path root = texturePacksRoot() / pack;
    const fs::path req(requested);
    if (req.is_absolute() || requested.find("..") != std::string::npos) return "";
    const fs::path candidates[] = { root / req, root / "textures" / req.filename(), root / req.filename() };
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec)) return c.string();
    }
    return "";
}

std::string packOverride(const std::string& requested) {
    return packOverrideFor(g_ActiveTexturePack, requested);
}

// Loads an image as RGBA, resized (nearest neighbour) to size x size.
// Tall animation strips (frames stacked vertically) use their first frame.
bool loadImageScaled(const std::string& file, int size, std::vector<unsigned char>& out) {
    int w = 0, h = 0, n = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* d = stbi_load(file.c_str(), &w, &h, &n, 4);
    if (!d) return false;

    int srcH = h;
    if (h > w && w > 0 && h % w == 0) srcH = w;   // first frame of an animation strip

    out.resize(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        const int sy = y * srcH / size;
        for (int x = 0; x < size; ++x) {
            const int sx = x * w / size;
            const unsigned char* s = d + (static_cast<size_t>(sy) * w + sx) * 4;
            unsigned char* o = out.data() + (static_cast<size_t>(y) * size + x) * 4;
            o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3];
        }
    }
    stbi_image_free(d);
    return true;
}

// (Re)uploads one block texture layer: pack image, else the original, else white.
void uploadLayer(int layer, const TextureSource& src) {
    std::vector<unsigned char> px;
    const std::string override = packOverride(src.requested);
    bool ok = !override.empty() && loadImageScaled(override, TEX_SIZE, px);
    if (!ok && !src.resolved.empty()) ok = loadImageScaled(src.resolved, TEX_SIZE, px);
    if (!ok) px.assign(static_cast<size_t>(TEX_SIZE) * TEX_SIZE * 4, 255);   // missing texture: white

    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

// Every .png in the game's folder and the mods folders, by file name (lower case), like
// Luanti: a picture can be used by its name alone, whichever mod or subfolder it's in.
// Built when mods are about to load; used when the usual places don't have a file.
std::unordered_map<std::string, std::string> g_TextureIndex;
std::vector<std::string> g_TextureIndexRoots;

void buildTextureIndex() {
    g_TextureIndex.clear();
    g_TextureIndexRoots.clear();
    const std::vector<std::string> roots = {
        "games/" + g_ActiveGameFolder, "../../games/" + g_ActiveGameFolder,
        "mods", "../mods", "../../mods", "textures", "../../textures" };
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        g_TextureIndexRoots.push_back(fs::absolute(root, ec).string());
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".png") continue;
            std::string key = it->path().filename().string();
            for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            g_TextureIndex.emplace(key, fs::absolute(it->path(), ec).string());   // first one found wins
        }
    }
    std::cout << "[Texture] Indexed " << g_TextureIndex.size() << " pictures in the game and mod folders" << std::endl;
}

// A picture by file name from the index ("" if none)
std::string indexedTexture(const std::string& path) {
    std::string key = fs::path(path).filename().string();
    for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto it = g_TextureIndex.find(key);
    return it == g_TextureIndex.end() ? std::string() : it->second;
}

std::string textureSearchHint() {
    std::string where;
    for (const auto& r : g_TextureIndexRoots) where += (where.empty() ? "" : ", ") + r;
    return where.empty() ? "no game or mods folder found" : where;
}

// Block textures no game, mod or pack had, so far (one warning lists them all)
static std::vector<std::string> g_MissingTextures;
static bool g_MissingTexturesReported = false;   // after that: each one as it's asked for
static void reportMissingTextures() {
    g_MissingTexturesReported = true;
    if (g_MissingTextures.empty()) return;
    std::string list;
    for (size_t i = 0; i < g_MissingTextures.size(); ++i) list += (i ? ", " : "") + g_MissingTextures[i];
    std::cerr << "[Texture] " << g_MissingTextures.size() << " block texture(s) not found: " << list << std::endl;

}

int loadTextureToLayer(const std::string& path) {
    std::string finalPath = path;

    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path
    };

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = p;
            found = true;
            break;
        }
    }
    if (!found) {   // anywhere in the game or a mod, by name
        const std::string indexed = indexedTexture(path);
        if (!indexed.empty()) { finalPath = indexed; found = true; }
    }

    if (!found && packOverride(path).empty()) {
        // listed once, together, after the mods have loaded (reportMissingTextures)
        const std::string name = fs::path(path).filename().string();
        if (std::find(g_MissingTextures.begin(), g_MissingTextures.end(), name) == g_MissingTextures.end()) {
            g_MissingTextures.push_back(name);
            if (g_MissingTexturesReported) std::cerr << "[Texture] Missing block texture: " << path << std::endl;
        }
    }
    if (found && packOverride(path).empty()) {
        int w, h, n;
        if (!stbi_info(finalPath.c_str(), &w, &h, &n)) return -1;   // unreadable image
    }
    // The same picture file used by several blocks (or particles) shares one layer
    static std::unordered_map<std::string, int> layerOfFile;
    static int layerGen = -1;                       // (a mod reload starts the layers again)
    if (layerGen != g_SessionGeneration) { layerOfFile.clear(); layerGen = g_SessionGeneration; }
    const std::string fileKey = found ? fs::path(finalPath).lexically_normal().generic_string() : ("?" + path);
    if (auto it = layerOfFile.find(fileKey); it != layerOfFile.end() && it->second < g_NextTextureLayer) return it->second;
    if (g_NextTextureLayer >= MAX_TEXTURE_LAYERS) {
        std::cerr << "[Texture] Out of texture layers (max " << MAX_TEXTURE_LAYERS << "): " << path << std::endl;
        return -1;
    }

    const int layer = g_NextTextureLayer++;
    layerOfFile[fileKey] = layer;
    if (static_cast<int>(g_LayerSources.size()) <= layer) g_LayerSources.resize(layer + 1);
    g_LayerSources[layer] = TextureSource{ path, found ? finalPath : "" };
    uploadLayer(layer, g_LayerSources[layer]);
    return layer;
}

// (Re)uploads a 2D texture at its own size: pack image, else the original.
bool uploadStandard(GLuint texID, const TextureSource& src) {
    std::string file = packOverride(src.requested);
    int width = 0, height = 0, nrChannels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = file.empty() ? nullptr : stbi_load(file.c_str(), &width, &height, &nrChannels, 4);
    if (!data && !src.resolved.empty()) {
        file = src.resolved;
        data = stbi_load(file.c_str(), &width, &height, &nrChannels, 4);
    }
    if (!data) return false;

    glBindTexture(GL_TEXTURE_2D, texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[Texture] Loaded " << file << " (" << width << "x" << height << ")" << std::endl;
    return true;
}

// Every folder a mod was loaded from. Pictures asked for after loading (the HUD, items
// used later) are looked for in all of them, since by then the "current mod" is just
// whichever loaded last.
std::vector<std::string> g_ModFolders;

// Where a picture is. The calling mod's own folder comes first, so a mod always gets its
// own picture even if the game or another folder has a file with the same name.
// "" if nowhere.
std::string findTextureFile(const std::string& path) {
    std::vector<std::string> searchPaths = {
        g_CurrentModPath + "/textures/" + path,
        g_CurrentModPath + "/" + path,
        "../../" + g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path,
        path,
        "textures/" + path,
        "../../textures/" + path,
        "games/" + g_ActiveGameFolder + "/textures/" + path,
        "../../games/" + g_ActiveGameFolder + "/textures/" + path
    };
    for (const auto& dir : g_ModFolders) {
        searchPaths.push_back(dir + "/textures/" + path);
        searchPaths.push_back(dir + "/" + path);
    }
    for (const auto& p : searchPaths) {
        std::error_code ec;
        if (!p.empty() && fs::is_regular_file(p, ec)) return fs::absolute(p, ec).string();
    }
    return indexedTexture(path);   // anywhere in the game or a mod, by name
}

GLuint loadStandardTexture(const std::string& path) {
    std::string finalPath = path;
    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path,
        "games/" + g_ActiveGameFolder + "/textures/" + path,
        "../../games/" + g_ActiveGameFolder + "/textures/" + path
    };
    for (const auto& dir : g_ModFolders) {   // any loaded mod's folder (see g_ModFolders)
        searchPaths.push_back(dir + "/textures/" + path);
        searchPaths.push_back(dir + "/" + path);
    }

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = fs::absolute(p).string();
            found = true;
            break;
        }
    }
    if (!found) {   // anywhere in the game or a mod, by name
        const std::string indexed = indexedTexture(path);
        if (!indexed.empty()) { finalPath = indexed; found = true; }
    }
    if (!found && packOverride(path).empty()) {
        std::cerr << "[Texture] NOT FOUND: " << path << " - no file with that name anywhere in "
                  << textureSearchHint() << std::endl;
        return 0;
    }

    GLuint texID = 0;
    glGenTextures(1, &texID);
    const TextureSource src{ path, found ? finalPath : "" };
    if (!uploadStandard(texID, src)) {
        std::cerr << "[Texture] Failed to decode: " << (found ? finalPath : path) << std::endl;
        glDeleteTextures(1, &texID);
        return 0;
    }
    g_StandardTextureSources[texID] = src;
    return texID;
}

// Re-uploads every texture with the current pack. No restart needed.
void reloadAllTextures() {
    for (int i = 0; i < static_cast<int>(g_LayerSources.size()); ++i) uploadLayer(i, g_LayerSources[i]);
    for (const auto& [id, src] : g_StandardTextureSources) uploadStandard(id, src);
    std::cout << "[Texture] Reloaded " << g_LayerSources.size() << " block textures and "
              << g_StandardTextureSources.size() << " UI textures"
              << (g_ActiveTexturePack.empty() ? " (default)" : " from pack '" + g_ActiveTexturePack + "'")
              << std::endl;
}

std::string readActiveTexturePack() {
    std::ifstream in(texturePacksRoot() / "active_pack.txt");
    std::string name;
    std::getline(in, name);
    while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    std::error_code ec;
    if (!isSafeFolderName(name) || !fs::is_directory(texturePacksRoot() / name, ec)) return "";
    return name;
}

struct TexturePackInfo {
    std::string id, title, author, description, path;
    int images = 0;      // image files in the pack
    int replaces = 0;    // how many of the game's textures it replaces
};

// "Default" first, then every folder in texturepacks/.
std::vector<TexturePackInfo> listTexturePacks() {
    std::vector<TexturePackInfo> packs;
    TexturePackInfo def;
    def.title = "Default";
    def.description = "The game's own textures.";
    packs.push_back(def);

    const fs::path root = texturePacksRoot();
    std::error_code ec;
    fs::create_directories(root, ec);

    std::vector<TexturePackInfo> found;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        TexturePackInfo p;
        p.id = entry.path().filename().string();
        p.path = entry.path().string();

        const fs::path conf = entry.path() / "pack.conf";
        if (fs::exists(conf, ec)) {
            auto kv = readConfFile(conf);
            p.title = kv.count("title") ? kv["title"] : kv["name"];
            p.author = kv["author"];
            p.description = kv["description"];
        }
        if (p.title.empty()) p.title = p.id;

        for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
            if (f.is_regular_file() && f.path().extension() == ".png") ++p.images;
        }

        std::set<std::string> counted;
        auto count = [&](const std::string& requested) {
            if (counted.insert(requested).second && !packOverrideFor(p.id, requested).empty()) ++p.replaces;
        };
        for (const auto& s : g_LayerSources) count(s.requested);
        for (const auto& [id, s] : g_StandardTextureSources) count(s.requested);

        found.push_back(p);
    }
    std::sort(found.begin(), found.end(),
              [](const TexturePackInfo& a, const TexturePackInfo& b) { return a.title < b.title; });
    packs.insert(packs.end(), found.begin(), found.end());
    return packs;
}

// Switches pack ("" = default), saves the choice and reloads every texture.
bool setTexturePack(const std::string& id, std::string& error) {
    std::error_code ec;
    if (!id.empty() && (!isSafeFolderName(id) || !fs::is_directory(texturePacksRoot() / id, ec))) {
        error = "Texture pack not found.";
        return false;
    }
    g_ActiveTexturePack = id;

    fs::create_directories(texturePacksRoot(), ec);
    std::ofstream out(texturePacksRoot() / "active_pack.txt", std::ios::trunc);
    out << id << "\n";
    if (!out) std::cerr << "[Texture] Could not save the chosen texture pack" << std::endl;

    reloadAllTextures();
    return true;
}

void openTexturePacksFolder() {
    std::error_code ec;
    fs::create_directories(texturePacksRoot(), ec);
    std::string abs = fs::absolute(texturePacksRoot(), ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());
#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    (void)std::system(cmd.c_str());
}

// ===========================================================================
// Models: finding model files, and models placed in the world from Lua
// ===========================================================================

// Finds a mod asset (model or texture). Tries the path as given, then the current
// mod's folder (and its models/ or textures/ subfolder), then the game folder.
std::string resolveAssetPath(const std::string& path, const std::string& subfolder) {
    if (path.empty()) return "";
    const std::string file = fs::path(path).filename().string();
    const std::vector<std::string> candidates = {
        path,
        "../../" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/" + subfolder + "/" + file,
        "games/" + g_ActiveGameFolder + "/" + path,
        "../../games/" + g_ActiveGameFolder + "/" + path,
        subfolder + "/" + file,
        "../../" + subfolder + "/" + file,
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (!p.empty() && fs::is_regular_file(p, ec)) return p;
    }
    // Asked for after loading (a command, a timer): by then the "current mod" is whichever loaded
    // last, so every mod's folder is looked in, as for pictures
    for (const std::string& folder : g_ModFolders) {
        for (const std::string& p : { folder + "/" + path, folder + "/" + subfolder + "/" + file }) {
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) return p;
        }
    }
    return "";
}

// A model's pictures: `explicitTex` (load_model's texture / an entity's "texture") or the one the
// file names as its main texture, then any other picture a .bbmodel lists but doesn't hold inside
// (looked for by its file name, like the main one). Embedded ones are already loaded.
void loadModelTextures(AnimatedModel& model, const std::string& explicitTex) {
    const std::string tex = explicitTex.empty() ? model.textureHint : explicitTex;
    if (!tex.empty()) {
        const std::string texPath = resolveAssetPath(tex, "textures");
        model.texture = loadStandardTexture(texPath.empty() ? tex : texPath);
    }
    for (size_t i = 1; i < model.textures.size(); ++i) {
        ModelTexture& t = model.textures[i];
        if (t.id || t.name.empty()) continue;
        const std::string texPath = resolveAssetPath(t.name, "textures");
        if (texPath.empty()) {
            std::cerr << "[Model] Texture " << t.name << " isn't in the model file or the mod's folder: "
                      << "its faces use the main texture" << std::endl;
            continue;
        }
        t.id = loadStandardTexture(texPath);
    }
}

struct ModelInstance {
    std::shared_ptr<AnimatedModel> model;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;        // degrees
    float scale = 1.0f;
    bool visible = true;
};

std::map<int, ModelInstance> g_ModelInstances;   // models placed with engine.add_model
int g_NextModelInstance = 1;
bool g_ThirdPerson = false;                      // F5 camera (the player model is only drawn in third person)
bool g_ThirdPersonFront = false;
bool g_HudHidden = false;                        // F1: no HUD, crosshair or hand (screenshots)
// The name of what's held (above the hotbar, for a moment after switching)
bool g_ShowBlockNames = true;                    // engine.set_block_name_display(false) hides it
uint8_t g_HeldNameFor = 0;                       // the held item whose name is showing
int g_HeldNameSlot = -1;
double g_HeldNameSince = -100.0;                 // when it changed (the name fades after 2 s)                 // ...in front of the player, looking back at them (F5 twice)

// ===========================================================================
// Settings (settings.txt next to the mods folder) and key bindings
//   key_<action> = <GLFW key code>      e.g. key_forward = 87
//   fov = 70, render_distance = 10, vsync = 1
// ===========================================================================

// Mouse buttons can be bound to controls too: they're stored as kMouseKeyBase + the GLFW mouse
// button (1000 = left, 1001 = right, 1002 = middle, 1003+ = side buttons), next to the keyboard
// keys (GLFW key codes all lie below 1000).
constexpr int kMouseKeyBase = 1000;
constexpr int kMouseKeyLast = kMouseKeyBase + GLFW_MOUSE_BUTTON_LAST;
inline bool isMouseKey(int key) { return key >= kMouseKeyBase && key <= kMouseKeyLast; }

struct KeyAction {
    const char* id;
    const char* label;
    int defaultKey;
};

// Every rebindable action, in the order shown on the Controls tab.
const std::vector<KeyAction>& keyActions() {
    static const std::vector<KeyAction> actions = {
        { "forward",   "Walk Forward",   GLFW_KEY_W },
        { "back",      "Walk Backward",  GLFW_KEY_S },
        { "left",      "Strafe Left",    GLFW_KEY_A },
        { "right",     "Strafe Right",   GLFW_KEY_D },
        { "jump",      "Jump",           GLFW_KEY_SPACE },
        { "sneak",     "Sneak",          GLFW_KEY_LEFT_SHIFT },
        { "sprint",    "Sprint",         GLFW_KEY_LEFT_CONTROL },
        { "pick_block", "Pick Block",    kMouseKeyBase + GLFW_MOUSE_BUTTON_MIDDLE },
        { "drop",      "Drop Item",      GLFW_KEY_Q },
        { "inventory", "Open Inventory", GLFW_KEY_E },
        { "chat",      "Open Chat",      GLFW_KEY_T },
        { "command",   "Open Command",   GLFW_KEY_SLASH },
        { "third_person", "Toggle Camera View", GLFW_KEY_F5 },
        { "hide_hud",    "Hide HUD",       GLFW_KEY_F1 },
        { "debug_info",  "Show FPS / Network Info", GLFW_KEY_F3 },
        { "player_list", "List Players",  GLFW_KEY_TAB },
        { "hotbar1",   "Hotbar Slot 1",  GLFW_KEY_1 },
        { "hotbar2",   "Hotbar Slot 2",  GLFW_KEY_2 },
        { "hotbar3",   "Hotbar Slot 3",  GLFW_KEY_3 },
        { "hotbar4",   "Hotbar Slot 4",  GLFW_KEY_4 },
        { "hotbar5",   "Hotbar Slot 5",  GLFW_KEY_5 },
        { "hotbar6",   "Hotbar Slot 6",  GLFW_KEY_6 },
        { "hotbar7",   "Hotbar Slot 7",  GLFW_KEY_7 },
        { "hotbar8",   "Hotbar Slot 8",  GLFW_KEY_8 },
        { "hotbar9",   "Hotbar Slot 9",  GLFW_KEY_9 },
    };
    return actions;
}

std::map<std::string, std::string> g_Settings;
bool g_SettingsInGame = false;   // settings opened from the pause menu (vs the title screen)

fs::path settingsFile() {
    const fs::path mods = globalModsFolder();
    return (mods.has_parent_path() ? mods.parent_path() : fs::path(".")) / "settings.txt";
}

void loadSettings() {
    g_Settings = readConfFile(settingsFile());
}

void saveSettings() {
    const fs::path file = settingsFile();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            std::cerr << "[Settings] Could not write " << file.string() << std::endl;
            return;
        }
        out << "# Game settings. Edited from the Settings screen.\n";
        for (const auto& [k, v] : g_Settings) out << k << " = " << v << "\n";
    }
    std::error_code ec;
    if (!replaceFile(tmp, file, ec)) {   // the old settings stay as they were
        std::cerr << "[Settings] Could not replace " << file.string() << ": " << ec.message() << std::endl;
    }
}

int settingInt(const std::string& name, int fallback) {
    const auto it = g_Settings.find(name);
    if (it == g_Settings.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

// ===========================================================================
// Multiplayer: player name (settings.txt) and saved servers (servers.txt)
// ===========================================================================

std::string playerName() {
    const auto it = g_Settings.find("player_name");
    return it != g_Settings.end() ? it->second : std::string();
}

// Makes sure settings.txt always has a valid player name (a new one like "Player4821"
// is made and saved the first time). Returns the name.
std::string ensurePlayerName() {
    std::string name = playerName();
    if (net::validPlayerName(name)) return name;
    std::random_device rd;
    name = "Player" + std::to_string(1000 + rd() % 9000);
    g_Settings["player_name"] = name;
    saveSettings();
    std::cout << "[Settings] No player name set - using " << name << std::endl;
    return name;
}

// Changes the player name (settings.txt + chat). Not allowed while online, since the
// server already knows you by your old name.
bool changePlayerName(const std::string& name, std::string& error) {
    if (!net::validPlayerName(name)) {
        error = "Names are 1-16 letters, numbers, _ or -";
        return false;
    }
    if (name != playerName() && (g_Server.running() || g_Client.active())) {
        error = "You can't change your name while playing online";
        return false;
    }
    if (playerName() != name) {
        g_Settings["player_name"] = name;
        saveSettings();
    }
    if (g_ChatSystem) g_ChatSystem->setPlayerName(name);
    return true;
}

// Server settings (Settings > Server): what other players see in their server list.
std::string serverMotd() {
    const auto it = g_Settings.find("server_motd");
    return it != g_Settings.end() ? it->second : std::string("A BlockSmith server");
}
int serverMaxPlayers() { return std::clamp(settingInt("server_max_players", 50), 2, 200); }
int serverPort() {
    const int p = settingInt("server_port", net::kDefaultPort);
    return (p > 0 && p <= 65535) ? p : net::kDefaultPort;
}

// server_icon.png next to settings.txt: shown beside your server in other players' lists.
fs::path serverIconFile() { return settingsFile().parent_path() / "server_icon.png"; }

std::vector<uint8_t> loadServerIcon() {
    std::vector<uint8_t> bytes;
    std::error_code ec;
    const fs::path file = serverIconFile();
    if (!fs::exists(file, ec)) return bytes;
    if (fs::file_size(file, ec) > net::kMaxIconBytes) {
        std::cerr << "[Server] server_icon.png is too big (max " << net::kMaxIconBytes / 1024
                  << " KB - use a 64x64 image)" << std::endl;
        return bytes;
    }
    std::ifstream in(file, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return bytes;
}

// Server icons received from other servers, uploaded as textures (keyed by address).
struct ServerIconTexture { GLuint tex = 0; size_t bytes = 0; uint32_t hash = 0; bool failed = false; };
std::map<std::string, ServerIconTexture> g_ServerIcons;

uint32_t hashBytes(const std::vector<uint8_t>& b) {
    uint32_t h = 2166136261u;
    for (uint8_t c : b) { h ^= c; h *= 16777619u; }
    return h;
}

// Texture for a server's icon, or 0 if it has none / it couldn't be read.
GLuint serverIconTexture(const std::string& address, const std::vector<uint8_t>& png) {
    ServerIconTexture& t = g_ServerIcons[address];
    const uint32_t h = hashBytes(png);
    if (t.bytes == png.size() && t.hash == h) return t.failed ? 0 : t.tex;   // unchanged

    if (t.tex) glDeleteTextures(1, &t.tex);
    t = ServerIconTexture{};
    t.bytes = png.size();
    t.hash = h;
    if (png.empty()) { t.failed = true; return 0; }

    int w = 0, hgt = 0, n = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* d = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &hgt, &n, 4);
    if (!d) { t.failed = true; return 0; }
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hgt, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // keep pixel art crisp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(d);
    return t.tex;
}

// Draws a texture at pixel position (top-left x, y) with the textured UI shader.
void drawUITexture(GLuint program, GLuint tex, float x, float y, float w, float h, int fbW, int fbH) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    const float x0 = x / fbW * 2.0f - 1.0f, x1 = (x + w) / fbW * 2.0f - 1.0f;
    const float y0 = 1.0f - y / fbH * 2.0f, y1 = 1.0f - (y + h) / fbH * 2.0f;   // y0 = top
    const float v[24] = { x0, y0, 0, 0,  x1, y0, 1, 0,  x1, y1, 1, 1,
                          x0, y0, 0, 0,  x1, y1, 1, 1,  x0, y1, 0, 1 };
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(program, "u_Texture"), 0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

fs::path serversFile() { return settingsFile().parent_path() / "servers.txt"; }

// ===========================================================================
// My Servers: dedicated servers set up from the game (servers/<name>/server.conf),
// run by BlockSmithServer in their own window.
// ===========================================================================
fs::path g_ExeDir;   // folder of the game's .exe (set in main)

// ---------------------------------------------------------------------------
// lua_api.txt: what this build of the engine (with the mods that loaded) offers modders.
// Written after the mods load, from what really exists in Lua, so it is never out of date.
// The full guide with explanations is docs/MODDING.md.
// ---------------------------------------------------------------------------
// Custom end marker (LUA): the Lua below contains a closing bracket followed by a quote,
// which would end a plain raw string early.
const char* kApiReferenceLua = R"LUA(
local out = {}
local function add(s) out[#out + 1] = s end
local function sorted_keys(t, want)
    local k = {}
    if type(t) ~= "table" then return k end
    -- next, not pairs: a table's own keys, without its __pairs (some engine objects - model
    -- animations - refuse to be listed that way)
    for name, v in next, t do
        if type(name) == "string" and want(v) then k[#k + 1] = name end
    end
    table.sort(k)
    return k
end
local is_fn = function(v) return type(v) == "function" end
local not_fn = function(v) return type(v) ~= "function" end

add("BlockSmith - Lua API reference (written by the engine at startup)")
add("Explanations and examples: docs/MODDING.md. Fluids: docs/FLUIDS.md")
add("")

-- 1. Engine tables
local engine_tables = { "engine", "world", "ui", "hotbar", "chat", "sky", "clouds", "fog", "underwater", "menu", "sound" }
local known = {}
local total = 0
add("=== Functions the engine gives you ===")
for _, tname in ipairs(engine_tables) do
    known[tname] = true
    local t = rawget(_G, tname)
    if type(t) == "table" then
        local fns = sorted_keys(t, is_fn)
        local vals = sorted_keys(t, not_fn)
        add("")
        add(tname .. " (" .. #fns .. " functions)")
        for _, f in ipairs(fns) do add("  " .. tname .. "." .. f .. "(...)"); total = total + 1 end
        for _, v in ipairs(vals) do add("  " .. tname .. "." .. v .. "  [" .. type(t[v]) .. "]") end
    end
end
add("")
add("Chunk (passed to generate_chunk): chunk:set_block(x, y, z, id), chunk:get_block(x, y, z),")
add("  chunk:set_block_with_meta(x, y, z, id, meta), chunk:set_metadata(x, y, z, meta), chunk:get_metadata(x, y, z)")

-- 2. Hooks: global functions the engine calls if a mod defines them
local hooks = {
    { "generate_chunk",           "(chunk, cx, cz)  build a new 16x16 chunk's terrain (runs on generator threads)" },
    { "set_world_seed",           "(seed)  the world's seed, before any generate_chunk" },
    { "on_update",                "(dt)  every frame" },
    { "on_chat_command",          "(text, player, is_operator, reply) -> true if handled  a /command" },
    { "on_player_drop",           "(item, count) -> true stops dropping it (Q / Ctrl+Q)" },
    { "can_pick_up_item",         "(item, count) -> false: the player can't pick items up now (dead...)" },
    { "on_chat_complete",         "(words, word) -> { ... }  Tab in chat: what fits `word` after the command words `words`" },
    { "on_server_action",         "(action, a, b) -> true if done  the server asks: give (item, count), clear, kill (reason)" },
    { "on_game_mode",             "(mode, is_operator)  the server set your game mode (mode nil = left the server)" },
    { "on_shared_changed",        "(key, value)  a shared value changed (engine.set_shared; e.g. the weather)" },
    { "render_hud",               "(W, H)  draw HUD pieces every frame in game (hearts, hunger...)" },
    { "on_player_click",          "(\"left\"|\"right\") -> true if used  before digging/placing (death screen...)" },
    { "on_pick_block",            "(x, y, z, item, meta, block) -> true if handled  pick block (middle mouse); else creative picks it" },
    { "handle_settings_mouse",    "(code) -> true if used  a mouse button while Settings is open (1000 + button, for keybinds)" },
    { "on_dig_block",             "(x, y, z, block, item) -> true if you handled the drops  (survival digging)" },
    { "get_dig_time",             "(block, item) -> seconds  override how long a block takes to dig" },
    { "on_right_click_block",     "(x, y, z, block, held_item, sneaking, repeat) -> true if handled" },
    { "on_block_broken",          "(x, y, z, block, meta, player)  a player broke a block (after it's gone)" },
    { "on_block_placed",          "(x, y, z, block, player)  a player placed a block" },
    { "can_edit_block",           "(player, x, y, z, old_block, new_block, old_meta, new_meta) -> false[, reason] to refuse  a player's change (block or metadata), on servers and LAN games" },
    { "on_player_join",           "(name, info)  someone joined (info: operator, mode, new_account)" },
    { "on_player_leave",          "(name, x, y, z)  someone left" },
    { "on_player_chat",           "(name, text) -> false to drop it, or new text  a chat line before everyone sees it" },
    { "on_server_start",          "()  the server started (in the game: you started hosting)" },
    { "on_server_stop",           "()  the server is stopping (in the game: you stopped hosting)" },
    { "on_use_hoe",               "(x, y, z, block) -> true if used  right-click with a hoe (tool_type \"hoe\")" },
    { "on_use_scythe",            "(x, y, z, block) -> true if it reaped (else it digs)  left-click with a scythe" },
    { "on_dimension_change",      "(to, from)  this game's player is now in another dimension (\"overworld\" or \"mod:name\")" },
    { "on_player_change_dimension", "(name, from, to)  a player went to another dimension (where the world runs)" },
    { "on_mod_message",           "(name, channel, data)  a player's game sent the server's mods a message (engine.send_to_server)" },
    { "on_server_message",        "(channel, data)  the server's mods sent this game a message (engine.send_to_player / send_to_all)" },
    { "on_player_harvest",        "(name, item, x, y, z)  a joined player's engine.request_harvest (the scythe): the server decides" },
    { "update_player_animation",  "(dt, is_moving, pitch)" },
    { "get_water_color",          "(x, z) -> { r, g, b, deep_r, deep_g, deep_b }  underwater fog colour here" },
    { "on_char_input",            "(codepoint)  typed text (unicode)" },
    { "on_scroll_input",          "(dx, dy)  mouse wheel" },
    { "toggle_main_inventory",    "() -> true if now open  the inventory key in survival (frees the mouse)" },
    { "handle_inventory_click",   "(mx, my, W, H, \"left\"|\"right\", shift)  click while your inventory is open" },
    { "handle_inventory_key",     "(key, action, mods) -> true if used  a key while your screen is open (before E / Escape)" },
    { "handle_inventory_scroll",  "(dy)  mouse wheel while your screen is open (instead of the hotbar)" },
    { "on_hotbar_full",           "(item, count) -> true if stored  a dug item didn't fit in the hotbar" },
    { "render_pause_menu",        "(W, H)  draw the pause menu" },
    { "handle_pause_click",       "(mx, my, W, H)" },
    { "render_settings",          "(W, H)  settings screen (also handle_settings_click/_key/_char/_scroll)" },
    { "render_world_select",      "(W, H)  world list (also handle_world_select_click/_key/_char/_scroll)" },
    { "render_multiplayer",       "(W, H)  server list (also handle_multiplayer_click/_key/_char/_scroll)" },
    { "render_mods",              "(W, H)  mods screen (also handle_mods_click/_key/_scroll)" },
    { "render_texture_packs",     "(W, H)  texture packs (also handle_texture_packs_click/_key/_scroll)" },
    { "render_player_list",       "(W, H)  player list (hold Tab)" },
    { "render_creative_inventory","(W, H)  creative inventory (also handle_creative_click/_key/_char/_scroll)" },
    { "render_inventory",         "(W, H)  survival inventory" },
    { "on_settings_opened",       "()  also on_world_select_opened, on_multiplayer_opened, on_mods_opened," },
    { "on_creative_opened",       "()  on_texture_packs_opened, on_creative_closed" },
}
add("")
add("=== Hooks: functions YOU define; the engine calls them ===")
add("  [x] = a loaded mod defines it")
for _, h in ipairs(hooks) do
    local used = type(rawget(_G, h[1])) == "function" and "[x]" or "[ ]"
    add("  " .. used .. " " .. h[1] .. h[2])
end

-- 3. Tables mods made for other mods (e.g. buckets.register)
local lua_std = { _G = true, string = true, table = true, math = true, package = true, io = true,
                  os = true, coroutine = true, debug = true, utf8 = true, bit32 = true, jit = true }
local mod_tables = sorted_keys(_G, function(v) return type(v) == "table" end)
local any = false
for _, tname in ipairs(mod_tables) do
    if not known[tname] and not lua_std[tname] then
        local fns = sorted_keys(_G[tname], is_fn)
        if #fns > 0 then
            if not any then add(""); add("=== APIs that loaded mods offer to other mods ==="); any = true end
            for _, f in ipairs(fns) do add("  " .. tname .. "." .. f .. "(...)") end
        end
    end
end

-- 4. Fluids
if engine and engine.list_fluids then
    add("")
    add("=== Fluids ===")
    for _, f in ipairs(engine.list_fluids()) do
        add(string.format("  %s: source %s, flowing %s, spreads %d, step %.2f s%s", f.name, f.source,
            (f.flowing ~= "" and f.flowing) or "-", f.spread, f.tick, f.infinite and ", infinite" or ""))
    end
end
return table.concat(out, "\n"), total
)LUA";

// Writes lua_api.txt next to the game (or in the working folder). Returns the path, "" on failure.
std::string writeApiReference(sol::state& lua) {
    try {
        sol::protected_function_result r = lua.safe_script(kApiReferenceLua, sol::script_pass_on_error);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[API] Couldn't build lua_api.txt: " << err.what() << std::endl;
            return "";
        }
        std::string text = r.get<std::string>(0);
        const int functions = r.get<int>(1);

        // Every registered block (only C++ knows the full list)
        std::string blockList;
        int blocks = 0;
        auto& reg = BlockRegistry::getInstance();
        for (int id = 1; id < 256; ++id) {
            const auto& b = reg.getBlock(static_cast<uint8_t>(id));
            if (b.name.empty() || b.name == "air" || reg.getBlockId(b.name) != id) continue;
            blockList += "  " + std::to_string(id) + "  " + b.name + "  (" + (b.pattern.empty() ? "cube" : b.pattern)
                       + (b.isWalkable ? ", solid" : "") + (b.isTransparent ? ", transparent" : "")
                       + (b.inventoryImage.empty() ? "" : ", item") + ")\n";
            ++blocks;
        }
        text += "\n\n=== Blocks (" + std::to_string(blocks) + " of 255) ===\n" + blockList;

        fs::path path = (g_ExeDir.empty() ? fs::path(".") : g_ExeDir) / "lua_api.txt";
        std::ofstream f(path);
        if (!f) { path = "lua_api.txt"; f.open(path); }
        if (!f) return "";
        f << text << "\n";
        std::cout << "[API] " << functions << " Lua functions, " << blocks << " blocks - see " << path.string() << std::endl;
        return path.string();
    } catch (const std::exception& e) {
        std::cerr << "[API] Couldn't write lua_api.txt: " << e.what() << std::endl;
        return "";
    }
}

// ---------------------------------------------------------------------------
// World generator threads (see ChunkGenerator.h): each gets its own Lua with the same
// mod files, but only what a world generator needs. Drawing, sound, chat, the world and
// the player belong to the main thread, so everything else is a harmless no-op there.
// ---------------------------------------------------------------------------
const char* kWorkerNoopLua = R"(
local function noop_table()
    return setmetatable({}, {
        __index = function(_, _) return noop_table() end,
        __call = function() return nil end,
    })
end
for _, name in ipairs({ "ui", "hotbar", "sky", "clouds", "fog", "underwater", "menu", "chat",
                        "player", "inventory", "audio", "sound", "input", "settings", "net", "remote",
                        "entities", "creative" }) do
    if _G[name] == nil then _G[name] = noop_table() end
end
local function fallback(t)
    return setmetatable(t, { __index = function(_, _) return function() return nil end end })
end
fallback(engine)
fallback(world)
)";

// What mods' Lua may load (LuaSandbox.h): require / dofile / loadfile only inside the game's
// folder, the global mods folder and the shared module folders (lualib/ next to mods/, and
// games/<game>/lualib/). No native modules, no precompiled bytecode. Same rules as the server.
// Online content settings (settings.txt): remote_content = true|false (default true) and
// remote_domains = sites approved besides GitHub (comma separated; "*.example.com" or "example.com")
remote::Policy remotePolicyFromSettings() {
    remote::Policy p;
    const auto on = g_Settings.find("remote_content");
    p.enabled = on == g_Settings.end() || (on->second != "false" && on->second != "0");
    const auto d = g_Settings.find("remote_domains");
    if (d != g_Settings.end()) {
        std::string item;
        std::stringstream ss(d->second);
        while (std::getline(ss, item, ',')) {
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t\r") + 1);
            for (char& c : item) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (item.rfind("https://", 0) == 0) item = item.substr(8);
            if (item.rfind("*.", 0) == 0) item = item.substr(2);
            while (!item.empty() && item.back() == '/') item.pop_back();
            if (!item.empty()) p.extraDomains.push_back(item);
        }
    }
    return p;
}

void installModSandbox(sol::state& lua) {
    const std::string game = g_LoadedGameFolder.empty() ? g_ActiveGameFolder : g_LoadedGameFolder;
    std::vector<fs::path> gameRoots, modRoots, libs;
    auto existing = [](const std::string& p, std::vector<fs::path>& into) {
        for (const std::string& c : {p, "../" + p, "../../" + p}) {
            std::error_code ec;
            if (fs::is_directory(c, ec)) into.push_back(c);
        }
    };
    existing("games/" + game, gameRoots);
    existing("mods", modRoots);
    existing("games/" + game + "/mods", modRoots);
    modRoots.push_back(globalModsFolder());
    existing("lualib", libs);
    existing("games/" + game + "/lualib", libs);
    luasandbox::install(lua, gameRoots, modRoots, libs);
    luasandbox::installTimeLimitHook(lua);
}

std::unique_ptr<sol::state> makeWorldGenLua(const std::vector<fs::path>& modFiles) {
    auto lua = std::make_unique<sol::state>();
    lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table,
                        sol::lib::coroutine, sol::lib::utf8);
    installModSandbox(*lua);
    (*lua)["print"] = [](sol::variadic_args) {};
    lua->new_usertype<ChunkData>("Chunk",
        "set_block", &ChunkData::setBlock, "setBlock", &ChunkData::setBlock,
        "get_block", &ChunkData::getLocalBlock, "getBlock", &ChunkData::getLocalBlock,
        "set_block_with_meta", &ChunkData::setBlockWithMeta,
        "set_metadata", &ChunkData::setMetadata, "get_metadata", &ChunkData::getLocalMetadata,
            // chunk:set_tint(x, z, r, g, b [, "grass" | "foliage"]) - biome colour (0..255 or 0..1)
            "set_tint", [](ChunkData& c, int x, int z, float r, float g, float b, sol::optional<std::string> channel) {
                const bool bytes = r > 1.0f || g > 1.0f || b > 1.0f;
                auto to8 = [bytes](float v) { return static_cast<uint32_t>(std::clamp(bytes ? v : v * 255.0f, 0.0f, 255.0f)); };
                c.setColumnTint(x, z, (to8(r) << 16) | (to8(g) << 8) | to8(b), channel && *channel == "foliage" ? 1 : 0);
            }
    );
    auto engine = lua->create_named_table("engine");
    engine["register_block"] = [](sol::variadic_args) {};   // the main thread already did it
    engine["get_block_id"] = [](const std::string& name) { return BlockRegistry::getInstance().getBlockId(name); };
    engine["set_block"] = [](sol::variadic_args) {};
    engine["get_block_name"] = [](sol::variadic_args) { return std::string("air"); };
    engine["is_server"] = []() { return false; };
    engine["is_world_generator"] = []() { return true; };
    auto world = lua->create_named_table("world");
    world["get_caves_enabled"] = []() { return CaveGenerator::isEnabled(); };
    world["set_caves_enabled"] = [](sol::variadic_args) {};   // the main thread's setting wins
    world["set_cave_settings"] = [](sol::variadic_args) {};   // same
    world["get_cave_settings"] = [](sol::this_state ts) { return caveSettingsTable(ts); };
    dims::installLua(*lua);   // (before the noop tables: dimensions.register is real here)
    lua->script(kWorkerNoopLua);
    for (const auto& file : modFiles) {
        (*lua)["g_CurrentModPath"] = file.parent_path().string();
        // errors shown by the main thread (here, where only world generation works, they're noise)
        try { lua->safe_script_file(file.string(), sol::script_pass_on_error); } catch (const std::exception&) {}
    }
    diag::installLuaTraceback(lua->lua_state());   // generating chunks: errors with their Lua stack
    return lua;
}

fs::path serverProfilesRoot() { return settingsFile().parent_path() / "servers"; }

int settingIntFrom(const std::map<std::string, std::string>& kv, const std::string& key, int fallback) {
    const auto it = kv.find(key);
    if (it == kv.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

std::vector<std::string> listServerProfiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(serverProfilesRoot(), ec)) {
        if (e.is_directory() && fs::exists(e.path() / "server.conf")) out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Same settings, defaults and comments as BlockSmithServer's own server.conf.
struct ServerSettingInfo { const char* key; const char* defaultValue; const char* comment; };
const std::vector<ServerSettingInfo>& serverSettingInfo() {
    static const std::vector<ServerSettingInfo> info = {
        {"name", "My Server", "Shown on the LAN list"},
        {"motd", "{gold}{b}A BlockSmith server{/b}", "Description in server lists. {gold} {#ff8800} {b}bold{/b} {n} = 2nd line"},
        {"icon", "server_icon.png", "Icon file in this folder (PNG, 64x64)"},
        {"game", "craft_and_ruin", "Game to run (folder in games/)"},
        {"world", "server_world", "World folder in saves/"},
        {"seed", "", "Seed for a new world (empty = random)"},
        {"port", "30000", "UDP port players connect to"},
        {"max_players", "50", "Most players at once (up to 4000)"},
        {"view_distance", "8", "Furthest players may see, in chunks"},
        {"spawn", "8 8", "Where new players appear (x z)"},
        {"welcome", "Welcome to the server, {player}!", "Sent to each player who joins"},
        {"whitelist", "false", "Only players in whitelist.txt may join"},
        {"gamemode", "survival", "survival or creative. Only operators can change a player's mode (/gamemode)"},
        {"disabled_mods", "", "Mods this server doesn't load (comma separated mod folder names)"},
        {"tick_rate", "30", "Updates per second (20-60)"},
        {"autosave_seconds", "60", ""},
    };
    return info;
}

std::map<std::string, std::string> readServerProfile(const std::string& profile) {
    auto kv = readConfFile(serverProfilesRoot() / profile / "server.conf");
    for (const auto& s : serverSettingInfo()) {
        if (!kv.count(s.key)) kv[s.key] = s.defaultValue;
    }
    return kv;
}

bool writeServerProfile(const std::string& profile, const std::map<std::string, std::string>& values,
                        std::string& error) {
    const fs::path dir = serverProfilesRoot() / profile;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream out(dir / "server.conf", std::ios::trunc);
    if (!out) { error = "Could not write " + (dir / "server.conf").string(); return false; }
    out << "# BlockSmith dedicated server - set up from the game's My Servers screen.\n"
        << "# You can also edit this file by hand (then type 'reload' in the server window).\n\n";
    for (const auto& s : serverSettingInfo()) {
        auto it = values.find(s.key);
        std::string v = it != values.end() ? it->second : s.defaultValue;
        v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == '\n' || c == '\r'; }), v.end());
        if (s.comment[0]) out << "# " << s.comment << "\n";
        out << s.key << " = " << v << "\n";
    }
    return true;
}

// A safe folder name for a new server ("My Server!" -> "My Server")
std::string serverProfileFolder(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.size() > 32) out.resize(32);
    return out;
}

fs::path findGameDir(const std::string& game) {
    for (const std::string& base : {std::string("games/"), std::string("../../games/")}) {
        std::error_code ec;
        if (fs::is_directory(base + game, ec)) return fs::path(base + game);
    }
    return {};
}

// Every mod a server running `game` could load: the game's own mods and the global mods/ folder.
struct ServerModEntry {
    std::string id, title, description, source;
    bool required = false;
};
std::vector<ServerModEntry> listModsForGame(const std::string& game) {
    std::vector<ServerModEntry> out;
    std::set<std::string> seen;
    auto scan = [&](const fs::path& root, const std::string& source) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec)) return;
        std::vector<fs::directory_entry> entries;
        for (const auto& e : fs::directory_iterator(root, ec)) entries.push_back(e);
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
        for (const auto& e : entries) {
            ServerModEntry m;
            if (e.is_directory()) m.id = e.path().filename().string();
            else if (e.path().extension() == ".lua") m.id = e.path().stem().string();
            else continue;
            if (!seen.insert(m.id).second) continue;
            m.source = source;
            m.title = m.id;
            if (e.is_directory()) {
                auto conf = readConfFile(e.path() / "mod.conf");
                if (!conf["title"].empty()) m.title = conf["title"];
                else if (!conf["name"].empty()) m.title = conf["name"];
                m.description = conf["description"];
                m.required = modIsRequired(e.path(), m.id);
            } else {
                m.required = (m.id == "base");
            }
            out.push_back(m);
        }
    };
    const fs::path gameDir = findGameDir(game);
    if (!gameDir.empty()) scan(gameDir / "mods", "Game");
    scan(globalModsFolder(), "Global");
    return out;
}

// BlockSmithServer.exe is built next to the game's .exe; also check the usual build folders
// (and the server's old name, CraftRuinServer, from before the engine was named).
// Firewall updates Windows was still applying when asked (Firewall.h): rule -> the setting to
// record once it finishes. Polled from the main loop (pollFirewallUpdates).
std::map<std::string, std::string> g_FirewallWatch;

void reportFirewallResult(FirewallStatus st, const std::string& settingKey, const std::string& ruleName) {
    if (st == FirewallStatus::Allowed) {
        g_Settings[settingKey] = "1";
        saveSettings();
        std::cout << "[Network] Firewall rule added for " << ruleName << std::endl;
    } else if (st == FirewallStatus::Pending) {
        g_FirewallWatch[ruleName] = settingKey;   // recorded when Windows finishes, not before
        std::cout << "[Network] Windows is still updating the firewall for " << ruleName << std::endl;
        if (g_ChatSystem) {
            g_ChatSystem->addMessage("Windows is still updating the firewall for " + ruleName + "...",
                                     glm::vec3(1.0f, 1.0f, 0.4f));
        }
    } else if (st == FirewallStatus::Failed) {
        std::cout << "[Network] Firewall rule not added (declined) - other PCs may not be able to join" << std::endl;
        if (g_ChatSystem) {
            g_ChatSystem->addMessage("Firewall not changed - if friends can't join, allow " + ruleName +
                                     " in Windows Security > Firewall > Allow an app.", glm::vec3(1.0f, 0.7f, 0.4f));
        }
    }
}

// Once a second: did a pending firewall update finish?
void pollFirewallUpdates(float dt) {
    static float wait = 0.0f;
    if (g_FirewallWatch.empty() || (wait -= dt) > 0.0f) return;
    wait = 1.0f;
    for (auto it = g_FirewallWatch.begin(); it != g_FirewallWatch.end();) {
        const FirewallStatus st = firewallStatus(it->first);
        if (st == FirewallStatus::Pending) { ++it; continue; }
        const std::string rule = it->first, key = it->second;
        it = g_FirewallWatch.erase(it);
        if (st != FirewallStatus::Idle) reportFirewallResult(st, key, rule);
    }
}

// The first time something is hosted here, ask Windows Firewall to let other PCs in (one
// "allow" prompt). Without it the LAN list shows the game but joining it times out.
void askFirewallOnce(const std::string& settingKey, const std::string& program, const std::string& ruleName) {
#ifdef _WIN32
    if (g_Settings[settingKey] == "1") return;
    if (g_FirewallWatch.count(ruleName)) return;   // an update for it is still running: no second prompt
    if (g_ChatSystem) {
        g_ChatSystem->addMessage("Windows will ask to let " + ruleName + " through the firewall, so other PCs on your network can join.",
                                 glm::vec3(1.0f, 1.0f, 0.4f));
    }
    reportFirewallResult(allowThroughFirewall(program, ruleName), settingKey, ruleName);
#else
    (void)settingKey; (void)program; (void)ruleName;
#endif
}

fs::path findServerExe() {
#ifdef _WIN32
    const std::vector<std::string> names = {"BlockSmithServer.exe", "CraftRuinServer.exe"};
#else
    const std::vector<std::string> names = {"BlockSmithServer", "CraftRuinServer"};
#endif
    std::vector<fs::path> places = {g_ExeDir, ".", "build/Release", "build/Debug", "build", "../Release", "../Debug"};
    for (const auto& exe : names) {
        for (const auto& p : places) {
            std::error_code ec;
            if (!p.empty() && fs::exists(p / exe, ec)) return fs::absolute(p / exe, ec);
        }
    }
    return {};
}

// Starts a server in its own console window (it keeps running if the game is closed).
bool startServerProfile(const std::string& profile, std::string& error) {
    const fs::path exe = findServerExe();
    if (exe.empty()) {
        error = "BlockSmithServer isn't built yet - build the BlockSmithServer target";
        return false;
    }
    std::error_code ec;
    const fs::path dir = fs::absolute(serverProfilesRoot() / profile, ec);
    const fs::path conf = dir / "server.conf";
    if (!fs::exists(conf, ec)) { error = "That server has no settings file"; return false; }
    fs::remove(dir / "stop.request", ec);
    auto quoted = [](std::string s) {
        s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
        return "\"" + s + "\"";
    };
    const std::string cwd = fs::current_path(ec).string();   // where games/ mods/ saves/ are found
#if defined(_WIN32)
    const std::string cmd = "start " + quoted("BlockSmith Server - " + profile) + " /D " + quoted(cwd) + " " +
                            quoted(exe.string()) + " --config " + quoted(conf.string());
#else
    const std::string cmd = quoted(exe.string()) + " --config " + quoted(conf.string()) + " > " +
                            quoted((dir / "server.log").string()) + " 2>&1 < /dev/null &";
#endif
    std::cout << "[Servers] Starting '" << profile << "': " << cmd << std::endl;
    if (std::system(cmd.c_str()) != 0) {
        error = "Couldn't start the server program";
        return false;
    }
    return true;
}

// Asks a running server to save and stop (it checks for this file every second).
void stopServerProfile(const std::string& profile) {
    std::ofstream(serverProfilesRoot() / profile / "stop.request") << "stop\n";
}

void openFolderInExplorer(const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string abs = fs::absolute(folder, ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());
#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    std::system(cmd.c_str());
}

struct SavedServer { std::string name, address; bool pinned = false; };

// servers.txt: one server per line, "name|address", or "name|address|pinned" for one pinned to the top
std::vector<SavedServer> loadServers() {
    std::vector<SavedServer> out;
    std::ifstream in(serversFile());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t bar = line.find('|');
        if (line.empty() || line[0] == '#' || bar == std::string::npos) continue;
        std::string rest = line.substr(bar + 1);
        bool pinned = false;
        const size_t bar2 = rest.find('|');
        if (bar2 != std::string::npos) {
            pinned = rest.substr(bar2 + 1) == "pinned";
            rest = rest.substr(0, bar2);
        }
        out.push_back({line.substr(0, bar), rest, pinned});
    }
    return out;
}

void saveServers(const std::vector<SavedServer>& servers) {
    std::ofstream out(serversFile(), std::ios::trunc);
    if (!out) {
        std::cerr << "[Multiplayer] Could not write " << serversFile().string() << std::endl;
        return;
    }
    out << "# Saved servers (name|address, then |pinned if pinned to the top). Edited from the Multiplayer screen.\n";
    for (const auto& s : servers) {
        std::string name = s.name, address = s.address;
        for (auto* str : {&name, &address}) {
            str->erase(std::remove_if(str->begin(), str->end(),
                                      [](char ch) { return ch == '|' || ch == '\n' || ch == '\r'; }),
                       str->end());
        }
        out << name << "|" << address << (s.pinned ? "|pinned" : "") << "\n";
    }
}

const KeyAction* findKeyAction(const std::string& id) {
    for (const auto& a : keyActions()) if (id == a.id) return &a;
    return nullptr;
}

// Key currently bound to an action (GLFW_KEY_UNKNOWN if the action doesn't exist).
int keyForAction(const std::string& action) {
    const KeyAction* a = findKeyAction(action);
    if (!a) return GLFW_KEY_UNKNOWN;
    return settingInt("key_" + action, a->defaultKey);
}

// True while the key bound to `action` is held. Use this instead of glfwGetKey for game controls.
bool isActionDown(GLFWwindow* window, const std::string& action) {
    const int key = keyForAction(action);
    if (isMouseKey(key)) return glfwGetMouseButton(window, key - kMouseKeyBase) == GLFW_PRESS;
    return key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST && glfwGetKey(window, key) == GLFW_PRESS;
}

std::string keyName(int key) {
    if (key == GLFW_KEY_UNKNOWN) return "None";
    if (isMouseKey(key)) {
        switch (key - kMouseKeyBase) {
            case GLFW_MOUSE_BUTTON_LEFT:   return "Left Mouse";
            case GLFW_MOUSE_BUTTON_RIGHT:  return "Right Mouse";
            case GLFW_MOUSE_BUTTON_MIDDLE: return "Middle Mouse";
            default: return "Mouse " + std::to_string(key - kMouseKeyBase + 1);
        }
    }
    if (const char* n = glfwGetKeyName(key, 0)) {
        std::string s(n);
        for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return s;
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return "Numpad " + std::to_string(key - GLFW_KEY_KP_0);
    switch (key) {
        case GLFW_KEY_SPACE:         return "Space";
        case GLFW_KEY_LEFT_SHIFT:    return "Left Shift";
        case GLFW_KEY_RIGHT_SHIFT:   return "Right Shift";
        case GLFW_KEY_LEFT_CONTROL:  return "Left Ctrl";
        case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case GLFW_KEY_LEFT_ALT:      return "Left Alt";
        case GLFW_KEY_RIGHT_ALT:     return "Right Alt";
        case GLFW_KEY_TAB:           return "Tab";
        case GLFW_KEY_ENTER:         return "Enter";
        case GLFW_KEY_KP_ENTER:      return "Numpad Enter";
        case GLFW_KEY_BACKSPACE:     return "Backspace";
        case GLFW_KEY_CAPS_LOCK:     return "Caps Lock";
        case GLFW_KEY_UP:            return "Up";
        case GLFW_KEY_DOWN:          return "Down";
        case GLFW_KEY_LEFT:          return "Left";
        case GLFW_KEY_RIGHT:         return "Right";
        case GLFW_KEY_INSERT:        return "Insert";
        case GLFW_KEY_DELETE:        return "Delete";
        case GLFW_KEY_HOME:          return "Home";
        case GLFW_KEY_END:           return "End";
        case GLFW_KEY_PAGE_UP:       return "Page Up";
        case GLFW_KEY_PAGE_DOWN:     return "Page Down";
        default:                     return "Key " + std::to_string(key);
    }
}

bool setKeybind(const std::string& action, int key, std::string& error) {
    if (!findKeyAction(action)) { error = "Unknown action."; return false; }
    if (key == GLFW_KEY_ESCAPE) { error = "Escape is reserved for menus."; return false; }
    if (isMouseKey(key)) {
        // Controls the game reads every frame can be on a mouse button; the rest (chat, inventory,
        // hotbar slots...) are keyboard-only
        static const std::set<std::string> mouseOk = { "forward", "back", "left", "right", "jump",
                                                       "sneak", "sprint", "pick_block" };
        if (!mouseOk.count(action)) { error = "That control needs a keyboard key."; return false; }
        const int b = key - kMouseKeyBase;
        if (b == GLFW_MOUSE_BUTTON_LEFT || b == GLFW_MOUSE_BUTTON_RIGHT) {
            error = "Left and right mouse dig and place.";
            return false;
        }
    } else if (key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST) { error = "That key can't be used."; return false; }
    g_Settings["key_" + action] = std::to_string(key);
    saveSettings();
    return true;
}

void resetKeybinds() {
    for (auto it = g_Settings.begin(); it != g_Settings.end();) {
        if (it->first.rfind("key_", 0) == 0) it = g_Settings.erase(it);
        else ++it;
    }
    saveSettings();
}

// OpenGL errors explained by the graphics driver itself (KHR_debug, where the driver has it):
// which call failed and why, in the log as "[GL] ...". Only errors (and the first few of each),
// so it costs nothing in a normal frame. Loaded by hand: the GL loader here doesn't include it.
#ifndef APIENTRY
#define APIENTRY
#endif
typedef void (APIENTRY* GLDebugProcBS)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                       const GLchar* message, const void* userParam);
typedef void (APIENTRY* GLDebugMessageCallbackBS)(GLDebugProcBS callback, const void* userParam);
static void APIENTRY glErrorReport(GLenum, GLenum type, GLuint id, GLenum, GLsizei, const GLchar* message, const void*) {
    if (type != 0x824C /* GL_DEBUG_TYPE_ERROR */) return;
    static std::map<GLuint, int> seen;
    if (++seen[id] > 3 || seen.size() > 40) return;          // (the first few of each kind)
    std::cerr << "[GL] " << message << std::endl;
}
static void installGLErrorReporting() {
    auto cb = reinterpret_cast<GLDebugMessageCallbackBS>(glfwGetProcAddress("glDebugMessageCallback"));
    if (!cb) cb = reinterpret_cast<GLDebugMessageCallbackBS>(glfwGetProcAddress("glDebugMessageCallbackARB"));
    if (!cb) return;
    glEnable(0x92E0);   // GL_DEBUG_OUTPUT
    glEnable(0x8242);   // GL_DEBUG_OUTPUT_SYNCHRONOUS: reported during the call that failed
    cb(glErrorReport, nullptr);
    while (glGetError() != GL_NO_ERROR) {}
}

// Video settings: value limits, current value, and applying them.
struct VideoSetting { const char* id; int minV, maxV, step; };
const VideoSetting kVideoSettings[] = {
    { "fov",             30, 110, 5 },
    { "render_distance",  2,  16, 1 },
    { "vsync",            0,   1, 1 },
    { "main_hand",        0,   1, 1 },   // 0 = right, 1 = left (left-handed players)
    { "brightness",       0, 100, 5 },   // 0 = moody, 50 = the game's own, 100 = bright (Minecraft's gamma)
    { "light_quality",    0,   2, 1 },   // traced lighting (shadows of lamps and spotlights): 0 off, 1 low, 2 high
    { "bloom",            0,   1, 1 },   // bright things glow
    { "filmic",           0,   1, 1 },
    { "artnet",           0,   2, 1 },   // Art-Net (DMX) input: 0 off, 1 this computer only, 2 the network   // filmic exposure for Lighting Low/High (a film curve; the eye adjusts)
    { "volume_master",    0, 100, 5 },
    { "volume_music",     0, 100, 5 },
    { "volume_effects",   0, 100, 5 },
    { "volume_ambient",   0, 100, 5 },
};

const VideoSetting* findVideoSetting(const std::string& id) {
    for (const auto& s : kVideoSettings) if (id == s.id) return &s;
    return nullptr;
}

int currentVideoSetting(const std::string& id) {
    if (id == "fov") return static_cast<int>(std::lround(g_FieldOfView));
    if (id == "render_distance") return g_WorldInstance ? g_WorldInstance->renderDistance() : settingInt(id, 10);
    if (id == "vsync") return settingInt(id, 1);
    if (id == "main_hand") return settingInt(id, 0);
    if (id == "brightness") return settingInt(id, 50);
    // (never more than this graphics card can take: GpuCheck.h)
    // (Off unless the player chose a level: the standard look by default, the realistic one opt-in)
    if (id == "light_quality") return std::clamp(settingInt(id, 0), 0, g_GpuCaps.maxLightQuality);
    if (id == "bloom") return g_GpuCaps.bloom ? std::clamp(settingInt(id, 0), 0, 1) : 0;
    if (id == "artnet") return std::clamp(settingInt(id, 0), 0, 2);
    if (id == "filmic") return g_GpuCaps.maxLightQuality > 0 ? std::clamp(settingInt(id, 1), 0, 1) : 0;
    if (id == "volume_music") return settingInt(id, 60);
    if (id.rfind("volume_", 0) == 0) return settingInt(id, 100);
    return 0;
}

void applyVideoSetting(const std::string& id, int value) {
    if (id == "fov") g_FieldOfView = static_cast<float>(value);
    else if (id == "render_distance") { if (g_WorldInstance) g_WorldInstance->setRenderDistance(value); }
    else if (id == "vsync") glfwSwapInterval(value ? 1 : 0);
    else if (id == "brightness") g_LightSettings.playerBrightness = std::clamp(value, 0, 100) / 100.0f;
    else if (id == "light_quality") g_LightQuality = std::clamp(value, 0, g_GpuCaps.maxLightQuality);
    else if (id == "bloom") g_BloomOn = value != 0 && g_GpuCaps.bloom;
    else if (id == "filmic") g_FilmicOn = value != 0;
    else if (id == "artnet") g_ArtNet.setMode(static_cast<ArtNetInput::Mode>(std::clamp(value, 0, 2)), settingInt("artnet_port", 6454));
    else if (id.rfind("volume_", 0) == 0) g_Audio.setVolume(id.substr(7), value / 100.0f);   // master, music...
}

bool setVideoSetting(const std::string& id, int value, std::string& error) {
    const VideoSetting* s = findVideoSetting(id);
    if (!s) { error = "Unknown setting."; return false; }
    value = std::clamp(value, s->minV, s->maxV);
    // Lighting and bloom are the player's own choice: only a click in their Settings changes them.
    // A game's or server's mods (which run in the player's game too) can't switch them on.
    // (Art-Net opens a network port: the player's choice only, too)
    if ((id == "light_quality" || id == "bloom" || id == "filmic" || id == "artnet") && !g_PlayerSettingsClick) {
        error = "Only the player can change this, in Settings";
        return false;
    }
    // What the graphics card can't take is refused, whoever asks (GpuCheck.h)
    if (id == "light_quality" && value > g_GpuCaps.maxLightQuality) {
        error = g_GpuCaps.lightReason.empty() ? "Not available on this graphics card" : g_GpuCaps.lightReason;
        return false;
    }
    if (id == "filmic" && value != 0 && g_GpuCaps.maxLightQuality <= 0) {
        error = "Works with Lighting Low or High, which this graphics card can't run";
        return false;
    }
    if (id == "bloom" && value != 0 && !g_GpuCaps.bloom) {
        error = g_GpuCaps.bloomReason.empty() ? "Not available on this graphics card" : g_GpuCaps.bloomReason;
        return false;
    }
    g_Settings[id] = std::to_string(value);
    saveSettings();
    applyVideoSetting(id, value);
    return true;
}

// Re-applies saved video settings (after mods, so the player's choices win).
void applyStoredVideoSettings() {
    for (const auto& s : kVideoSettings) {
        if (g_Settings.count(s.id)) applyVideoSetting(s.id, std::clamp(settingInt(s.id, s.minV), s.minV, s.maxV));
    }
}

// What the player's client mods may do right now: no limits in singleplayer or as a LAN host;
// on a server, what it says (ClientModRules.h). Settings: client_mods = false turns them off.
ClientModRules currentClientModRules() {
    ClientModRules r = ClientModRules::unlimited();
    if (g_Client.state() == GameClient::State::Joined) {
        const std::string v = g_Client.getShared(ClientModRules::kSharedKey);
        r = v.empty() ? ClientModRules() : ClientModRules::decode(v);   // a server that doesn't say: defaults
    }
    return r;
}
// The rules a LAN host gives players who join (settings.txt: lan_client_mods, lan_client_mods_chat,
// lan_client_mods_players, lan_client_mods_block_range). Friends: no limits unless changed.
ClientModRules lanClientModRules() {
    return ClientModRules::fromConfig(g_Settings, "lan_client_mods", ClientModRules::unlimited());
}

// Engine functions for client mods that read the world, held to the server's rules
// (block range, other players). Added to the client state's `client` table before mods load.
void addClientModWorldApi(ClientMods& cm) {
    sol::table client = cm.api();
    ClientMods* mods = &cm;
    auto near3 = [mods](int x, int y, int z) {
        const int r = mods->rules().blockRange;
        if (r <= 0) return true;
        const glm::vec3 d = glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) - g_PlayerPosition;
        return glm::dot(d, d) <= static_cast<float>(r) * static_cast<float>(r);
    };
    auto near2 = [mods](int x, int z) {
        const int r = mods->rules().blockRange;
        if (r <= 0) return true;
        const float dx = x + 0.5f - g_PlayerPosition.x, dz = z + 0.5f - g_PlayerPosition.z;
        return dx * dx + dz * dz <= static_cast<float>(r) * static_cast<float>(r);
    };
    auto blockName = [](uint8_t id) -> std::string {
        return id == 0 ? std::string("air") : BlockRegistry::getInstance().getBlock(id).name;
    };

    // client.get_block(x, y, z) -> name, meta  (nil when not loaded or out of the allowed range)
    client["get_block"] = [near3, blockName](int x, int y, int z, sol::this_state ts) {
        sol::state_view lv(ts);
        const glm::ivec3 p(x, y, z);
        if (!g_WorldInstance || !near3(x, y, z) || (y >= 0 && y < CHUNK_HEIGHT && !g_WorldInstance->isLoaded(p))) {
            return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, sol::lua_nil));
        }
        return std::make_tuple(sol::make_object(lv, blockName(g_WorldInstance->getBlockAt(p))),
                               sol::make_object(lv, static_cast<int>(g_WorldInstance->getMetadataAt(p))));
    };
    // client.get_light_level(x, y, z) -> 0..15 like engine.get_light_level (nil out of range)
    client["get_light_level"] = [near3](int x, int y, int z, sol::this_state ts) -> sol::object {
        if (!g_WorldInstance || !near3(x, y, z)) return sol::make_object(ts, sol::lua_nil);
        const uint16_t l = g_WorldInstance->getLightAt(glm::ivec3(x, y, z));
        float day = 1.0f;
        if (g_LightingSystem) day = std::max(0.0f, std::min(1.0f, g_LightingSystem->getSunDirection().y * 2.0f + 0.5f));
        const int skyNow = std::max(0, light::sky(l) - static_cast<int>(std::lround((1.0f - day) * 11.0f)));
        return sol::make_object(ts, std::max(skyNow, std::max(light::red(l), std::max(light::green(l), light::blue(l)))));
    };
    // client.get_surface(x, z) -> y, name: the highest block in that column (for maps), or nil
    client["get_surface"] = [near2, blockName](int x, int z, sol::this_state ts) {
        sol::state_view lv(ts);
        const auto nil = std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, sol::lua_nil));
        if (!g_WorldInstance || !near2(x, z) || !g_WorldInstance->isLoaded(glm::ivec3(x, 0, z))) return nil;
        for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
            const uint8_t id = g_WorldInstance->getBlockAt(glm::ivec3(x, y, z));
            if (id != 0) return std::make_tuple(sol::make_object(lv, y), sol::make_object(lv, blockName(id)));
        }
        return nil;
    };
    // client.get_block_color(name) -> r, g, b (0..1): the average colour of its top picture,
    // tinted like the block (for maps). Worked out once per block.
    client["get_block_color"] = [](const std::string& name) {
        static std::map<std::string, glm::vec3> cache;
        static int cacheGen = -1;                   // (other mods, other blocks)
        if (cacheGen != g_SessionGeneration) { cache.clear(); cacheGen = g_SessionGeneration; }
        const auto it = cache.find(name);
        if (it != cache.end()) return std::make_tuple(it->second.r, it->second.g, it->second.b);
        glm::vec3 c(0.5f);
        auto& reg = BlockRegistry::getInstance();
        const uint8_t id = reg.getBlockId(name);
        if (id != 0) {
            const BlockProperty& b = reg.getBlock(id);
            int layer = b.faces[2].baseLayer;
            for (int f : { 4, 0, 1, 5, 3 }) if (layer < 0) layer = b.faces[f].baseLayer;
            c = b.defaultTint;
            if (layer >= 0 && layer < static_cast<int>(g_LayerSources.size())) {
                const TextureSource& src = g_LayerSources[layer];
                std::string file = packOverride(src.requested);
                if (file.empty()) file = src.resolved;
                int w = 0, h = 0, n = 0;
                if (unsigned char* px = file.empty() ? nullptr : stbi_load(file.c_str(), &w, &h, &n, 4)) {
                    double sum[3] = { 0, 0, 0 };
                    long count = 0;
                    for (int i = 0; i < w * h; ++i) {
                        if (px[i * 4 + 3] < 128) continue;
                        for (int k = 0; k < 3; ++k) sum[k] += px[i * 4 + k];
                        ++count;
                    }
                    stbi_image_free(px);
                    if (count > 0) {
                        glm::vec3 avg(sum[0] / count / 255.0, sum[1] / count / 255.0, sum[2] / count / 255.0);
                        c = b.faces[2].tintBase || b.faces[2].baseLayer < 0 ? avg * b.defaultTint : avg;
                    }
                }
            }
        }
        cache[name] = c;
        return std::make_tuple(c.r, c.g, c.b);
    };
    // client.get_players() -> { { name, x, y, z, yaw }, ... } other players (empty if the server says no)
    client["get_players"] = [mods](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table list = lv.create_table();
        if (!mods->rules().players) return list;
        const net::PlayerMap& others = playersHere();
        int i = 1;
        for (const auto& [id, v] : others) {
            sol::table e = lv.create_table();
            e["name"] = v.name;
            e["x"] = v.position.x; e["y"] = v.position.y; e["z"] = v.position.z;
            e["yaw"] = v.yaw;
            list[i++] = e;
        }
        return list;
    };
    // client.get_server_info() -> { mode = "singleplayer" | "lan_host" | "server", address }
    client["get_server_info"] = [](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        const bool joined = g_Client.state() == GameClient::State::Joined;
        t["mode"] = g_Server.running() ? "lan_host" : joined ? "server" : "singleplayer";
        if (joined) t["address"] = g_Client.address();
        else t["world"] = g_ActiveWorldName;
        return t;
    };
    // client.send_chat_message(text) -> true if sent: chat (or a /command) as if the player typed
    // it. Only if the server allows it.
    client["send_chat_message"] = [mods](const std::string& text) {
        if (!mods->rules().chat || !g_ChatSystem || text.empty()) return false;
        g_ChatSystem->sendText(text);
        return true;
    };
}

bool settingsOpen() {
    return g_CurrentGameState == GameState::SETTINGS ||
           (g_CurrentGameState == GameState::PLAYING && g_Paused && g_SettingsInGame);
}

void closeSettings() {
    if (g_CurrentGameState == GameState::SETTINGS) g_CurrentGameState = GameState::MENU;
    g_SettingsInGame = false;   // in-game: back to the pause menu
}


// engine.register_block(...): the shared definition (BlockDefs.cpp), with textures
// uploaded to the GPU here.
// Which mod registered each block / item (its folder's name), for ui.get_block_info(name).mod:
// the creative inventory uses it to give a mod's items a tab of their own
static std::unordered_map<std::string, std::string> g_BlockMod;
std::string blockModOf(const std::string& name) {
    auto it = g_BlockMod.find(name);
    return it == g_BlockMod.end() ? std::string() : it->second;
}

// The name players see: the block's description ("Oak Log"), or one made from its name
// (log_oak -> "Log Oak") when it has none
std::string blockDisplayName(const BlockProperty& b) {
    if (!b.description.empty()) return b.description;
    std::string out;
    bool up = true;
    for (char ch : b.name) {
        if (ch == '_' || ch == ':') { out += ' '; up = true; continue; }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
        up = false;
    }
    return out;
}
// "mod:name", like Luanti's item names: the mod that registered it, then its name
std::string blockFullName(const BlockProperty& b) {
    const std::string mod = blockModOf(b.name);
    return mod.empty() ? b.name : mod + ":" + b.name;
}

void registerBlockFromLua(const std::string& name, float r, float g, float b, sol::object texObj, sol::object overlayObj, sol::optional<sol::table> propsTable) {
    if (!g_CurrentModPath.empty()) g_BlockMod[name] = fs::path(g_CurrentModPath).filename().string();
    registerBlockDefinition(name, r, g, b, texObj, overlayObj, propsTable,
                            [](const std::string& path) { return loadTextureToLayer(path); });
}

uint8_t getBlockIdLua(const std::string& name) {
    return BlockRegistry::getInstance().getBlockId(name);
}

bool isWaterBlockName(const std::string& name) {
    return name == "water" || name == "flowing_water";
}

// engine.register_item_use(item, fn): what right-clicking with an item does (buckets etc.)
std::unordered_map<std::string, sol::protected_function> g_ItemUseHandlers;

// A player broke the block at pos: air - or, for a waterlogged slab or stairs, the water it
// held (a source) (BlockShape.h)
void clearBrokenBlock(World& world, const glm::ivec3& pos) {
    const uint8_t id = world.getBlockAt(pos);
    if (id != 0 && shape::isWaterlogged(BlockRegistry::getInstance().getBlock(id), world.getMetadataAt(pos))) {
        FluidRegistry::get().ensureFresh();
        const FluidDef* water = FluidRegistry::get().byName("water");
        if (water && water->sourceId != 0) {
            world.setBlockWithMetaAt(pos, water->sourceId, 0);
            return;
        }
    }
    world.setBlockAt(pos, 0);
}

// Is the block at p a full source of any fluid (water, lava, modded fluids)?
bool isFluidSourceAt(const World& world, const glm::ivec3& p) {
    FluidRegistry::get().ensureFresh();
    return FluidRegistry::get().cell(world.getBlockAt(p), world.getMetadataAt(p)).source;
}

// Walks the ray block by block (the blocks the ray passes through, in order) and returns
// the first fluid block, or nothing if a solid block or the distance comes first.
struct FluidRayHit { bool hit = false; glm::ivec3 pos{0}; };
FluidRayHit raycastFluid(const glm::vec3& origin, const glm::vec3& dir, float maxDist, const World& world) {
    FluidRayHit out;
    glm::ivec3 cell(static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z)));
    glm::ivec3 step;
    glm::vec3 tMax, tDelta;
    for (int a = 0; a < 3; ++a) {
        step[a] = dir[a] > 0 ? 1 : (dir[a] < 0 ? -1 : 0);
        tDelta[a] = dir[a] != 0 ? std::abs(1.0f / dir[a]) : 1e30f;
        const float edge = dir[a] > 0 ? std::floor(origin[a]) + 1.0f - origin[a] : origin[a] - std::floor(origin[a]);
        tMax[a] = dir[a] != 0 ? edge * tDelta[a] : 1e30f;
    }
    for (float t = 0.0f; t <= maxDist;) {
        const uint8_t id = world.getBlockAt(cell);
        if (id != 0) {
            const auto& prop = BlockRegistry::getInstance().getBlock(id);
            if (FluidRegistry::get().forBlock(id)) { out.hit = true; out.pos = cell; return out; }
            if (prop.isSelectable || prop.isWalkable) return out;   // something solid in the way
        }
        const int a = (tMax.x < tMax.y) ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        t = tMax[a];
        tMax[a] += tDelta[a];
        cell[a] += step[a];
    }
    return out;
}

float underwaterDepth(const World& world, const glm::vec3& eye) {
    const int bx = static_cast<int>(std::floor(eye.x));
    const int bz = static_cast<int>(std::floor(eye.z));
    const int by = static_cast<int>(std::floor(eye.y));

    auto isWaterAt = [&](int y) {
        const uint8_t id = world.getBlockAt(glm::ivec3(bx, y, bz));
        return id != 0 && isWaterBlockName(BlockRegistry::getInstance().getBlock(id).name);
    };

    if (!isWaterAt(by)) return -1.0f;

    int top = by;
    for (int y = by + 1; y < by + 128 && isWaterAt(y); ++y) {
        top = y;
    }

    const float surfaceY = static_cast<float>(top) + 1.0f;
    return glm::max(surfaceY - eye.y, 0.0f);
}

// The water block's own colour: the average of its top texture (what you see on the surface),
// times its tint colour if the texture is tinted. Worked out once (again after a texture pack
// change). Shallow = that colour, deep = a darker version of it.
WaterColors waterBlockColors() {
    static WaterColors cached{ g_Underwater.shallowColor, g_Underwater.deepColor };
    static int cachedLayer = -2;
    static size_t cachedSources = 0;
    auto& reg = BlockRegistry::getInstance();
    const uint8_t id = reg.getBlockId("water");
    if (id == 0) return WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor };
    const BlockProperty& w = reg.getBlock(id);
    int layer = w.faces[2].baseLayer;
    for (int f = 0; layer < 0 && f < 6; ++f) layer = w.faces[f].baseLayer;
    if (layer == cachedLayer && cachedSources == g_LayerSources.size()) return cached;
    cachedLayer = layer;
    cachedSources = g_LayerSources.size();
    if (layer < 0 || layer >= static_cast<int>(g_LayerSources.size())) return cached;

    std::vector<unsigned char> px;
    const TextureSource& src = g_LayerSources[layer];
    const std::string override = packOverride(src.requested);
    bool ok = !override.empty() && loadImageScaled(override, TEX_SIZE, px);
    if (!ok && !src.resolved.empty()) ok = loadImageScaled(src.resolved, TEX_SIZE, px);
    if (!ok) return cached;
    glm::vec3 sum(0.0f);
    float weight = 0.0f;
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        const float a = px[i + 3] / 255.0f;
        sum += glm::vec3(px[i], px[i + 1], px[i + 2]) / 255.0f * a;
        weight += a;
    }
    if (weight <= 0.0f) return cached;
    glm::vec3 color = sum / weight;
    if (w.faces[2].tintBase) color *= w.defaultTint;
    cached = WaterColors{ color, color * 0.35f };
    std::cout << "[Underwater] Colour from the water block: " << color.r << ", " << color.g << ", " << color.b << std::endl;
    return cached;
}

WaterColors queryWaterColors(sol::state& lua, const glm::vec3& eye) {
    const WaterColors fallback = g_Underwater.colorsFromLua ? WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor }
                                                            : waterBlockColors();
    try {
        sol::function fn = lua["get_water_color"];
        if (!fn.valid()) return fallback;
        sol::table t = fn(static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.z)));
        if (t.size() < 6) return fallback;
        return WaterColors{
            glm::vec3(t.get<float>(1), t.get<float>(2), t.get<float>(3)),
            glm::vec3(t.get<float>(4), t.get<float>(5), t.get<float>(6))
        };
    } catch (...) {
        return fallback;
    }
}

void HeldLight::update(const World& w) {
    if (!g_LightingSystem) return;
    uint8_t held = 0;
    if (g_LightSettings.heldLight && g_Hotbar && w.lighting()) held = g_Hotbar->getActiveBlockId();
    const light::BlockLight& bl = light::blockLight(held);
    if (!bl.emits()) {
        if (id >= 0) { g_LightingSystem->removePointLight(id); id = -1; }
        return;
    }
    const int level = std::max<int>(bl.emit[0], std::max<int>(bl.emit[1], bl.emit[2]));
    const glm::vec3 color(bl.emit[0] / 15.0f, bl.emit[1] / 15.0f, bl.emit[2] / 15.0f);
    const glm::vec3 pos = g_PlayerPosition + glm::vec3(0.0f, 1.2f, 0.0f);
    const float radius = 1.0f + level * 0.6f;
    const auto& all = g_LightingSystem->getPointLights();
    if (id >= static_cast<int>(all.size()) || (id >= 0 && !all[id].active)) id = -1;   // cleared by sky.reset
    if (id < 0) id = g_LightingSystem->addPointLight(pos, color, radius, 0.9f);
    else g_LightingSystem->updatePointLight(id, pos, color, radius, 0.9f);
}

// ---- Particles: pictures by name, block bits, and Lua's particle definitions ----
// A particle picture's texture layer by file name ("particle_flame.png"), -1 if there's no such
// picture. Loaded the first time it's asked for (from any mod's folder) and kept.
int particleTexture(const std::string& name) {
    static std::unordered_map<std::string, int> cache;
    static int cacheGen = -1;                       // (a mod reload starts the layers again)
    if (cacheGen != g_SessionGeneration) { cache.clear(); cacheGen = g_SessionGeneration; }
    if (name.empty() || name.size() > 128) return -1;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    int layer = -1;
    const std::string indexed = indexedTexture(name);
    if (!indexed.empty() || fs::exists(name)) layer = loadTextureToLayer(name);
    cache[name] = layer;
    return layer;
}
int particleSprite(const char* what) { return particleTexture(std::string("particle_") + what + ".png"); }
// The picture a block's bits show: its side (or any face it has)
int blockParticleLayer(uint8_t id) {
    if (id == 0) return -1;
    const auto& prop = BlockRegistry::getInstance().getBlock(id);
    for (int f : { 4, 0, 2, 1, 5, 3 }) if (prop.faces[f].baseLayer >= 0) return prop.faces[f].baseLayer;
    return -1;
}

bool readParticleVec(const sol::object& o, glm::vec3& out, float limit) {
    if (!o.is<sol::table>()) return false;
    sol::table t = o.as<sol::table>();
    float v[3];
    const char* keys[3] = { "x", "y", "z" };
    for (int i = 0; i < 3; ++i) {
        sol::optional<float> n = t[keys[i]];
        if (!n) n = t.get<sol::optional<float>>(i + 1);
        if (!n || !std::isfinite(*n)) return false;
        v[i] = std::clamp(*n, -limit, limit);
    }
    out = glm::vec3(v[0], v[1], v[2]);
    return true;
}
bool readParticleColor(const sol::object& o, glm::vec3& out) {
    if (o.is<std::string>()) {
        std::string hex = o.as<std::string>();
        if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
        if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
        const unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
        out = glm::vec3(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
        return true;
    }
    glm::vec3 c;
    if (!readParticleVec(o, c, 255.0f)) return false;
    if (c.x > 1.0f || c.y > 1.0f || c.z > 1.0f) c /= 255.0f;
    out = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
    return true;
}
// A Lua particle definition (see particles.spawn) -> spec, how many, and the random spread
bool readParticleDef(sol::table def, ParticleSpec& s, int& amount, glm::vec3& spread, glm::vec3& velSpread) {
    sol::object posObj = def["pos"];
    if (!readParticleVec(posObj, s.pos, 1.0e7f)) {
        sol::optional<float> x = def["x"], y = def["y"], z = def["z"];
        if (!x || !y || !z || !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) return false;
        s.pos = glm::vec3(*x, *y, *z);
    }
    readParticleVec(def["velocity"], s.vel, 100.0f);
    s.acc = glm::vec3(0.0f);
    if (!readParticleVec(def["acceleration"], s.acc, 200.0f)) {
        const float g = def.get_or("gravity", 0.0f);
        if (std::isfinite(g)) s.acc = glm::vec3(0.0f, -std::clamp(g, -200.0f, 200.0f), 0.0f);
    }
    auto num = [&](const char* key, float fallback, float lo, float hi) {
        const float v = def.get_or(key, fallback);
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    s.drag = num("drag", 0.0f, 0.0f, 50.0f);
    s.life = num("life", 1.0f, 0.01f, 60.0f);
    s.size = num("size", 0.1f, 0.005f, 4.0f);
    s.sizeEnd = def["size_end"].valid() ? num("size_end", s.size, 0.0f, 4.0f) : -1.0f;
    readParticleColor(def["color"], s.color);
    glm::vec3 ce;
    if (readParticleColor(def["color_end"], ce)) s.colorEnd = ce;
    s.glow = def.get_or("glow", false);
    s.collide = def.get_or("collide", true);
    s.bounce = def.get_or("bounce", false);
    s.frames.clear();
    sol::object tex = def["texture"];
    if (tex.is<std::string>()) {
        const int l = particleTexture(tex.as<std::string>());
        if (l >= 0) s.frames.push_back(l);
    } else if (tex.is<sol::table>()) {
        for (const auto& kv : tex.as<sol::table>()) {
            if (!kv.second.is<std::string>() || s.frames.size() >= 32) continue;
            const int l = particleTexture(kv.second.as<std::string>());
            if (l >= 0) s.frames.push_back(l);
        }
    } else if (sol::optional<std::string> block = def["block"]) {
        const uint8_t id = BlockRegistry::getInstance().getBlockId(*block);
        const int l = blockParticleLayer(id);
        if (l >= 0) {
            s.frames.push_back(l);
            s.piece = true;
            const auto& prop = BlockRegistry::getInstance().getBlock(id);
            for (const auto& f : prop.faces) if (f.baseLayer == l && f.tintBase) { s.color *= prop.defaultTint; break; }
        }
    }
    if (s.frames.empty()) return false;
    amount = std::clamp(def.get_or("amount", 1), 1, 512);
    readParticleVec(def["spread"], spread, 64.0f);
    readParticleVec(def["velocity_spread"], velSpread, 100.0f);
    spread = glm::abs(spread); velSpread = glm::abs(velSpread);
    return true;
}

// particles.register_ambient: Minecraft's "animate tick" - each frame some random blocks near
// the player are looked at, and those of these kinds run their action (smoke from torches...)
struct AmbientEffect {
    std::vector<std::string> names;
    sol::protected_function action;
    std::string label;
};
std::vector<AmbientEffect> g_AmbientEffects;
size_t g_AmbientResolvedFor = 0;
std::vector<std::vector<int>> g_AmbientByBlock;   // block id -> effects

void runAmbientEffects(const World& world, const glm::vec3& player, float dt) {
    if (g_AmbientEffects.empty() || dt <= 0.0f) return;
    auto& reg = BlockRegistry::getInstance();
    const size_t blocks = reg.getAllBlocks().size();
    if (g_AmbientResolvedFor != blocks || g_AmbientByBlock.size() != 256) {
        g_AmbientResolvedFor = blocks;
        g_AmbientByBlock.assign(256, {});
        for (size_t i = 0; i < g_AmbientEffects.size(); ++i) {
            for (const auto& n : g_AmbientEffects[i].names) {
                const uint8_t id = reg.getBlockId(n);
                if (id != 0) g_AmbientByBlock[id].push_back(static_cast<int>(i));
            }
        }
    }
    static std::mt19937 rng(1234567u);
    static float owed = 0.0f;
    // about 3 looks a second at each block within 16 (33^3 blocks): ~110,000 a second, of which
    // this many each frame (it's cheap: the action only runs for the kinds that have one)
    owed += 110000.0f * std::min(dt, 0.1f);
    int samples = static_cast<int>(owed);
    owed -= static_cast<float>(samples);
    samples = std::min(samples, 12000);
    std::uniform_int_distribution<int> d(-16, 16);
    const glm::ivec3 c(glm::floor(player));
    int calls = 0;
    for (int i = 0; i < samples && calls < 64; ++i) {
        const glm::ivec3 p(c.x + d(rng), c.y + d(rng), c.z + d(rng));
        if (p.y < 0 || p.y >= CHUNK_HEIGHT) continue;
        const uint8_t id = world.getBlockAt(p);
        if (id == 0 || g_AmbientByBlock[id].empty()) continue;
        const std::string& name = reg.getBlock(id).name;
        for (int e : g_AmbientByBlock[id]) {
            ++calls;
            luasandbox::TimeLimit limit(0.05, "particles ambient");
            auto r = g_AmbientEffects[static_cast<size_t>(e)].action(p.x, p.y, p.z, name);
            if (!r.valid()) {
                static std::string lastError;
                sol::error err = r;
                if (lastError != err.what()) { lastError = err.what(); std::cerr << "[Particles] " << g_AmbientEffects[static_cast<size_t>(e)].label << ": " << err.what() << std::endl; }
            }
        }
    }
}

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aTintColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in float aBaseLayer;
layout (location = 4) in float aOverlayLayer;
layout (location = 5) in float aTintBase;
layout (location = 6) in float aTintOverlay;
layout (location = 7) in float aFaceShading;
layout (location = 8) in vec4 aLight;   // sunlight, red, green, blue (0..1), see Light.h

out vec3 TintColor;
out vec4 VoxelLight;
out vec2 TexCoord;
out float BaseLayer;
out float OverlayLayer;
out float TintBase;
out float TintOverlay;
out float LightIntensity;
out vec3 FragPos;
out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 u_SunDir;
uniform int u_Unlit;
uniform float u_Time;

void main() {
    vec3 pos = aPos;
    TintColor = aTintColor;
    TexCoord = aTexCoord;
    BaseLayer = aBaseLayer;
    OverlayLayer = aOverlayLayer;
    TintBase = aTintBase;
    TintOverlay = aTintOverlay;

    WorldPos = (model * vec4(pos, 1.0)).xyz;

    if (u_Unlit == 1) {
        LightIntensity = 1.0f;
    } else {
        // Face direction and ambient occlusion; the light itself comes per corner (aLight)
        LightIntensity = clamp(aFaceShading, 0.2f, 1.0f);
    }
    VoxelLight = aLight;

    vec4 viewPos = view * model * vec4(pos, 1.0);
    FragPos = viewPos.xyz;
    gl_Position = projection * viewPos;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 TintColor;
in vec2 TexCoord;
in float BaseLayer;
in float OverlayLayer;
in float TintBase;
in float TintOverlay;
in float LightIntensity;
in vec3 FragPos;
in vec3 WorldPos;
in vec4 VoxelLight;

out vec4 FragColorOut;
uniform sampler2DArray textureArray;
uniform int u_IsGUI;
uniform int u_Unlit;
uniform vec3 u_SunDir;
uniform float u_Time;

struct PointLight {
    vec3 position;
    vec3 color;
    float radius;
    float intensity;
};
uniform int u_NumPointLights;
uniform PointLight u_PointLights[16];

uniform int   u_FogEnabled;
uniform int   u_FogExp;
uniform float u_FogStart;
uniform float u_FogEnd;
uniform float u_FogDensity;
uniform vec3  u_FogColor;

uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;
uniform float u_WeatherLight;
uniform float u_ChunkFade;
uniform int   u_TintPass;         // 1 = stained glass as a colour filter (multiply blending; WorldRenderer::drawTint)

// Block light (Light.h). Chunks carry light per corner; anything else drawn with this shader
// (other players, particles...) uses the light where the camera is.
uniform int   u_VoxelLight;       // 1 = use the vertex light (chunks)
uniform vec4  u_FallbackLight;    // sun, r, g, b (0..1) for everything else
uniform float u_MinLight;         // the darkest anything gets (sky.set_min_light)
uniform float u_Brightness;       // 0 = moody, 1 = bright (sky.set_brightness, like Minecraft's slider)
uniform float u_BlockLightScale;  // how strong block light is (sky.set_block_light_strength)
uniform float u_Gamma;            // the player's Brightness slider above "Default": 0..1 lift of the final light
uniform vec3  u_CameraPos;        // (spotlights: which way a face looks)
uniform int   u_TraceSteps;       // shadow rays' longest walk through the blocks (lighting quality)
uniform int   u_SunSteps;         // the sun's shadow rays (0 = the sun isn't traced)
uniform vec3  u_MoonDir;          // toward the moon
uniform float u_MoonShadow;       // how much the moon's shadows show (0 = none: day, new moon, rain)
uniform int   u_Filmic;           // 1 = filmic exposure (Settings: the eye adjusts, a film curve)
uniform float u_SkyFog;           // 0..1: how much fog takes the colour of the sky behind it
uniform float u_CamWaterDepth;    // how deep the camera is under water (blocks; 0 = not under)
uniform float u_SkyBrightness;    // the sky's brightness (Skybox::brightness): fog matches it
uniform float u_SkyHalo;          // the sky's glow around the sun (0 = the standard look)
uniform vec4  u_WeatherTint;      // the weather's tint over the sky (rgb, how much): fog and reflections get it too
uniform int   u_SSR;              // 1 = water reflects the world (lighting High)
uniform sampler2D u_SceneColor;   // the frame before water was drawn (SceneCopy.h)
uniform sampler2D u_SceneDepth;
uniform mat4  u_ViewProj;
)" BLOCKSMITH_VOXEL_TRACE_GLSL BLOCKSMITH_SPOT_GLSL BLOCKSMITH_SKY_GLSL R"(

// How much of the sky a face sees past the blocks around it (a few blocks out): crevices,
// corners and the ground under overhangs get less. With the mesher's own corner shading, this
// gives the soft contact darkening real light has.
float voxAO(vec3 p, vec3 n) {
    vec3 t1 = abs(n.y) > 0.5 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 t2 = cross(n, t1);
    vec3 o = p + n * 0.5;
    float occ = 0.0;
    for (int i = 0; i < 8; ++i) {
        float a = float(i) * 0.785398;
        vec3 dir = t1 * cos(a) + t2 * sin(a);
        occ += min(voxOpacity(ivec3(floor(o + n * 0.4 + dir * 1.4))), 1.0) * 0.6;
        occ += min(voxOpacity(ivec3(floor(o + n * 1.5 + dir * 2.2))), 1.0) * 0.4;
    }
    occ += min(voxOpacity(ivec3(floor(o + n * 2.0))), 1.0) * 0.8;
    return 1.0 - 0.55 * clamp(occ / 8.8, 0.0, 1.0);
}

// Under water (Lighting Low/High): how much daylight is left in the water around the camera.
// Water swallows light: dimmer the deeper you are, and much darker when the sun is low.
float underwaterLight() {
    float dl = smoothstep(-0.15, 0.35, normalize(u_SunDir).y) * clamp(u_WeatherLight, 0.3, 1.0);
    return mix(0.12, 0.62, dl) * exp(-u_CamWaterDepth * 0.08);
}

// Depth buffer value -> distance (the projection's near 0.1, far 500)
float ssrLinZ(float d) { return 100.0 / (500.1 - (d * 2.0 - 1.0) * 499.9); }

// Light level (0..1) -> brightness: Minecraft's curve (each level ~20% dimmer), lifted by u_Brightness
float lightCurve(float l) {
    // Clamped: blending across a triangle can give a hair over 1, and pow() of a negative
    // number is NaN on most GPUs (drawn as black specks on brightly lit surfaces)
    l = clamp(l, 0.0, 1.0);
    float v = l / (4.0 - 3.0 * l);
    float inv = clamp(1.0 - v, 0.0, 1.0);
    float lifted = 1.0 - inv * inv * inv * inv;
    return mix(v, lifted, u_Brightness);
}

// The rippling net of light the sun makes on things under water (caustics): tiles every 4
// blocks, slowly moving with time
float waterCaustic(vec2 p, float t) {
    p = mod(p * 1.5707963, 6.2831853) - 250.0;
    vec2 i = p;
    float c = 1.0;
    const float inten = 0.005;
    for (int n = 0; n < 4; n++) {
        float tt = t * 0.45 * (1.0 - (3.5 / float(n + 1)));
        i = p + vec2(cos(tt - i.x) + sin(tt + i.y), sin(tt - i.y) + cos(tt + i.x));
        c += 1.0 / length(vec2(p.x / (sin(i.x + tt) / inten), p.y / (cos(i.y + tt) / inten)));
    }
    c /= 4.0;
    c = 1.17 - pow(c, 1.4);
    return clamp(pow(abs(c), 8.0), 0.0, 2.0);
}

void main() {
    vec4 finalColor = vec4(TintColor, 1.0);
    // The surface's true direction (before anything can skip a pixel: screen-space slopes need
    // every pixel of the 2x2 block). Water uses it below to flow the right way.
    vec3 surfN = cross(dFdx(WorldPos), dFdy(WorldPos));
    surfN = dot(surfN, surfN) > 1e-12 ? normalize(surfN) : vec3(0.0, 1.0, 0.0);

    if (u_Unlit == 0 && u_IsGUI == 0 && u_ChunkFade < 1.0) {
        float threshold = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        if (threshold > u_ChunkFade) discard;
    }

    if (BaseLayer >= 0.0) {
        vec2 animatedUV = TexCoord;

        if (TintColor.b > TintColor.r && TintColor.b > TintColor.g && BaseLayer > 0.0 && u_IsGUI == 0) {
            // Water moves the way water goes. (Texture v runs down a block's sides, and along +z
            // on its top; the pattern moves the way the coordinate it's sampled at moves back.)
            vec3 n = surfN.y < 0.0 ? -surfN : surfN;          // (the surface's upward side)
            if (abs(n.y) < 0.5) {
                // a side or a waterfall: down
                animatedUV.y -= u_Time * 0.6f;
            } else {
                // the top: downhill where the water slopes (flowing water), otherwise still.
                // Snapped to 8 directions at one speed, so both halves of a block's surface
                // (which can tilt a little differently) move together - no seam between them.
                vec2 slope = n.xz;
                if (length(slope) > 0.05) {
                    float a = round(atan(slope.y, slope.x) / 0.7853982) * 0.7853982;
                    animatedUV -= vec2(cos(a), sin(a)) * u_Time * 0.45f;
                }
            }
            animatedUV.x += sin(u_Time * 1.2f + WorldPos.z * 0.5f) * 0.02f;
        }

        vec4 baseTex = texture(textureArray, vec3(animatedUV, BaseLayer));
        if (baseTex.a < 0.05) discard;

        vec3 baseRgb = (TintBase > 0.5) ? (baseTex.rgb * TintColor) : baseTex.rgb;
        finalColor = vec4(baseRgb, baseTex.a);

        if (OverlayLayer >= 0.0) {
            vec4 overlayTex = texture(textureArray, vec3(TexCoord, OverlayLayer));
            if (overlayTex.a > 0.05) {
                vec3 overlayRgb = (TintOverlay > 0.5) ? (overlayTex.rgb * TintColor) : overlayTex.rgb;
                finalColor.rgb = mix(finalColor.rgb, overlayRgb, overlayTex.a);
            }
        }
    }

    if (u_IsGUI == 1) {
        FragColorOut = finalColor;
        return;
    }

    // The face's direction, from how the position changes across the screen (flat faces), turned
    // toward the camera: for spotlights and shadows traced through the blocks (VoxelVolume.h)
    vec3 faceN = normalize(cross(dFdx(WorldPos), dFdy(WorldPos)));
    if (dot(faceN, u_CameraPos - WorldPos) < 0.0) faceN = -faceN;
    // Block faces are all along the axes: snapped to the nearest one. (Far away a face is only a
    // pixel or two, and the screen-space estimate wobbles there: unsnapped, distant sunlit faces
    // could come out turned away from the sun and drawn dark.)
    {
        vec3 an = abs(faceN);
        faceN = (an.x > an.y && an.x > an.z) ? vec3(sign(faceN.x), 0.0, 0.0)
              : (an.y > an.z) ? vec3(0.0, sign(faceN.y), 0.0) : vec3(0.0, 0.0, sign(faceN.z));
    }

    // The light from the sky through the day: warm white by day, a warm glow (not an orange
    // wash: the sky and fog carry the sunset's colour) around sunset, blue moonlight at night
    vec3 dayLightColor = vec3(1.0f, 0.97f, 0.90f);
    vec3 sunsetLightColor = vec3(1.0f, 0.80f, 0.62f);
    vec3 nightLightColor = vec3(0.58f, 0.66f, 0.92f);

    vec3 currentSunLightColor = mix(sunsetLightColor, dayLightColor, smoothstep(-0.05f, 0.35f, u_SunDir.y));
    if (u_SunDir.y < 0.0f) {
        currentSunLightColor = mix(sunsetLightColor, nightLightColor, smoothstep(0.0f, 0.25f, -u_SunDir.y));
    }

    // ---- Lighting Low / High: light worked out the way it really behaves (simply) ----
    // Each surface gets: sunlight (if it faces the sun and a ray to the sun gets through the
    // blocks), the sky's light (bluish, less in corners and under overhangs), a little light
    // bounced off the ground, moonlight at night, and torches - added up as real amounts of light
    // (in linear colour, brighter than the screen can show), then brought to the screen like a
    // camera does: filmic exposure (Settings), or a plain one.
    bool realistic = u_Unlit == 0 && u_SunSteps > 0 && u_VoxEnabled == 1 && u_VoxelLight == 1;
    vec3 albedoW = finalColor.rgb;        // what lamps and spotlights multiply
    vec3 blkLight = vec3(0.0);            // (torch light here, for the night colour fade)
    vec3 shadedColor;
    if (realistic) {
        vec4 L = VoxelLight;
        vec3 albedo = pow(max(finalColor.rgb, vec3(0.0)), vec3(2.2));
        albedoW = albedo;
        // the mesher's fixed per-side shading taken back out (the sun does that now), its corner
        // darkening kept
        float faceConst = abs(faceN.y) > 0.5 ? (faceN.y > 0.0 ? 1.0 : 0.5) : (abs(faceN.x) > 0.5 ? 0.8 : 0.7);
        float vao = clamp(LightIntensity / faceConst, 0.0, 1.0);
        // what this face belongs to: leaves (light shines through them), plants (lit like the
        // ground they stand on, whichever way their crossed panels face)
        float selfO = voxOpacity(ivec3(floor(WorldPos - faceN * 0.02)));
        bool leaf = abs(selfO - 0.50196) < 0.002;
        // (the water's surface seen from below has air behind it: that's water, not a plant)
        bool waterFace = voxIsWater(ivec3(floor(WorldPos + faceN * 0.02))) || voxIsWater(ivec3(floor(WorldPos - faceN * 0.02)));
        bool plant = selfO < 0.01 && !waterFace;
        vec3 n = plant ? vec3(0.0, 1.0, 0.0) : faceN;
        float ao = vao * ((plant || !voxColumnReady(ivec3(floor(WorldPos)))) ? 1.0 : voxAO(WorldPos, faceN));
        vec3 sd = normalize(u_SunDir);
        float sunUp = smoothstep(-0.02, 0.10, sd.y);
        float dayAmt = smoothstep(-0.18, 0.25, sd.y);
        float sky = lightCurve(L.x);                       // open to the sky here (0 deep underground)
        float near_ = 1.0 - smoothstep(44.0, 58.0, length(WorldPos - u_CameraPos));
        // (blocks here not traced yet - just joined, or just came into range: lit like far-off
        // land until they are, instead of flashing bright and then darkening)
        bool ready = voxColumnReady(ivec3(floor(WorldPos)));
        if (!ready) near_ = 0.0;
        float clearSky = clamp(u_WeatherLight, 0.0, 1.0);
        clearSky = clearSky * clearSky * clearSky;         // (clouds and rain: hardly any direct sun)

        // Sunlight: warm, strong; orange and weaker when low
        float ndl = dot(n, sd);
        float wrap = leaf ? clamp(ndl * 0.6 + 0.4, 0.0, 1.0) : max(ndl, 0.0);
        float sunVis = 0.0;
        if (wrap > 0.0 && sunUp > 0.0) {
            float T = 1.0;
            if (near_ > 0.0) {
                g_VoxSoftFoliage = leaf ? 1.0 : 0.0;
                T = voxTransmittance(WorldPos + n * 0.05, WorldPos + sd * 90.0, u_SunSteps);
                g_VoxSoftFoliage = 0.0;
            }
            // (past the traced blocks, and to keep caves dark: open sky means sunlit)
            sunVis = mix(smoothstep(0.55, 0.95, sky), T * smoothstep(0.15, 0.55, sky), near_);
        }
        vec3 sunTint = mix(vec3(1.0, 0.55, 0.28), vec3(1.0, 0.94, 0.84), smoothstep(0.02, 0.4, sd.y));
        vec3 sunLight = sunTint * 3.3 * sunUp * clearSky;
        vec3 direct = sunLight * wrap * sunVis * mix(1.0, vao, 0.35);

        // The sky's light: blue by day, dusky at sunset, dim blue at night; grey in bad weather
        vec3 skyCol = mix(vec3(0.035, 0.045, 0.085),
                          mix(vec3(0.42, 0.36, 0.40), vec3(0.36, 0.42, 0.55), smoothstep(0.05, 0.4, sd.y)), dayAmt);
        skyCol = mix(vec3(dot(skyCol, vec3(0.333))), skyCol, clearSky * 0.7 + 0.3) * mix(0.55, 1.0, u_WeatherLight);
        float hemi = 0.62 + 0.38 * n.y;                    // tops see the whole sky, sides half
        vec3 ambient = skyCol * hemi * sky * ao;
        // sunlight bounced off the ground onto sides and undersides
        vec3 bounce = sunLight * 0.10 * sky * (0.5 - 0.5 * n.y) * ao * vec3(1.0, 0.92, 0.8);

        // Moonlight, with soft shadows (u_MoonShadow)
        vec3 moon = vec3(0.0);
        vec3 md = normalize(u_MoonDir);
        float moonUp = smoothstep(0.0, 0.2, md.y) * (1.0 - dayAmt);
        if (moonUp > 0.0) {
            float mdl = max(dot(n, md), 0.0);
            float mv = 1.0;
            if (u_MoonShadow > 0.0 && near_ > 0.0 && mdl > 0.0)
                mv = mix(1.0, voxTransmittance(WorldPos + n * 0.05, WorldPos + md * 90.0, u_SunSteps / 2), u_MoonShadow * near_);
            moon = vec3(0.06, 0.075, 0.13) * mdl * mv * moonUp * smoothstep(0.3, 0.8, sky) * clamp(u_WeatherLight, 0.0, 1.0);
        }

        // Torches and other block light (they flicker a little)
        vec3 blk = vec3(lightCurve(L.y), lightCurve(L.z), lightCurve(L.w)) * u_BlockLightScale;
        blk *= 1.0 + 0.025 * (sin(u_Time * 9.1) + sin(u_Time * 13.7 + 1.3)) * 0.5;
        vec3 blockL = blk * blk * 1.8 * mix(1.0, ao, 0.6);
        blkLight = blk;

        // Under water: bluer and dimmer with depth; the surface's ripples focus the sun into a
        // moving net of light (caustics)
        float wd = voxWaterDepth(WorldPos + faceN * 0.05);
        if (wd > 0.0) {
            vec2 q = WorldPos.xz + sd.xz / max(sd.y, 0.25) * wd;
            float c = waterCaustic(q, u_Time) * 0.6 + waterCaustic(q * 0.7 + 1.3, u_Time * 0.8) * 0.4;
            vec3 absorb = exp(-wd * vec3(0.10, 0.04, 0.03));
            direct *= absorb * (0.7 + c * 1.8 * exp(-wd * 0.12));
            ambient *= absorb;
        }

        float minL = mix(u_MinLight, max(u_MinLight, 0.08), u_Gamma);
        shadedColor = albedo * (direct + ambient + bounce + moon + blockL + vec3(minL * minL * 0.5 + 0.002));
    } else if (u_Unlit == 1) {
        shadedColor = finalColor.rgb * LightIntensity * currentSunLightColor;
    } else {
        vec4 L = (u_VoxelLight == 1) ? VoxelLight : u_FallbackLight;
        // Sunlight follows the time of day and the weather (sky.set_light); block light doesn't
        // How much daylight: a smooth curve, so it dims gradually through sunset (about 55% just
        // after the sun sets) down to a moody but readable night
        float daylight = mix(0.40f, 1.0f, smoothstep(-0.22f, 0.32f, u_SunDir.y));
        vec3 sunPart = lightCurve(L.x) * daylight * currentSunLightColor * u_WeatherLight;
        // In daylight, faces turned toward the sun are a little brighter and warmer, faces turned
        // away a little cooler: you can tell where the sun is, even without traced shadows
        {
            float faceSun = dot(faceN, normalize(u_SunDir));
            float dayAmt = smoothstep(0.0f, 0.25f, u_SunDir.y) * clamp(L.x * 1.5f, 0.0f, 1.0f);
            sunPart *= 1.0f + dayAmt * (faceSun > 0.0f ? 0.08f : 0.06f) * faceSun;
            sunPart *= mix(vec3(1.0f), faceSun > 0.0f ? vec3(1.03f, 1.0f, 0.95f) : vec3(0.96f, 0.98f, 1.04f),
                           dayAmt * abs(faceSun));
        }
        vec3 blockPart = vec3(lightCurve(L.y), lightCurve(L.z), lightCurve(L.w)) * u_BlockLightScale;
        // Flames are alive: block light flickers very slightly
        blockPart *= 1.0f + 0.025f * (sin(u_Time * 9.1f) + sin(u_Time * 13.7f + 1.3f)) * 0.5f;
        // Screen blend: torches still show at dusk, and coloured lights mix
        vec3 lightAmount = vec3(1.0f) - (vec3(1.0f) - sunPart) * (vec3(1.0f) - min(blockPart, vec3(1.0f)));
        // The player's brightness (gamma), as in Minecraft: on the FINAL light, so dark caves
        // and nights come up too. The darkest anything gets rises a little (2% -> 8% at full),
        // then the whole range is lifted with Minecraft's curve: a pitch-black cave ends up
        // about 28% lit at "Bright", while daylight barely changes.
        lightAmount = max(lightAmount, vec3(mix(u_MinLight, max(u_MinLight, 0.08f), u_Gamma)));
        vec3 inv = vec3(1.0f) - clamp(lightAmount, 0.0f, 1.0f);
        lightAmount = mix(lightAmount, vec3(1.0f) - inv * inv * inv * inv, u_Gamma);
        shadedColor = finalColor.rgb * LightIntensity * lightAmount;
    }

    vec3 pointLightContribution = vec3(0.0f);
    for(int i = 0; i < u_NumPointLights && i < 16; i++) {
        float dist = distance(WorldPos, u_PointLights[i].position);
        if(dist < u_PointLights[i].radius) {
            float attenuation = 1.0f - (dist / u_PointLights[i].radius);
            attenuation = attenuation * attenuation;
            // blocks in between cast shadows (traced through the blocks around the camera)
            attenuation *= voxTransmittance(WorldPos + faceN * 0.02, u_PointLights[i].position, u_TraceSteps / 2);
            pointLightContribution += u_PointLights[i].color * u_PointLights[i].intensity * attenuation;
        }
    }
    shadedColor += albedoW * pointLightContribution;

    // Spotlights (Spotlights.h): cone, gobo and traced shadows
    if (u_Unlit == 0 && u_NumSpots > 0) {
        vec3 spot = vec3(0.0);
        for (int i = 0; i < u_NumSpots && i < 16; ++i) spot += spotLightAt(WorldPos, faceN, i, u_TraceSteps);
        shadedColor += albedoW * spot;
    }

    // Realistic light onto the screen
    if (realistic) {
        float dayAmt = smoothstep(-0.18, 0.25, normalize(u_SunDir).y);
        // How open to the sky the camera is (u_FallbackLight.x: the sky light where it is): in
        // caves and closed rooms the exposure is set for torchlight, whatever the time of day
        float camSky = smoothstep(0.15, 0.7, u_FallbackLight.x);
        vec3 c;
        if (u_Filmic == 1) {
            // Filmic: the eye adjusts (more exposure at night, so moonlit land stays readable),
            // and a film curve: deep shadows, bright highlights that roll off softly
            float exposure = mix(0.65, mix(1.5, 0.30, dayAmt), camSky);
            vec3 x = shadedColor * exposure;
            c = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
        } else {
            // Plain: a gentle curve, flatter and closer to the standard look
            float exposure = mix(0.8, mix(1.25, 0.47, dayAmt), camSky);
            c = vec3(1.0) - exp(-shadedColor * exposure);
        }
        c = pow(c, vec3(1.0 / 2.2));
        // (the curve takes a little colour out of bright parts: a touch back)
        float lum = dot(c, vec3(0.299, 0.587, 0.114));
        c = clamp(mix(vec3(lum), c, 1.12), 0.0, 1.0);
        // At night, out in the open, colours fade toward a cool blue-grey, as they do for the
        // eye by moonlight (torchlit places keep their colour)
        float nightSee = (1.0 - dayAmt) * camSky * (1.0 - smoothstep(0.0, 0.25, max(max(blkLight.r, blkLight.g), blkLight.b)));
        c = mix(c, lum * vec3(0.78, 0.9, 1.15), 0.6 * nightSee);
        vec3 inv = vec3(1.0) - c;
        shadedColor = mix(c, vec3(1.0) - inv * inv * inv, u_Gamma * 0.6);   // (the Brightness slider)
    }

    // The water's surface (lighting quality Low/High): it reflects the sky more the flatter you
    // look across it, and small moving waves catch the sun (or the moon) in bright glints
    if (u_Unlit == 0 && u_SunSteps > 0 && u_VoxEnabled == 1 && u_VoxelLight == 1 && abs(faceN.y) > 0.9) {
        ivec3 wc = ivec3(floor(WorldPos - faceN * 0.05));
        if (voxIsWater(wc) && !voxIsWater(wc + ivec3(0, 1, 0))) {
            vec3 V = normalize(u_CameraPos - WorldPos);
            vec2 wp = WorldPos.xz;
            float t = u_Time;
            vec3 n = normalize(vec3(0.05 * cos(wp.x * 1.7 + t * 1.3) + 0.035 * cos((wp.x + wp.y) * 2.9 + t * 1.9) +
                                    0.02 * cos(wp.x * 5.3 - wp.y * 1.1 + t * 2.7),
                                    1.0,
                                    0.05 * sin(wp.y * 1.9 + t * 1.1) + 0.035 * sin((wp.x - wp.y) * 3.3 + t * 2.3) +
                                    0.02 * sin(wp.y * 4.7 + wp.x * 1.3 + t * 2.9)));
            float near_ = 1.0 - smoothstep(44.0, 58.0, length(WorldPos - u_CameraPos));
            if (faceN.y > 0.0) {
                float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
                vec3 R = reflect(-V, n);
                vec3 sd = normalize(u_SunDir);
                // what it reflects: the sky in that direction, or (High) the world, found by
                // following the reflected ray across the picture drawn so far
                vec3 refl = blocksmithSky(normalize(vec3(R.x, max(R.y, 0.02), R.z)), sd, u_SkyHalo) * clamp(u_WeatherLight, 0.3, 1.0);
                if (u_WeatherTint.a > 0.0) refl = mix(refl, u_WeatherTint.rgb, u_WeatherTint.a);
                if (u_SSR == 1) {
                    vec3 pos = WorldPos, prev = WorldPos;
                    float stepL = 0.3;
                    for (int i = 0; i < 32; ++i) {
                        prev = pos;
                        pos += R * stepL;
                        stepL *= 1.15;
                        vec4 cp = u_ViewProj * vec4(pos, 1.0);
                        if (cp.w <= 0.01) break;
                        vec3 ndc = cp.xyz / cp.w;
                        vec2 suv = ndc.xy * 0.5 + 0.5;
                        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
                        float sdp = texture(u_SceneDepth, suv).r;
                        if (sdp >= 0.99999) continue;                 // (sky there)
                        float rz = ssrLinZ(ndc.z * 0.5 + 0.5), sz = ssrLinZ(sdp);
                        if (rz > sz + 0.02 && rz - sz < stepL + 1.0) {
                            vec3 a = prev, b = pos;                   // narrow it down
                            for (int j = 0; j < 5; ++j) {
                                vec3 m = (a + b) * 0.5;
                                vec4 c2 = u_ViewProj * vec4(m, 1.0);
                                vec3 n2 = c2.xyz / c2.w;
                                if (ssrLinZ(n2.z * 0.5 + 0.5) > ssrLinZ(texture(u_SceneDepth, n2.xy * 0.5 + 0.5).r)) b = m;
                                else a = m;
                            }
                            vec4 c3 = u_ViewProj * vec4(b, 1.0);
                            vec2 huv = c3.xy / c3.w * 0.5 + 0.5;
                            vec2 e = smoothstep(vec2(0.0), vec2(0.08), huv) * (vec2(1.0) - smoothstep(vec2(0.92), vec2(1.0), huv));
                            refl = mix(refl, texture(u_SceneColor, huv).rgb, e.x * e.y);
                            break;
                        }
                    }
                }
                shadedColor = mix(shadedColor, refl, fres * 0.85);
                finalColor.a = mix(finalColor.a, 1.0, fres * 0.7);
                float sunUp = smoothstep(0.0, 0.15, sd.y);
                if (sunUp > 0.0 && dot(R, sd) > 0.9) {
                    float vis = voxTransmittance(WorldPos + vec3(0.0, 0.05, 0.0), WorldPos + sd * 90.0, u_SunSteps);
                    float rs = max(dot(R, sd), 0.0);
                    float spec = pow(rs, 400.0) * 5.0 + pow(rs, 40.0) * 0.35;
                    shadedColor += currentSunLightColor * spec * vis * sunUp * near_ * u_WeatherLight;
                    finalColor.a = max(finalColor.a, min(1.0, spec * vis));
                }
                vec3 md = normalize(u_MoonDir);
                if (u_MoonShadow > 0.0 && md.y > 0.0 && dot(R, md) > 0.95) {
                    float rs = max(dot(R, md), 0.0);
                    shadedColor += vec3(0.6, 0.7, 0.9) * pow(rs, 300.0) * 1.5 * u_MoonShadow * near_;
                }
            } else {
                // From under the water: the whole surface overhead is see-through (the sky and
                // anything above show through it, tinted by the water) and shimmers all over
                // with the light coming down through the moving waves - the same everywhere,
                // no window or edge
                vec3 sd = normalize(u_SunDir);
                float cosUp = clamp(-V.y, 0.0, 1.0);            // 1 = looking straight up at it
                float camDepth = max(u_CamWaterDepth, 0.0);
                float clarity = exp(-camDepth * 0.12);          // (deeper: more water in the way)
                vec3 below = u_FogColor * underwaterLight();
                float daylight_ = smoothstep(-0.1, 0.3, sd.y) * clamp(u_WeatherLight, 0.3, 1.0);
                // two layers of moving caustics, and sparkles where the waves catch the light
                // (a fine net of light, softened so it reads from a distance, over rolling bands
                // where the waves tilt toward the light)
                float sh1 = sqrt(clamp(waterCaustic(WorldPos.xz * 1.6, u_Time * 1.2), 0.0, 1.0));
                float sh2 = sqrt(clamp(waterCaustic(WorldPos.xz * 2.7 + 3.7, u_Time * 0.9), 0.0, 1.0));
                float waves = clamp(0.5 + 5.0 * (n.x * 0.8 + n.z * 0.6), 0.0, 1.0);
                float shimmer = clamp(sh1 * 0.45 + sh2 * 0.25 + waves * 0.45, 0.0, 1.2);
                float sparkle = pow(clamp(sh1 * sh2 * waves * 1.6, 0.0, 1.0), 2.0);
                vec3 lightCol = mix(below, vec3(0.8, 0.93, 1.0), 0.45) * (0.45 + 0.55 * daylight_);
                shadedColor = lightCol * (0.4 + 1.2 * shimmer) + vec3(0.9, 0.97, 1.0) * sparkle * 1.2 * daylight_ * near_;
                // see-through all over; only a little more solid looking across it, and more the
                // deeper you are
                float a = mix(0.42, 0.62, 1.0 - cosUp);
                a = mix(a, 0.85, 1.0 - clarity);
                finalColor.a = clamp(a + 0.22 * (shimmer - 0.4) + 0.3 * sparkle, 0.3, 0.92);
            }
        }
    }

    if (u_Unlit == 0) {
        shadedColor *= mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    }

    float dist = length(FragPos);
    float fogFactor = 1.0;
    if (u_FogEnabled == 1) {
        if (u_FogExp == 1) {
            float d = dist * u_FogDensity;
            fogFactor = exp(-d * d);
        } else {
            fogFactor = clamp((u_FogEnd - dist) / max(u_FogEnd - u_FogStart, 0.001), 0.0, 1.0);
        }
    }

    if (u_TintPass == 1) {
        // The glass's own colour, not lit (a filter works the same at night), stronger where the
        // picture is more opaque; clear or white parts let everything through. Fades with fog.
        vec3 filterRgb = mix(vec3(1.0), finalColor.rgb, clamp(finalColor.a * 2.2, 0.0, 1.0));
        FragColorOut = vec4(mix(vec3(1.0), filterRgb, fogFactor), 1.0);
        return;
    }

    // A light grade: a touch richer colour and a gentle contrast curve (before the fog, which
    // keeps its own colour)
    if (u_Unlit == 0 && !realistic) {
        float lum = dot(shadedColor, vec3(0.299f, 0.587f, 0.114f));
        shadedColor = max(mix(vec3(lum), shadedColor, 1.08f), vec3(0.0f));
        vec3 c = clamp(shadedColor, 0.0f, 1.0f);
        shadedColor = mix(shadedColor, c * c * (3.0f - 2.0f * c), 0.15f);
    }
    // The fog takes the colour of the sky behind it (so far-off land melts into the sunset
    // instead of a grey wall with a hard edge against the sky)
    vec3 fogC = u_FogColor;
    // under water, the water around you is only as bright as the light that reaches it
    if (realistic && u_CamWaterDepth > 0.0) fogC *= underwaterLight();
    if (u_SkyFog > 0.0 && fogFactor < 1.0) {
        vec3 vd = normalize(WorldPos - u_CameraPos);
        vec3 sk = blocksmithSky(normalize(vec3(vd.x, max(vd.y, 0.0) * 0.6 + 0.01, vd.z)), normalize(u_SunDir), u_SkyHalo);
        if (u_SkyBrightness < 0.999) {
            float l = dot(sk, vec3(0.299, 0.587, 0.114));
            sk = max(mix(vec3(l), sk, 1.0 + (1.0 - u_SkyBrightness) * 1.2), vec3(0.0)) * u_SkyBrightness;
        }
        fogC = mix(u_FogColor, sk, u_SkyFog);
    }
    // Clouds, rain and storms grey the sky over (sky.set_sky_tint): the fog the same, so distant
    // land fades into the same dark grey instead of bright blue under a storm
    if (u_WeatherTint.a > 0.0) fogC = mix(fogC, u_WeatherTint.rgb, u_WeatherTint.a);
    vec3 finalRgb = mix(fogC, shadedColor, fogFactor);
    FragColorOut = vec4(finalRgb, finalColor.a);
}
)";

const char* sunVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    TexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* sunFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D u_Texture;
uniform int u_HasTexture;

void main() {
    if (u_HasTexture == 1) {
        vec4 texColor = texture(u_Texture, TexCoord);
        if (texColor.a < 0.1) discard;
        FragColor = texColor;
    } else {
        FragColor = vec4(1.0, 0.95, 0.6, 1.0);
    }
}
)";

const char* wireframeVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* wireframeFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 u_Color;   // outline: black; survival digging: a darkening box
void main() {
    FragColor = u_Color;
}
)";

const char* uiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
out vec4 FragColor;
void main() {
    FragColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* uiFragmentShaderSource = R"(
#version 330 core
in vec4 FragColor;
out vec4 finalColor;
void main() {
    finalColor = FragColor;
}
)";

const char* texturedUIVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* texturedUIFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D u_Texture;
uniform vec4 u_Tint;      // ui.draw_image's tint / alpha (set to white when the program is made)
void main() {
    vec4 texColor = texture(u_Texture, TexCoords) * u_Tint;
    if (texColor.a < 0.01) discard;
    FragColor = texColor;
}
)";

const char* fontVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
uniform mat4 projection;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

const char* fontFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D fontTexture;
uniform vec4 textColor;
void main() {
    float alpha = texture(fontTexture, TexCoords).r;
    if (alpha < 0.05) discard;
    FragColor = vec4(textColor.rgb, textColor.a * alpha);
}
)";

const char* modelVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;
out vec3 Normal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* modelFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D texture_diffuse1;
uniform vec3 u_LightDir;
uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;
uniform float u_WeatherLight;
uniform vec3  u_LocalLight;     // entities: the light where they stand (1 = full)
uniform vec4  u_Tint;           // entities: rgb, a = how much (the red flash when hit)

void main() {
    vec3 norm = normalize(Normal);
    // Minecraft-like shading: tops brightest, sides a little darker, undersides darkest, and a
    // touch more on the side facing the sun. How bright it is overall is u_LocalLight (the
    // light where the model stands, and the time of day).
    float face = 0.78f + 0.22f * norm.y;
    float diff = face * (0.88f + 0.12f * max(dot(norm, -u_LightDir), 0.0f));
    vec4 texColor = texture(texture_diffuse1, TexCoords);
    if(texColor.a < 0.1) discard;

    vec3 litColor = texColor.rgb * diff * u_WeatherLight * u_LocalLight * mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    litColor = mix(litColor, u_Tint.rgb, u_Tint.a);
    FragColor = vec4(litColor, texColor.a);
}
)";

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width == 0 || height == 0) return;
    glViewport(0, 0, width, height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_ChatSystem && g_ChatSystem->isOpen) {   // the wheel scrolls back through the chat
        g_ChatSystem->scrollLog(yoffset * 3.0);
        return;
    }
    uidraw::addScroll(xoffset, yoffset);          // ui.get_scroll(), for every screen
    EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->player) return;

    if (settingsOpen()) {
        callLuaUI(ctx->lua, "handle_settings_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::WORLD_SELECT) {
        callLuaUI(ctx->lua, "handle_world_select_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::MULTIPLAYER) {
        callLuaUI(ctx->lua, "handle_multiplayer_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::MODS) {
        callLuaUI(ctx->lua, "handle_mods_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
        callLuaUI(ctx->lua, "handle_texture_packs_scroll", yoffset);
        return;
    }

    // A mod's screen is open: the wheel is its (handle_inventory_scroll(dy)), not the hotbar's
    if (g_CurrentGameState == GameState::PLAYING && g_LuaInventoryOpen) {
        callLuaUI(ctx->lua, "handle_inventory_scroll", yoffset);
        return;
    }

    if (g_Inventory.isCreativeOpen()) {
        // Creative inventory open: scroll its item grid (handled in inventory.lua)
        g_Inventory.handleCreativeScroll(yoffset);

        try {
            sol::state* lua = ctx->lua;
            if (lua) {
                sol::function scrollLua = (*lua)["on_scroll_input"];
                if (scrollLua.valid()) scrollLua(xoffset, yoffset);
            }
        } catch (...) {}
        return;
    }

    if (g_Hotbar) {
        if (yoffset > 0) g_Hotbar->previousSlot();
        else if (yoffset < 0) g_Hotbar->nextSlot();
    }
}

// Puts OpenGL into a clean state for 2D UI and text (menus, loading screen, chat).
// Lua menu text (ui.draw_text / ui.text_width / ui.text_height) should follow the menus' own
// scale, not the window size. FontRenderer stretches text with the window (relative to its
// 1920x1080 base), so cancel that out here. kUiTextSize = how big scale 1.0 looks: the font
// is baked at 32 px, so 2/3 gives ~21 px text (capitals ~14 px, letters ~10 px wide).
constexpr float kUiTextSize = 2.0f / 3.0f;
constexpr float kFontBaseW = 1920.0f, kFontBaseH = 1080.0f;   // FontRenderer's base resolution

glm::vec2 uiTextScale(float scale, const Dimensions2D& fb) {
    const float w = fb.width > 0 ? static_cast<float>(fb.width) : kFontBaseW;
    const float h = fb.height > 0 ? static_cast<float>(fb.height) : kFontBaseH;
    return glm::vec2(scale * kUiTextSize * kFontBaseW / w, scale * kUiTextSize * kFontBaseH / h);
}

void resetUIRenderState() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    uidraw::applyClip();          // ui.push_clip (scissor off when there's no clip)
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindVertexArray(0);
}

void drawLoadingScreen(GLuint uiProgram, GLuint fontProgram, float progress, int width, int height) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    auto quad = [&](float x0, float y0, float x1, float y1, glm::vec4 c) {
        const float v[36] = {
            x0, y0, c.r, c.g, c.b, c.a,  x1, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,
            x0, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,  x0, y1, c.r, c.g, c.b, c.a
        };
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glUseProgram(uiProgram);
    quad(-1.0f, -1.0f, 1.0f, 1.0f, glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));
    quad(-0.40f, -0.12f, 0.40f, -0.06f, glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));
    const float fillX = -0.40f + 0.80f * glm::clamp(progress, 0.0f, 1.0f);
    quad(-0.40f, -0.12f, fillX, -0.06f, glm::vec4(0.35f, 0.75f, 0.35f, 1.0f));
    glBindVertexArray(0);

    if (g_FontRenderer) {
        const Dimensions2D screen{width, height};
        glUseProgram(fontProgram);
        g_FontRenderer->renderText(fontProgram, "Loading world...",
                                   width * 0.5f - 120.0f, height * 0.5f + 60.0f,
                                   1.2f, 1.2f, glm::vec4(1.0f), screen);
        const int percent = static_cast<int>(glm::clamp(progress, 0.0f, 1.0f) * 100.0f);
        g_FontRenderer->renderText(fontProgram, std::to_string(percent) + "%",
                                   width * 0.5f - 20.0f, height * 0.5f - 60.0f,
                                   0.9f, 0.9f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), screen);
        if (g_SpawnChunkWaiting) {
            const glm::vec4 warn(1.0f, 0.8f, 0.4f, 1.0f);
            const std::string a = "The ground where you are can't be loaded yet - still trying.";
            const std::string b = "Press Esc to go back to the menu.";
            g_FontRenderer->renderText(fontProgram, a, width * 0.5f - g_FontRenderer->textWidth(a, 0.8f, screen) * 0.5f,
                                       height * 0.5f - 120.0f, 0.8f, 0.8f, warn, screen);
            g_FontRenderer->renderText(fontProgram, b, width * 0.5f - g_FontRenderer->textWidth(b, 0.8f, screen) * 0.5f,
                                       height * 0.5f - 160.0f, 0.8f, 0.8f, warn, screen);
        }
    }
}

// Between two sessions (see main): everything the old mods set up is forgotten, so the next
// session starts as if the game had just been started - with the next world's mods.
void resetForReload() {
    std::cout << "[Mods] Reloading for '" << g_Reload.world << "' (game " << g_Reload.game << ")" << std::endl;
    ++g_SessionGeneration;
    g_Spotlights.clear();                // (mods' spotlights go with the mods)
    g_BloomStrength = 0.6f;              // (and a game's bloom tuning)
    g_SunShaftStrength = 0.5f;
    g_BloomThreshold = 0.75f;
    g_Voxels.clear();
    VoxelVolume::invalidateOpacity();

    // Blocks, fluids and what's worked out from them
    resetBlockDefinitions();   // the registry, and ids start from 1 again
    FluidRegistry::get().reset();
    light::invalidateBlockLights();
    CaveGenerator::reset();

    // Textures: the block texture array and every picture loaded by name
    if (g_TextureArrayID) { glDeleteTextures(1, &g_TextureArrayID); g_TextureArrayID = 0; }
    g_NextTextureLayer = 0;
    g_LayerSources.clear();
    for (const auto& [tex, src] : g_StandardTextureSources) { GLuint t = tex; glDeleteTextures(1, &t); }
    g_StandardTextureSources.clear();
    g_TextureIndex.clear();
    g_TextureIndexRoots.clear();
    if (g_CrackTexture) { glDeleteTextures(1, &g_CrackTexture); g_CrackTexture = 0; }
    g_CrackStages = 10;

    // Which mods ran, and what they set
    g_ModFolders.clear();
    g_ModFileStatus.clear();
    g_ModEnabledAtStartup.clear();
    g_CurrentModPath.clear();
    g_BlockMod.clear();
    g_ItemUseHandlers.clear();
    g_SharedValues.clear();
    g_SharedChanged.clear();
    g_ModToOwnGame.clear();
    g_ModToOwnServer.clear();
    g_ModStorage.clear();
    g_ModStorageDirty = false;
    g_ModelInstances.clear();
    g_NextModelInstance = 1;
    g_EntityVisuals.clear();

    // Settings mods change, back to the engine's own
    g_FootstepDistance = 1.3f;
    g_FlyAllowed = true;
    dims::Registry::get().clear();   // the next mods register their dimensions again
    g_MoveKeyMask = 0xFF;   // every movement control back (a disabled mod may have frozen some: set_player_controls)
    g_PlaceRepeatSeconds = 0.2f;
    g_LuaInventoryOpen = false;
    g_FirstPerson = FirstPersonView{};
    g_LightSettings = LightSettings{};
    g_HeldLight = HeldLight{};
    g_Fog = FogSettings{};
    g_Underwater = UnderwaterSettings{};
    g_UnderwaterBlend = 0.0f;
    g_Inventory = InventorySystem{};
    g_ThirdPerson = false;
    g_ThirdPersonFront = false;
    g_HudHidden = false;
    g_ShowBlockNames = true;
    g_Paused = false;
    g_SettingsInGame = false;

    // The next session: this game, with this world's mods, opening it (main does that)
    g_ActiveGameFolder = g_Reload.game;
    g_SessionWorld = g_Reload.world;
    g_CurrentGameState = GameState::LOADING;
    g_LoadingProgress = 0.0f;
}

int main(int argc, char** argv) {
    // Remember where the game's .exe is: BlockSmithServer is built next to it (My Servers screen)
    if (argc > 0 && argv[0]) {
        std::error_code exeEc;
        const fs::path exe = fs::absolute(fs::path(argv[0]), exeEc);
        if (!exeEc) g_ExeDir = exe.parent_path();
    }
    // Diagnostics (Diagnostics.h): crash reports, logs/latest.log next to settings.txt, a trace
    // under every warning, and (once the graphics card is known) the system report
    diag::init("client");
    diag::startLogFile(settingsFile().parent_path());
    diag::enableWarningTraces();
    std::cout << "BlockSmith " << BLOCKSMITH_VERSION << " (commit " << BLOCKSMITH_COMMIT << ", protocol "
              << net::kVersionName << ")" << std::endl;
    try {
        if (!glfwInit()) return -1;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        // Window title: the game being played, on the BlockSmith engine
        std::string windowTitle = "BlockSmith";
        for (const auto& g : getAvailableGames()) {
            if (g.folderName == g_ActiveGameFolder) windowTitle = g.title + " - BlockSmith";
        }
        GLFWwindow* window = glfwCreateWindow(1280, 720, windowTitle.c_str(), NULL, NULL);
        if (!window) { glfwTerminate(); return -1; }
        // The window's icon is always BlockSmith's. On Windows it comes from the .exe (the
        // GLFW_ICON resource in BlockSmith.rc); elsewhere from icons/blocksmith.png.
#ifndef _WIN32
        {
            const std::vector<std::string> iconFiles = { (g_ExeDir / "icons/blocksmith.png").string(),
                                                         "icons/blocksmith.png", "../../icons/blocksmith.png" };
            for (const auto& f : iconFiles) {
                int iw = 0, ih = 0, ic = 0;
                stbi_set_flip_vertically_on_load(false);
                unsigned char* pixels = stbi_load(f.c_str(), &iw, &ih, &ic, 4);
                if (!pixels) continue;
                GLFWimage icon{ iw, ih, pixels };
                glfwSetWindowIcon(window, 1, &icon);
                stbi_image_free(pixels);
                break;
            }
        }
#endif
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1); // vsync (1 = once per screen refresh)

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "[Error] Failed to initialize GLAD\n";
            return -1;
        }
        installGLErrorReporting();

        // The window and its OpenGL context last the whole run - through every mod reload below
        struct WindowShutdown {
            GLFWwindow* window;
            ~WindowShutdown() {
                glfwDestroyWindow(window);
                glfwTerminate();
            }
        } windowShutdown{ window };

        // ===================================================================================
        // SESSIONS. Everything mods make - the Lua state, blocks, textures, entities, sounds,
        // the menus' objects - belongs to one session. Playing a world that runs other mods
        // (or another game) ends the session behind the loading screen, forgets all of it and
        // starts a new one with that world's mods, which then opens the world: like Luanti, but
        // in the same window without a restart. (The body below keeps its old indentation.)
        // ===================================================================================
        for (;;) {
        {
        if (g_SessionGeneration > 0) {
            // The window's title names the game now loaded
            std::string title = "BlockSmith";
            for (const auto& g : getAvailableGames()) {
                if (g.folderName == g_ActiveGameFolder) title = g.title + " - BlockSmith";
            }
            glfwSetWindowTitle(window, title.c_str());
            windowTitle = title;
        }
        // Frees this session's OpenGL objects LAST. Everything that owns OpenGL objects (the
        // hotbar, fonts, sky, and locals declared below such as the world renderer and
        // particles) deletes them in its destructor, which needs the context to still exist.
        // Locals are destroyed in reverse order, so this one - made before all of them - goes
        // after them. (The window itself goes after every session: WindowShutdown.)
        struct GlShutdown {
            GLFWwindow* window;
            ~GlShutdown() {
                g_ModelInstances.clear();   // placed models free their meshes (OpenGL buffers) here
                g_EntityVisuals.clear();
                delete g_Hotbar;         g_Hotbar = nullptr;
                delete g_FontRenderer;   g_FontRenderer = nullptr;
                delete g_ChatSystem;     g_ChatSystem = nullptr;
                delete g_LightingSystem; g_LightingSystem = nullptr;
                delete g_Skybox;         g_Skybox = nullptr;
                delete g_Clouds;         g_Clouds = nullptr;
                delete g_MainMenu;       g_MainMenu = nullptr;
                glfwSetWindowUserPointer(window, nullptr);   // the session's EngineContext is gone
            }
        } glShutdown{ window };

        // Which graphics card we actually got (see NvOptimusEnablement at the top of this file)
        {
            const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            g_GpuName = renderer ? renderer : "unknown";
            std::cout << "[Graphics] GPU: " << g_GpuName << " (" << (vendor ? vendor : "?") << ", OpenGL "
                      << (version ? version : "?") << ")" << std::endl;
            static bool reported = false;   // (once, not again for every session)
            if (!reported) {
                reported = true;
                diag::addReportLine(std::string("Protocol: ") + net::kVersionName);
                diag::addReportLine("Graphics: " + g_GpuName + " (" + (vendor ? vendor : "?") + ", OpenGL " +
                                    (version ? version : "?") + ")");
                diag::addReportLine("Settings: " + fs::absolute(settingsFile()).string());
                diag::printSystemReport();
            }
        }

        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        g_Hotbar = new Hotbar();
        g_FontRenderer = new FontRenderer(baseResolution);
        g_ChatSystem = new ChatSystem();
        g_LightingSystem = new LightingSystem();
        g_Skybox = new Skybox();
        g_Skybox->init();
        g_Clouds = new Clouds();
        g_Clouds->init();
        if (g_SessionGeneration == 0 && !g_Weather.init()) std::cerr << "[Weather] Couldn't set up weather effects" << std::endl;
        g_Audio.init();   // no sound card: the game runs silently
        for (const char* group : { "master", "music", "effects", "ambient" }) {
            const std::string id = std::string("volume_") + group;
            g_Audio.setVolume(group, currentVideoSetting(id) / 100.0f);
        }
        g_MainMenu = new MainMenu();

        // The game's font, else one that comes with the operating system. With neither, there is
        // no text at all (menus would be blank buttons), so say why - in the console and in the
        // window's title bar, where the player sees it - and carry on with text switched off. The
        // renderer stays (some screens draw through it unconditionally): without an atlas
        // (ready() is false) it draws nothing and measures every text as 0 wide.
        if (!g_FontRenderer->init("fonts/PixeloidSans.ttf")) {
            std::cerr << "[Font] fonts/PixeloidSans.ttf couldn't be used - trying the system's fonts" << std::endl;
            if (!g_FontRenderer->init(FontRenderer::systemFonts())) {
                std::cerr << "[Font Error] No font could be loaded: the game will show no text. Check that the "
                             "'fonts' folder is next to the game (with PixeloidSans.ttf in it)." << std::endl;
                glfwSetWindowTitle(window, (windowTitle + " - no font found: is the 'fonts' folder missing?").c_str());
            }
        }

        EngineContext ctx;
        glfwSetWindowUserPointer(window, &ctx);

        glfwSetCharCallback(window, [](GLFWwindow* win, unsigned int codepoint) {
            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleCharInput(codepoint);
                return;
            }
            uidraw::addChar(codepoint);           // ui.get_typed_text(), for every screen

            EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));

            // Settings screen text fields (player name)
            if (settingsOpen()) {
                std::string ch;
                if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
                if (c) callLuaUI(c->lua, "handle_settings_char", static_cast<int>(codepoint), ch);
                return;
            }

            // Creative inventory search box (inventory.lua decides if it wants the character)
            if (g_CurrentGameState == GameState::PLAYING && g_Inventory.handleCreativeChar(codepoint)) {
                return;
            }

            // Multiplayer screen text fields (player name, server address)
            if (g_CurrentGameState == GameState::MULTIPLAYER) {
                std::string ch;
                if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
                if (c) callLuaUI(c->lua, "handle_multiplayer_char", static_cast<int>(codepoint), ch);
                return;
            }

            // World select screen text fields (world name / seed)
            if (g_CurrentGameState == GameState::WORLD_SELECT) {
                std::string ch;
                if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
                if (c) callLuaUI(c->lua, "handle_world_select_char", static_cast<int>(codepoint), ch);
                return;
            }

            if (c && c->lua) {
                try {
                    sol::state* lua = c->lua;
                    sol::function charLua = (*lua)["on_char_input"];
                    if (charLua.valid()) charLua(codepoint);
                } catch (...) {}
            }
        });

        glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
            EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
            if (!c) return;
            if (!(g_ChatSystem && g_ChatSystem->isOpen)) {
                uidraw::addKey(key, action, mods);   // ui.get_key_events(), for every screen
                if (action == GLFW_PRESS) uidraw::noteShortcut(key, mods, glfwGetTime());   // Ctrl+C/X/V: clipboard allowed
            }

            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleKeyInput(key, scancode, action, mods, win);
                return;
            }

            // Settings (title screen or pause menu): Lua gets every key first, so it can
            // capture a key for rebinding. Otherwise Escape closes the settings.
            if (settingsOpen()) {
                if (callLuaUI(c->lua, "handle_settings_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) closeSettings();
                return;
            }

            // While typing in the creative search box, inventory.lua gets the keys first
            // (so 'E', 'T' or Escape don't close the inventory / open chat mid-typing).
            if (g_CurrentGameState == GameState::PLAYING && g_Inventory.handleCreativeKey(key, action, mods)) {
                return;
            }

            // World select: Lua gets the key first; otherwise Escape goes back to the title
            if (g_CurrentGameState == GameState::WORLD_SELECT) {
                if (callLuaUI(c->lua, "handle_world_select_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Multiplayer screen: same idea
            if (g_CurrentGameState == GameState::MULTIPLAYER) {
                if (callLuaUI(c->lua, "handle_multiplayer_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Mods screen: same idea
            if (g_CurrentGameState == GameState::MODS) {
                if (callLuaUI(c->lua, "handle_mods_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Texture packs screen
            if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                if (callLuaUI(c->lua, "handle_texture_packs_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            if (g_CurrentGameState == GameState::MENU && action == GLFW_PRESS) {
                if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, true);
                return;
            }
            // Stuck loading (the ground under the player can't be loaded): Esc leaves the world
            if (g_CurrentGameState == GameState::LOADING && g_SpawnChunkWaiting && action == GLFW_PRESS &&
                key == GLFW_KEY_ESCAPE) {
                g_SpawnChunkWaiting = false;
                sol::protected_function quit = (*c->lua)["engine"]["quit_to_menu"];
                if (quit.valid()) quit();
                return;
            }

            // Client mods see the keys pressed while playing (they can't take them away)
            if (g_CurrentGameState == GameState::PLAYING && g_ClientMods && !g_Paused &&
                !(g_ChatSystem && g_ChatSystem->isOpen) && glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
                g_ClientMods->key(key, action, mods);
            }

            // A mod's screen is open (engine.set_inventory_open / toggle_main_inventory): it sees
            // every key first. handle_inventory_key(key, action, mods) -> true keeps the key (so a
            // text field can take E, or Escape can leave the field before closing the screen)
            if (g_CurrentGameState == GameState::PLAYING && g_LuaInventoryOpen && !g_Paused &&
                !(g_ChatSystem && g_ChatSystem->isOpen)) {
                if (callLuaUI(c->lua, "handle_inventory_key", key, action, mods)) return;
            }

            if (g_CurrentGameState == GameState::PLAYING && action == GLFW_PRESS) {
                // While paused, only Escape does anything (it resumes the game)
                if (g_Paused) {
                    if (key == GLFW_KEY_ESCAPE) {
                        g_Paused = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }
                    return;
                }

                // Keys come from the Controls settings (see keyActions())
                if (key == keyForAction("chat") || key == keyForAction("command")) {
                    // The chat system expects T (plain chat) or Slash (starts with "/")
                    g_ChatSystem->openChat(win, key == keyForAction("command") ? GLFW_KEY_SLASH : GLFW_KEY_T);
                    return;
                }

                if (key == keyForAction("hide_hud")) {
                    g_HudHidden = !g_HudHidden;
                    return;
                }

                if (key == keyForAction("third_person")) {
                    // Like Minecraft: first person -> behind the player -> in front of them -> first person
                    if (!g_ThirdPerson) { g_ThirdPerson = true; g_ThirdPersonFront = false; }
                    else if (!g_ThirdPersonFront) g_ThirdPersonFront = true;
                    else { g_ThirdPerson = false; g_ThirdPersonFront = false; }
                    return;
                }

                if (key == keyForAction("debug_info")) {
                    g_ShowDebugInfo = !g_ShowDebugInfo;
                    return;
                }

                if (key == keyForAction("inventory")) {
                    // Creative, with a mod's screen open (e.g. the survival inventory, opened from the
                    // creative one's Inventory tab): E just closes it
                    if (g_Creative && g_LuaInventoryOpen && !g_Inventory.isCreativeOpen()) {
                        sol::protected_function toggleInv = (*c->lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) toggleInv();
                        g_LuaInventoryOpen = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }
                    // Survival: no creative menu (free blocks); a mod may show its own inventory
                    if (!g_Creative && !g_Inventory.isCreativeOpen()) {
                        // toggle_main_inventory() -> true if it's now open
                        sol::protected_function toggleInv = (*c->lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) {
                            auto r = toggleInv();
                            g_LuaInventoryOpen = r.valid() && r.get_type() == sol::type::boolean && r.get<bool>();
                            glfwSetInputMode(win, GLFW_CURSOR, g_LuaInventoryOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                        }
                        return;
                    }
                    // Use native C++ inventory toggle & cursor update
                    g_Inventory.toggleCreative();
                    int currentMode = glfwGetInputMode(win, GLFW_CURSOR);
                    if (currentMode == GLFW_CURSOR_DISABLED) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    else glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                    try {
                        sol::state* lua = c->lua;
                        sol::function toggleInv = (*lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) toggleInv();
                    } catch (...) {}
                    return;
                }

                if (key == GLFW_KEY_ESCAPE) {
                    // Escape closes the inventory first; otherwise it opens the pause menu
                    if (g_LuaInventoryOpen) {
                        sol::protected_function toggleInv = (*c->lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) toggleInv();
                        g_LuaInventoryOpen = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }
                    if (g_Inventory.isCreativeOpen()) {
                        g_Inventory.setCreativeOpen(false);
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }
                    g_Paused = true;
                    g_ThumbnailWanted = true;   // a picture of the world for the world list (thumbnail.png)
                    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }
        });

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        initTextureArray();

        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table,
                           sol::lib::coroutine, sol::lib::utf8);
        diag::installLuaTraceback(lua.lua_state());   // every Lua error the game catches: with its Lua stack
        // The player's client mods: their own Lua state, made after (so gone before) the game's
        ClientMods clientMods;
        struct ClientModsPointer {
            explicit ClientModsPointer(ClientMods* m) { g_ClientMods = m; }
            ~ClientModsPointer() { g_ClientMods = nullptr; }
        } clientModsPointer(&clientMods);

        auto compileShader = [](GLenum type, const char* src) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, NULL);
            glCompileShader(shader);
            // A shader this graphics driver won't take: say so (otherwise whatever it draws
            // just doesn't show, with no clue why)
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[2048];
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::cerr << "[Shader] " << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                          << " shader failed on this graphics driver:\n" << log << std::endl;
            }
            return shader;
        };

        GLuint uiProgram = glCreateProgram();
        glAttachShader(uiProgram, compileShader(GL_VERTEX_SHADER, uiVertexShaderSource));
        glAttachShader(uiProgram, compileShader(GL_FRAGMENT_SHADER, uiFragmentShaderSource));
        glLinkProgram(uiProgram);

        GLuint texturedUIProgram = glCreateProgram();
        glAttachShader(texturedUIProgram, compileShader(GL_VERTEX_SHADER, texturedUIVertexShaderSource));
        glAttachShader(texturedUIProgram, compileShader(GL_FRAGMENT_SHADER, texturedUIFragmentShaderSource));
        glLinkProgram(texturedUIProgram);
        glUseProgram(texturedUIProgram);   // no tint unless ui.draw_image asks (uniforms start at 0)
        glUniform4f(glGetUniformLocation(texturedUIProgram, "u_Tint"), 1.0f, 1.0f, 1.0f, 1.0f);
        glUseProgram(0);

        GLuint fontProgram = glCreateProgram();
        glAttachShader(fontProgram, compileShader(GL_VERTEX_SHADER, fontVertexShaderSource));
        glAttachShader(fontProgram, compileShader(GL_FRAGMENT_SHADER, fontFragmentShaderSource));
        glLinkProgram(fontProgram);

        GLuint modelShaderProgram = glCreateProgram();
        glAttachShader(modelShaderProgram, compileShader(GL_VERTEX_SHADER, modelVertexShaderSource));
        glAttachShader(modelShaderProgram, compileShader(GL_FRAGMENT_SHADER, modelFragmentShaderSource));
        glLinkProgram(modelShaderProgram);
        glUseProgram(modelShaderProgram);
        glUniform1f(glGetUniformLocation(modelShaderProgram, "u_WeatherLight"), 1.0f);   // full light until weather says otherwise
        glUniform3f(glGetUniformLocation(modelShaderProgram, "u_LocalLight"), 1.0f, 1.0f, 1.0f);
        glUniform4f(glGetUniformLocation(modelShaderProgram, "u_Tint"), 0.0f, 0.0f, 0.0f, 0.0f);

        // ------------------------------------------------------------------
        // ui.* table
        // ------------------------------------------------------------------
        auto uiTable = lua.create_named_table("ui");
        // Pictures drawn by ui.draw_image, loaded once (by path) and kept
        static std::unordered_map<std::string, GLuint> s_uiImages;
        auto uiImage = [](const std::string& path) {
            auto it = s_uiImages.find(path);
            if (it == s_uiImages.end()) it = s_uiImages.emplace(path, loadStandardTexture(path)).first;
            return it->second;
        };
        // ui.draw_image(path, x, y, w, h [, u0, v0, u1, v1])
        // ui.draw_image(path, x, y, w, h, { u0, v0, u1, v1, tint = {r, g, b [, a]}, alpha,
        //                                   rotation = degrees (around the centre), flip_x, flip_y })
        //   A picture (png/jpg) on screen, in pixels. u0..v1 pick part of the image (0..1), e.g. one
        //   icon from a sheet. Found in the mod's folder / textures/ like block textures.
        uiTable["draw_image"] = [texturedUIProgram, uiImage](const std::string& path, float x, float y, float w, float h,
                                                              sol::variadic_args va) {
            const GLuint tex = uiImage(path);
            if (tex == 0 || w <= 0.0f || h <= 0.0f) return;
            float u0 = 0, v0 = 0, u1 = 1, v1 = 1, rotation = 0;
            float tint[4] = { 1, 1, 1, 1 };
            bool flipX = false, flipY = false;
            if (va.size() >= 1 && va[0].get_type() == sol::type::table) {
                sol::table o = va[0];
                u0 = o.get_or("u0", 0.0f); v0 = o.get_or("v0", 0.0f); u1 = o.get_or("u1", 1.0f); v1 = o.get_or("v1", 1.0f);
                rotation = o.get_or("rotation", 0.0f);
                flipX = o.get_or("flip_x", false);
                flipY = o.get_or("flip_y", false);
                if (o["tint"].get_type() == sol::type::table) {
                    sol::table t = o["tint"];
                    for (int i = 0; i < 4; ++i) tint[i] = t.get_or(i + 1, 1.0f);
                }
                tint[3] *= o.get_or("alpha", 1.0f);
            } else {
                auto num = [&va](size_t i, float d) { return (va.size() > i && va[i].is<float>()) ? va[i].as<float>() : d; };
                u0 = num(0, 0.0f); v0 = num(1, 0.0f); u1 = num(2, 1.0f); v1 = num(3, 1.0f);
            }
            if (flipX) std::swap(u0, u1);
            if (flipY) std::swap(v0, v1);
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            const float sw = static_cast<float>(vp[2]), sh = static_cast<float>(vp[3]);
            if (sw <= 0 || sh <= 0) return;
            // Corners in pixels (bottom-left, bottom-right, top-right, top-left), turned around the centre
            float px[4] = { x, x + w, x + w, x }, py[4] = { y + h, y + h, y, y };
            if (rotation != 0.0f) {
                const float cx = x + w * 0.5f, cy = y + h * 0.5f, ca = std::cos(rotation * 0.01745329f), sa = std::sin(rotation * 0.01745329f);
                for (int i = 0; i < 4; ++i) {
                    const float dx = px[i] - cx, dy = py[i] - cy;
                    px[i] = cx + dx * ca - dy * sa;
                    py[i] = cy + dx * sa + dy * ca;
                }
            }
            float nx[4], ny[4];
            for (int i = 0; i < 4; ++i) { nx[i] = px[i] / sw * 2.0f - 1.0f; ny[i] = 1.0f - py[i] / sh * 2.0f; }
            const float verts[24] = { nx[0], ny[0], u0, v1,  nx[1], ny[1], u1, v1,  nx[2], ny[2], u1, v0,
                                      nx[0], ny[0], u0, v1,  nx[2], ny[2], u1, v0,  nx[3], ny[3], u0, v0 };
            static GLuint vao = 0, vbo = 0;
            if (vao == 0) {
                glGenVertexArrays(1, &vao);
                glGenBuffers(1, &vbo);
                glBindVertexArray(vao);
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
                glEnableVertexAttribArray(1);
            }
            uidraw::flush();                       // shapes drawn before it stay underneath
            uidraw::applyClip();
            glUseProgram(texturedUIProgram);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(glGetUniformLocation(texturedUIProgram, "u_Texture"), 0);
            const GLint tintLoc = glGetUniformLocation(texturedUIProgram, "u_Tint");
            glUniform4f(tintLoc, tint[0], tint[1], tint[2], tint[3]);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glUniform4f(tintLoc, 1.0f, 1.0f, 1.0f, 1.0f);   // (the program is shared: back to no tint)
        };
        // ui.world_thumbnail(world) -> a path for ui.draw_image (the world's thumbnail.png), or nil
        //   if it has none. A newer picture (the world was played since) is loaded fresh.
        uiTable["world_thumbnail"] = [](const std::string& worldName, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            if (worldName.empty() || worldName.find('/') != std::string::npos || worldName.find('\\') != std::string::npos ||
                worldName.find("..") != std::string::npos) return sol::make_object(lv, sol::lua_nil);
            const fs::path file = savesRoot() / worldName / "thumbnail.png";
            std::error_code ec;
            if (!fs::exists(file, ec)) return sol::make_object(lv, sol::lua_nil);
            static std::unordered_map<std::string, fs::file_time_type> seen;
            const std::string path = file.string();
            const auto when = fs::last_write_time(file, ec);
            auto it = seen.find(path);
            if (it != seen.end() && it->second != when) {          // changed: forget the old picture
                auto img = s_uiImages.find(path);
                if (img != s_uiImages.end()) { if (img->second) glDeleteTextures(1, &img->second); s_uiImages.erase(img); }
            }
            seen[path] = when;
            return sol::make_object(lv, path);
        };
        // ui.get_image_size(path) -> width, height in pixels (nil if it can't be loaded)
        uiTable["get_image_size"] = [uiImage](const std::string& path) -> std::tuple<sol::optional<int>, sol::optional<int>> {
            const GLuint tex = uiImage(path);
            if (tex == 0) return { sol::nullopt, sol::nullopt };
            GLint w = 0, h = 0, prev = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
            glBindTexture(GL_TEXTURE_2D, tex);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
            return { w, h };
        };

        // ui.draw_button(x, y, w, h [, state [, danger]]) -> true if drawn: the game's button picture
        //   (menu/button.png, else mods/base/menu/button.png, else the engine's built-in one),
        //   9-slice, like the title screen. state: "normal", "hover", "disabled" or "selected".
        //   danger = true tints it red (delete buttons). Draw the label on top yourself.
        uiTable["draw_button"] = [texturedUIProgram](float x, float y, float w, float h,
                                                      sol::optional<std::string> state, sol::optional<bool> danger) {
            if (!g_MainMenu) return false;
            uidraw::flush();                       // (keeps drawing order)
            uidraw::applyClip();
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            const std::string st = state.value_or("normal");
            const int code = st == "hover" ? 1 : st == "disabled" ? 2 : st == "selected" ? 3 : 0;
            return g_MainMenu->drawButton(texturedUIProgram, x, y, w, h, code, danger.value_or(false), vp[2], vp[3]);
        };
        // ui.get_button_text_color([hovered]) -> r, g, b: the label colour the game asked for in game.conf
        uiTable["get_button_text_color"] = [](sol::optional<bool> hovered) {
            const glm::vec4 c = g_MainMenu ? g_MainMenu->buttonTextColor(hovered.value_or(false))
                                           : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
            return std::make_tuple(c.r, c.g, c.b);
        };

        uiTable["get_mouse_pos"] = [window]() {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            int framebufferWidth, framebufferHeight;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            scaleCursorToFramebuffer(window, x, y, framebufferWidth, framebufferHeight);
            return std::make_tuple(x, y);
        };

        // Adds ui.draw_rect, ui.draw_block_icon, ui.get_all_blocks, ui.get_block_info,
        // ui.get_time, ui.keys, ui.actions (and a 0-based hotbar.* that gets replaced below).
        g_Inventory.registerLuaBindings(lua);

        // ui.draw_text - uses the real font shader. Supports both call styles:
        //   ui.draw_text(text, x, y, r, g, b)                    (old style, scale 1)
        //   ui.draw_text(text, x, y, scale, r, g, b [, a])       (new style)
        uiTable["draw_text"] = [&](const std::string& text, float x, float y, sol::variadic_args va) {
            if (!g_FontRenderer) return;
            uidraw::flush();                       // shapes drawn before the text stay underneath

            std::vector<float> v;
            for (auto arg : va) v.push_back(arg.as<float>());

            float scale = 1.0f, r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            if (v.size() == 3) {
                r = v[0]; g = v[1]; b = v[2];
            } else if (v.size() >= 4) {
                scale = v[0]; r = v[1]; g = v[2]; b = v[3];
                if (v.size() >= 5) a = v[4];
            }

            Dimensions2D windowSize;
            glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);

            resetUIRenderState();
            glUseProgram(fontProgram);
            const glm::vec2 s = uiTextScale(scale, windowSize);   // same size at any window size
            g_FontRenderer->renderText(fontProgram, text, x, y, s.x, s.y, glm::vec4(r, g, b, a), windowSize);
        };

        // ui.text_width(text, scale) -> exact width in pixels that ui.draw_text will use.
        // ui.text_height(scale)     -> height of capital letters above the baseline, in pixels.
        // Menus should measure with these instead of guessing letter sizes.
        uiTable["text_width"] = [window](const std::string& text, sol::optional<float> scale) -> float {
            if (!g_FontRenderer) return 0.0f;
            Dimensions2D windowSize;
            glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
            return g_FontRenderer->textWidth(text, uiTextScale(scale.value_or(1.0f), windowSize).x, windowSize);
        };
        uiTable["text_height"] = [window](sol::optional<float> scale) -> float {
            if (!g_FontRenderer) return 0.0f;
            Dimensions2D windowSize;
            glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
            return g_FontRenderer->capHeight(uiTextScale(scale.value_or(1.0f), windowSize).y, windowSize);
        };

        // ui.draw_server_icon(address, x, y, size) -> true if drawn, false if that server has no icon
        uiTable["draw_server_icon"] = [window, texturedUIProgram](const std::string& address,
                                                                  float x, float y, float size) {
            const ServerStatus* st = g_Pinger.status(address);
            if (!st || st->icon.empty()) return false;
            uidraw::flush();
            const GLuint tex = serverIconTexture(address, st->icon);
            if (!tex) return false;
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            if (fbW <= 0 || fbH <= 0) return false;
            resetUIRenderState();
            drawUITexture(texturedUIProgram, tex, x, y, size, size, fbW, fbH);
            return true;
        };

        // ui.get_mouse() -> x, y, left_down, right_down   (framebuffer pixels, same as clicks)
        uiTable["get_mouse"] = [window]() {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            int fw, fh;
            glfwGetFramebufferSize(window, &fw, &fh);
            scaleCursorToFramebuffer(window, x, y, fw, fh);
            const bool left  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
            const bool right = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            return std::make_tuple(x, y, left, right);
        };

        lua.new_usertype<Bone>("Bone",
            "set_rotation", [](Bone& b, float x, float y, float z) { b.rotation = glm::vec3(x, y, z); },
            "get_rotation", [](Bone& b) { return std::make_tuple(b.rotation.x, b.rotation.y, b.rotation.z); },
            "set_position", [](Bone& b, float x, float y, float z) { b.position = glm::vec3(x, y, z); }
        );

        lua.new_usertype<AnimatedModel>("Model",
            "get_bone", &AnimatedModel::getBone,
            // model:reset_pose() - every bone back to the pose in the model file
            "reset_pose", &AnimatedModel::resetPose,
            // model:is_loaded() - false when the file was missing or broken (load_model still returns a model)
            "is_loaded", [](const AnimatedModel& m) { return m.loaded; }
        );

        // ---- Animations (Bedrock / Blockbench .animation.json, or the ones saved in a .bbmodel) ----
        // local anims = engine.load_animations("player.animation.json")   (mod folder or its models/)
        // local walk = anims:get("animation.player.walk")   (or the short "walk", or nil/"" = the first)
        // walk:apply(model, time [, weight])   pose the model at `time` seconds; weight 0..1 blends
        // walk:get_length(), walk:is_looping(), walk:set_loop(bool), walk:get_name(), anims:names()
        lua.new_usertype<ModelAnimation>("Animation",
            "apply", [](ModelAnimation& a, std::shared_ptr<AnimatedModel> model, float time, sol::optional<float> weight) {
                if (model) a.apply(*model, time, weight.value_or(1.0f));
            },
            "get_length", [](ModelAnimation& a) { return a.length; },
            "is_looping", [](ModelAnimation& a) { return a.loop; },
            "set_loop", [](ModelAnimation& a, bool on) { a.loop = on; },
            "get_name", [](ModelAnimation& a) { return a.name; }
        );
        lua.new_usertype<AnimationSet>("AnimationSet",
            "get", [](AnimationSet& s, sol::optional<std::string> name) { return s.get(name.value_or("")); },
            "names", [](AnimationSet& s) { return sol::as_table(s.names()); }
        );

        // ------------------------------------------------------------------
        // hotbar.* table - ALL slot numbers are 1-based (Lua convention)
        // ------------------------------------------------------------------
        sol::table hotbarTable = lua.create_named_table("hotbar");
        hotbarTable["set_size"] = [](int size) { if (g_Hotbar) g_Hotbar->resize(size); };
        hotbarTable["get_size"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["get_slot_count"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["set_slot_size"] = [](float size) { if (g_Hotbar) g_Hotbar->slotSize = size; };
        hotbarTable["set_position"] = [](float x, float y) { if (g_Hotbar) { g_Hotbar->posX = x; g_Hotbar->posY = y; } };

        // hotbar.set_slot(slot, name [, count])  - name "" or "air" clears the slot
        hotbarTable["set_slot"] = [](int slotIndex, const std::string& blockName, sol::optional<int> count) {
            if (!g_Hotbar) return;
            int adj = slotIndex - 1;
            if (adj < 0 || adj >= g_Hotbar->slotCount) return;
            uint8_t blockId = (blockName.empty() || blockName == "air")
                ? 0 : BlockRegistry::getInstance().getBlockId(blockName);
            g_Hotbar->setSlot(adj, blockId, blockId == 0 ? 0 : count.value_or(64));
        };

        // hotbar.get_slot(slot) -> name, count   ("" and 0 when empty)
        hotbarTable["get_slot"] = [](int slotIndex) {
            if (!g_Hotbar) return std::make_tuple(std::string(), 0);
            HotbarSlot slot = g_Hotbar->getSlot(slotIndex - 1);
            if (slot.blockId == 0) return std::make_tuple(std::string(), 0);
            return std::make_tuple(BlockRegistry::getInstance().getBlock(slot.blockId).name, slot.count);
        };

        // hotbar.get_rect(W, H) -> x, y, w, h: where the bar is drawn (pixels, top-left), so HUD
        // mods can put hearts and hunger right above it
        hotbarTable["get_rect"] = [](float W, float H) {
            float x = 0, y = 0, w = 0, h = 0;
            if (g_Hotbar) hotbarScreenRect(*g_Hotbar, W, H, x, y, w, h);
            return std::make_tuple(x, y, w, h);
        };
        hotbarTable["get_active_slot"] = []() { return g_Hotbar ? g_Hotbar->activeSlot + 1 : 1; };
        hotbarTable["set_active_slot"] = [](int slotIndex) {
            if (g_Hotbar) {
                int adj = slotIndex - 1;
                if (adj >= 0 && adj < g_Hotbar->slotCount) g_Hotbar->activeSlot = adj;
            }
        };
        hotbarTable["is_creative_open"] = []() { return g_Inventory.isCreativeOpen(); };
        // (frees the mouse while it's open and captures it again after, like the inventory key)
        hotbarTable["toggle_creative"] = [window]() {
            g_Inventory.toggleCreative();
            const bool open = g_Inventory.isCreativeOpen();
            glfwSetInputMode(window, GLFW_CURSOR, (open || g_LuaInventoryOpen) ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        };
        // hotbar.set_creative_open(on): opens or closes the creative inventory, freeing the mouse
        // while it (or a mod's screen) is open. The survival inventory uses it to go back to it.
        hotbarTable["set_creative_open"] = [window](bool open) {
            g_Inventory.setCreativeOpen(open);
            glfwSetInputMode(window, GLFW_CURSOR, (open || g_LuaInventoryOpen) ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        };
        hotbarTable["get_slot_block"] = [](int slotIndex) -> std::string {
            if (!g_Hotbar) return "air";
            int adj = slotIndex - 1;
            HotbarSlot slot = g_Hotbar->getSlot(adj);
            if (slot.blockId == 0) return "air";
            return BlockRegistry::getInstance().getBlock(slot.blockId).name;
        };
        hotbarTable["set_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->customTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_selected_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->selectedTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_texture_scale"] = [](float s) {
            if (g_Hotbar) g_Hotbar->textureScale = glm::clamp(s, 0.25f, 4.0f);
        };

        auto skyTable = lua.create_named_table("sky");
        // The time of day (DayTime.h): 0..1 - 0.25 midnight, 0.5 sunrise, 0.75 midday, 0 sunset.
        // sky.set_time jumps to that time (the clock keeps running from there). On a server the
        // server's clock wins: it's sent every few seconds (its own Lua or 'time set' change it).
        // sky.set_time(t [, forward]): forward = true moves on to the next time it's t (so the day
        // count goes up, like Minecraft's /time set); without it, t on the same day
        skyTable["set_time"] = [](double time, sol::optional<bool> forward) {
            if (!g_LightingSystem || !std::isfinite(time)) return;
            g_LightingSystem->customTimeOfDay = -1.0f;
            g_LightingSystem->clock.setTimeOfDay(time, forward.value_or(false));
        };
        // sky.add_time(hours): moves the clock on (or back, negative)
        skyTable["add_time"] = [](double hours) {
            if (!g_LightingSystem || !std::isfinite(hours)) return;
            g_LightingSystem->clock.total = std::max(0.0, g_LightingSystem->clock.total + hours / 24.0);
        };
        skyTable["get_time"] = []() { return g_LightingSystem ? g_LightingSystem->getTimeOfDay() : 0.0f; };
        // sky.get_day(): days since the world began (0 = the first; +1 at each midnight)
        skyTable["get_day"] = []() { return g_LightingSystem ? static_cast<double>(g_LightingSystem->clock.day()) : 0.0; };
        // sky.get_clock(): "07:15", the time of day on a 24-hour clock
        skyTable["get_clock"] = []() { return g_LightingSystem ? g_LightingSystem->clock.clock() : std::string("12:00"); };
        // sky.set_time_speed(s): 1 normal, 2 twice as fast, 0 = time stands still. sky.get_time_speed()
        skyTable["set_time_speed"] = [](double speed) {
            if (g_LightingSystem && std::isfinite(speed)) g_LightingSystem->clock.speed = std::clamp(speed, 0.0, 1000.0);
        };
        skyTable["get_time_speed"] = []() { return g_LightingSystem ? g_LightingSystem->clock.speed : 1.0; };
        // sky.set_day_length(seconds): how long a whole day lasts (default 1200 = 20 minutes). sky.get_day_length()
        skyTable["set_day_length"] = [](double seconds) {
            if (g_LightingSystem && std::isfinite(seconds)) g_LightingSystem->clock.secondsPerDay = std::clamp(seconds, 10.0, 1.0e7);
        };
        skyTable["get_day_length"] = []() { return g_LightingSystem ? g_LightingSystem->clock.secondsPerDay : 1200.0; };
        // Stars: sky.set_stars(density, brightness). density 0..1 (default 0.06), brightness 0 = off (default 0.9)
        skyTable["set_stars"] = [](float density, sol::optional<float> brightness) {
            if (!g_Skybox) return;
            g_Skybox->starDensity = std::max(0.0f, std::min(1.0f, density));
            if (brightness) g_Skybox->starBrightness = std::max(0.0f, *brightness);
        };
        skyTable["get_stars"] = []() -> std::tuple<float, float> {
            return g_Skybox ? std::make_tuple(g_Skybox->starDensity, g_Skybox->starBrightness) : std::make_tuple(0.0f, 0.0f);
        };
        skyTable["set_moon_phase"] = [](int phase) { if (g_LightingSystem) g_LightingSystem->forcedMoonPhase = phase; };
        skyTable["get_moon_phase"] = []() { return g_LightingSystem ? g_LightingSystem->getMoonPhase() : 0; };
        skyTable["set_moon_textures"] = [](sol::table paths) {
            if (!g_LightingSystem) return;
            std::vector<std::string> pList;
            for (int i = 0; i < paths.size(); ++i) pList.push_back(paths[i + 1]);
            g_LightingSystem->setMoonPhaseTextures(pList);
        };
        skyTable["set_rain_intensity"] = [](float intensity) {
            if (g_LightingSystem) g_LightingSystem->weatherRainIntensity = glm::clamp(intensity, 0.0f, 1.0f);
        };
        skyTable["get_rain_intensity"] = []() { return g_LightingSystem ? g_LightingSystem->weatherRainIntensity : 0.0f; };
        skyTable["set_sun_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setSunColor(r, g, b); };
        skyTable["set_ambient_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setAmbientColor(r, g, b); };
        skyTable["set_fog_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setFogColor(r, g, b); };
        skyTable["set_sun_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setSunIntensity(intensity); };
        skyTable["set_ambient_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setAmbientIntensity(intensity); };
        skyTable["reset"] = []() { if (g_LightingSystem) g_LightingSystem->resetToDefaults(); };
        // ---- Weather effects (drawn by WeatherEffects; DECIDED by Lua, see mods/weather) ----
        // sky.set_precipitation{ type = "rain"|"snow"|any, intensity = 0..1, fall_speed, size,
        //     length, sway, wind_x, wind_z, color = {r,g,b,a}, texture = "ash.png", radius,
        //     height, max_particles, streaks = bool }
        //   `type` starts from that preset; other fields change only what you give, so
        //   sky.set_precipitation{ intensity = 0.3 } just thins out the current rain.
        skyTable["set_precipitation"] = [](sol::table t) {
            static std::unordered_map<std::string, GLuint> textures;
            PrecipitationSettings p = g_Weather.precipitation();
            sol::optional<std::string> type = t["type"];
            if (type) {
                const float keep = p.intensity;
                if (*type != p.type) p = WeatherEffects::preset(*type);
                p.intensity = keep;
            }
            p.intensity = t.get_or("intensity", p.intensity);
            p.fallSpeed = t.get_or("fall_speed", p.fallSpeed);
            p.size = t.get_or("size", p.size);
            p.length = t.get_or("length", p.length);
            p.sway = t.get_or("sway", p.sway);
            p.wind.x = t.get_or("wind_x", p.wind.x);
            p.wind.y = t.get_or("wind_z", p.wind.y);
            p.radius = t.get_or("radius", p.radius);
            p.height = t.get_or("height", p.height);
            p.maxParticles = t.get_or("max_particles", p.maxParticles);
            p.streaks = t.get_or("streaks", p.streaks);
            sol::optional<sol::table> color = t["color"];
            if (color) {
                p.color = glm::vec4(color->get_or(1, p.color.r), color->get_or(2, p.color.g),
                                    color->get_or(3, p.color.b), color->get_or(4, p.color.a));
            }
            sol::optional<std::string> tex = t["texture"];
            if (tex) {
                if (tex->empty()) p.texture = 0;
                else {
                    auto it = textures.find(*tex);
                    if (it == textures.end()) it = textures.emplace(*tex, loadStandardTexture(*tex)).first;
                    p.texture = it->second;
                }
            }
            g_Weather.setPrecipitation(p);
        };
        // sky.get_precipitation() -> { type, intensity }
        skyTable["get_precipitation"] = [](sol::this_state ts) {
            sol::state_view lv(ts);
            sol::table t = lv.create_table();
            t["type"] = g_Weather.precipitation().type;
            t["intensity"] = g_Weather.precipitation().intensity;
            return t;
        };
        // sky.set_sky_tint(r, g, b, a): colour laid over the sky (a = 0 off, 1 = all that colour)
        skyTable["set_sky_tint"] = [](float r, float g, float b, float a) {
            g_Weather.setSkyTint(glm::vec4(r, g, b, glm::clamp(a, 0.0f, 1.0f)));
        };
        // sky.set_light(level): sunlight on the world, 1 = normal, 0.5 = stormy, 0 = black
        skyTable["set_light"] = [](float level) { g_Weather.setLight(level); };
        skyTable["get_light"] = []() { return g_Weather.light(); };
        // sky.flash(strength 0..1, seconds): a lightning flash
        skyTable["flash"] = [](float strength, sol::optional<float> seconds) {
            g_Weather.flash(strength, seconds.value_or(0.35f));
        };
        // sky.lightning_bolt(x, z [, top_y [, seconds]]): a bolt from the clouds (or top_y) down to
        // the ground at x, z, flickering for `seconds` (0.45). Only drawn: no fire, no damage (mods
        // decide that on the server). Pair it with sky.flash for the light.
        skyTable["lightning_bolt"] = [](float x, float z, sol::optional<float> topY, sol::optional<float> seconds) {
            if (!std::isfinite(x) || !std::isfinite(z)) return false;
            float ground = g_PlayerPosition.y;
            if (g_WorldInstance) {
                const int g = g_WorldInstance->groundHeightAt(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)));
                if (g >= 0) ground = static_cast<float>(g);
            }
            float top = topY.value_or(g_Clouds ? g_Clouds->settings().height : ground + 80.0f);
            if (!std::isfinite(top)) return false;
            top = std::max(top, ground + 20.0f);
            g_Weather.strike(glm::vec3(x, top, z), glm::vec3(x, ground, z), seconds.value_or(0.45f));
            return true;
        };

        // Moving lights (not tied to a block): sky.add_light(x, y, z, r, g, b, radius, intensity) -> id,
        // sky.move_light(id, x, y, z [, r, g, b, radius, intensity]), sky.remove_light(id).
        // Up to 16 show at once. For lights that stay put, use a block with `light` instead.
        skyTable["add_light"] = [](float x, float y, float z, float r, float g, float b, float radius, float intensity) {
            return g_LightingSystem ? g_LightingSystem->addPointLight(glm::vec3(x, y, z), glm::vec3(r, g, b), radius, intensity) : -1;
        };
        skyTable["move_light"] = [](int id, float x, float y, float z, sol::optional<float> r, sol::optional<float> g,
                                    sol::optional<float> b, sol::optional<float> radius, sol::optional<float> intensity) {
            if (!g_LightingSystem) return;
            const auto& all = g_LightingSystem->getPointLights();
            if (id < 0 || id >= static_cast<int>(all.size()) || !all[id].active) return;
            const PointLight& cur = all[id];
            g_LightingSystem->updatePointLight(id, glm::vec3(x, y, z),
                glm::vec3(r.value_or(cur.color.x), g.value_or(cur.color.y), b.value_or(cur.color.z)),
                radius.value_or(cur.radius), intensity.value_or(cur.intensity));
        };
        skyTable["remove_light"] = [](int id) { if (g_LightingSystem) g_LightingSystem->removePointLight(id); };
        // sky.set_bloom(strength [, threshold]): how much bright things glow (default 0.6) and how
        // bright they must be (0..1, default 0.75) - when the player has Bloom on (Settings).
        // sky.get_bloom() -> on, strength, threshold
        skyTable["set_bloom"] = [](float strength, sol::optional<float> threshold) {
            g_BloomStrength = std::clamp(strength, 0.0f, 2.0f);
            if (threshold) g_BloomThreshold = std::clamp(*threshold, 0.0f, 0.99f);
        };
        // sky.set_sun_shafts(strength): how much the air glows where sunlight gets through
        // (0 = none, default 0.5, up to 2) - when the player's lighting quality is High
        skyTable["set_sun_shafts"] = [](float strength) { g_SunShaftStrength = std::clamp(strength, 0.0f, 2.0f); };
        skyTable["get_bloom"] = []() {
            return std::make_tuple(g_BloomOn && g_GpuCaps.bloom, g_BloomStrength, g_BloomThreshold);
        };

        // ---- Spotlights (Spotlights.h): stage lights with beams, gobos and traced shadows ----
        // lights.add_spot{ x, y, z, pan, tilt, color, dimmer, intensity, angle, softness, range,
        //                  beam, strobe, gobo, gobo_rotation, gobo_spin, shadows } -> id
        // lights.set(id, { ...any of those... }), lights.get(id) -> table, lights.aim(id, x, y, z),
        // lights.remove(id), lights.list() -> ids, lights.clear(), lights.set_haze(amount [, color]),
        // lights.get_haze() -> amount, color
        {
            sol::table lightsTable = lua.create_named_table("lights");
            auto colorFrom = [](const sol::object& o, glm::vec3 fallback) {
                if (o.get_type() == sol::type::string) {
                    std::string h = o.as<std::string>();
                    if (!h.empty() && h[0] == '#') h.erase(0, 1);
                    unsigned v = 0;
                    if (h.size() == 6 && std::sscanf(h.c_str(), "%6x", &v) == 1)
                        return glm::vec3(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
                } else if (o.get_type() == sol::type::table) {
                    sol::table t = o.as<sol::table>();
                    glm::vec3 c(t.get_or(1, fallback.r), t.get_or(2, fallback.g), t.get_or(3, fallback.b));
                    if (c.r > 1.0f || c.g > 1.0f || c.b > 1.0f) c /= 255.0f;   // 0..255 works too
                    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
                }
                return fallback;
            };
            auto apply = [colorFrom](SpotLight& s, const sol::table& t) {
                auto num = [&](const char* k, float& v) { sol::object o = t[k]; if (o.get_type() == sol::type::number) v = o.as<float>(); };
                num("x", s.position.x); num("y", s.position.y); num("z", s.position.z);
                num("pan", s.pan); num("tilt", s.tilt);
                num("dimmer", s.dimmer); num("intensity", s.intensity);
                num("angle", s.angle); num("softness", s.softness); num("range", s.range);
                num("beam", s.beam); num("strobe", s.strobe);
                num("gobo_rotation", s.goboRotation); num("gobo_spin", s.goboSpin);
                s.color = colorFrom(t["color"], s.color);
                sol::object sh = t["shadows"];
                if (sh.get_type() == sol::type::boolean) s.shadows = sh.as<bool>();
                sol::object g = t["gobo"];
                if (g.get_type() == sol::type::string) {
                    const std::string name = g.as<std::string>();
                    s.gobo = name.empty() ? -1 : loadTextureToLayer(name);   // a picture: white lets light through
                } else if (g.get_type() == sol::type::boolean && !g.as<bool>()) {
                    s.gobo = -1;
                }
                s.dimmer = std::clamp(s.dimmer, 0.0f, 1.0f);
                s.intensity = std::max(0.0f, s.intensity);
                s.angle = std::clamp(s.angle, 1.0f, 170.0f);
                s.softness = std::clamp(s.softness, 0.0f, 1.0f);
                s.range = std::clamp(s.range, 0.0f, 256.0f);
                s.beam = std::max(0.0f, s.beam);
                s.strobe = std::clamp(s.strobe, 0.0f, 30.0f);
                s.tilt = std::clamp(s.tilt, -90.0f, 90.0f);
            };
            lightsTable["add_spot"] = [apply](sol::table t) {
                SpotLight s;
                apply(s, t);
                return g_Spotlights.add(s);
            };
            lightsTable["set"] = [apply](int id, sol::table t) {
                SpotLight* s = g_Spotlights.get(id);
                if (!s) return false;
                apply(*s, t);
                return true;
            };
            lightsTable["get"] = [](int id, sol::this_state ts) -> sol::object {
                sol::state_view lv(ts);
                SpotLight* s = g_Spotlights.get(id);
                if (!s) return sol::make_object(lv, sol::lua_nil);
                char hex[8];
                std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(s->color.r * 255 + 0.5f),
                              static_cast<int>(s->color.g * 255 + 0.5f), static_cast<int>(s->color.b * 255 + 0.5f));
                sol::table t = lv.create_table();
                t["x"] = s->position.x; t["y"] = s->position.y; t["z"] = s->position.z;
                t["pan"] = s->pan; t["tilt"] = s->tilt; t["color"] = std::string(hex);
                t["dimmer"] = s->dimmer; t["intensity"] = s->intensity; t["angle"] = s->angle;
                t["softness"] = s->softness; t["range"] = s->range; t["beam"] = s->beam; t["strobe"] = s->strobe;
                t["gobo_rotation"] = s->goboRotation; t["gobo_spin"] = s->goboSpin; t["shadows"] = s->shadows;
                t["has_gobo"] = s->gobo >= 0;
                return sol::make_object(lv, t);
            };
            // lights.aim(id, x, y, z): pan and tilt to point at that spot
            lightsTable["aim"] = [](int id, float x, float y, float z) {
                SpotLight* s = g_Spotlights.get(id);
                if (!s) return false;
                const glm::vec3 d = glm::vec3(x, y, z) - s->position;
                const float flat = std::sqrt(d.x * d.x + d.z * d.z);
                if (flat < 1e-4f && std::fabs(d.y) < 1e-4f) return false;
                s->pan = glm::degrees(std::atan2(d.x, -d.z));
                s->tilt = glm::degrees(std::atan2(d.y, flat));
                return true;
            };
            lightsTable["remove"] = [](int id) { return g_Spotlights.remove(id); };
            lightsTable["list"] = []() { return sol::as_table(g_Spotlights.ids()); };
            lightsTable["clear"] = []() { for (int id : g_Spotlights.ids()) g_Spotlights.remove(id); };
            // ---- dmx.*: Art-Net input (ArtNet.h), read only. The player turns it on in Settings.
            //   dmx.status() -> { mode = "off"|"local"|"network", listening, port, packets, error }
            //   dmx.get(universe, channel) -> 0..255 (channel 1..512; 0 if nothing has come)
            //   dmx.read(universe, first, count) -> { values }  (a fixture's channels in one go)
            //   dmx.frame(universe) -> how many packets have come for it (changes when new data comes)
            //   dmx.age(universe) -> seconds since the last packet (nil: never)
            //   dmx.universes() -> { { universe, frames, age, source }, ... }
            {
                sol::table dmxTable = lua.create_named_table("dmx");
                dmxTable["status"] = [&lua]() {
                    sol::table t = lua.create_table();
                    const auto m = g_ArtNet.mode();
                    t["mode"] = std::string(m == ArtNetInput::Mode::Off ? "off" : m == ArtNetInput::Mode::Local ? "local" : "network");
                    t["listening"] = g_ArtNet.listening();
                    t["port"] = g_ArtNet.port();
                    t["packets"] = static_cast<double>(g_ArtNet.packets());
                    if (!g_ArtNet.lastError().empty()) t["error"] = g_ArtNet.lastError();
                    return t;
                };
                dmxTable["get"] = [](int universe, int channel) {
                    const auto* u = g_ArtNet.universe(universe);
                    if (!u || channel < 1 || channel > 512) return 0;
                    return static_cast<int>(u->data[static_cast<size_t>(channel - 1)]);
                };
                dmxTable["read"] = [&lua](int universe, int first, int count) {
                    sol::table t = lua.create_table();
                    const auto* u = g_ArtNet.universe(universe);
                    count = std::clamp(count, 0, 512);
                    for (int i = 0; i < count; ++i) {
                        const int ch = first + i;
                        t[i + 1] = (u && ch >= 1 && ch <= 512) ? static_cast<int>(u->data[static_cast<size_t>(ch - 1)]) : 0;
                    }
                    return t;
                };
                dmxTable["frame"] = [](int universe) {
                    const auto* u = g_ArtNet.universe(universe);
                    return u ? static_cast<double>(u->frames) : 0.0;
                };
                dmxTable["age"] = [](int universe) -> sol::optional<double> {
                    const auto* u = g_ArtNet.universe(universe);
                    if (!u || u->lastTime < 0.0) return sol::nullopt;
                    return glfwGetTime() - u->lastTime;
                };
                dmxTable["universes"] = [&lua]() {
                    sol::table t = lua.create_table();
                    int i = 1;
                    for (const auto& [addr, u] : g_ArtNet.universes()) {
                        sol::table e = lua.create_table();
                        e["universe"] = addr;
                        e["frames"] = static_cast<double>(u.frames);
                        e["age"] = glfwGetTime() - u.lastTime;
                        e["source"] = u.source;
                        t[i++] = e;
                    }
                    return t;
                };
            }
            lightsTable["set_haze"] = [colorFrom](float amount, sol::optional<sol::object> color) {
                g_Spotlights.haze = std::clamp(amount, 0.0f, 4.0f);
                if (color) g_Spotlights.hazeColor = colorFrom(*color, g_Spotlights.hazeColor);
            };
            // lights.get_quality() -> "off" | "low" | "high": the player's lighting setting (traced
            // shadows), so a mod can go easy on lights where it's off
            lightsTable["get_quality"] = []() {
                return std::string(g_LightQuality >= 2 ? "high" : g_LightQuality == 1 ? "low" : "off");
            };
            lightsTable["get_haze"] = []() {
                char hex[8];
                const glm::vec3 c = g_Spotlights.hazeColor;
                std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", static_cast<int>(c.r * 255 + 0.5f),
                              static_cast<int>(c.g * 255 + 0.5f), static_cast<int>(c.b * 255 + 0.5f));
                return std::make_tuple(g_Spotlights.haze, std::string(hex));
            };
        }

        // ---- Block light look (Light.h) ----
        // sky.set_min_light(v): the darkest anything gets, 0..1 (default 0.02; 0 = pitch black caves)
        skyTable["set_min_light"] = [](float v) { g_LightSettings.minLight = std::max(0.0f, std::min(1.0f, v)); };
        // sky.set_brightness(v): 0 = moody, 1 = bright, like Minecraft's brightness slider (default 0.35)
        skyTable["set_brightness"] = [](float v) { g_LightSettings.brightness = std::max(0.0f, std::min(1.0f, v)); };
        // sky.set_block_light_strength(v): how strong torches and other light sources are (default 1)
        skyTable["set_block_light_strength"] = [](float v) { g_LightSettings.blockLightScale = std::max(0.0f, v); };
        // sky.set_held_light(on): holding a light source lights up around the player (default on)
        skyTable["set_held_light"] = [](bool on) { g_LightSettings.heldLight = on; };

        auto cloudTable = lua.create_named_table("clouds");
        cloudTable["set_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.color = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_night_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.nightColor = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_height"] = [](float y) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.height = y; g_Clouds->setSettings(s);
        };
        cloudTable["set_thickness"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.thickness = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_cell_size"] = [](float c) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.cellSize = c; g_Clouds->setSettings(s);
        };
        cloudTable["set_threshold"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.threshold = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_scroll_speed"] = [](float v) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.scrollSpeed = v; g_Clouds->setSettings(s);
        };
        cloudTable["set_texture"] = [](const std::string& path) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.texturePath = path; g_Clouds->setSettings(s);
        };
        // clouds.set_weather(name, seconds) -> true if known: "clear" (none), "cloudy", "rain",
        // "thunder"/"storm" (one huge dark mass), "snow", "blizzard" (bunched together)
        cloudTable["set_weather"] = [](const std::string& name, sol::optional<float> seconds) {
            return g_Clouds && g_Clouds->setWeather(name, seconds.value_or(0.0f));
        };
        // clouds.set_clump(0..1): 0 scattered puffs, 1 bunched into big masses
        cloudTable["set_clump"] = [](float v) {
            if (!g_Clouds || !std::isfinite(v)) return;
            CloudSettings s = g_Clouds->settings(); s.clump = std::clamp(v, 0.0f, 1.0f); g_Clouds->setSettings(s);
        };

        // ---- particles.* (ParticleSystem.h): Minecraft-style particles, drawn by this game only ----
        //   particles.spawn{ pos = {x,y,z}, velocity = {x,y,z}, gravity = 12 (or acceleration = {x,y,z}),
        //       drag = 0, life = 1, size = 0.1, size_end, color = "#ff8800" / {r,g,b}, color_end,
        //       texture = "particle_flame.png" (or { frames... } played over its life), or
        //       block = "stone" (bits of that block), glow = false, collide = true, bounce = false,
        //       amount = 1, spread = {x,y,z} (random around pos), velocity_spread = {x,y,z} }
        //   particles.add_spawner{ ...the same, plus rate = 10 (a second), time = 0 (seconds, 0 =
        //       until removed), follow_player = false, life = {min, max}, size = {min, max} } -> id
        //   particles.remove_spawner(id)
        //   particles.register_ambient{ nodenames = {...}, action = function(x, y, z, block) end }:
        //       blocks near the player are picked at random every frame (Minecraft's "animate
        //       tick": each block within 16 about 3 times a second); action runs for these kinds,
        //       e.g. torches puffing smoke, lava popping. Particles never leave this game.
        {
            auto particleTable = lua.create_named_table("particles");
            particleTable["spawn"] = [](sol::table def) {
                ParticleSpec s;
                int amount = 1;
                glm::vec3 spread(0.0f), velSpread(0.0f);
                if (!g_Particles || !readParticleDef(def, s, amount, spread, velSpread)) return false;
                if (amount <= 1 && spread == glm::vec3(0.0f) && velSpread == glm::vec3(0.0f)) g_Particles->spawn(s);
                else g_Particles->burst(s, amount, spread, velSpread);
                return true;
            };
            particleTable["add_spawner"] = [](sol::table def, sol::this_state ts) -> sol::object {
                sol::state_view lv(ts);
                ParticleSpawner sp;
                int amount = 1;
                glm::vec3 spread(0.0f), velSpread(0.0f);
                if (!g_Particles || !readParticleDef(def, sp.spec, amount, spread, velSpread)) return sol::make_object(lv, sol::lua_nil);
                sp.minPos = -spread; sp.maxPos = spread;
                sp.minVel = -velSpread; sp.maxVel = velSpread;
                auto range = [&](const char* key, float& lo, float& hi, float def0, float maxV) {
                    sol::object o = def[key];
                    if (o.is<sol::table>()) {
                        sol::table t = o.as<sol::table>();
                        lo = t.get_or(1, def0); hi = t.get_or(2, lo);
                    } else if (o.is<double>()) {
                        lo = hi = static_cast<float>(o.as<double>());
                    } else {
                        lo = hi = def0;
                    }
                    if (!std::isfinite(lo) || !std::isfinite(hi)) lo = hi = def0;
                    lo = std::clamp(lo, 0.01f, maxV); hi = std::clamp(std::max(lo, hi), 0.01f, maxV);
                };
                range("life", sp.minLife, sp.maxLife, 1.0f, 60.0f);
                range("size", sp.minSize, sp.maxSize, 0.1f, 4.0f);
                sp.spec.size = sp.minSize;
                const float rate = def.get_or("rate", 10.0f);
                const float time = def.get_or("time", 0.0f);
                sp.rate = std::isfinite(rate) ? std::clamp(rate, 0.0f, 1000.0f) : 10.0f;
                sp.time = std::isfinite(time) ? std::clamp(time, 0.0f, 3600.0f) : 0.0f;
                sp.followPlayer = def.get_or("follow_player", false);
                const int id = g_Particles->addSpawner(sp);
                return id < 0 ? sol::make_object(lv, sol::lua_nil) : sol::make_object(lv, id);
            };
            particleTable["remove_spawner"] = [](int id) { if (g_Particles) g_Particles->removeSpawner(id); };
            particleTable["count"] = []() { return g_Particles ? static_cast<int>(g_Particles->count()) : 0; };
            particleTable["register_ambient"] = [](sol::table def) {
                AmbientEffect a;
                if (sol::optional<sol::table> names = def["nodenames"]) {
                    for (const auto& kv : *names) if (kv.second.is<std::string>()) a.names.push_back(kv.second.as<std::string>());
                }
                sol::object fn = def["action"];
                if (a.names.empty() || fn.get_type() != sol::type::function) {
                    std::cerr << "[Particles] register_ambient needs nodenames and an action function" << std::endl;
                    return;
                }
                a.action = fn.as<sol::protected_function>();
                a.label = def.get_or("label", std::string("ambient"));
                g_AmbientEffects.push_back(std::move(a));
                g_AmbientResolvedFor = 0;
            };
        }

        auto fogTable = lua.create_named_table("fog");
        fogTable["set_enabled"]      = [](bool on) { g_Fog.enabled = on; };
        fogTable["set_exponential"]  = [](bool on) { g_Fog.exponential = on; };
        fogTable["set_range"]        = [](float s, float e) { g_Fog.start = s; g_Fog.end = e; };
        fogTable["set_density"]      = [](float d) { g_Fog.density = d; };
        fogTable["set_rain_scale"]   = [](float s) { g_Fog.rainScale = glm::clamp(s, 0.0f, 1.0f); };
        fogTable["set_day_color"]    = [](float r, float g, float b) { g_Fog.dayColor = glm::vec3(r, g, b); };
        fogTable["set_sunset_color"] = [](float r, float g, float b) { g_Fog.sunsetColor = glm::vec3(r, g, b); };
        fogTable["set_night_color"]  = [](float r, float g, float b) { g_Fog.nightColor = glm::vec3(r, g, b); };

        auto chatTable = lua.create_named_table("chat");
        chatTable["register_command"] = [](const std::string& name,
                                          const std::string& usage,
                                          const std::string& description) {
            if (g_ChatSystem) g_ChatSystem->registerCommand(name, usage, description);
        };
        chatTable["send"] = [](const std::string& text,
                               sol::optional<float> r,
                               sol::optional<float> g,
                               sol::optional<float> b) {
            if (!g_ChatSystem) return;
            if (r.has_value() && g.has_value() && b.has_value()) {
                g_ChatSystem->addMessage(text, glm::vec3(*r, *g, *b));
            } else {
                g_ChatSystem->addMessage(text);
            }
        };

        // Tab in chat (ChatSystem::refreshSuggestions): mods' completions for their commands
        // (on_chat_complete(words, word) -> { ... }), the server's when online (asked, and
        // shown when its answer comes), and player names in plain chat
        g_ChatSystem->setCompleter([&lua](const std::string& head, const std::vector<std::string>& before,
                                          const std::string& word, bool command) {
            std::vector<ChatSystem::Suggestion> out;
            // (the server sends "name\tlabel" for command names: the label is what the list shows)
            auto add = [&out](const std::string& x) {
                if (x.empty()) return;
                const size_t tab = x.find('\t');
                if (tab == std::string::npos) out.push_back({x, ""});
                else if (tab > 0) out.push_back({x.substr(0, tab), x.substr(tab + 1)});
            };
            const bool joined = g_Client.state() == GameClient::State::Joined;
            if (command && !before.empty()) {
                try {
                    sol::protected_function fn = lua["on_chat_complete"];
                    if (fn.valid()) {
                        auto r = fn(sol::as_table(before), word);
                        if (r.valid() && r.get_type() == sol::type::table) {
                            sol::table t = r;
                            for (size_t i = 1; i <= t.size() && out.size() < 200; ++i) {
                                sol::object v = t[i];
                                if (v.is<std::string>()) add(v.as<std::string>());
                            }
                        }
                    }
                } catch (...) {}
            }
            if (joined) {
                std::vector<std::string> remote;
                if (g_Client.completionFor(head, remote)) { for (const auto& x : remote) add(x); }
                else g_Client.requestCompletion(head);
                if (!command) {
                    add(playerName());
                    for (const auto& [id, v] : g_Client.players()) add(v.name);
                }
            } else {
                // Our own world (or hosting): player names, and the engine's own /gamemode
                if (!command) {
                    add(playerName());
                    for (const auto& [id, v] : g_Server.players()) add(v.name);
                } else if (before.size() == 1 && (before[0] == "gamemode" || before[0] == "gm")) {
                    add("survival"); add("creative");
                } else if (before.size() == 2 && (before[0] == "gamemode" || before[0] == "gm")) {
                    for (const auto& [id, v] : g_Server.players()) add(v.name);
                }
            }
            return out;
        });
        g_ChatSystem->setCommandHandler([&lua](const std::string& command) -> bool {
            // /netstats - frame rate, ping and how often other players' positions arrive
            if (command == "netstats") {
                const glm::vec3 info(0.6f, 0.9f, 1.0f);
                g_ChatSystem->addMessage("FPS: " + std::to_string(static_cast<int>(g_Fps)), info);
                std::vector<std::string> lines;
                if (g_Server.running()) lines = g_Server.statsLines();
                else lines = g_Client.statsLines();
                for (const auto& l : lines) g_ChatSystem->addMessage(l, info);
                return true;
            }
            // /name <new name> - built in, so it works in every game
            if (command == "name" || command.rfind("name ", 0) == 0) {
                std::string name = command.size() > 5 ? command.substr(5) : std::string();
                name.erase(0, name.find_first_not_of(' '));
                name.erase(name.find_last_not_of(' ') + 1);
                if (name.empty()) {
                    g_ChatSystem->addMessage("Your name is " + playerName() + ". Use /name <new name> to change it.");
                    return true;
                }
                std::string error;
                if (changePlayerName(name, error)) g_ChatSystem->addMessage("Your name is now " + name);
                else g_ChatSystem->addMessage(error, glm::vec3(1.0f, 0.55f, 0.55f));
                return true;
            }
            // Hosting: /gamemode <mode> <player> sets a joined player's mode (you're the admin of
            // your own LAN game; they can't change it themselves)
            if (g_Server.running()) {
                std::istringstream gs(command);
                std::string w0, w1, w2;
                gs >> w0 >> w1 >> w2;
                if ((w0 == "gamemode" || w0 == "gm") && !w2.empty()) {
                    const std::string mode = (w1 == "creative" || w1 == "c" || w1 == "1") ? "creative"
                                           : (w1 == "survival" || w1 == "s" || w1 == "0") ? "survival" : "";
                    if (mode.empty()) {
                        g_ChatSystem->addMessage("Usage: /gamemode survival|creative [player]", glm::vec3(1.0f, 0.55f, 0.55f));
                    } else if (g_Server.setPlayerMode(w2, mode)) {
                        g_ChatSystem->addMessage(w2 + "'s game mode is now " + mode, glm::vec3(0.75f, 0.85f, 1.0f));
                        g_Server.messagePlayer(w2, "Your game mode is now " + mode + " (by the host)", glm::vec3(0.6f, 0.9f, 1.0f));
                    } else {
                        g_ChatSystem->addMessage(w2 + " isn't in your game", glm::vec3(1.0f, 0.55f, 0.55f));
                    }
                    return true;
                }
            }
            bool handledByLua = false;
            try {
                sol::protected_function fn = lua["on_chat_command"];
                if (fn.valid()) {
                    // on_chat_command(text, player, is_operator, reply) - same arguments as on a
                    // dedicated server, so one mod works in both places
                    std::function<void(const std::string&)> reply = [](const std::string& t) {
                        if (g_ChatSystem) g_ChatSystem->addMessage(t, glm::vec3(0.75f, 0.85f, 1.0f));
                    };
                    const bool isOp = g_Client.state() == GameClient::State::Joined ? g_Client.isOperator() : true;
                    sol::protected_function_result result = fn(command, playerName(), isOp, reply);
                    if (result.valid() && result.get_type() == sol::type::boolean) handledByLua = result.get<bool>();
                }
            } catch (...) {}
            if (handledByLua) return true;

            // Online: the server runs everything else (/list, /msg, and for operators /kick,
            // /ban, /save, /mods...). It answers in chat.
            if (g_Client.state() == GameClient::State::Joined) {
                const std::string word = command.substr(0, command.find(' '));
                if (word == "help") {
                    g_Client.sendCommand(command);   // the server's commands...
                    return false;                     // ...and this game's /help as well
                }
                if (word != "clear") {
                    g_Client.sendCommand(command);
                    return true;
                }
            }
            return false;
        });

        auto worldTable = lua.create_named_table("world");
        worldTable["set_render_distance"] = [](int chunks) { if (g_WorldInstance) g_WorldInstance->setRenderDistance(chunks); };
        worldTable["get_render_distance"] = []() { return g_WorldInstance ? g_WorldInstance->renderDistance() : 10; };
        worldTable["get_falling_blocks"] = [](sol::this_state ts) {
        // world.get_falling_blocks() -> { { name, x, y, z }, ... }: sand, gravel... in the air now.
        // x, y, z: the block's bottom corner (y has a fraction while it falls)
        sol::state_view lv(ts);
        sol::table out = lv.create_table();
        if (apiWorld()) {
            int i = 1;
            for (const auto& f : apiWorld()->fallingBlocks()) {
                sol::table t = lv.create_table();
                t["name"] = BlockRegistry::getInstance().getBlock(f.id).name;
                t["x"] = f.pos.x; t["y"] = f.pos.y; t["z"] = f.pos.z;
                out[i++] = t;
            }
        }
        return out;
    };
        worldTable["set_load_budget"]      = [](float ms) { if (g_WorldInstance) g_WorldInstance->setLoadBudgetMs(ms); };
        worldTable["set_mesh_budget"]      = [](float ms) { if (g_WorldRenderer) g_WorldRenderer->setMeshBudgetMs(ms); };
        worldTable["get_loaded_chunks"]   = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->loadedChunkCount()) : 0; };
        worldTable["get_pending_chunks"]  = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->pendingLoadCount()) : 0; };
        // world.set_caves_enabled(false) turns off the built-in caves (e.g. if your mapgen makes its own)
        worldTable["set_caves_enabled"]   = [](bool on) { CaveGenerator::setEnabled(on); };
        worldTable["get_caves_enabled"]   = []() { return CaveGenerator::isEnabled(); };
        // world.set_cave_settings({ tunnels, tunnel_frequency, ravines, caverns, noodles, lava_level... })
        worldTable["set_cave_settings"]   = [](sol::table t) { applyCaveSettings(t); };
        worldTable["get_cave_settings"]   = [](sol::this_state ts) { return caveSettingsTable(ts); };

        auto underwaterTable = lua.create_named_table("underwater");
        underwaterTable["set_shallow_color"] = [](float r, float g, float b) { g_Underwater.shallowColor = glm::vec3(r, g, b); g_Underwater.colorsFromLua = true; };
        underwaterTable["set_deep_color"]    = [](float r, float g, float b) { g_Underwater.deepColor = glm::vec3(r, g, b); g_Underwater.colorsFromLua = true; };
        // underwater.get_water_color() -> r, g, b (0..1): the water block's own colour (its texture
        // and tint), e.g. to build biome water colours from in get_water_color(x, z)
        underwaterTable["get_water_color"]   = []() {
            const glm::vec3 c = waterBlockColors().shallow;
            return std::make_tuple(c.r, c.g, c.b);
        };
        // underwater.use_water_color() - back to colours taken from the water block (the default)
        underwaterTable["use_water_color"]   = []() { g_Underwater.colorsFromLua = false; };
        underwaterTable["set_visibility"]    = [](float shallow, float deep) {
            g_Underwater.shallowVisibility = glm::max(shallow, 1.0f);
            g_Underwater.deepVisibility    = glm::max(deep, 1.0f);
        };
        underwaterTable["set_full_depth"]    = [](float d) { g_Underwater.fullDepth = glm::max(d, 1.0f); };
        underwaterTable["set_fade"]          = [](float inSeconds, float outSeconds) {
            g_Underwater.fadeInSeconds  = glm::max(inSeconds, 0.01f);
            g_Underwater.fadeOutSeconds = glm::max(outSeconds, 0.01f);
        };
        underwaterTable["set_tint_strength"] = [](float s) { g_Underwater.tintStrength = glm::clamp(s, 0.0f, 1.0f); };

        auto menuTable = lua.create_named_table("menu");
        menuTable["get_games"] = [&]() {
            auto games = getAvailableGames();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& g : games) {
                sol::table gt = lua.create_table();
                gt["folder"] = g.folderName;
                gt["title"] = g.title;
                gt["author"] = g.author;
                gt["description"] = g.description;
                t[idx++] = gt;
            }
            return t;
        };
        menuTable["get_active_game"] = []() { return g_ActiveGameFolder; };
        menuTable["set_active_game"] = [](std::string folderName) { g_ActiveGameFolder = folderName; };

        auto engineTable = lua.create_named_table("engine");
        dims::installLua(lua);   // dimensions.register / get / list (DimensionsLua.h)
        // Server-side events and player functions (ServerLuaApi.h): in singleplayer and when
        // hosting a LAN game, the same as on the dedicated server
        g_ServerApi.lua = &lua;
        g_ServerApi.server = &g_Server;
        g_ServerApi.dedicated = false;
        g_ServerApi.timeLimit = 5.0;
        g_ServerApi.isOperator = [](const std::string&) { return false; };   // LAN guests never are (the host is local)
        g_ServerApi.localPlayer = [](ServerLuaApi::PlayerInfo& p) {
            if (!g_PlayerInstance || !g_WorldInstance || g_Client.active()) return false;   // (on a server it decides)
            if (g_CurrentGameState != GameState::PLAYING && g_CurrentGameState != GameState::LOADING) return false;
            p.name = playerName();
            p.pos = g_PlayerInstance->position;
            p.mode = g_Creative ? "creative" : "survival";
            p.op = true;
            p.dimension = g_WorldInstance->dimension();
            return true;
        };
        // Dimensions: this game's player moves here; anyone else asks the server we're hosting
        g_ServerApi.changeDimension = [](const std::string& name, const std::string& d, const glm::vec3& pos) {
            if (name == playerName() && !g_Client.active()) return changeLocalDimension(d, pos);
            return g_Server.running() && g_Server.setPlayerDimension(name, d, pos);
        };
        g_ServerApi.loadArea = [&lua](const std::string& d, int x, int z, int radius) {
            if (g_Client.active()) return false;   // (joined: the server's world, not ours to load)
            World* w = g_DimWorlds.get(d);
            if (!w) return false;
            g_DimWorlds.touch(d);
            const int r = (radius + CHUNK_WIDTH - 1) / CHUNK_WIDTH;
            const int ccx = static_cast<int>(std::floor(x / static_cast<float>(CHUNK_WIDTH)));
            const int ccz = static_cast<int>(std::floor(z / static_cast<float>(CHUNK_WIDTH)));
            bool ok = true;
            for (int cx = ccx - r; cx <= ccx + r; ++cx)
                for (int cz = ccz - r; cz <= ccz + r; ++cz)
                    if (!w->chunkData({cx, cz})) ok = w->loadChunk(cx, cz, lua) && ok;
            return ok;
        };
        g_ServerApi.loadDimension = [](const std::string& d) {
            World* w = g_DimWorlds.get(d);
            if (w) g_DimWorlds.touch(d);
            return w != nullptr;
        };
        g_ServerApi.localMessage = [](const std::string& text, const glm::vec3& color) {
            if (g_ChatSystem) g_ChatSystem->addMessage(text, color);
        };
        g_ServerApi.localModMessage = [](const std::string& channel, const std::string& data) {
            if (g_ModToOwnGame.size() >= 4096) return false;   // (full: dropped, and not counted as sent)
            g_ModToOwnGame.push_back({ channel, data });
            return true;
        };
        g_ServerApi.localTeleport = [](const glm::vec3& pos) {
            if (!g_PlayerInstance) return;
            g_PlayerInstance->position = pos;
            g_PlayerInstance->velocity = glm::vec3(0.0f);
        };
        g_ServerApi.fillServerInfo = [](sol::table& t) {
            t["name"] = playerName();
            t["world"] = g_ActiveWorldName;
            t["hosting"] = g_Server.running();
        };
        g_ServerApi.addFunctions(engineTable);
        // Block text (signs): reading and setting where the world runs; this game's player's own
        // edits go to the server when joined, else through the same checks here
        blockstate::install(engineTable, []() { return apiWorld(); });   // BlockStateLua.h
        blocktext::install(engineTable, []() { return apiWorld(); }, []() { return !g_Client.active(); },
            [](const glm::ivec3& pos, int side, const std::vector<std::string>& linesIn) {
                if (g_Client.state() == GameClient::State::Joined) return g_Client.sendBlockText(pos, side, linesIn);
                if (g_Client.active()) return false;
                World* w = apiWorld();
                if (!w) return false;
                const uint8_t id = w->getBlockAt(pos);
                if (id == 0 || !BlockRegistry::getInstance().getBlock(id).holdsText) return false;
                const BlockText* old = w->getBlockTextAt(pos);
                if (old && old->waxed) return false;
                std::vector<std::string> lines = linesIn;
                std::string refusal;
                if (!g_ServerApi.canEditText(playerName(), pos, side, lines, refusal)) {
                    if (!refusal.empty() && g_ChatSystem) g_ChatSystem->addMessage(refusal, glm::vec3(1.0f, 0.55f, 0.55f));
                    return false;
                }
                BlockText t = old ? *old : BlockText();
                (side == 1 ? t.back : t.front).lines = lines;
                return w->setBlockTextAt(pos, t);
            });
        engineTable["register_block"] = &registerBlockFromLua;
        engineTable["get_block_id"] = &getBlockIdLua;
        engineTable["set_fov"] = [](float fov) { g_FieldOfView = fov; };
        // engine.is_server() -> false here, true on BlockSmithServer (for mods that run on both)
        engineTable["is_server"] = []() { return false; };
        engineTable["set_sun_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setSunTexture(path); };
        engineTable["set_moon_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setMoonTexture(path); };

        // engine.load_model(path [, texture [, scale]])
        //   path    - .json (Bedrock geometry), .bbmodel (a Blockbench project, textures and all) or
        //             .b3d (Luanti mesh), found in the mod's folder or models/
        //   texture - image for the model; defaults to the one inside a .bbmodel / named inside a .b3d
        //   scale   - size multiplier; .b3d models default to 0.1 (10 units = 1 block)
        engineTable["load_model"] = [](std::string path, sol::optional<std::string> texture, sol::optional<float> scale) {
            auto model = std::make_shared<AnimatedModel>();
            const std::string resolved = resolveAssetPath(path, "models");
            if (resolved.empty()) {
                std::cerr << "[Model Error] Model file not found: " << path
                          << " (looked in the mod's folder and its models/ folder)" << std::endl;
                return model;
            }
            model->loadFromFile(resolved);
            loadModelTextures(*model, texture.value_or(""));
            if (scale.has_value()) model->scale = scale.value();
            return model;
        };

        engineTable["load_animations"] = [](const std::string& path) -> std::shared_ptr<AnimationSet> {
            const std::string resolved = resolveAssetPath(path, "models");
            if (resolved.empty()) {
                std::cerr << "[Animation] File not found: " << path
                          << " (looked in the mod's folder and its models/ folder)" << std::endl;
                return nullptr;
            }
            auto set = std::make_shared<AnimationSet>();
            if (!set->loadFromFile(resolved)) return nullptr;
            return set;
        };

        // ---- Models in the world (mobs, decorations, ...) ----
        // engine.add_model(model, x, y, z [, yaw [, scale]]) -> id   (x,y,z = feet position, yaw in degrees)
        engineTable["add_model"] = [](std::shared_ptr<AnimatedModel> model, float x, float y, float z,
                                      sol::optional<float> yaw, sol::optional<float> scale) {
            ModelInstance inst;
            inst.model = model;
            inst.position = glm::vec3(x, y, z);
            inst.yaw = yaw.value_or(0.0f);
            inst.scale = scale.value_or(1.0f);
            const int id = g_NextModelInstance++;
            g_ModelInstances[id] = inst;
            return id;
        };
        // engine.move_model(id, x, y, z [, yaw])
        engineTable["move_model"] = [](int id, float x, float y, float z, sol::optional<float> yaw) {
            auto it = g_ModelInstances.find(id);
            if (it == g_ModelInstances.end()) return;
            it->second.position = glm::vec3(x, y, z);
            if (yaw.has_value()) it->second.yaw = yaw.value();
        };
        engineTable["set_model_visible"] = [](int id, bool visible) {
            auto it = g_ModelInstances.find(id);
            if (it != g_ModelInstances.end()) it->second.visible = visible;
        };
        engineTable["remove_model"] = [](int id) { g_ModelInstances.erase(id); };

        // engine.drop_item(item, count [, x, y, z [, vx, vy, vz [, pickup_delay]]]) -> true if dropped
        //   Into the world as an item to pick up. Without a position: thrown from your eyes the way
        //   you look, as Q does (pickup delay 2 s). Works joined to a server too (it makes it).
        engineTable["drop_item"] = [](const std::string& item, sol::optional<int> count, sol::optional<float> x,
                                      sol::optional<float> y, sol::optional<float> z, sol::optional<float> vx,
                                      sol::optional<float> vy, sol::optional<float> vz, sol::optional<float> delay) {
            if (!g_PlayerInstance) return false;
            const Player& pl = *g_PlayerInstance;
            const glm::vec3 look = glm::normalize(glm::vec3(std::cos(glm::radians(pl.yaw)) * std::cos(glm::radians(pl.pitch)),
                                                            std::sin(glm::radians(pl.pitch)),
                                                            std::sin(glm::radians(pl.yaw)) * std::cos(glm::radians(pl.pitch))));
            glm::vec3 pos = pl.position + glm::vec3(0.0f, 1.32f, 0.0f);   // eyes, a little lower (Minecraft)
            glm::vec3 vel = look * 6.0f + glm::vec3(0.0f, 2.0f, 0.0f);
            float pickup = 2.0f;
            if (x && y && z) { pos = glm::vec3(*x, *y, *z); vel = glm::vec3(0.0f, 2.0f, 0.0f); pickup = 0.5f; }
            if (vx && vy && vz) vel = glm::vec3(*vx, *vy, *vz);
            if (delay) pickup = std::clamp(*delay, 0.0f, 10.0f);
            return dropItemInWorld(item, std::clamp(count.value_or(1), 1, 64 * 64), pos, vel, pickup);
        };

        // engine.request_harvest(item, x, y, z) -> true if asked: joined to a server, asks it to use
        //   `item` (the scythe) on that block. The server works out what that does from its own
        //   world, checks the player may change those blocks, and makes the change and whatever it
        //   yields together (its mods' on_player_harvest). Nothing happens here; the blocks and
        //   items arrive from the server. Not joined: false (the game does it itself).
        engineTable["request_harvest"] = [](const std::string& item, int x, int y, int z) {
            if (g_Client.state() != GameClient::State::Joined || item.empty() || item.size() > 64) return false;
            g_Client.sendHarvest(item, glm::ivec3(x, y, z));
            return true;
        };

        // ---- entities.* (EntityLua.h): mobs and dropped items ----
        {
            EntityLuaHost host;
            host.manager = &g_Entities;
            host.isAuthority = [] { return entitiesHere(); };
            host.players = [] {
                std::vector<std::pair<std::string, glm::vec3>> out;
                if (g_PlayerInstance && g_CurrentGameState == GameState::PLAYING) out.push_back({playerName(), g_PlayerInstance->position});
                if (g_Server.running()) for (const auto& p : g_Server.playerPositions()) out.push_back(p);
                return out;
            };
            // Dimensions: each player with where they are, and each loaded dimension's world
            host.playerPlaces = [] {
                std::vector<std::tuple<std::string, glm::vec3, std::string>> out;
                if (g_PlayerInstance && g_WorldInstance && g_CurrentGameState == GameState::PLAYING)
                    out.emplace_back(playerName(), g_PlayerInstance->position, g_WorldInstance->dimension());
                if (g_Server.running()) for (const auto& p : g_Server.playerPlaces()) out.emplace_back(p.name, p.pos, p.dimension);
                return out;
            };
            host.worldFor = [](const std::string& d) -> const World* { return g_DimWorlds.find(d); };
            // Hurting a player: theirs to decide (the survival mod's on_server_action "damage")
            host.damagePlayer = [&lua](const std::string& name, float amount, const std::string& reason) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%g", amount);
                if (name == playerName()) {
                    sol::protected_function hook = lua["on_server_action"];
                    if (hook.valid()) {
                        auto r = hook("damage", std::string(buf), reason);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_server_action: " << err.what() << std::endl; }
                    }
                } else if (g_Server.running()) {
                    g_Server.sendAction(name, "damage", buf, reason);
                }
            };
            host.giveItem = [](const std::string& name, const std::string& item, int count) {
                if (name == playerName()) return canPickUpItem(item, count) && giveHotbarItem(item, count);
                return g_Server.running() && g_Server.sendAction(name, "give", item, std::to_string(count));
            };
            host.lightLevel = [](int x, int y, int z) {
                const uint16_t l = apiWorld() ? apiWorld()->getLightAt(glm::ivec3(x, y, z)) : ChunkData::kFullSky;
                float day = 1.0f;
                if (g_LightingSystem) day = std::max(0.0f, std::min(1.0f, g_LightingSystem->getSunDirection().y * 2.0f + 0.5f));
                const int skyNow = std::max(0, light::sky(l) - static_cast<int>(std::lround((1.0f - day) * 11.0f)));
                return std::max(skyNow, std::max(light::red(l), std::max(light::green(l), light::blue(l))));
            };
            host.isDay = [] {
                if (!g_LightingSystem) return true;
                const double t = g_LightingSystem->clock.time();
                return t >= 0.5 && t < 0.98;
            };
            host.spawningEnabled = [] {
                const auto it = g_SharedValues.find("gamerule.doMobSpawning");
                return it == g_SharedValues.end() || it->second != "false";
            };
            // Each type's looks: its model, texture and animations (from the mod's folder)
            host.onRegister = [](const std::string& name, const sol::table& def) {
                EntityVisual v;
                const std::string modelFile = def.get_or("model", std::string());
                v.scale = def.get_or("scale", 1.0f);
                if (!modelFile.empty()) {
                    const std::string resolved = resolveAssetPath(modelFile, "models");
                    if (resolved.empty()) {
                        std::cerr << "[Entities] " << name << ": model not found: " << modelFile
                                  << " (looked in the mod's folder and its models/ folder)" << std::endl;
                    } else {
                        v.model = std::make_shared<AnimatedModel>();
                        v.model->loadFromFile(resolved);
                        loadModelTextures(*v.model, def.get_or("texture", std::string()));
                    }
                }
                const std::string animFile = def.get_or("animations", std::string());
                if (!animFile.empty()) {
                    const std::string resolved = resolveAssetPath(animFile, "models");
                    auto set = std::make_shared<AnimationSet>();
                    if (!resolved.empty() && set->loadFromFile(resolved)) v.anims = set;
                    else std::cerr << "[Entities] " << name << ": animations not found: " << animFile << std::endl;
                }
                g_EntityVisuals[name] = v;
            };
            g_EntityLua.bind(lua, host);
        }
        // engine.register_random_tick / register_abm / register_lbm: how the world changes by itself
        g_BlockMods.bind(lua, engineTable);
        g_BlockMods.randomTickSpeed = [] {       // the randomTickSpeed game rule (shared by /gamerule)
            const auto it = g_SharedValues.find("gamerule.randomTickSpeed");
            if (it == g_SharedValues.end() || it->second.empty()) return 3;
            try { return std::stoi(it->second); } catch (...) { return 3; }
        };
        engineTable["is_third_person"] = []() { return g_ThirdPerson; };
        // engine.set_hud_visible(on) / engine.is_hud_visible(): the HUD, crosshair and hand (F1)
        engineTable["set_hud_visible"] = [](bool on) { g_HudHidden = !on; };
        // engine.set_block_name_display(on): the name of the block pointed at, and of the held
        //   item after switching (on by default). engine.get_block_description(name) -> "Oak Log"
        engineTable["set_block_name_display"] = [](bool on) { g_ShowBlockNames = on; };
        engineTable["get_block_description"] = [](const std::string& name) -> std::string {
            auto& reg = BlockRegistry::getInstance();
            const uint8_t id = reg.getBlockId(name);
            return id == 0 ? std::string() : blockDisplayName(reg.getBlock(id));
        };
        engineTable["is_hud_visible"] = []() { return !g_HudHidden; };
        // engine.set_player_look(yaw, pitch): where the player looks, in degrees (yaw -90 = north,
        //   -Z; 0 = east, +X; pitch 90 = straight up). engine.get_player_look() -> yaw, pitch
        engineTable["set_player_look"] = [](float yaw, float pitch) {
            if (!g_PlayerInstance || !std::isfinite(yaw) || !std::isfinite(pitch)) return false;
            g_PlayerInstance->yaw = yaw;
            // (closer to straight up or down than this, the view maths runs out of precision and nothing draws)
            g_PlayerInstance->pitch = std::clamp(pitch, -89.9f, 89.9f);
            return true;
        };
        engineTable["get_player_look"] = []() -> std::tuple<float, float> {
            if (!g_PlayerInstance) return { -90.0f, 0.0f };
            return { g_PlayerInstance->yaw, g_PlayerInstance->pitch };
        };
        // engine.get_camera_mode() -> "first", "back" or "front"; engine.set_camera_mode(mode) (F5 cycles them)
        engineTable["get_camera_mode"] = []() -> std::string {
            return !g_ThirdPerson ? "first" : (g_ThirdPersonFront ? "front" : "back");
        };
        engineTable["set_camera_mode"] = [](const std::string& mode) {
            if (mode == "first")      { g_ThirdPerson = false; g_ThirdPersonFront = false; }
            else if (mode == "back")  { g_ThirdPerson = true;  g_ThirdPersonFront = false; }
            else if (mode == "front") { g_ThirdPerson = true;  g_ThirdPersonFront = true; }
            else return false;
            return true;
        };

        engineTable["set_block"] = [](int x, int y, int z, const std::string& blockName) {
            if (apiWorld()) {
                uint8_t id = BlockRegistry::getInstance().getBlockId(blockName);
                apiWorld()->setBlockAt(glm::ivec3(x, y, z), id);
            }
        };
        engineTable["get_block_name"] = [](int x, int y, int z) -> std::string {
            if (apiWorld()) {
                uint8_t id = apiWorld()->getBlockAt(glm::ivec3(x, y, z));
                if (id == 0) return "air";
                return BlockRegistry::getInstance().getBlock(id).name;
            }
            return "air";
        };
        // engine.get_metadata(x, y, z) -> 0..255 (water: flow level in the low 3 bits, 8 = falling)
        // engine.get_light(x, y, z) -> sun, red, green, blue (0..15 each): the light at a block
        engineTable["get_light"] = [](int x, int y, int z) -> std::tuple<int, int, int, int> {
            const uint16_t l = apiWorld() ? apiWorld()->getLightAt(glm::ivec3(x, y, z)) : ChunkData::kFullSky;
            return std::make_tuple(light::sky(l), light::red(l), light::green(l), light::blue(l));
        };
        // engine.get_light_level(x, y, z) -> 0..15: how bright it is there right now (the brightest
        // of block light and sunlight, sunlight lower at night) - for mob spawning, plant growth...
        engineTable["get_light_level"] = [](int x, int y, int z) -> int {
            const uint16_t l = apiWorld() ? apiWorld()->getLightAt(glm::ivec3(x, y, z)) : ChunkData::kFullSky;
            float day = 1.0f;
            if (g_LightingSystem) day = std::max(0.0f, std::min(1.0f, g_LightingSystem->getSunDirection().y * 2.0f + 0.5f));
            const int skyNow = std::max(0, light::sky(l) - static_cast<int>(std::lround((1.0f - day) * 11.0f)));
            return std::max(skyNow, std::max(light::red(l), std::max(light::green(l), light::blue(l))));
        };
        // engine.set_smooth_lighting(on) / engine.set_ambient_occlusion(on): how chunks are shaded
        // (both on by default). Every chunk is redrawn.
        engineTable["set_smooth_lighting"] = [](bool on) {
            ChunkMesh::smoothLighting = on;
            if (apiWorld()) apiWorld()->markAllDirty();
        };
        engineTable["set_ambient_occlusion"] = [](bool on) {
            ChunkMesh::ambientOcclusion = on;
            if (apiWorld()) apiWorld()->markAllDirty();
        };
        engineTable["get_metadata"] = [](int x, int y, int z) -> int {
            return apiWorld() ? apiWorld()->getMetadataAt(glm::ivec3(x, y, z)) : 0;
        };
        // engine.set_block_meta(x, y, z, name, meta): block and metadata in one change
        engineTable["set_block_meta"] = [](int x, int y, int z, const std::string& blockName, int meta) {
            if (!apiWorld()) return;
            const uint8_t id = BlockRegistry::getInstance().getBlockId(blockName);
            apiWorld()->setBlockWithMetaAt(glm::ivec3(x, y, z), id, static_cast<uint8_t>(meta & 0xFF));
        };
        // engine.register_fluid, register_fluid_reaction, get_fluid, is_fluid_source,
        // list_fluids (Fluids.cpp)
        addFluidLuaApi(lua, engineTable, []() { return apiWorld(); });
        // engine.is_replaceable(x, y, z): air, a fluid or a plant - room to put something
        // engine.is_opaque(x, y, z) -> true for a solid block light can't pass (stone, dirt...):
        // not air, water, plants, leaves or glass. (Grass under one turns to dirt.)
        engineTable["is_opaque"] = [](int x, int y, int z) {
            if (!apiWorld()) return false;
            const uint8_t id = apiWorld()->getBlockAt(glm::ivec3(x, y, z));
            if (id == 0 || FluidRegistry::get().forBlock(id)) return false;
            const auto& b = BlockRegistry::getInstance().getBlock(id);
            return b.isWalkable && !b.isTransparent && !b.isCutout;
        };
        engineTable["is_replaceable"] = [](int x, int y, int z) {
            if (!apiWorld()) return false;
            const uint8_t id = apiWorld()->getBlockAt(glm::ivec3(x, y, z));
            if (id == 0) return true;
            const auto& prop = BlockRegistry::getInstance().getBlock(id);
            return FluidRegistry::get().forBlock(id) != nullptr || prop.pattern == "plantlike";
        };
        // engine.register_item_use(item, function(pointed) ... end)
        //   Right-clicking with `item` in the active hotbar slot calls fn instead of placing it.
        //   pointed = { item, slot, count,
        //               under = {x,y,z,name} (block looked at, or nil),
        //               above = {x,y,z}      (the space in front of it),
        //               fluid = {x,y,z,name,source} (water looked at first, or nil),
        //               player = {x,y,z} (feet) }
        //   Return true when the click was used (false lets the block be placed as normal).
        engineTable["register_item_use"] = [](const std::string& item, sol::protected_function fn) {
            g_ItemUseHandlers[item] = std::move(fn);
        };
        // ---- Shared values: decided by whoever runs the world, seen by every player ----
        // engine.is_authority() -> true in singleplayer and when hosting; false when joined to
        //   someone else's world/server (then their Lua decides, e.g. the weather)
        // engine.is_flying() / engine.set_flying(on): creative flying (double-tap Jump). Only
        // in creative; set_flying returns false otherwise. engine.set_fly_allowed(false) turns
        // double-tap flying off (e.g. for a parkour map), true turns it back on.
        engineTable["is_flying"] = []() { return g_Flying; };
        engineTable["set_flying"] = [](bool on) {
            if (on && !(g_Creative && g_FlyAllowed)) return false;
            g_Flying = on;
            return true;
        };
        engineTable["set_fly_allowed"] = [](bool on) { g_FlyAllowed = on; if (!on) g_Flying = false; };
        engineTable["is_authority"] = []() { return g_Client.state() != GameClient::State::Joined; };
        // engine.set_shared(key, value) -> true; false when joined (only the server sets them)
        engineTable["set_shared"] = [](const std::string& key, const std::string& value) {
            if (g_Client.state() == GameClient::State::Joined || key.empty()) return false;
            const auto it = g_SharedValues.find(key);
            if (it != g_SharedValues.end() && it->second == value) return true;
            g_SharedValues[key] = value;
            g_SharedChanged.push_back(key);
            if (g_Server.running()) g_Server.setShared(key, value);   // LAN players get it too
            return true;
        };
        // engine.get_shared(key) -> value, or "" if never set
        engineTable["get_shared"] = [](const std::string& key) -> std::string {
            if (g_Client.state() == GameClient::State::Joined) return g_Client.getShared(key);
            const auto it = g_SharedValues.find(key);
            return it == g_SharedValues.end() ? std::string() : it->second;
        };
        // engine.get_player_position() -> x, y, z (feet) of this game's player
        engineTable["get_player_position"] = []() {
            return std::make_tuple(g_PlayerPosition.x, g_PlayerPosition.y, g_PlayerPosition.z);
        };
        // ---- Survival building blocks (mods/survival uses these; any game can) ----
        // engine.set_creative(bool) / engine.is_creative()
        //   creative: blocks break instantly, placing is free, the creative menu opens
        //   survival: hold to dig (hardness, tools), placing uses up the stack, no creative menu
        //   On a server, the server decides (only its operators can change modes): switching to
        //   anything but the mode the server gave you is refused and returns false.
        // ---- First-person hand (FirstPerson.h) ----
        // engine.set_first_person_hand(on): show the held block / arm in first person (default on)
        engineTable["set_first_person_hand"] = [](bool on) { g_FirstPerson.enabled = on; };
        // engine.set_third_person_item(on [, right_bone [, left_bone]]): show what's held in the
        // player model's hand in F5 (default on), and which bone is the hand (its bottom end)
        engineTable["set_third_person_item"] = [](bool on, sol::optional<std::string> r, sol::optional<std::string> l) {
            g_FirstPerson.showInThirdPerson = on;
            if (r) g_FirstPerson.rightHandBones.insert(g_FirstPerson.rightHandBones.begin(), *r);
            if (l) g_FirstPerson.leftHandBones.insert(g_FirstPerson.leftHandBones.begin(), *l);
        };
        // engine.set_first_person_arm({ right = "bone", left = "bone", color = { r, g, b } }):
        // which bone of g_PlayerModel is the arm shown when the hand is empty (with everything
        // attached to it), and the colour of the plain arm used if the model has none
        engineTable["set_first_person_arm"] = [](sol::table t) {
            sol::optional<std::string> r = t["right"];
            sol::optional<std::string> l = t["left"];
            if (r) g_FirstPerson.rightArmBones.insert(g_FirstPerson.rightArmBones.begin(), *r);
            if (l) g_FirstPerson.leftArmBones.insert(g_FirstPerson.leftArmBones.begin(), *l);
            sol::optional<sol::table> c = t["color"];
            if (c) {
                glm::vec3 col((*c)[1].get_or(0.88f), (*c)[2].get_or(0.69f), (*c)[3].get_or(0.55f));
                if (col.r > 1.0f || col.g > 1.0f || col.b > 1.0f) col /= 255.0f;
                g_FirstPerson.skinColor = col;
            }
        };
        // engine.swing_hand(): play the hand swing (e.g. when a mod's item is used)
        engineTable["swing_hand"] = []() { g_FirstPerson.swing(); };
        // engine.get_swing() -> 0..1 how far through the current swing (0.3 s), or -1 when not
        // swinging. The player model uses it to swing its arm in third person.
        engineTable["get_swing"] = []() { return g_FirstPerson.swingProgress(); };
        // engine.is_left_handed(): the player's Main Hand setting
        engineTable["is_left_handed"] = []() { return settingInt("main_hand", 0) == 1; };
        // engine.set_place_repeat(seconds): holding right-click places / uses again this often
        // (Minecraft: 0.2). 0 = one per click.
        engineTable["set_place_repeat"] = [](float seconds) { g_PlaceRepeatSeconds = std::max(0.0f, seconds); };
        engineTable["set_creative"] = [](bool on) -> bool {
            if (g_Client.state() == GameClient::State::Joined && !g_Client.gameMode().empty() &&
                on != (g_Client.gameMode() == "creative")) {
                std::cout << "[Game mode] Refused: on this server only operators can change your game mode" << std::endl;
                return false;
            }
            g_Creative = on;
            g_Dig = DigState{};
            if (!on && g_Inventory.isCreativeOpen()) g_Inventory.setCreativeOpen(false);
            return true;
        };
        engineTable["is_creative"] = []() { return g_Creative; };
        // engine.get_server_game_mode() -> "survival" / "creative" (what the server gave you), or nil
        // when you're not on a server (then your own game decides)
        engineTable["get_server_game_mode"] = [](sol::this_state ts) -> sol::object {
            if (g_Client.state() != GameClient::State::Joined || g_Client.gameMode().empty()) return sol::make_object(ts, sol::lua_nil);
            return sol::make_object(ts, g_Client.gameMode());
        };
        // engine.is_operator() -> true in singleplayer and when hosting; on a server, whether you're one of its operators
        engineTable["is_operator"] = []() {
            return g_Client.state() == GameClient::State::Joined ? g_Client.isOperator() : true;
        };
        // engine.set_footstep_distance(blocks): how far you walk between footstep sounds (1.3)
        engineTable["set_footstep_distance"] = [](float blocks) { g_FootstepDistance = std::clamp(blocks, 0.3f, 10.0f); };

        // engine.get_player_state() -> { x, y, z, vx, vy, vz, yaw, pitch, on_ground, moving,
        //   sprinting, sneaking, jumping, feet_fluid, head_fluid }   (fluid names or nil)
        engineTable["get_player_state"] = [](sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            Player* p = g_PlayerInstance;
            if (!p) return sol::make_object(lv, sol::lua_nil);
            sol::table t = lv.create_table();
            t["x"] = p->position.x; t["y"] = p->position.y; t["z"] = p->position.z;
            t["vx"] = p->velocity.x; t["vy"] = p->velocity.y; t["vz"] = p->velocity.z;
            t["yaw"] = p->yaw; t["pitch"] = p->pitch;
            t["on_ground"] = p->isGrounded;
            const uint8_t k = g_LastMoveKeys;
            const bool moving = (k & (MoveInput::Forward | MoveInput::Back | MoveInput::Left | MoveInput::Right)) != 0;
            t["moving"] = moving;
            t["sprinting"] = moving && (k & MoveInput::Sprint) != 0;
            t["sneaking"] = (k & MoveInput::Sneak) != 0;
            t["jumping"] = (k & MoveInput::Jump) != 0;
            t["flying"] = g_Flying;
            if (apiWorld()) {
                auto fluidAt = [](const glm::vec3& v) -> const FluidDef* {
                    FluidRegistry::get().ensureFresh();
                    return FluidRegistry::get().forBlock(apiWorld()->getBlockAt(glm::ivec3(
                        static_cast<int>(std::floor(v.x)), static_cast<int>(std::floor(v.y)), static_cast<int>(std::floor(v.z)))));
                };
                if (const FluidDef* f = fluidAt(p->position + glm::vec3(0.0f, 0.1f, 0.0f))) t["feet_fluid"] = f->name;
                if (const FluidDef* f = fluidAt(p->eyePosition())) t["head_fluid"] = f->name;
            }
            return t;
        };
        // engine.set_player_position(x, y, z) -> true; false when joined to a server
        engineTable["set_player_position"] = [](float x, float y, float z) {
            if (!g_PlayerInstance || g_Client.state() == GameClient::State::Joined) return false;
            g_PlayerInstance->position = glm::vec3(x, y, z);
            g_PlayerInstance->velocity = glm::vec3(0.0f);
            return true;
        };
        // engine.add_player_velocity(vx, vy, vz): a push (knockback). Singleplayer/host only.
        engineTable["add_player_velocity"] = [](float x, float y, float z) {
            if (!g_PlayerInstance || g_Client.state() == GameClient::State::Joined) return false;
            g_PlayerInstance->velocity += glm::vec3(x, y, z);
            return true;
        };
        // engine.respawn_player(x, y, z): back to a spawn point. When joined, the server
        // moves you to its spawn instead (the coordinates are ignored).
        // Like Minecraft, respawning is in the overworld (wherever you died), unless `dimension`
        // says otherwise (a respawn anchor's): engine.respawn_player(x, y, z [, dimension])
        engineTable["respawn_player"] = [](sol::optional<float> x, sol::optional<float> y, sol::optional<float> z,
                                           sol::optional<std::string> dimension) {
            if (g_Client.state() == GameClient::State::Joined) { g_Client.sendCommand("respawn"); return true; }
            if (!g_PlayerInstance || !x || !y || !z) return false;
            // the dimension: the one given, else where the dimension we died in sends us (its
            // `respawn`: the overworld by default) - at that dimension's spawn if it isn't the overworld
            glm::vec3 at(*x, *y, *z);
            std::string d;
            if (dimension) d = DimensionWorlds::norm(*dimension);
            else if (g_WorldInstance) {
                d = dims::respawnDimension(g_WorldInstance->dimension());
                if (!d.empty()) at = dims::Registry::get().type(d).spawn;
            }
            if (g_WorldInstance && g_WorldInstance->dimension() != d) return changeLocalDimension(d, at);
            if (!dimension && !d.empty()) { g_PlayerInstance->position = at; g_PlayerInstance->velocity = glm::vec3(0.0f); return true; }
            g_PlayerInstance->position = glm::vec3(*x, *y, *z);
            g_PlayerInstance->velocity = glm::vec3(0.0f);
            return true;
        };
        // engine.set_player_controls{ move = bool, jump = bool, sprint = bool, sneak = bool }
        //   e.g. sprint = false when hungry, everything false while dead. Missing = unchanged.
        engineTable["set_player_controls"] = [](sol::table t) {
            auto flag = [&t](const char* name, uint8_t bits) {
                sol::optional<bool> on = t[name];
                if (!on) return;
                if (*on) g_MoveKeyMask |= bits; else g_MoveKeyMask &= static_cast<uint8_t>(~bits);
            };
            flag("move", MoveInput::Forward | MoveInput::Back | MoveInput::Left | MoveInput::Right);
            flag("jump", MoveInput::Jump);
            flag("sprint", MoveInput::Sprint);
            flag("sneak", MoveInput::Sneak);
        };

        // engine.get_storage(key) -> text or nil; engine.set_storage(key, text or nil)
        //   saved with the world (singleplayer and hosting; kept in memory when joined)
        engineTable["get_storage"] = [](const std::string& key, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            const auto it = g_ModStorage.find(key);
            if (it == g_ModStorage.end()) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, it->second);
        };
        engineTable["set_storage"] = [](const std::string& key, sol::optional<std::string> value) {
            if (value) g_ModStorage[key] = *value; else g_ModStorage.erase(key);
            g_ModStorageDirty = true;
        };

        // engine.set_dig_crack_texture(file, stages): the crack drawn on a block being dug, a
        //   vertical strip of `stages` square pictures (first stage at the top), like
        //   Minecraft's destroy stages. "" goes back to the plain darkening box.
        //   Leave out `stages` to count them from the picture (height / width), like Luanti's
        //   crack_anylength.png - which the engine also uses by itself if the game has one.
        engineTable["set_dig_crack_texture"] = [](const std::string& path, sol::optional<int> stages) {
            g_CrackTexture = path.empty() ? 0 : loadStandardTexture(path);
            int n = stages.value_or(0);
            if (g_CrackTexture != 0 && n <= 0) {
                GLint w = 0, h = 0;
                glBindTexture(GL_TEXTURE_2D, g_CrackTexture);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
                glBindTexture(GL_TEXTURE_2D, 0);
                n = (w > 0 && h >= w) ? h / w : 1;
            }
            g_CrackStages = std::clamp(n > 0 ? n : 10, 1, 64);
            return g_CrackTexture != 0 || path.empty();
        };
        // ---- Sound (Audio.h): sound.* ----
        auto soundTable = lua.create_named_table("sound");
        // sound.play(name [, { pos = {x,y,z}, gain, pitch, loop, group, max_distance }]) -> handle / nil
        //   name: "my_mod_click" plays my_mod_click.ogg/.wav (or a random .1/.2/... variant)
        //   from any mod's folder or sounds/ folder. With `pos` it's heard from that place.
        //   group: "effects" (default), "ambient", "music" or "ui" - each has its own volume.
        // sound.play(name [, opts]); the sandboxed copy is what client mods get (ClientMods.cpp):
        // it only plays files inside the sound folders (AudioSystem::findFiles)
        auto playSound = [](const std::string& name, sol::optional<sol::table> opts, sol::this_state ts,
                            bool sandboxed) -> sol::object {
            sol::state_view lv(ts);
            SoundParams p;
            p.sandboxed = sandboxed;
            if (opts) {
                const sol::table& o = *opts;
                p.gain = o.get_or("gain", 1.0f);
                p.pitch = o.get_or("pitch", 1.0f);
                p.loop = o.get_or("loop", false);
                p.group = o.get_or("group", std::string("effects"));
                p.maxDistance = o.get_or("max_distance", 24.0f);
                p.refDistance = o.get_or("ref_distance", 2.0f);
                sol::optional<sol::table> pos = o["pos"];
                if (pos) {
                    p.positional = true;
                    p.position = glm::vec3(pos->get_or("x", 0.0f), pos->get_or("y", 0.0f), pos->get_or("z", 0.0f));
                }
            }
            const int h = g_Audio.play(name, p);
            if (h == 0) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, h);
        };
        soundTable["play"] = [playSound](const std::string& name, sol::optional<sol::table> opts, sol::this_state ts) {
            return playSound(name, opts, ts, false);
        };
        soundTable["__play_sandboxed"] = [playSound](const std::string& name, sol::optional<sol::table> opts, sol::this_state ts) {
            return playSound(name, opts, ts, true);
        };
        soundTable["stop"] = [](int h) { g_Audio.stop(h); };
        // sound.fade(handle, gain, seconds): to 0 stops it at the end
        soundTable["fade"] = [](int h, float gain, sol::optional<float> seconds) { g_Audio.fade(h, gain, seconds.value_or(1.0f)); };
        soundTable["set_gain"] = [](int h, float gain) { g_Audio.setGain(h, gain); };
        soundTable["set_pitch"] = [](int h, float pitch) { g_Audio.setPitch(h, pitch); };
        soundTable["set_position"] = [](int h, float x, float y, float z) { g_Audio.setPosition(h, glm::vec3(x, y, z)); };
        soundTable["is_playing"] = [](int h) { return g_Audio.isPlaying(h); };
        soundTable["exists"] = [](const std::string& name) { return g_Audio.soundExists(name); };
        soundTable["__exists_sandboxed"] = [](const std::string& name) { return g_Audio.soundExists(name, true); };
        soundTable["is_available"] = []() { return g_Audio.available(); };
        // sound.play_music(name [, { fade = 2, loop = false, gain = 1 }]) - streamed, crossfades
        soundTable["play_music"] = [](const std::string& name, sol::optional<sol::table> opts) {
            float fade = 2.0f, gain = 1.0f;
            bool loop = false;
            if (opts) { fade = opts->get_or("fade", fade); loop = opts->get_or("loop", loop); gain = opts->get_or("gain", gain); }
            return g_Audio.playMusic(name, fade, loop, gain) != 0;
        };
        soundTable["stop_music"] = [](sol::optional<float> fade) { g_Audio.stopMusic(fade.value_or(2.0f)); };
        soundTable["get_music"] = []() { return g_Audio.currentMusic(); };
        // sound.set_volume(group, 0..1) / sound.get_volume(group): "master", "music", "effects",
        //   "ambient", "ui" (the Settings sliders change the same values)
        soundTable["set_volume"] = [](const std::string& group, float v) { g_Audio.setVolume(group, v); };
        soundTable["get_volume"] = [](const std::string& group) { return g_Audio.volume(group); };

        // engine.find_texture(name) -> full path, or nil if no such picture (looks in the game's
        //   textures, the calling mod's folder and every loaded mod's folder)
        engineTable["find_texture"] = [](const std::string& name, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            const std::string found = findTextureFile(name);
            if (found.empty()) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, found);
        };
        // engine.set_inventory_open(bool) / engine.is_inventory_open(): a mod's inventory screen
        //   (frees the mouse while open; clicks go to handle_inventory_click)
        engineTable["set_inventory_open"] = [window](bool open) {
            g_LuaInventoryOpen = open;
            glfwSetInputMode(window, GLFW_CURSOR, open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        };
        engineTable["is_inventory_open"] = []() { return g_LuaInventoryOpen; };
        // engine.get_dig_progress() -> progress (0..1), x, y, z - or nil when not digging
        engineTable["get_dig_progress"] = []() -> std::tuple<sol::optional<float>, int, int, int> {
            if (!g_Dig.active) return { sol::nullopt, 0, 0, 0 };
            return { g_Dig.progress, g_Dig.pos.x, g_Dig.pos.y, g_Dig.pos.z };
        };
        // engine.override_block(name, { hardness, tool, needs_tool, drops, tool_type, tool_speed,
        //   in_creative }) - change a block another mod registered (like Luanti's override_item)
        engineTable["override_block"] = [](const std::string& name, sol::table props) {
            auto& reg = BlockRegistry::getInstance();
            const uint8_t id = reg.getBlockId(name);
            if (id == 0) return false;
            BlockProperty b = reg.getBlock(id);
            applyDigProperties(b, props);
            applySoundProperties(b, props);
            applyLightProperties(b, props);
            sol::optional<std::string> biome = props["biome_tint"];
            if (biome) b.biomeTint = (*biome == "grass") ? 0 : (*biome == "foliage") ? 1 : -1;
            b.in_creative = props.get_or("in_creative", b.in_creative);
            reg.registerBlock(b);
            return true;
        };
        // engine.list_blocks() -> { "stone", "dirt", ... } every registered block (and item)
        engineTable["list_blocks"] = [](sol::this_state ts) {
            sol::state_view lv(ts);
            sol::table t = lv.create_table();
            auto& reg = BlockRegistry::getInstance();
            int n = 1;
            for (int id = 1; id < 256; ++id) {
                const auto& b = reg.getBlock(static_cast<uint8_t>(id));
                if (!b.name.empty() && b.name != "air" && reg.getBlockId(b.name) == id) t[n++] = b.name;
            }
            return t;
        };
        // engine.get_block_def(name) -> { name, hardness (by hand, s), tool, needs_tool, drops,
        //   tool_type, tool_speed, solid, item } or nil
        engineTable["get_block_def"] = [](const std::string& name, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            auto& reg = BlockRegistry::getInstance();
            const uint8_t id = reg.getBlockId(name);
            if (id == 0) return sol::make_object(lv, sol::lua_nil);
            const BlockProperty& b = reg.getBlock(id);
            sol::table t = lv.create_table();
            t["name"] = b.name;
            t["hardness"] = blockHardness(b);
            t["tool"] = blockTool(b);
            t["needs_tool"] = blockNeedsTool(b);
            t["drops"] = b.dropsSet ? b.drops : b.name;
            t["pick"] = b.pick.empty() ? b.name : b.pick;
            t["tool_type"] = b.toolType;
            t["tool_speed"] = b.toolSpeed;
            t["solid"] = b.isWalkable;
            t["item"] = !b.inventoryImage.empty();
            t["has_sounds"] = !b.soundFootstep.name.empty() || !b.soundDig.name.empty() ||
                              !b.soundDug.name.empty() || !b.soundPlace.name.empty();
            t["description"] = blockDisplayName(b);
            t["full_name"] = blockFullName(b);
            return t;
        };

        // engine.is_outdoors(x, y, z) -> true if nothing is above (rain reaches it)
        engineTable["is_outdoors"] = [](int x, int y, int z) {
            return apiWorld() && apiWorld()->hasSkyAbove(glm::ivec3(x, y, z));
        };
        // engine.give_item(name [, count]) -> true if it fit: into a slot holding the same item,
        // else the first empty slot
        engineTable["give_item"] = [](const std::string& name, sol::optional<int> count) {
            return giveHotbarItem(name, count.value_or(1));
        };

        engineTable["draw_text"] = [&](std::string text, float x, float y) {
            if (g_FontRenderer) {
                Dimensions2D windowSize;
                glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
                g_FontRenderer->renderText(fontProgram, text, x, y, 1.0f, 1.0f, glm::vec4(1.0f), windowSize);
            }
        };

        engineTable["get_worlds"] = [&]() {
            auto worlds = getAvailableWorlds();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : worlds) t[idx++] = w;
            return t;
        };

        engineTable["get_active_world"] = []() { return g_ActiveWorldName; };
        engineTable["set_active_world"] = [](std::string name) { g_ActiveWorldName = name; };
        engineTable["quit"] = [&]() { glfwSetWindowShouldClose(window, true); };

        // Pause menu controls (used by pause_menu.lua)
        engineTable["is_paused"] = []() { return g_Paused; };
        engineTable["pause"] = [window]() {
            if (g_CurrentGameState != GameState::PLAYING) return;
            g_Paused = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };
        engineTable["resume"] = [window]() {
            g_Paused = false;
            g_SettingsInGame = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        };

        // ---- Settings (used by settings_menu.lua and pause_menu.lua) ----
        // engine.open_settings() - from the pause menu (the title screen's Settings button opens it too)
        engineTable["open_settings"] = [&lua]() {
            if (g_CurrentGameState == GameState::PLAYING) {
                g_Paused = true;
                g_SettingsInGame = true;
            } else {
                g_CurrentGameState = GameState::SETTINGS;
            }
            callLuaUI(&lua, "on_settings_opened");
        };
        engineTable["close_settings"] = []() { closeSettings(); };
        engineTable["is_in_game"] = []() { return g_CurrentGameState == GameState::PLAYING; };

        // engine.get_keybinds() -> { { action, label, key, key_name, default_key }, ... } in display order
        engineTable["get_keybinds"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& a : keyActions()) {
                sol::table e = lua.create_table();
                const int key = keyForAction(a.id);
                e["action"] = std::string(a.id);
                e["label"] = std::string(a.label);
                e["key"] = key;
                e["key_name"] = keyName(key);
                e["default_key"] = a.defaultKey;
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_keybind(action, key) -> true, or false + reason
        engineTable["set_keybind"] = [](const std::string& action, int key) {
            std::string error;
            const bool ok = setKeybind(action, key, error);
            return std::make_tuple(ok, error);
        };
        engineTable["reset_keybinds"] = []() { resetKeybinds(); };
        engineTable["key_name"] = [](int key) { return keyName(key); };

        // engine.get_video_settings() -> { { id, value, min, max, step }, ... }
        engineTable["get_video_settings"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& s : kVideoSettings) {
                sol::table e = lua.create_table();
                e["id"] = std::string(s.id);
                e["value"] = currentVideoSetting(s.id);
                e["min"] = s.minV;
                e["max"] = s.maxV;
                e["step"] = s.step;
                // Choices shown as buttons; ones this graphics card can't take are greyed out
                // (disabled) with the reason (GpuCheck.h)
                if (std::string(s.id) == "artnet") {
                    sol::table opts = lua.create_table();
                    const char* names[3] = { "Off", "This computer", "Network" };
                    for (int v = 0; v < 3; ++v) {
                        sol::table o = lua.create_table();
                        o["label"] = std::string(names[v]);
                        o["value"] = v;
                        opts[v + 1] = o;
                    }
                    e["options"] = opts;
                    std::string note = "DMX from lighting software or a console, for mods like Stage Lights. "
                                       "This computer: software on this PC. Network: a desk or another PC.";
                    if (!g_ArtNet.lastError().empty()) note = "Art-Net input couldn't start: " + g_ArtNet.lastError();
                    e["note"] = note;
                }
                if (std::string(s.id) == "filmic") {
                    sol::table opts = lua.create_table();
                    for (int v = 0; v < 2; ++v) {
                        sol::table o = lua.create_table();
                        o["label"] = std::string(v ? "On" : "Off");
                        o["value"] = v;
                        if (v && g_GpuCaps.maxLightQuality <= 0) o["disabled"] = true;
                        opts[v + 1] = o;
                    }
                    e["options"] = opts;
                    e["note"] = std::string(g_GpuCaps.maxLightQuality <= 0
                        ? "Needs Lighting Low or High, which this graphics card can't run"
                        : "With Lighting Low or High: a film-like curve and exposure that adjusts to day and night");
                }
                if (std::string(s.id) == "light_quality" || std::string(s.id) == "bloom") {
                    const bool isLight = std::string(s.id) == "light_quality";
                    sol::table opts = lua.create_table();
                    const char* names[3] = { "Off", isLight ? "Low" : "On", "High" };
                    const int count = isLight ? 3 : 2;
                    for (int v = 0; v < count; ++v) {
                        sol::table o = lua.create_table();
                        o["label"] = std::string(names[v]);
                        o["value"] = v;
                        const bool ok = isLight ? v <= g_GpuCaps.maxLightQuality : (v == 0 || g_GpuCaps.bloom);
                        if (!ok) o["disabled"] = true;
                        opts[v + 1] = o;
                    }
                    e["options"] = opts;
                    const std::string& why = isLight ? g_GpuCaps.lightReason : g_GpuCaps.bloomReason;
                    if (!why.empty()) e["note"] = why;
                }
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_video_setting(id, value) -> true, or false + reason. Applies immediately.
        engineTable["set_video_setting"] = [](const std::string& id, int value) {
            std::string error;
            const bool ok = setVideoSetting(id, value, error);
            return std::make_tuple(ok, error);
        };

        engineTable["quit_to_menu"] = [window, &lua]() {
            g_SettingsInGame = false;
            g_HudHidden = false;   // (F1 is for the world you were in: the next one shows the HUD)
            // Save, then unload the world so the next one starts clean (blocks still falling
            // land first, so none is lost in the air)
            if (g_CurrentGameState == GameState::PLAYING) {
                if (g_WorldInstance) g_WorldInstance->finishFalling();
                saveCurrentWorld();
            }
            g_DimWorlds.unloadAll();          // (the world's other dimensions close with it)
            dims::currentRef().clear();
            const bool wasOnline = g_Client.active();
            if (g_Client.active()) g_Client.disconnect();
            if (g_Server.running()) { g_ServerApi.serverStopping(); g_ServerApi.flush(); g_Server.stop(); }
            g_NetError.clear();
            if (g_WorldInstance) {
                g_WorldInstance->reset();
                g_WorldInstance->setSaveDirectory("");
            }
            g_ModelInstances.clear();   // models placed in the old world
            g_Entities.clear();         // and its entities (saved just above)
            g_BlockMods.clearWorld();
            g_EntityLua.reset();
            g_EntityLooks.clear();
            // The hotbar belongs to that world (saved just above): nothing of it stays behind
            if (g_Hotbar) for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
            g_ModStorage.clear();       // (so does mod storage - the next world loads its own)
            g_ThirdPerson = false;
            g_ThirdPersonFront = false;
            g_Paused = false;
            g_Inventory.setCreativeOpen(false);
            g_CurrentGameState = wasOnline ? GameState::MULTIPLAYER : GameState::MENU;
            if (wasOnline) callLuaUI(&lua, "on_multiplayer_opened");
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };
        engineTable["start_game"] = [window]() { beginLoadingWorld(window); };
        // engine.take_world_error() -> why the last world was closed or couldn't be opened, once
        //   (e.g. its world generator failed), else nil
        engineTable["take_world_error"] = [](sol::this_state ts) -> sol::object {
            if (g_WorldError.empty()) return sol::make_object(ts, sol::lua_nil);
            std::string e;
            e.swap(g_WorldError);
            return sol::make_object(ts, e);
        };

        // ---- World saves (used by world_select.lua) ----
        // engine.list_worlds() -> { { name = "My World", seed = "123" }, ... }  newest first
        engineTable["list_worlds"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : listSavedWorlds()) {
                sol::table e = lua.create_table();
                e["name"] = w.name;
                e["seed"] = w.seed;
                if (!w.gameMode.empty()) e["game_mode"] = w.gameMode;
                e["played"] = playedAgo(w.lastPlayed);   // "2 hours ago"
                std::error_code tec;
                e["has_thumbnail"] = fs::exists(savesRoot() / w.name / "thumbnail.png", tec);
                t[idx++] = e;
            }
            return t;
        };
        // engine.create_world(name, seedText [, options]) -> folder name actually used, or nil + error
        //   options = { game_mode = "survival", ... }: saved with the world, read by mods as
        //   engine.get_storage("world:game_mode")
        engineTable["create_world"] = [&lua](const std::string& name, sol::optional<std::string> seed,
                                             sol::optional<sol::table> options) {
            if (!selectedGameIsLoaded()) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil),
                                       std::string("Restart the game to create worlds for this game."));
            }
            std::map<std::string, std::string> opts;
            if (options) {
                options->for_each([&opts](const sol::object& k, const sol::object& v) {
                    if (!k.is<std::string>()) return;
                    if (v.is<std::string>()) opts[k.as<std::string>()] = v.as<std::string>();
                    else if (v.is<bool>()) opts[k.as<std::string>()] = v.as<bool>() ? "true" : "false";
                    else if (v.is<double>()) opts[k.as<std::string>()] = std::to_string(v.as<double>());
                });
            }
            return std::make_tuple(sol::make_object(lua, createWorld(name, seed.value_or(""), opts)), std::string());
        };
        // engine.play_world(name) -> true, or false + error message
        engineTable["play_world"] = [window](const std::string& name) {
            // The target: a world folder directly in saves/ ...
            if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
                name.find('\\') != std::string::npos || name.find(':') != std::string::npos) {
                return std::make_tuple(false, std::string("Invalid world name."));
            }
            // ... of the game selected on the title screen, which must be installed. It doesn't
            // have to be the game that's loaded now: beginLoadingWorld reloads the mods for it
            // (the world's game and its mods) first. openActiveWorld checks the loaded game again
            // before the save is restored.
            if (!gameInstalled(g_ActiveGameFolder)) {
                return std::make_tuple(false, "The selected game ('" + g_ActiveGameFolder + "') isn't installed.");
            }
            WorldMeta meta;
            if (readWorldMeta(savesRoot() / name / "world.dat", meta) && !meta.game.empty() &&
                meta.game != g_ActiveGameFolder) {
                return std::make_tuple(false, "This world was made with '" + meta.game + "', but '" +
                                              g_ActiveGameFolder + "' is selected.");
            }
            g_ActiveWorldName = name;
            beginLoadingWorld(window);
            return std::make_tuple(true, std::string());
        };
        // engine.delete_world(name) -> true, or false + error message
        engineTable["delete_world"] = [](const std::string& name) {
            std::string error;
            const bool ok = deleteWorld(name, error);
            return std::make_tuple(ok, error);
        };
        // engine.get_game_info() -> { selected, loaded, title, can_play }
        engineTable["get_game_info"] = [&lua]() {
            sol::table t = lua.create_table();
            std::string title = g_ActiveGameFolder;
            for (const auto& g : getAvailableGames()) {
                if (g.folderName == g_ActiveGameFolder) { title = g.title; break; }
            }
            t["selected"] = g_ActiveGameFolder;
            t["loaded"] = g_LoadedGameFolder;
            t["title"] = title;
            t["can_play"] = selectedGameIsLoaded();
            return t;
        };
        // engine.get_update_info() -> { enabled, checked, available, required, current, commit, latest,
        //   min_supported, download, notes, error }: is there a newer BlockSmith (UpdateCheck.h)
        engineTable["get_update_info"] = [&lua]() {
            sol::table t = lua.create_table();
            t["enabled"] = g_Updates && g_Updates->enabled();
            t["current"] = std::string(BLOCKSMITH_VERSION);
            t["commit"] = std::string(BLOCKSMITH_COMMIT);
            if (g_Updates) {
                const update::Info& u = g_Updates->info();
                t["checked"] = u.checked;
                t["available"] = u.available;
                t["required"] = u.required;
                t["latest"] = u.latest;
                t["min_supported"] = u.minSupported;
                t["download"] = u.download;
                t["notes"] = u.notes;
                t["error"] = u.error;
            }
            return t;
        };
        // engine.open_update_page() -> true if the browser was asked to open the new version's
        //   download page (only on the title-screen pages, and only the checked https link from
        //   the version file: UpdateCheck.h)
        engineTable["open_update_page"] = []() {
            if (g_CurrentGameState == GameState::PLAYING || g_CurrentGameState == GameState::LOADING) return false;
            if (!g_Updates || g_Updates->info().download.empty()) return false;
            return weblink::open(g_Updates->info().download);
        };
        engineTable["save_world"] = []() {
            if (g_CurrentGameState == GameState::PLAYING) saveCurrentWorld();
        };
        engineTable["back_to_title"] = []() { g_CurrentGameState = GameState::MENU; };

        // ---- Multiplayer (used by multiplayer.lua and pause_menu.lua) ----
        // engine.list_servers() -> { { name = "Home", address = "192.168.1.20" }, ... }
        engineTable["list_servers"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& s : loadServers()) {
                sol::table e = lua.create_table();
                e["name"] = s.name;
                e["address"] = s.address;
                e["pinned"] = s.pinned;
                t[i++] = e;
            }
            return t;
        };
        // engine.save_servers(list)
        engineTable["save_servers"] = [](sol::table list) {
            std::vector<SavedServer> servers;
            for (size_t i = 1; i <= list.size(); ++i) {
                sol::optional<sol::table> e = list[i];
                if (!e) continue;
                servers.push_back({e->get_or<std::string>("name", "Server"),
                                   e->get_or<std::string>("address", ""),
                                   e->get_or("pinned", false)});
            }
            saveServers(servers);
        };
        engineTable["get_player_name"] = []() { return playerName(); };
        // engine.set_player_name(name) -> true, or false + error
        // engine.set_player_password(password) - "" removes it. Only a hash is stored (settings.txt),
        // and it never leaves this PC: servers get a per-server challenge answer instead (Auth.h),
        // which protects your name there (Settings > Player) and is no use on any other server.
        engineTable["set_player_password"] = [](const std::string& password) {
            if (password.empty()) g_Settings.erase("player_password_token");
            else g_Settings["player_password_token"] = net::passwordToken(password);
            saveSettings();
            return true;
        };
        engineTable["has_player_password"] = []() {
            const auto it = g_Settings.find("player_password_token");
            return it != g_Settings.end() && !it->second.empty();
        };
        engineTable["set_player_name"] = [](const std::string& name) {
            std::string error;
            const bool ok = changePlayerName(name, error);
            return std::make_tuple(ok, error);
        };
        // engine.join_server(address) -> true, or false + error (connecting continues in the background)
        engineTable["join_server"] = [](const std::string& address) {
            if (!selectedGameIsLoaded()) {
                return std::make_tuple(false, std::string("Restart the game with this game selected to join its servers."));
            }
            if (g_Server.running()) g_Server.stop();
            g_HudHidden = false;   // joining a server: the HUD shows (F1 was for the last world)
            std::string error;
            g_NetError.clear();
            const int view = g_WorldInstance ? g_WorldInstance->renderDistance() : 8;
            const std::string token = g_Settings.count("player_password_token") ? g_Settings["player_password_token"] : "";
            // The identity this server had last time (settings.txt server_id.<host:port>): the
            // password is only used to answer the server we joined before (Auth.h)
            std::string knownId;
            {
                std::string host;
                uint16_t port = 0;
                if (net::parseAddress(address, host, port)) {
                    const auto it = g_Settings.find("server_id." + host + ":" + std::to_string(port));
                    if (it != g_Settings.end()) knownId = it->second;
                }
            }
            if (!g_Client.connect(*g_WorldInstance, address, playerName(), g_LoadedGameFolder, view, error, token, knownId)) {
                return std::make_tuple(false, error);
            }
            return std::make_tuple(true, std::string());
        };
        engineTable["cancel_join"] = []() {
            if (g_Client.active()) g_Client.disconnect();
            g_NetError.clear();
        };
        // engine.get_connection_status() -> { state = "idle"|"connecting"|"joined", address, error }
        engineTable["get_connection_status"] = [&lua]() {
            sol::table t = lua.create_table();
            const auto st = g_Client.state();
            t["state"] = st == GameClient::State::Joined ? "joined"
                       : st == GameClient::State::Connecting ? "connecting" : "idle";
            t["address"] = g_Client.address();
            if (!g_NetError.empty()) t["error"] = g_NetError;
            return t;
        };
        // engine.host_game([port]) -> true, or false + error   (opens the current world to others)
        engineTable["host_game"] = [](sol::optional<int> port) {
            if (!g_WorldInstance || g_CurrentGameState != GameState::PLAYING || g_Client.active()) {
                return std::make_tuple(false, std::string("Open a singleplayer world first"));
            }
            if (!net::validPlayerName(playerName())) {
                return std::make_tuple(false, std::string("Set your name in Settings > Player first"));
            }
            const int p = port.value_or(serverPort());
            if (p <= 0 || p > 65535) return std::make_tuple(false, std::string("Invalid port"));
            g_Server.setInfo(serverMotd(), serverMaxPlayers());
            g_Server.setIcon(loadServerIcon());
            for (const auto& g : getAvailableGames()) {   // "Plays <title>" in other players' lists
                if (g.folderName == g_LoadedGameFolder) g_Server.setGameTitle(g.title);
            }
            std::string error;
            // This game's identity as a host (made once): players' password answers are bound to it
            if (g_Settings["lan_server_id"].size() < 16) {
                g_Settings["lan_server_id"] = auth::randomHex(16);
                saveSettings();
            }
            g_Server.setServerId(g_Settings["lan_server_id"]);
            if (!g_Server.start(*g_WorldInstance, static_cast<uint16_t>(p), g_LoadedGameFolder, playerName(), error)) {
                return std::make_tuple(false, error);
            }
            for (const auto& [k, v] : g_SharedValues) g_Server.setShared(k, v);   // weather etc.
            g_Server.setShared(ClientModRules::kSharedKey, lanClientModRules().encode());   // players' client mods
            // Players who join get this world's game mode; only the host can change it
            {
                const auto it = g_ModStorage.find("world:game_mode");
                g_Server.setDefaultMode(it != g_ModStorage.end() && !it->second.empty() ? it->second : std::string("survival"));
            }
            g_Server.onJoin = [](const std::string& name) { g_ServerApi.playerJoined(name); };
            // Entities: the other players see ours, and can hit them and drop items
            g_Server.setEntities(&g_Entities);
            g_Server.onEntityAct = [](const std::string& name, uint32_t id, int kind, const std::string& item, const glm::vec3& at) {
                if (kind == 0) g_EntityLua.punch(id, name, at, item);
                else g_EntityLua.rightClick(id, name, item);
            };
            // Dimensions: guests may be in any of the world's dimensions (the background ones run here)
            g_Server.worldFor = [](const std::string& d) { return g_DimWorlds.get(d); };
            g_Server.forEachWorld = [](const std::function<void(const std::string&, World&)>& fn) {
                if (g_WorldInstance) fn(g_WorldInstance->dimension(), *g_WorldInstance);
                g_DimWorlds.forEach(fn);
            };
            g_Server.onDimensionChange = [](const std::string& name, const std::string& from, const std::string& to) {
                g_ServerApi.dimensionChanged(name, from, to);
            };
            g_Server.onHarvest = [](const std::string& name, const std::string& item, const glm::ivec3& pos) {
                g_ServerApi.harvest(name, item, pos);
            };
            g_Server.canEditText = [](const std::string& name, const glm::ivec3& pos, int side, std::vector<std::string>& lines,
                                      std::string& refusal) {
                return g_ServerApi.canEditText(name, pos, side, lines, refusal);
            };
            g_Server.onModMessage = [](const std::string& name, const std::string& channel, const std::string& data) {
                g_ServerApi.modMessage(name, channel, data);
            };
            g_Server.onDropItem = [](const std::string&, const std::string& item, int count, const glm::vec3& at, const GameServer::DropInfo& how) {
                if (BlockRegistry::getInstance().getBlockId(item) == 0) return;
                const uint32_t id = how.hasVelocity ? g_Entities.dropItem(item, count, at, how.velocity) : g_EntityLua.drop(item, count, at);
                Entity* e = g_Entities.get(id);
                if (!e) return;
                if (how.returned) { e->vel = glm::vec3(0.0f); e->pickupDelay = 3.0f; }
                else if (how.pickupDelay >= 0.0f) e->pickupDelay = how.pickupDelay;
            };
            g_Server.onLeave = [](const std::string& name, const glm::vec3& pos) { g_ServerApi.playerLeft(name, pos); };
            g_Server.onChat = [](const std::string& name, std::string& text) { return g_ServerApi.chat(name, text); };
            g_Server.canEdit = [](const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t oldMeta,
                                  uint8_t newId, uint8_t newMeta) {
                return g_ServerApi.canEdit(name, pos, oldId, oldMeta, newId, newMeta);
            };
            g_Server.onCommand = [](const std::string& name, const std::string& command) {
                // Mods' commands first (on_chat_command; LAN guests are never operators)
                {
                    std::function<void(const std::string&)> reply = [name](const std::string& text) {
                        g_Server.messagePlayer(name, text, glm::vec3(0.75f, 0.85f, 1.0f));
                    };
                    bool handled = false;
                    g_ServerApi.call("on_chat_command", [&](sol::protected_function_result& r) {
                        handled = r.get_type() == sol::type::boolean && r.get<bool>();
                    }, command, name, false, reply);
                    if (handled) return;
                }
                const std::string word = command.substr(0, command.find(' '));
                if (word == "gamemode" || word == "gm") {
                    if (command.find(' ') == std::string::npos) {
                        g_Server.messagePlayer(name, "Your game mode: " + g_Server.playerMode(name) + ". Only the host can change it.",
                                               glm::vec3(0.75f, 0.85f, 1.0f));
                    } else {
                        g_Server.messagePlayer(name, "Only the host can change game modes", glm::vec3(1.0f, 0.55f, 0.55f));
                    }
                    return;
                }
                g_Server.messagePlayer(name, "This game doesn't run commands (only /gamemode to see your mode)",
                                       glm::vec3(1.0f, 0.55f, 0.55f));
            };
            g_ServerApi.serverStarted();   // on_server_start()
            askFirewallOnce("firewall_ok_game", "", "BlockSmith");
            if (g_ChatSystem) {
                g_ChatSystem->addMessage("Hosting on port " + std::to_string(p) + ". Friends can join with your IP address.",
                                         glm::vec3(1.0f, 1.0f, 0.4f));
            }
            return std::make_tuple(true, std::string());
        };
        engineTable["stop_hosting"] = []() {
            if (!g_Server.running()) return;
            g_ServerApi.serverStopping();   // on_server_stop()
            g_ServerApi.flush();
            g_Server.stop();
            if (g_ChatSystem) g_ChatSystem->addMessage("Stopped hosting", glm::vec3(1.0f, 1.0f, 0.4f));
        };
        // engine.get_network_info() -> { hosting, joined, port, players, address }
        engineTable["get_network_info"] = [&lua]() {
            sol::table t = lua.create_table();
            t["hosting"] = g_Server.running();
            t["joined"] = g_Client.state() == GameClient::State::Joined;
            t["port"] = static_cast<int>(g_Server.port());
            t["players"] = g_Server.running() ? g_Server.playerCount()
                         : static_cast<int>(g_Client.players().size()) + 1;
            t["address"] = g_Client.address();
            return t;
        };
        // ---- Server settings (Settings > Server) ----
        // engine.get_server_settings() -> { motd, max_players, port }
        engineTable["get_server_settings"] = [&lua]() {
            sol::table t = lua.create_table();
            t["motd"] = serverMotd();
            t["max_players"] = serverMaxPlayers();
            t["port"] = serverPort();
            return t;
        };
        // engine.set_server_setting(id, value) -> true, or false + error. Applies straight away
        // while hosting (the port only next time you host).
        engineTable["set_server_setting"] = [](const std::string& id, const std::string& value) {
            if (id == "motd") {
                std::string motd;
                for (char ch : value) if (ch >= 32 && ch < 127) motd += ch;
                if (motd.size() > static_cast<size_t>(net::kMaxMotdLength)) motd.resize(net::kMaxMotdLength);
                g_Settings["server_motd"] = motd;
            } else if (id == "max_players" || id == "port") {
                int v = 0;
                try { v = std::stoi(value); } catch (...) { return std::make_tuple(false, std::string("Type a number")); }
                if (id == "max_players") {
                    if (v < 2 || v > 200) return std::make_tuple(false, std::string("Max players must be 2-200"));
                    g_Settings["server_max_players"] = std::to_string(v);
                } else {
                    if (v < 1024 || v > 65535) return std::make_tuple(false, std::string("Port must be 1024-65535"));
                    g_Settings["server_port"] = std::to_string(v);
                }
            } else {
                return std::make_tuple(false, std::string("Unknown setting"));
            }
            saveSettings();
            if (g_Server.running()) g_Server.setInfo(serverMotd(), serverMaxPlayers());
            return std::make_tuple(true, std::string());
        };

        // ---- Server list status (multiplayer.lua) ----
        // engine.ping_server(address)   starts checking a server (answer comes within ~5 s)
        engineTable["ping_server"] = [](const std::string& address) { g_Pinger.ping(address); };
        // engine.get_server_status(address) -> nil if never pinged, else
        //   { state = "pinging"|"online"|"offline", motd, players, max_players, ping, names = {...},
        //     version, game, compatible, error }
        engineTable["get_server_status"] = [&lua](const std::string& address) -> sol::object {
            const ServerStatus* st = g_Pinger.status(address);
            if (!st) return sol::make_object(lua, sol::lua_nil);
            sol::table t = lua.create_table();
            t["state"] = st->state == ServerStatus::State::Online ? "online"
                       : st->state == ServerStatus::State::Offline ? "offline" : "pinging";
            t["motd"] = st->motd;
            t["players"] = st->players;
            t["max_players"] = st->maxPlayers;
            t["ping"] = st->ping;
            t["version"] = st->version;
            t["game"] = st->game;
            t["game_title"] = st->gameTitle.empty() ? st->game : st->gameTitle;
            // Is that game installed here? (then the player only needs to select it)
            bool installed = false;
            for (const auto& g : getAvailableGames()) if (g.folderName == st->game) installed = true;
            t["game_installed"] = installed;
            t["same_game"] = st->game.empty() || st->game == g_LoadedGameFolder;
            t["compatible"] = st->compatible;
            // Which BlockSmith the server runs, and how it compares with this game: "newer" (an
            // update is available for you), "older" (the server is behind), "same", or "" (an older
            // server that doesn't say)
            t["engine_version"] = st->engineVersion;
            t["engine_commit"] = st->engineCommit;
            {
                update::Version sv, mine;
                std::string cmp;
                if (update::parseVersion(st->engineVersion, sv) && update::parseVersion(BLOCKSMITH_VERSION, mine)) {
                    const int c = update::compare(sv, mine);
                    cmp = c > 0 ? "newer" : (c < 0 ? "older" : "same");
                }
                t["engine_compare"] = cmp;
            }
            t["error"] = st->error;
            sol::table names = lua.create_table();
            for (size_t i = 0; i < st->names.size(); ++i) names[i + 1] = st->names[i];
            t["names"] = names;
            return t;
        };

        // ---- My Servers (server_manager.lua): dedicated servers set up from the game ----
        // engine.list_games() -> { { folder, title, description }, ... }
        engineTable["list_games"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& g : getAvailableGames()) {
                sol::table e = lua.create_table();
                e["folder"] = g.folderName;
                e["title"] = g.title;
                e["description"] = g.description;
                t[i++] = e;
            }
            return t;
        };
        // engine.list_server_profiles() -> { "Survival", "Skyblock", ... }   (folders in servers/)
        engineTable["list_server_profiles"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& p : listServerProfiles()) t[i++] = p;
            return t;
        };
        // engine.get_server_profile(name) -> { name=..., motd=..., game=..., port=..., ... } (all strings)
        engineTable["get_server_profile"] = [&lua](const std::string& profile) {
            sol::table t = lua.create_table();
            for (const auto& [k, v] : readServerProfile(profile)) t[k] = v;
            return t;
        };
        // engine.save_server_profile(name, settings) -> true, or false + error
        engineTable["save_server_profile"] = [](const std::string& profile, sol::table settings) {
            std::map<std::string, std::string> values = readServerProfile(profile);
            for (const auto& s : serverSettingInfo()) {
                sol::object v = settings[s.key];
                if (v.is<std::string>()) values[s.key] = v.as<std::string>();
                else if (v.is<double>()) values[s.key] = std::to_string(static_cast<long long>(v.as<double>()));
                else if (v.is<bool>()) values[s.key] = v.as<bool>() ? "true" : "false";
            }
            std::string error;
            const bool ok = writeServerProfile(profile, values, error);
            return std::make_tuple(ok, error);
        };
        // engine.create_server_profile(displayName) -> folder name, or nil + error
        engineTable["create_server_profile"] = [&lua](const std::string& displayName) {
            const std::string folder = serverProfileFolder(displayName);
            if (folder.empty()) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil), std::string("Type a name with letters or numbers"));
            }
            std::error_code ec;
            if (fs::exists(serverProfilesRoot() / folder, ec)) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil), std::string("There's already a server called '" + folder + "'"));
            }
            // Defaults: this game, a world named after the server, the next free port
            std::map<std::string, std::string> values;
            values["name"] = displayName;
            values["game"] = g_LoadedGameFolder;
            values["world"] = folder;
            int port = net::kDefaultPort;
            for (const auto& other : listServerProfiles()) {
                port = std::max(port, settingIntFrom(readServerProfile(other), "port", net::kDefaultPort) + 1);
            }
            if (port == static_cast<int>(net::kLanPort)) ++port;   // the LAN list uses 30001
            values["port"] = std::to_string(port);
            std::string error;
            if (!writeServerProfile(folder, values, error)) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil), error);
            }
            return std::make_tuple(sol::make_object(lua, folder), std::string());
        };
        // engine.delete_server_profile(name) -> true, or false + error (its world in saves/ is kept)
        engineTable["delete_server_profile"] = [](const std::string& profile) {
            if (serverProfileFolder(profile) != profile || profile.empty()) {
                return std::make_tuple(false, std::string("Invalid server name"));
            }
            std::error_code ec;
            fs::remove_all(serverProfilesRoot() / profile, ec);
            if (ec) return std::make_tuple(false, "Couldn't delete it: " + ec.message());
            return std::make_tuple(true, std::string());
        };
        // engine.list_mods_for_game(game) -> { { id, title, description, source = "Game"|"Global", required }, ... }
        engineTable["list_mods_for_game"] = [&lua](const std::string& game) {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& m : listModsForGame(game)) {
                sol::table e = lua.create_table();
                e["id"] = m.id;
                e["title"] = m.title;
                e["description"] = m.description;
                e["source"] = m.source;
                e["required"] = m.required;
                t[i++] = e;
            }
            return t;
        };
        // engine.start_server_profile(name) -> true, or false + error (opens the server's window)
        engineTable["start_server_profile"] = [](const std::string& profile) {
            const fs::path serverExe = findServerExe();
            if (!serverExe.empty()) askFirewallOnce("firewall_ok_server", serverExe.string(), "BlockSmith Server");
            std::string error;
            const bool ok = startServerProfile(profile, error);
            return std::make_tuple(ok, error);
        };
        engineTable["stop_server_profile"] = [](const std::string& profile) { stopServerProfile(profile); };
        engineTable["open_server_profile_folder"] = [](const std::string& profile) {
            openFolderInExplorer(serverProfilesRoot() / profile);
        };
        engineTable["has_server_build"] = []() { return !findServerExe().empty(); };
        // engine.allow_firewall() -> asks Windows again to let this game accept connections
        //   (for a "Fix: friends can't join" button). Returns ok, status: true, "allowed" (always,
        //   off Windows) / false, "failed" / false, "pending" (Windows is still applying it; the
        //   game records the result itself when it finishes - asking again meanwhile starts nothing)
        engineTable["allow_firewall"] = []() {
            const FirewallStatus st = allowThroughFirewall("", "BlockSmith");
            if (st == FirewallStatus::Allowed) { g_Settings["firewall_ok_game"] = "1"; saveSettings(); }
            if (st == FirewallStatus::Pending) g_FirewallWatch["BlockSmith"] = "firewall_ok_game";
            return std::make_tuple(st == FirewallStatus::Allowed,
                                   std::string(st == FirewallStatus::Allowed ? "allowed"
                                               : st == FirewallStatus::Pending ? "pending" : "failed"));
        };

        // engine.get_lan_games() -> { { address, host, motd, players, max_players }, ... }
        engineTable["get_lan_games"] = [&lua]() {
            sol::table t = lua.create_table();
            int i = 1;
            for (const auto& g : g_Lan.games()) {
                sol::table e = lua.create_table();
                e["address"] = g.address;
                e["host"] = g.hostName;
                e["motd"] = g.motd;
                e["players"] = g.players;
                e["max_players"] = g.maxPlayers;
                t[i++] = e;
            }
            return t;
        };
        // engine.is_scanning_lan() -> true while listening for LAN games
        engineTable["is_scanning_lan"] = []() { return g_Lan.running(); };
        // engine.has_server_icon() -> true if server_icon.png exists (Settings > Server)
        engineTable["has_server_icon"] = []() {
            std::error_code ec;
            return fs::exists(serverIconFile(), ec);
        };

        // engine.get_player_list() -> { title = "...", players = { { name, ping, host, you }, ... } }
        //   ping is in ms (-1 = not known yet). Used by player_list.lua (hold Tab).
        engineTable["get_player_list"] = [&lua]() {
            sol::table t = lua.create_table();
            sol::table list = lua.create_table();
            int i = 1;
            auto add = [&](const std::string& name, int ping, bool host, bool you) {
                sol::table e = lua.create_table();
                e["name"] = name;
                e["ping"] = ping;
                e["host"] = host;
                e["you"] = you;
                list[i++] = e;
            };
            if (g_Server.running()) {
                t["title"] = "Hosting on port " + std::to_string(g_Server.port());
                add(playerName(), 0, true, true);
                for (const auto& [id, v] : g_Server.players()) add(v.name, v.ping, false, false);
            } else if (g_Client.state() == GameClient::State::Joined) {
                t["title"] = g_Client.address();
                for (const auto& [id, v] : g_Client.players()) add(v.name, v.ping, id == 0, false);
                add(playerName(), g_Client.ping(), false, true);
            } else {
                t["title"] = "Singleplayer";
                add(playerName(), -1, false, true);
            }
            t["players"] = list;
            return t;
        };
        // engine.send_to_server(channel, data) -> true if sent: a message for the server's mods
        // (their on_mod_message(name, channel, data)). In singleplayer, or when hosting, this
        // game is the server: its own on_mod_message gets it (next frame), with your name.
        // data is text (numbers become text); at most 4 KB; a flood is dropped by the server.
        engineTable["send_to_server"] = [](const std::string& channel, sol::object data) {
            std::string text;
            if (data.get_type() == sol::type::string) text = data.as<std::string>();
            else if (data.get_type() == sol::type::number) {
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.14g", data.as<double>());
                text = buf;
            } else if (data.valid() && data.get_type() != sol::type::lua_nil) return false;
            if (!net::validModChannel(channel) || text.size() > net::kMaxModMessageFromPlayer) return false;
            if (g_Client.active()) return g_Client.sendModMessage(channel, text);
            if (g_ModToOwnServer.size() >= 4096) return false;
            g_ModToOwnServer.push_back({ channel, text });
            return true;
        };
        // engine.send_chat(text) -> sends a chat message to everyone (multiplayer only). True if sent.
        engineTable["send_chat"] = [](const std::string& text) {
            if (g_Server.running()) { g_Server.sendChat(text); return true; }
            if (g_Client.state() == GameClient::State::Joined) { g_Client.sendChat(text); return true; }
            return false;
        };

        // ---- Mods screen (used by mods_menu.lua) ----
        // engine.list_mods() -> { { id, title, author, description, version, depends,
        //                           source, path, status, error, lua_files, is_folder }, ... }
        // status is "loaded", "error" or "not_loaded"
        engineTable["list_mods"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& m : listInstalledMods()) {
                sol::table e = lua.create_table();
                e["id"] = m.id;
                e["title"] = m.title;
                e["author"] = m.author;
                e["description"] = m.description;
                e["version"] = m.version;
                e["depends"] = m.depends;
                e["source"] = m.source;
                e["path"] = m.path;
                e["status"] = m.status;
                e["error"] = m.error;
                e["lua_files"] = m.luaFiles;
                e["is_folder"] = m.isFolder;
                e["enabled"] = m.enabled;
                e["required"] = m.required;
                e["restart_needed"] = m.restartNeeded;
                t[idx++] = e;
            }
            // The player's client mods (clientmods/), after the others. Off until switched on.
            if (g_ClientMods) {
                for (const auto& m : g_ClientMods->list()) {
                    sol::table e = lua.create_table();
                    e["id"] = "client:" + m.name;
                    e["title"] = m.title;
                    e["author"] = m.author;
                    e["description"] = m.description;
                    e["version"] = m.version;
                    e["depends"] = m.depends;
                    e["source"] = std::string("Client mod");
                    e["path"] = m.path;
                    e["status"] = m.status;
                    e["error"] = m.error;
                    e["lua_files"] = m.luaFiles;
                    e["is_folder"] = true;
                    e["enabled"] = m.enabled;
                    e["required"] = false;
                    e["restart_needed"] = m.restartNeeded;
                    e["client"] = true;
                    t[idx++] = e;
                }
            }
            return t;
        };
        // engine.get_client_mods_folder() -> the full path of the clientmods folder in use
        engineTable["get_client_mods_folder"] = []() -> std::string {
            if (!g_ClientMods) return "";
            std::error_code ec;
            return fs::absolute(g_ClientMods->folder(), ec).string();
        };
        // engine.open_mods_folder(["client"]): the mods folder, or "client" = the client mods folder
        engineTable["open_mods_folder"] = [](sol::optional<std::string> which) {
            if (which && *which == "client" && g_ClientMods) {
                std::error_code ec;
                const fs::path dir = g_ClientMods->folder();
                fs::create_directories(dir, ec);
                std::string abs = fs::absolute(dir, ec).string();
                abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());
#if defined(_WIN32)
                const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
                const std::string cmd = "open \"" + abs + "\"";
#else
                const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
                std::cout << "[Mods] Opening " << abs << std::endl;
                (void)std::system(cmd.c_str());
                return;
            }
            openModsFolder();
        };

        // ---- Texture packs (used by texture_packs.lua) ----
        // engine.list_texture_packs() -> { { id, title, author, description, path, images, replaces, active }, ... }
        //   The first entry is always the default textures (id = "").
        engineTable["list_texture_packs"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& p : listTexturePacks()) {
                sol::table e = lua.create_table();
                e["id"] = p.id;
                e["title"] = p.title;
                e["author"] = p.author;
                e["description"] = p.description;
                e["path"] = p.path;
                e["images"] = p.images;
                e["replaces"] = p.replaces;
                e["total"] = static_cast<int>(g_LayerSources.size() + g_StandardTextureSources.size());
                e["active"] = (p.id == g_ActiveTexturePack);
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_texture_pack(id) -> true, or false + reason. "" = default. Applies immediately.
        engineTable["set_texture_pack"] = [](const std::string& id) {
            std::string error;
            const bool ok = setTexturePack(id, error);
            return std::make_tuple(ok, error);
        };
        engineTable["get_texture_pack"] = []() { return g_ActiveTexturePack; };
        engineTable["open_texture_packs_folder"] = []() { openTexturePacksFolder(); };
        // ---- Which mods a world runs (world list > Mods) ----
        // engine.get_world_mods(world) -> { game = "...", mods = { { id, title, description,
        //     source = "Game"|"Global", required, enabled, default, choice }, ... } }
        //   Game mods are always on. For mods/ folder mods: `default` = on for the game (Mods
        //   screen), `choice` = the world's own (true / false, nil = follows the game),
        //   `enabled` = what the world runs. `loaded` = running right now.
        engineTable["get_world_mods"] = [&lua](const std::string& worldName) -> sol::object {
            if (!validFolderName(worldName)) return sol::make_object(lua, sol::lua_nil);
            const std::string game = worldGame(worldName);
            const auto defaults = loadGameModDefaults(game);
            const auto choices = loadWorldModChoices(worldName);
            sol::table t = lua.create_table();
            sol::table list = lua.create_table();
            int i = 1;
            // the ones a world can switch first, then the game's own
            for (const auto& id : installedGlobalModIds()) {
                sol::table e = lua.create_table();
                std::string title = id, description;
                const fs::path folder = globalModsFolder() / id;
                std::error_code ec;
                if (fs::is_directory(folder, ec)) {
                    auto conf = readConfFile(folder / "mod.conf");
                    if (!conf["title"].empty()) title = conf["title"];
                    else if (!conf["name"].empty()) title = conf["name"];
                    description = conf["description"];
                }
                const bool required = modIsRequired(folder, id);
                const auto d = defaults.find(id);
                const auto c = choices.find(id);
                e["id"] = id; e["title"] = title; e["description"] = description;
                e["source"] = "Global"; e["required"] = required;
                e["default"] = required || (d != defaults.end() && d->second);
                if (c != choices.end() && !required) e["choice"] = c->second;
                e["enabled"] = globalModOn(game, worldName, id, &defaults, &choices);
                e["loaded"] = g_LoadedGlobalMods.count(id) > 0 && game == g_LoadedGameFolder;
                list[i++] = e;
            }
            for (const auto& m : listModsForGame(game)) {
                if (m.source != "Game") continue;
                sol::table e = lua.create_table();
                e["id"] = m.id; e["title"] = m.title; e["description"] = m.description;
                e["source"] = "Game"; e["required"] = true; e["enabled"] = true; e["default"] = true;
                e["loaded"] = (game == g_LoadedGameFolder);
                list[i++] = e;
            }
            t["game"] = game;
            t["mods"] = list;
            return t;
        };
        // engine.set_world_mod(world, id, on) -> true, or false + reason. on = true / false, or
        //   nil to follow the game's default again. Used the next time the world is opened.
        engineTable["set_world_mod"] = [](const std::string& worldName, const std::string& id, sol::optional<bool> on) {
            if (!validFolderName(worldName)) return std::make_tuple(false, std::string("Invalid world name."));
            std::error_code ec;
            if (!fs::is_directory(savesRoot() / worldName, ec)) return std::make_tuple(false, std::string("No such world."));
            if (!validModId(id)) return std::make_tuple(false, std::string("Invalid mod name."));
            if (!fs::exists(globalModsFolder() / id, ec)) return std::make_tuple(false, std::string("Mod not found."));
            if (modIsRequired(globalModsFolder() / id, id)) {
                return std::make_tuple(false, std::string("This mod is required and always runs."));
            }
            auto choices = loadWorldModChoices(worldName);
            if (on) choices[id] = *on; else choices.erase(id);
            if (!writeOnOffFile(worldModsFile(worldName), choices,
                                "# Mods from the mods/ folder this world runs (true) or doesn't (false). Mods not\n"
                                "# listed follow the game's choice (the Mods screen). The game's own mods always run.\n")) {
                return std::make_tuple(false, std::string("Could not save the world's mod list."));
            }
            return std::make_tuple(true, std::string());
        };

        // engine.set_mod_enabled(id, on) -> true, or false + reason: on by default for the selected
        //   game's worlds (a world's own choice wins). Applies when a world is opened.
        engineTable["set_mod_enabled"] = [](const std::string& id, bool on) {
            std::string error;
            if (id.rfind("client:", 0) == 0) {   // a client mod (clientmods/mods.conf)
                const bool ok = g_ClientMods && g_ClientMods->setEnabled(id.substr(7), on, error);
                if (!g_ClientMods && error.empty()) error = "Client mods aren't available.";
                return std::make_tuple(ok, error);
            }
            const bool ok = setModEnabled(id, on, error);
            return std::make_tuple(ok, error);
        };

        World world;
        g_WorldInstance = &world;
        world.setLighting(true);   // block light (Light.h); the dedicated server doesn't need it
        WorldRenderer worldRenderer;          // meshes and draws `world` (client only)
        g_WorldRenderer = &worldRenderer;

        // Worlds are tied to the game whose mods are loaded now (see openActiveWorld)
        g_LoadedGameFolder = g_ActiveGameFolder;

        // Player settings (key bindings, FOV, ...) from settings.txt
        loadSettings();
        // Once a run: what this graphics card can take (Settings greys out the rest)
        {
            static bool s_gpuChecked = false;
            if (!s_gpuChecked) {
                s_gpuChecked = true;
                const auto it = g_Settings.find("gpu_check");
                g_GpuCaps = detectGpuCaps(it != g_Settings.end() && (it->second == "off" || it->second == "false" || it->second == "0"));
            }
            g_LightQuality = currentVideoSetting("light_quality");
            g_BloomOn = currentVideoSetting("bloom") != 0;
            g_FilmicOn = currentVideoSetting("filmic") != 0;
        }

        // update_check = false in settings.txt turns the new-version check off; update_url points
        // it at another version file (it must be allowed like any remote content)
        if (!g_Updates) {
            const auto on = g_Settings.find("update_check");
            const bool enabled = on == g_Settings.end() || (on->second != "false" && on->second != "0");
            const auto url = g_Settings.find("update_url");
            g_Updates = std::make_unique<update::Checker>(
                settingsFile().parent_path() / "cache" / "update",
                (url != g_Settings.end() && !url->second.empty()) ? url->second : std::string(update::kDefaultUrl),
                remotePolicyFromSettings(), enabled);
        }

        // Every player always has a name (used in chat and multiplayer)
        g_ChatSystem->setPlayerName(ensurePlayerName());
        g_ChatSystem->registerCommand("name", "[new name]", "Show or change your player name.");
        g_ChatSystem->registerCommand("netstats", "", "Show FPS, ping and multiplayer update rates.");
        // Typed chat goes to everyone when playing online (returns false offline = show locally)
        g_ChatSystem->setMessageSender([](const std::string& text) -> bool {
            if (g_Server.running()) { g_Server.sendChat(text); return true; }
            if (g_Client.state() == GameClient::State::Joined) { g_Client.sendChat(text); return true; }
            return false;
        });

        // The chosen texture pack must be known before mods load their textures
        g_ActiveTexturePack = readActiveTexturePack();
        if (!g_ActiveTexturePack.empty()) {
            std::cout << "[Texture] Using texture pack '" << g_ActiveTexturePack << "'" << std::endl;
        }

        std::vector<std::string> modSearchPaths = { "mods", "../mods", "../../mods", "games/" + g_ActiveGameFolder + "/mods" };
        buildTextureIndex();   // pictures anywhere in the game / mods can be used by name
        std::vector<fs::path> modFiles;
        std::set<std::string> initFolders;
        // The mods/ folder mods this session runs: the game's defaults, or the world's own choice
        const std::set<std::string> sessionGlobalMods = resolveGlobalMods(g_ActiveGameFolder, g_SessionWorld);
        g_LoadedGlobalMods = sessionGlobalMods;

        std::set<std::string> seenModRoots;   // the same folder reached by two paths is only loaded once
        for (const auto& searchPath : modSearchPaths) {
            std::string resolvedPath = searchPath;
            if (!fs::exists(resolvedPath) && fs::exists("../../" + searchPath)) resolvedPath = "../../" + searchPath;
            if (!fs::exists(resolvedPath)) continue;

            const std::string rootKey = modPathKey(resolvedPath);
            if (!seenModRoots.insert(rootKey).second) {
                std::cout << "[Mod] Search path '" << searchPath << "' leads to " << rootKey
                          << ", which is already loaded - skipping it" << std::endl;
                continue;
            }
            std::cout << "[Mod] Mod folder: " << rootKey << " (search path '" << searchPath << "')" << std::endl;

            // Mods in the mods/ folder can be switched off on the Mods screen; game mods always load.
            const bool isGlobalFolder = searchPath.rfind("games/", 0) != 0;
            std::set<std::string> reportedDisabled;
            const size_t rootStart = modFiles.size();

            for (const auto& entry : fs::recursive_directory_iterator(resolvedPath)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;

                if (isGlobalFolder) {
                    std::error_code relEc;
                    const fs::path rel = fs::relative(entry.path(), resolvedPath, relEc);
                    const std::string modId = (relEc || rel.empty()) ? "" : rel.begin()->string();
                    if (modId == "clientmods") continue;   // mods/clientmods/: the player's client mods, not game mods
                    if (!modId.empty()) {
                        // On for this game and the world being opened (Luanti-style world mods)
                        const bool on = sessionGlobalMods.count(modId) > 0;
                        g_ModEnabledAtStartup[modId] = on;
                        if (!on) {
                            if (reportedDisabled.insert(modId).second) {
                                std::cout << "[Mod] Not loading " << modId << " (off for "
                                          << (g_SessionWorld.empty() ? "this game" : "world '" + g_SessionWorld + "'") << ")" << std::endl;
                            }
                            continue;
                        }
                    }
                }

                if (entry.path().filename() == "init.lua") {
                    initFolders.insert(entry.path().parent_path().string());
                }
                modFiles.push_back(entry.path());
            }
            modorder::sortRoot(modFiles, rootStart);   // alphabetical, whatever the file system's order
        }
        modorder::applyDepends(modFiles);              // mod.conf depends = ... loads after those mods

        auto isInsideInitFolder = [&initFolders](const fs::path& file) {
            for (fs::path p = file.parent_path(); ; p = p.parent_path()) {
                if (initFolders.count(p.string())) return true;
                if (p.empty() || p == p.parent_path()) return false;
            }
        };

        auto runModFile = [&](const fs::path& file) {
            g_CurrentModPath = file.parent_path().string();
            if (std::find(g_ModFolders.begin(), g_ModFolders.end(), g_CurrentModPath) == g_ModFolders.end()) {
                g_ModFolders.push_back(g_CurrentModPath);
            }
            lua["g_CurrentModPath"] = g_CurrentModPath;
            std::cout << "[Mod] Running " << modPathKey(file) << std::endl;
            try {
                lua.script_file(file.string());
                g_ModFileStatus[modPathKey(file)] = "";              // ran OK (shown on the Mods screen)
            } catch (const std::exception& e) {
                std::cerr << "[Mod] Failed to load " << file.string() << ": " << e.what() << std::endl;
                g_ModFileStatus[modPathKey(file)] = e.what();
            }
        };

        installModSandbox(lua);   // before any mod runs: require / dofile only inside mods and lualib
        // Online content for mods (remote.*): data only, from GitHub and approved sites
        std::unique_ptr<RemoteLua> remoteContent;
        {
            std::vector<fs::path> modRoots;
            for (const std::string& p : {std::string("mods"), std::string("../mods"), std::string("../../mods"),
                                         "games/" + g_ActiveGameFolder + "/mods", "../../games/" + g_ActiveGameFolder + "/mods"}) {
                std::error_code ec;
                if (fs::is_directory(p, ec)) modRoots.push_back(p);
            }
            modRoots.push_back(globalModsFolder());
            remoteContent = std::make_unique<RemoteLua>(lua, settingsFile().parent_path() / "cache" / "remote",
                                                        remotePolicyFromSettings(), modRoots);
        }
        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua") runModFile(file);
        }
        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua" || isInsideInitFolder(file)) continue;
            runModFile(file);
        }

        // The player's own client mods (clientmods/), after the game's mods so they can use
        // its ui.* and sound.*. settings.txt: client_mods = false switches them all off.
        // Where client mods are: a clientmods/ folder next to mods/ or inside it, or next to the
        // game (the first one that exists; made next to mods/ if there's none)
        const fs::path clientModsFolder = [] {
            const fs::path modsDir = globalModsFolder();
            const fs::path beside = (modsDir.has_parent_path() ? modsDir.parent_path() : fs::path(".")) / "clientmods";
            const std::vector<fs::path> places = { beside, modsDir / "clientmods", "clientmods", "../clientmods",
                                                   "../../clientmods", g_ExeDir / "clientmods" };
            std::error_code ec;
            for (const auto& p : places) if (!p.empty() && fs::is_directory(p, ec)) return p;
            return beside;
        }();
        {
            std::error_code ec;
            std::cout << "[Client mods] Folder: " << fs::absolute(clientModsFolder, ec).string() << std::endl;
        }
        clientMods.setFolder(clientModsFolder.string());
        // Sounds are looked for in every mod's folder (and its sounds/), then the game's. Installed
        // as the client mods' folders become known (before their init.lua runs) and again after
        // loading; Audio's sandboxed lookups check every file against these same roots each time.
        auto installSoundFolders = [&clientMods] {
            std::vector<std::string> soundFolders = g_ModFolders;
            for (const auto& f : clientMods.modFolders()) soundFolders.push_back(f);   // client mods' sounds, last
            soundFolders.push_back("games/" + g_ActiveGameFolder);
            soundFolders.push_back("../../games/" + g_ActiveGameFolder);
            g_Audio.setSearchFolders(soundFolders);
        };
        clientMods.onModFoldersReady = installSoundFolders;
        if (settingInt("client_mods", 1) != 0 && g_Settings["client_mods"] != "false") {
            const fs::path folder = clientModsFolder;
            clientMods.showChat = [](const std::string& text, float r, float g, float b) {
                if (g_ChatSystem) g_ChatSystem->addMessage(text, glm::vec3(r, g, b));
            };
            clientMods.init(lua, folder.string());
            addClientModWorldApi(clientMods);
            clientMods.load();
            // The HUD is drawn through the game's state, like render_hud
            lua["__client_hud"] = [](float W, float H) { if (g_ClientMods) g_ClientMods->hud(W, H); };
            g_ChatSystem->setLocalCommandHandler([](const std::string& text) {
                return g_ClientMods && g_ClientMods->command(text);
            });
            g_ChatSystem->setMessageListener([](const std::string& text) {
                if (g_ClientMods) g_ClientMods->chatMessage(text);
            });
        }

        // Saved video settings win over values set by mods during loading
        applyStoredVideoSettings();

        g_Lua = &lua;
        installSoundFolders();   // (also with client mods off)
        // Other players building (and, when hosting, players editing our world): their
        // break/place sounds where it happened. Fluids flowing make no sound.
        {
            auto editSound = [](const glm::ivec3& pos, uint8_t oldId, uint8_t newId) {
                FluidRegistry::get().ensureFresh();
                if (FluidRegistry::get().forBlock(oldId) || FluidRegistry::get().forBlock(newId)) return;
                // someone else broke it: its bits fly too
                if (newId == 0 && oldId != 0 && g_Particles) {
                    g_Particles->spawnBreakParticles(glm::vec3(pos), oldId, static_cast<float>(blockParticleLayer(oldId)));
                }
                const glm::vec3 at = glm::vec3(pos) + glm::vec3(0.5f);
                if (newId == 0) playBlockSound(oldId, "dug", at);
                else playBlockSound(newId, "place", at);
            };
            g_Client.onBlockChanged = editSound;
            // Hosting: other players' edits also reach on_block_broken / on_block_placed here,
            // since the host decides the world (leaf decay and the like)
            g_Server.onPlayerEdit = [editSound](const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t newId) {
                editSound(pos, oldId, newId);
                if (FluidRegistry::get().forBlock(oldId) || FluidRegistry::get().forBlock(newId)) return;
                g_ServerApi.edited(name, pos, oldId, newId);   // on_block_broken / on_block_placed (with the player)
            };
        }
        // Luanti's crack picture, if the game has one and no mod chose another
        if (g_CrackTexture == 0) {
            const std::vector<std::string> places = { "textures/crack_anylength.png", "../../textures/crack_anylength.png",
                "games/" + g_ActiveGameFolder + "/textures/crack_anylength.png",
                "../../games/" + g_ActiveGameFolder + "/textures/crack_anylength.png" };
            for (const auto& f : places) {
                std::error_code fec;
                if (fs::exists(f, fec)) {
                    sol::protected_function setCrack = lua["engine"]["set_dig_crack_texture"];
                    if (setCrack.valid()) setCrack("crack_anylength.png");
                    std::cout << "[Dig] Using crack_anylength.png (" << g_CrackStages << " stages)" << std::endl;
                    break;
                }
            }
        }

        // lua_api.txt: everything modders can use in this build (see writeApiReference)
        reportMissingTextures();
        writeApiReference(lua);
        engineTable["write_api_reference"] = [&lua]() { return writeApiReference(lua); };

        lua.new_usertype<ChunkData>("Chunk",
            "set_block", &ChunkData::setBlock, "setBlock", &ChunkData::setBlock,
            "get_block", &ChunkData::getLocalBlock, "getBlock", &ChunkData::getLocalBlock,
            "set_block_with_meta", &ChunkData::setBlockWithMeta,
            "set_metadata", &ChunkData::setMetadata, "get_metadata", &ChunkData::getLocalMetadata,
            // chunk:set_tint(x, z, r, g, b [, "grass" | "foliage"]) - biome colour (0..255 or 0..1)
            "set_tint", [](ChunkData& c, int x, int z, float r, float g, float b, sol::optional<std::string> channel) {
                const bool bytes = r > 1.0f || g > 1.0f || b > 1.0f;
                auto to8 = [bytes](float v) { return static_cast<uint32_t>(std::clamp(bytes ? v : v * 255.0f, 0.0f, 255.0f)); };
                c.setColumnTint(x, z, (to8(r) << 16) | (to8(g) << 8) | to8(b), channel && *channel == "foliage" ? 1 : 0);
            }
        );

        // New chunks are generated on background threads (singleplayer and LAN hosting alike),
        // so exploring never makes the game - or the players connected to it - stutter.
        // Two cores stay free: this thread and the graphics driver.
        CaveGenerator::prepare();
        std::vector<fs::path> genModFiles;
        for (const auto& f : modFiles) if (f.filename() == "init.lua") genModFiles.push_back(f);
        for (const auto& f : modFiles) if (f.filename() != "init.lua" && !isInsideInitFolder(f)) genModFiles.push_back(f);
        ChunkGenerator chunkGenerator(0, [genModFiles]() { return makeWorldGenLua(genModFiles); }, world.seed(), 2);
        world.setGenerator(&chunkGenerator);
        // The world's other dimensions use the same generator threads (Dimensions.h)
        g_DimWorlds.primary = &world;
        g_DimWorlds.generator = &chunkGenerator;
        g_DimWorlds.onLoaded = [](const std::string& d, World& w) {
            if (g_Server.running()) w.setRecordChanges(true);
            g_BlockMods.loadDimension(d, w.saveDirectory());
        };
        g_DimWorlds.onUnloading = [](const std::string& d, World& w) {
            g_BlockMods.saveDimension(d, w.saveDirectory());
            g_BlockMods.forgetDimension(d);
        };
        std::cout << "[World] Generating chunks on " << chunkGenerator.threadCount() << " background thread"
                  << (chunkGenerator.threadCount() == 1 ? "" : "s") << std::endl;

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, compileShader(GL_VERTEX_SHADER, vertexShaderSource));
        glAttachShader(shaderProgram, compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource));
        glLinkProgram(shaderProgram);
        {
            GLint ok = 0;
            glGetProgramiv(shaderProgram, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[2048];
                glGetProgramInfoLog(shaderProgram, sizeof(log), nullptr, log);
                std::cerr << "[Shader] the world shader failed to link on this graphics driver:\n" << log << std::endl;
            }
            // Every texture it reads gets its own unit right away (two kinds of texture on one
            // unit - as they all are, on 0, until set - and strict drivers refuse every draw)
            glUseProgram(shaderProgram);
            glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);
            glUniform1i(glGetUniformLocation(shaderProgram, "u_Voxels"), 3);
            glUniform1i(glGetUniformLocation(shaderProgram, "u_SceneColor"), 5);
            glUniform1i(glGetUniformLocation(shaderProgram, "u_SceneDepth"), 6);
        }
        glUseProgram(shaderProgram);
        glUniform1f(glGetUniformLocation(shaderProgram, "u_WeatherLight"), 1.0f);
        glUniform4f(glGetUniformLocation(shaderProgram, "u_FallbackLight"), 1.0f, 0.0f, 0.0f, 0.0f);
        glUniform1f(glGetUniformLocation(shaderProgram, "u_MinLight"), g_LightSettings.minLight);
        glUniform1f(glGetUniformLocation(shaderProgram, "u_Brightness"), g_LightSettings.effectiveBrightness());
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_Gamma"), g_LightSettings.gammaLift());
        glUniform1f(glGetUniformLocation(shaderProgram, "u_BlockLightScale"), g_LightSettings.blockLightScale);

        GLuint sunProgram = glCreateProgram();
        glAttachShader(sunProgram, compileShader(GL_VERTEX_SHADER, sunVertexShaderSource));
        glAttachShader(sunProgram, compileShader(GL_FRAGMENT_SHADER, sunFragmentShaderSource));
        glLinkProgram(sunProgram);

        GLuint wireframeProgram = glCreateProgram();
        glAttachShader(wireframeProgram, compileShader(GL_VERTEX_SHADER, wireframeVertexShaderSource));
        glAttachShader(wireframeProgram, compileShader(GL_FRAGMENT_SHADER, wireframeFragmentShaderSource));
        glLinkProgram(wireframeProgram);

        float sunVertices[] = { -12.0f, 12.0f, 0.0f, 0.0f, 1.0f, -12.0f, -12.0f, 0.0f, 0.0f, 0.0f, 12.0f, -12.0f, 0.0f, 1.0f, 0.0f, 12.0f, 12.0f, 0.0f, 1.0f, 1.0f };
        unsigned int sunIndices[] = { 0, 1, 2, 2, 3, 0 };
        GLuint sunVAO, sunVBO, sunEBO;
        glGenVertexArrays(1, &sunVAO); glGenBuffers(1, &sunVBO); glGenBuffers(1, &sunEBO);
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(sunVertices), sunVertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIndices), sunIndices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        float crosshairVertices[] = { -0.015f, 0.0f, 1,1,1,0.8f, 0.015f, 0.0f, 1,1,1,0.8f, 0.0f, -0.025f, 1,1,1,0.8f, 0.0f, 0.025f, 1,1,1,0.8f };
        GLuint crosshairVAO, crosshairVBO;
        glGenVertexArrays(1, &crosshairVAO); glGenBuffers(1, &crosshairVBO);
        glBindVertexArray(crosshairVAO);
        glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

        Player player;
        ParticleSystem particles;   // particles (ParticleSystem.h; needs the GL context, so created here)
        g_Particles = &particles;
        struct ParticlesGone { ~ParticlesGone() { g_Particles = nullptr; } } particlesGone;
        PlayerAvatars playerAvatars;   // other players in multiplayer
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);

        ctx.player = &player; ctx.lua = &lua;
        g_PlayerInstance = &player;

        glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
            if (g_CurrentGameState == GameState::PLAYING) {
                if (g_ChatSystem && g_ChatSystem->isOpen) return;
                EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (c && c->player && glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
                    c->player->handleMouseLook(xpos, ypos);
                }
            }
        });

        glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
            // ui.get_mouse_events(): presses and releases (with double-clicks), for every screen
            if (action == GLFW_PRESS || action == GLFW_RELEASE) {
                uidraw::MouseEvent e;
                e.button = button;
                e.pressed = action == GLFW_PRESS;
                e.mods = mods;
                int fw = 0, fh = 0;
                glfwGetFramebufferSize(win, &fw, &fh);
                glfwGetCursorPos(win, &e.x, &e.y);
                scaleCursorToFramebuffer(win, e.x, e.y, fw, fh);
                uidraw::addMouse(e);
            }
            // Chat open: a click on a web address in it ("Open this link?" first)
            if (g_ChatSystem && g_ChatSystem->isOpen && action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
                int fw = 0, fh = 0;
                glfwGetFramebufferSize(win, &fw, &fh);
                double mx = 0.0, my = 0.0;
                glfwGetCursorPos(win, &mx, &my);
                scaleCursorToFramebuffer(win, mx, my, fw, fh);
                if (g_ChatSystem->handleMouseClick(mx, my, win)) return;
            }
            // Settings > Controls waiting for a new key: any mouse button can be the answer.
            // handle_settings_mouse(code) -> true = used (code = 1000 + button, as keybinds store it)
            if (action == GLFW_PRESS && settingsOpen()) {
                EngineContext* sctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (sctx && sctx->lua) {
                    sol::protected_function f = (*sctx->lua)["handle_settings_mouse"];
                    if (f.valid()) {
                        auto r = f(kMouseKeyBase + button);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] handle_settings_mouse: " << err.what() << std::endl; }
                        else if (r.get_type() == sol::type::boolean && r.get<bool>()) return;
                    }
                }
            }
            // A mod's inventory screen: handle_inventory_click(mx, my, W, H, "left"|"right", shift)
            if (g_LuaInventoryOpen && g_CurrentGameState == GameState::PLAYING && action == GLFW_PRESS &&
                (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)) {
                EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (!ctx) return;
                int sw, sh;
                glfwGetFramebufferSize(win, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(win, &mx, &my);
                scaleCursorToFramebuffer(win, mx, my, sw, sh);
                callLuaUI(ctx->lua, "handle_inventory_click", mx, my, sw, sh,
                          std::string(button == GLFW_MOUSE_BUTTON_LEFT ? "left" : "right"),
                          (mods & GLFW_MOD_SHIFT) != 0);
                return;
            }
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
                EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (!ctx) return;

                int sw, sh;
                glfwGetFramebufferSize(win, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(win, &mx, &my);
                scaleCursorToFramebuffer(win, mx, my, sw, sh);

                if (g_CurrentGameState == GameState::MENU && g_MainMenu) {
                    std::string actionResult = g_MainMenu->handleMouseClick(mx, my, sw, sh);
                    if (actionResult == "play") {
                        // Singleplayer -> world selection screen (world_select.lua)
                        g_CurrentGameState = GameState::WORLD_SELECT;
                        callLuaUI(ctx->lua, "on_world_select_opened");
                    } else if (actionResult == "update") {
                        // The new version's download page, in the player's own browser (they clicked
                        // it). Only an https GitHub page or the version file's own site gets here
                        // (UpdateCheck.h), and WebLink opens only a plain, safe address.
                        if (g_Updates && !g_Updates->info().download.empty() &&
                            !weblink::open(g_Updates->info().download)) {
                            std::cerr << "[Update] Couldn't open " << g_Updates->info().download << std::endl;
                        }
                    } else if (actionResult == "multiplayer") {
                        // Multiplayer -> server list (multiplayer.lua)
                        g_CurrentGameState = GameState::MULTIPLAYER;
                        callLuaUI(ctx->lua, "on_multiplayer_opened");
                    } else if (actionResult == "mods") {
                        // Mods -> installed mods screen (mods_menu.lua)
                        g_CurrentGameState = GameState::MODS;
                        callLuaUI(ctx->lua, "on_mods_opened");
                    } else if (actionResult == "texturepacks") {
                        // Textures -> texture pack picker (texture_packs.lua)
                        g_CurrentGameState = GameState::TEXTURE_PACKS;
                        callLuaUI(ctx->lua, "on_texture_packs_opened");
                    } else if (actionResult == "settings") {
                        // Settings -> key bindings and video settings (settings_menu.lua)
                        g_CurrentGameState = GameState::SETTINGS;
                        callLuaUI(ctx->lua, "on_settings_opened");
                    } else if (actionResult == "quit") {
                        glfwSetWindowShouldClose(win, true);
                    }
                } else if (settingsOpen()) {
                    // (the player's own click: the only thing allowed to change their lighting and
                    // bloom - see setVideoSetting)
                    g_PlayerSettingsClick = true;
                    callLuaUI(ctx->lua, "handle_settings_click", mx, my, sw, sh);
                    g_PlayerSettingsClick = false;
                } else if (g_CurrentGameState == GameState::WORLD_SELECT) {
                    callLuaUI(ctx->lua, "handle_world_select_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::MULTIPLAYER) {
                    callLuaUI(ctx->lua, "handle_multiplayer_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::MODS) {
                    callLuaUI(ctx->lua, "handle_mods_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                    callLuaUI(ctx->lua, "handle_texture_packs_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::PLAYING && g_Paused) {
                    // Forwarded to handle_pause_click in pause_menu.lua
                    try {
                        sol::protected_function clickFn = (*ctx->lua)["handle_pause_click"];
                        if (clickFn.valid()) {
                            sol::protected_function_result res = clickFn(mx, my, sw, sh);
                            if (!res.valid()) {
                                sol::error err = res;
                                std::cerr << "[Pause Lua] handle_pause_click: " << err.what() << std::endl;
                            }
                        }
                    } catch (...) {}
                } else if (g_CurrentGameState == GameState::PLAYING && g_Inventory.isCreativeOpen()) {
                    // Forwarded to handle_creative_click in inventory.lua
                    g_Inventory.handleCreativeClick(mx, my, static_cast<float>(sw), static_cast<float>(sh), "left",
                                                    (mods & GLFW_MOD_SHIFT) != 0);   // shift-click: to the trash

                    try {
                        sol::state* lua = ctx->lua;
                        sol::function handleInvClick = (*lua)["handle_inventory_click"];
                        if (handleInvClick.valid()) {
                            handleInvClick(mx, my, sw, sh);
                        }
                    } catch (...) {}
                }
            }
        });

        float lastFrameTime = static_cast<float>(glfwGetTime());
        bool leftMousePressedLast = false;
        bool rightMousePressedLast = false;

        // The world's generator can't run (World::fatalError): save what there is, leave the world
        // and show why on the world list - instead of loading forever or playing in a void
        auto leaveBrokenWorld = [&]() {
            g_WorldError = world.fatalError();
            std::cerr << "[World] Closing the world: " << g_WorldError << std::endl;
            sol::protected_function quit = lua["engine"]["quit_to_menu"];
            if (quit.valid()) quit();
            g_CurrentGameState = GameState::WORLD_SELECT;
            callLuaUI(&lua, "on_world_select_opened");
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);   // (read again each frame)
        };

        // A new session made for a world: open it now that its mods are loaded
        if (g_Reload.pending) {
            const std::string target = g_Reload.world;
            g_Reload = ReloadRequest{};
            if (!target.empty()) {
                g_ActiveWorldName = target;
                beginLoadingWorld(window);
            }
        }

        // A reload asked for during the session: one more frame (the loading screen), then go
        int reloadFrames = 0;
        while (!glfwWindowShouldClose(window)) {
            if (g_Reload.pending && reloadFrames++ >= 1) break;
            float currentFrameTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;
            {
                static int frames = 0;
                static float fpsTimer = 0.0f;
                ++frames;
                fpsTimer += deltaTime;
                if (fpsTimer >= 0.5f) { g_Fps = frames / fpsTimer; frames = 0; fpsTimer = 0.0f; }
            }

            uidraw::beginFrame();   // last frame's input is gone; this frame's arrives below
            glfwPollEvents();

            g_PlayerPosition = player.position;
            {
                const glm::vec3 eye = player.eyePosition();
                const glm::vec3 fwd(std::cos(glm::radians(player.yaw)) * std::cos(glm::radians(player.pitch)),
                                    std::sin(glm::radians(player.pitch)),
                                    std::sin(glm::radians(player.yaw)) * std::cos(glm::radians(player.pitch)));
                g_Audio.setListener(eye, fwd, glm::vec3(0.0f, 1.0f, 0.0f));
                g_Audio.update(deltaTime);
                pollFirewallUpdates(deltaTime);
                if (remoteContent) remoteContent->update();   // finished downloads -> mods' callbacks
            }
            g_LastMoveKeys = readMoveInput(window, player.yaw, player.pitch).keys;

            // Shared values that changed (set here, or sent by the server): on_shared_changed(key, value)
            {
                std::vector<std::string> changed;
                changed.swap(g_SharedChanged);
                if (g_Client.state() == GameClient::State::Joined) {
                    for (auto& k : g_Client.takeSharedChanges()) changed.push_back(std::move(k));
                }
                if (!changed.empty()) {
                    sol::protected_function fn = lua["on_shared_changed"];
                    for (const auto& k : changed) {
                        if (!fn.valid()) break;
                        const std::string value = g_Client.state() == GameClient::State::Joined
                            ? g_Client.getShared(k) : g_SharedValues[k];
                        auto r = fn(k, value);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_shared_changed: " << err.what() << std::endl; }
                    }
                }
            }

            // Mod messages: from the server (or this game's own server side) to on_server_message,
            // and from this game's own mods to on_mod_message when it is the server itself
            {
                std::vector<LocalModMessage> toGame, toServer;
                toGame.swap(g_ModToOwnGame);
                toServer.swap(g_ModToOwnServer);
                if (g_Client.state() == GameClient::State::Joined) {
                    for (auto& m : g_Client.takeModMessages()) toGame.push_back({ std::move(m.channel), std::move(m.data) });
                }
                if (!toGame.empty()) {
                    sol::protected_function fn = lua["on_server_message"];
                    for (const auto& m : toGame) {
                        if (!fn.valid()) break;
                        auto r = fn(m.channel, m.data);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_server_message: " << err.what() << std::endl; }
                    }
                }
                if (!g_Client.active()) {
                    for (const auto& m : toServer) g_ServerApi.modMessage(playerName(), m.channel, m.data);
                }
            }

            // Game mode from the server (or back to our own game after leaving): on_game_mode(mode, is_operator)
            if (g_Client.takeModeChange()) {
                const std::string mode = g_Client.gameMode();
                if (!mode.empty()) {
                    g_Creative = mode == "creative";
                    g_Dig = DigState{};
                    if (!g_Creative && g_Inventory.isCreativeOpen()) g_Inventory.setCreativeOpen(false);
                }
                sol::protected_function fn = lua["on_game_mode"];
                if (fn.valid()) {
                    auto r = mode.empty() ? fn(sol::lua_nil, true) : fn(mode, g_Client.isOperator());
                    if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_game_mode: " << err.what() << std::endl; }
                }
            }

            // The time of day (DayTime.h). It runs while playing - in singleplayer not while paused,
            // like Minecraft; on a server or a LAN game it keeps going, as the server's does. On a
            // server, the server's clock wins (it's sent every few seconds); when hosting a LAN
            // game, ours goes to everyone who joins.
            if (g_LightingSystem && g_CurrentGameState == GameState::PLAYING && g_WorldInstance) {
                const bool joined = g_Client.state() == GameClient::State::Joined;
                if (!g_Paused || joined || g_Server.running()) g_LightingSystem->update(deltaTime);
                DayClock fromServer;
                if (joined && g_Client.takeDayTime(fromServer)) {
                    g_LightingSystem->clock = fromServer;
                    g_LightingSystem->customTimeOfDay = -1.0f;
                }
                if (g_Server.running()) g_Server.setDayTime(g_LightingSystem->clock);
            }

            // The server answered a chat completion (Tab): show it
            if (g_Client.takeCompletionArrived() && g_ChatSystem && g_ChatSystem->isOpen) g_ChatSystem->refreshSuggestions();

            // What the server asked our game to do (/give, /clear, /kill): mods first
            // (on_server_action(action, a, b) -> true if they did it: the survival mod kills with
            // its death screen and clears its inventory), then the engine's own way
            if (g_Client.state() == GameClient::State::Joined) {
                for (const auto& act : g_Client.takeActions()) {
                    bool handled = false;
                    sol::protected_function hook = lua["on_server_action"];
                    if (hook.valid()) {
                        auto r = hook(act.action, act.a, act.b);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_server_action: " << err.what() << std::endl; }
                        else handled = r.get_type() == sol::type::boolean && r.get<bool>();
                    }
                    if (handled) continue;
                    if (act.action == "give") {
                        int count = 1;
                        try { count = std::clamp(std::stoi(act.b), 1, 64 * 64); } catch (...) {}
                        const bool canTake = canPickUpItem(act.a, count);     // (not while dead...)
                        while (count > 0 && canTake) {            // in stacks of up to 64
                            const int n = std::min(count, 64);
                            if (!giveHotbarItem(act.a, n)) break;
                            count -= n;
                        }
                        // No room: back on the ground at our feet (picked up again when there's room)
                        if (count > 0 && g_PlayerInstance) {
                            g_Client.sendDropItem(act.a, count, g_PlayerInstance->position + glm::vec3(0.0f, 0.5f, 0.0f), true);
                        }
                    } else if (act.action == "clear") {
                        if (g_Hotbar) for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
                    } else if (act.action == "kill") {
                        g_Client.sendCommand("respawn");          // (no survival: straight back to spawn)
                    }
                }
            }

            // Client mods: what the server allows, joining / leaving, and their update(dt)
            if (g_ClientMods && g_ClientMods->ready()) {
                g_ClientMods->setRules(currentClientModRules());
                const bool inWorld = g_CurrentGameState == GameState::PLAYING && g_WorldInstance;
                const std::string where = !inWorld ? "" : g_Server.running() ? "lan_host"
                                        : g_Client.state() == GameClient::State::Joined ? "server" : "singleplayer";
                g_ClientMods->update(deltaTime, where, where == "server" ? g_Client.address() : g_ActiveWorldName);
            }

            // on_update(dt): errors are printed (once until the message changes), never hidden
            {
                static std::string lastUpdateError;
                g_ArtNet.poll(glfwGetTime());           // (DMX that arrived: before mods read it)
                sol::protected_function onUpdate = lua["on_update"];
                if (onUpdate.valid()) {
                    sol::protected_function_result r = onUpdate(deltaTime);
                    if (!r.valid()) {
                        sol::error err = r;
                        if (lastUpdateError != err.what()) {
                            lastUpdateError = err.what();
                            std::cerr << "[Lua] on_update: " << lastUpdateError << std::endl;
                        }
                    }
                }
            }

            // Entities: move, think (their mods' on_step), get picked up, spawn. Ours to run in
            // singleplayer (not while paused) and when hosting (always: the others still play).
            if (g_CurrentGameState == GameState::PLAYING && g_WorldInstance && entitiesHere() &&
                (!g_Paused || g_Server.running())) {
                g_EntityLua.setWorld(g_WorldInstance);
                g_EntityLua.step(std::min(deltaTime, 0.1f));
                // ABMs (grass spreading, crops...) near players; LBMs on chunks that just loaded
                // (each dimension's own: ours on screen, and the ones guests are in)
                std::map<std::string, std::vector<glm::vec3>> byDim;
                if (g_PlayerInstance) byDim[g_WorldInstance->dimension()].push_back(g_PlayerInstance->position);
                if (g_Server.running()) for (const auto& p : g_Server.playerPlaces()) byDim[p.dimension].push_back(p.pos);
                g_BlockMods.update(std::min(deltaTime, 0.25f), *g_WorldInstance, byDim[g_WorldInstance->dimension()]);
                g_DimWorlds.forEach([&](const std::string& d, World& w) {
                    dims::InDimension here(d);
                    g_BlockMods.update(std::min(deltaTime, 0.25f), w, byDim[d]);
                });
                // the background dimensions load around their players, flow, and unload when empty
                byDim.erase(g_WorldInstance->dimension());
                g_DimWorlds.update(lua, std::min(deltaTime, 0.25f), glfwGetTime(), byDim);
            }

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (height == 0) height = 1;

            bool isCursorDisabled = (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED);

            // ---- Multiplayer networking (every frame, also while paused or loading) ----
            if (g_CurrentGameState == GameState::MULTIPLAYER) {
                g_Pinger.update();
                if (!g_Lan.running()) g_Lan.start();   // "Scanning for games on your local network"
                g_Lan.update();
            } else if (g_Lan.running()) {
                g_Lan.stop();
            }
            if (g_Server.running()) {
                g_Server.update(player.position, player.yaw, player.pitch, deltaTime);
                g_ServerApi.flush();   // kicks asked for by mods (never in the middle of the network code)
                for (const auto& line : g_Server.takeChat()) {
                    if (g_ChatSystem) g_ChatSystem->addMessage(line.text, line.color);
                }
            }
            if (g_Client.active()) {
                g_Client.update(MoveState{player.position, player.velocity, player.isGrounded},
                                player.yaw, player.pitch, deltaTime);
                // The server disagreed with where we moved ourselves: take its answer
                // (with our newer inputs already replayed on top)
                MoveState corrected;
                if (g_Client.takeCorrection(corrected)) {
                    player.position = corrected.position;
                    player.velocity = corrected.velocity;
                    player.isGrounded = corrected.grounded;
                }
                for (const auto& line : g_Client.takeChat()) {
                    if (g_ChatSystem) g_ChatSystem->addMessage(line.text, line.color);
                }
                GameClient::Welcome welcome;
                if (g_Client.takeWelcome(welcome)) {
                    // Remember this server's identity (first visit): from now on the password is
                    // only used to answer this same server at this address (Auth.h)
                    if (!g_Client.serverId().empty()) {
                        std::string& known = g_Settings["server_id." + g_Client.address()];
                        if (known.empty()) { known = g_Client.serverId(); saveSettings(); }
                    }
                    // Joined: the client already reset the world into remote mode
                    if (g_LightingSystem) {           // (until the server sends its time)
                        g_LightingSystem->clock = DayClock{};
                        g_LightingSystem->customTimeOfDay = -1.0f;
                    }
                    player.position = welcome.spawn;
                    g_SpawnPlaced = true;
                    if (g_Hotbar) for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
                    g_ModelInstances.clear();
                    g_ActiveWorldName.clear();
                    g_CurrentGameState = GameState::LOADING;
                    g_PendingWorldOpen = false;
                    g_LoadStartTime = glfwGetTime();
                    g_LoadingProgress = 0.0f;
                    g_Paused = false;
                }
            }
            {
                std::string reason;
                if (g_Client.takeDisconnect(reason)) {
                    g_NetError = reason;
                    if (g_CurrentGameState == GameState::LOADING || g_CurrentGameState == GameState::PLAYING) {
                        // Kicked or lost connection while in the world: back to the server list
                        world.reset();
                        g_ModelInstances.clear();
                        g_ThirdPerson = false;
                        g_ThirdPersonFront = false;
                        g_Paused = false;
                        g_SettingsInGame = false;
                        g_Inventory.setCreativeOpen(false);
                        g_CurrentGameState = GameState::MULTIPLAYER;
                        callLuaUI(&lua, "on_multiplayer_opened");
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        isCursorDisabled = false;
                    }
                }
            }

            if (g_CurrentGameState == GameState::LOADING && !g_Reload.pending) {
                if (g_PendingWorldOpen) {
                    g_PendingWorldOpen = false;
                    if (!openActiveWorld(world, player)) {
                        // Wrong game for this world - back to the world list
                        world.setSaveDirectory("");
                        g_CurrentGameState = GameState::WORLD_SELECT;
                        callLuaUI(&lua, "on_world_select_opened");
                        continue;
                    }
                }

                world.setLoadBudgetMs(40.0f);
                worldRenderer.setMeshBudgetMs(30.0f);
                worldRenderer.setUploadBudgetMs(30.0f);
                world.update(player.position, lua);
                if (!world.fatalError().empty()) { leaveBrokenWorld(); continue; }
                worldRenderer.update(world, player.position);

                const int sx = static_cast<int>(std::floor(player.position.x));
                const int sz = static_cast<int>(std::floor(player.position.z));
                const bool timedOut = (glfwGetTime() - g_LoadStartTime) > kLoadTimeoutSec;

                // The chunk the player stands in must be there before they can move: the timeout
                // may skip waiting for meshes, never for the ground (a missing chunk isn't air).
                // Until it loads, the saved position is kept as it is.
                const bool spawnChunkReady = world.isLoaded(glm::ivec3(sx, 0, sz));
                if (!g_SpawnPlaced) {
                    const int ground = world.groundHeightAt(sx, sz);
                    if (ground >= 0) {
                        player.position.y = static_cast<float>(ground);
                        g_SpawnPlaced = true;
                    } else if (timedOut && spawnChunkReady) {   // loaded, with nothing to stand on
                        player.position.y = 100.0f;
                        g_SpawnPlaced = true;
                    }
                }

                g_LoadingProgress = worldRenderer.areaReadyProgress(world, player.position, kSpawnReadyRadius);
                g_SpawnChunkWaiting = timedOut && !spawnChunkReady;   // Esc = back to the menu

                if (g_SpawnPlaced && spawnChunkReady && (g_LoadingProgress >= 1.0f || timedOut)) {
                    g_SpawnChunkWaiting = false;
                    world.setLoadBudgetMs(4.0f);
                    worldRenderer.setMeshBudgetMs(4.0f);
                    worldRenderer.setUploadBudgetMs(3.0f);
                    g_CurrentGameState = GameState::PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
            }

            if (g_CurrentGameState == GameState::PLAYING) {
                // Autosave
                if (glfwGetTime() - g_LastAutosave >= kAutosaveSeconds) {
                    saveCurrentWorld();
                    g_LastAutosave = glfwGetTime();
                }

                // Chunks keep loading even while paused / in menus (other players may need them)
                world.update(player.position, lua);
                if (!world.fatalError().empty()) { leaveBrokenWorld(); continue; }

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    // Hotbar slot keys (rebindable in Settings > Controls)
                    for (int slot = 0; slot < 9; ++slot) {
                        if (isActionDown(window, "hotbar" + std::to_string(slot + 1)) && slot < g_Hotbar->slotCount) {
                            g_Hotbar->activeSlot = slot;
                        }
                    }
                    const glm::vec3 stepFrom = player.position;
                    const bool wasGrounded = player.isGrounded;
                    const float fallSpeed = -player.velocity.y;
                    // Creative flying: double-tap Jump (only while the game has the keyboard)
                    updateFlight(window, g_Creative && g_FlyAllowed, player.isGrounded);
                    player.update(deltaTime, world, window, lua);
                    // Footsteps: every g_FootstepDistance blocks walked on the ground (so faster
                    // walking gives faster steps), quieter when sneaking;
                    // a landing makes one too
                    {
                        static float stepDistance = 0.0f;
                        const glm::ivec3 under(static_cast<int>(std::floor(player.position.x)),
                                               static_cast<int>(std::floor(player.position.y - 0.2f)),
                                               static_cast<int>(std::floor(player.position.z)));
                        const uint8_t ground = world.getBlockAt(under);
                        if (player.isGrounded && ground != 0) {
                            const glm::vec2 moved(player.position.x - stepFrom.x, player.position.z - stepFrom.z);
                            stepDistance += glm::length(moved);
                            const bool sneaking = (g_LastMoveKeys & MoveInput::Sneak) != 0;
                            if (!wasGrounded && fallSpeed > 4.0f) {
                                playBlockSound(ground, "footstep", player.position, 0.8f);
                                stepDistance = 0.0f;
                            } else if (stepDistance > g_FootstepDistance && glm::length(moved) < 2.0f) {
                                playBlockSound(ground, "footstep", player.position, sneaking ? 0.3f : 0.6f);
                                stepDistance = 0.0f;
                            }
                            // Particles, Minecraft's way: a puff of the ground's bits on landing (more
                            // the harder the fall), and a trail of them while sprinting
                            const int groundLayer = blockParticleLayer(ground);
                            if (!wasGrounded && fallSpeed > 6.0f) {
                                particles.spawnFootParticles(player.position, ground, static_cast<float>(groundLayer),
                                                             std::min(60, static_cast<int>((fallSpeed - 5.0f) * 5.0f)),
                                                             std::min(3.0f, fallSpeed * 0.12f));
                            }
                            static float sprintOwed = 0.0f;
                            const float speedNow = deltaTime > 0.0f ? glm::length(moved) / deltaTime : 0.0f;
                            if ((g_LastMoveKeys & MoveInput::Sprint) != 0 && speedNow > 4.0f && !sneaking) {
                                for (sprintOwed += deltaTime * 20.0f; sprintOwed >= 1.0f; sprintOwed -= 1.0f) {
                                    particles.spawnFootParticles(player.position, ground, static_cast<float>(groundLayer), 1, 0.6f);
                                }
                            }
                        }
                    }
                    // Water: a splash when you drop in, bubbles while your head is under
                    {
                        static bool feetWasWet = false;
                        static float bubbleOwed = 0.0f;
                        auto wetAt = [&](const glm::vec3& p) {
                            const uint8_t id = world.getBlockAt(glm::ivec3(glm::floor(p)));
                            return id != 0 && isWaterBlockName(BlockRegistry::getInstance().getBlock(id).name);
                        };
                        const bool feetWet = wetAt(player.position + glm::vec3(0.0f, 0.1f, 0.0f));
                        if (feetWet && !feetWasWet && fallSpeed > 3.0f) {
                            // the surface: up from the feet while it's water
                            glm::vec3 top = player.position;
                            for (int i = 0; i < 4 && wetAt(top + glm::vec3(0.0f, 1.0f, 0.0f)); ++i) top.y += 1.0f;
                            top.y = std::floor(top.y) + 0.95f;
                            const int splash = particleSprite("splash");
                            if (splash >= 0) {
                                ParticleSpec sp;
                                sp.frames = { splash };
                                sp.pos = top;
                                sp.vel = glm::vec3(0.0f, 3.2f, 0.0f);          // a spray up, falling back
                                sp.acc = glm::vec3(0.0f, -14.0f, 0.0f);
                                sp.life = 0.6f; sp.size = 0.12f;
                                sp.collide = false;
                                particles.burst(sp, std::min(60, static_cast<int>(fallSpeed * 4.0f)), glm::vec3(0.5f, 0.0f, 0.5f),
                                                glm::vec3(1.6f, 1.4f, 1.6f));
                            }
                            const int bubble = particleSprite("bubble");
                            if (bubble >= 0) {
                                ParticleSpec b;
                                b.frames = { bubble };
                                b.pos = player.position + glm::vec3(0.0f, 0.5f, 0.0f);
                                b.acc = glm::vec3(0.0f, 3.0f, 0.0f);
                                b.drag = 2.0f; b.life = 0.8f; b.size = 0.08f; b.collide = false;
                                particles.burst(b, 16, glm::vec3(0.4f, 0.4f, 0.4f), glm::vec3(0.8f, 0.5f, 0.8f));
                            }
                        }
                        feetWasWet = feetWet;
                        if (wetAt(player.eyePosition())) {
                            const int bubble = particleSprite("bubble");
                            for (bubbleOwed += deltaTime * 2.5f; bubbleOwed >= 1.0f && bubble >= 0; bubbleOwed -= 1.0f) {
                                ParticleSpec b;
                                b.frames = { bubble };
                                const float yawR = glm::radians(player.yaw);
                                b.pos = player.position + glm::vec3(std::cos(yawR) * 0.3f, 1.5f, std::sin(yawR) * 0.3f);
                                b.vel = glm::vec3(particles.randomRange(-0.2f, 0.2f), 0.6f, particles.randomRange(-0.2f, 0.2f));
                                b.acc = glm::vec3(0.0f, 1.5f, 0.0f);
                                b.drag = 1.5f; b.life = particles.randomRange(0.8f, 1.6f); b.size = 0.07f; b.collide = false;
                                particles.spawn(b);
                            }
                        } else {
                            bubbleOwed = 0.0f;
                        }
                    }
                    // Online: tell the server exactly what we just did, so it can do the same
                    // (client-side prediction - see Net.h)
                    if (g_Client.state() == GameClient::State::Joined) {
                        g_Client.recordInput(readMoveInput(window, player.yaw, player.pitch), deltaTime);
                    }

                    {
                        // update_player_animation(dt, is_moving, pitch) - errors are printed, not hidden
                        static std::string lastAnimError;
                        sol::protected_function updateAnimFunc = lua["update_player_animation"];
                        if (updateAnimFunc.valid()) {
                            bool isMoving = isActionDown(window, "forward") || isActionDown(window, "back") ||
                                            isActionDown(window, "left")    || isActionDown(window, "right");
                            sol::protected_function_result r = updateAnimFunc(deltaTime, isMoving, player.pitch);
                            if (!r.valid()) {
                                sol::error err = r;
                                if (lastAnimError != err.what()) {
                                    lastAnimError = err.what();
                                    std::cerr << "[Lua] update_player_animation: " << lastAnimError << std::endl;
                                }
                            }
                        }
                    }
                }

                // Water flows while you play, chat or browse the inventory. Pausing stops it in
                // singleplayer, but not while hosting (the other players are still playing).
                // Each fluid keeps its own step timer (water 0.25 s, lava 1.5 s...), see Fluids.h
                if (!g_Paused || g_Server.running()) {
                    world.updateFluids(deltaTime);
                }
            }

            glm::vec3 rayOrigin = player.eyePosition();
            glm::vec3 rayDir;
            rayDir.x = cos(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir.y = sin(glm::radians(player.pitch));
            rayDir.z = sin(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir = glm::normalize(rayDir);

            RaycastResult rayResult = raycastBlock(rayOrigin, rayDir, 6.0f, world);

            bool leftPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool rightPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if (g_CurrentGameState == GameState::PLAYING && isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                uint8_t activeBlockId = g_Hotbar->getActiveBlockId();
                std::string activeItemName = (activeBlockId != 0) ? BlockRegistry::getInstance().getBlock(activeBlockId).name : "air";

                // Mods get clicks first (e.g. the survival death screen):
                // on_player_click("left"|"right") -> true = used, nothing else happens
                bool clickUsed = false;
                const bool leftClick = leftPress && !leftMousePressedLast;
                const bool rightClick = rightPress && !rightMousePressedLast;
                // A scythe reaped crops with this press (on_use_scythe returned true): the rest of
                // the press digs nothing (not the replanted crop either). Cleared at the start of
                // every new left press, wherever it's aimed (air too), before anything else.
                static bool scytheReaped = false;
                if (leftClick) scytheReaped = false;
                // Holding right-click keeps placing / using, like Minecraft (every 4 ticks = 0.2 s,
                // engine.set_place_repeat). rightUse = this frame places or uses something.
                static float rightRepeatTimer = 0.0f;
                bool rightRepeat = false;
                if (rightClick) {
                    rightRepeatTimer = g_PlaceRepeatSeconds;
                } else if (rightPress && g_PlaceRepeatSeconds > 0.0f) {
                    rightRepeatTimer -= deltaTime;
                    if (rightRepeatTimer <= 0.0f) {
                        rightRepeat = true;
                        rightRepeatTimer += g_PlaceRepeatSeconds;
                        if (rightRepeatTimer < 0.0f) rightRepeatTimer = g_PlaceRepeatSeconds;
                    }
                }
                const bool rightUse = rightClick || rightRepeat;
                if (leftClick && !clickUsed) g_FirstPerson.swing();   // every left click swings, even at nothing (like Minecraft)
                const bool sneaking = isActionDown(window, "sneak");
                if (leftClick || rightClick) {
                    sol::protected_function onClick = lua["on_player_click"];
                    if (onClick.valid()) {
                        auto r = onClick(leftClick ? "left" : "right");
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_player_click: " << err.what() << std::endl; }
                        else if (r.get_type() == sol::type::boolean && r.get<bool>()) clickUsed = true;
                    }
                }
                if (g_MoveKeyMask == 0) clickUsed = true;   // frozen (e.g. dead): no digging or placing

                // Drop (Q, Settings > Controls): one of what's in your hand, thrown the way you look;
                // Ctrl+Q the whole stack. Held down, it keeps dropping (like Minecraft).
                // on_player_drop(item, count) -> true stops it (a mod's own rules)
                {
                    static bool dropWasDown = false;
                    static float dropRepeat = 0.0f;
                    const bool dropDown = isActionDown(window, "drop");
                    bool doDrop = false;
                    if (dropDown && !dropWasDown) { doDrop = true; dropRepeat = 0.4f; }
                    else if (dropDown) {
                        dropRepeat -= deltaTime;
                        if (dropRepeat <= 0.0f) { doDrop = true; dropRepeat = 0.1f; }
                    }
                    dropWasDown = dropDown;
                    const HotbarSlot inHand = g_Hotbar->getSlot(g_Hotbar->activeSlot);
                    if (doDrop && g_MoveKeyMask != 0 && inHand.blockId != 0 && inHand.count > 0) {
                        const bool wholeStack = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                                glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
                        const int n = wholeStack ? inHand.count : 1;
                        const std::string itemName = BlockRegistry::getInstance().getBlock(inHand.blockId).name;
                        bool stopped = false;
                        sol::protected_function hook = lua["on_player_drop"];
                        if (hook.valid()) {
                            auto r = hook(itemName, n);
                            if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_player_drop: " << err.what() << std::endl; }
                            else stopped = r.get_type() == sol::type::boolean && r.get<bool>();
                        }
                        if (!stopped) {
                            const glm::vec3 look = rayDir;
                            if (dropItemInWorld(itemName, n, player.position + glm::vec3(0.0f, 1.32f, 0.0f),
                                                look * 6.0f + glm::vec3(0.0f, 2.0f, 0.0f), 2.0f)) {
                                g_Hotbar->setSlot(g_Hotbar->activeSlot, inHand.count - n > 0 ? inHand.blockId : 0,
                                                  std::max(0, inHand.count - n));
                                g_FirstPerson.swing();
                            }
                        }
                    }
                }

                // Pick block (middle mouse by default, Settings > Controls): the block you're looking
                // at goes into your hand. on_pick_block(x, y, z, block, meta) -> true = a mod did it
                // (the survival mod takes it from your inventory); otherwise, in creative, the engine
                // selects it in the hotbar or puts it there, like Minecraft.
                {
                    static bool pickWasDown = false;
                    const bool pickDown = isActionDown(window, "pick_block");
                    if (pickDown && !pickWasDown && rayResult.hit && g_MoveKeyMask != 0) {
                        pickBlock(lua, world, rayResult.blockPos);
                    }
                    pickWasDown = pickDown;
                }

                // An entity in front of the block you're looking at: a click hits it (left) or
                // uses it (right) instead, and nothing behind it is dug or placed on
                {
                    float entityDist = 0.0f;
                    const uint32_t aimed = raycastEntities(rayOrigin, rayDir, 4.5f, entityDist);
                    float blockDist = 1e9f;
                    if (rayResult.hit) {
                        const glm::vec3 mn(rayResult.blockPos), mx = mn + glm::vec3(1.0f);
                        float t0 = 0.0f, t1 = 1e9f;
                        for (int a = 0; a < 3; ++a) {
                            if (std::fabs(rayDir[a]) < 1e-8f) continue;
                            float ta = (mn[a] - rayOrigin[a]) / rayDir[a], tb = (mx[a] - rayOrigin[a]) / rayDir[a];
                            if (ta > tb) std::swap(ta, tb);
                            t0 = std::max(t0, ta);
                            t1 = std::min(t1, tb);
                        }
                        blockDist = t0;
                    }
                    if (aimed != 0 && entityDist < blockDist && !clickUsed) {
                        if (leftClick) {
                            if (entitiesHere()) g_EntityLua.punch(aimed, playerName(), player.position, activeItemName);
                            else g_Client.sendEntityAct(aimed, 0, activeItemName);
                        } else if (rightClick) {
                            g_FirstPerson.swing();
                            if (entitiesHere()) g_EntityLua.rightClick(aimed, playerName(), activeItemName);
                            else g_Client.sendEntityAct(aimed, 1, activeItemName);
                        }
                        clickUsed = true;            // (holding the button doesn't dig behind it)
                        rayResult.hit = false;       // no block outline behind it either
                    }
                }

                auto& reg = BlockRegistry::getInstance();
                auto spawnBreakParticles = [&](const glm::ivec3& pos, uint8_t id) {
                    particles.spawnBreakParticles(glm::vec3(pos), id, static_cast<float>(blockParticleLayer(id)));
                };
                const uint8_t heldId = g_Hotbar->getActiveBlockId();
                const BlockProperty* held = heldId != 0 ? &reg.getBlock(heldId) : nullptr;
                auto rightTool = [&](const BlockProperty& block) {
                    const std::string want = blockTool(block);
                    return held && !want.empty() && held->toolType == want;
                };
                // Seconds to dig `block` with what's in hand (-1 = can't). get_dig_time(block, item)
                // in Lua may decide instead.
                auto digSeconds = [&](const BlockProperty& block) -> float {
                    sol::protected_function hook = lua["get_dig_time"];
                    if (hook.valid()) {
                        auto r = hook(block.name, activeItemName);
                        if (r.valid() && r.get_type() == sol::type::number) return r.get<float>();
                    }
                    float t = blockHardness(block);
                    if (t <= 0.0f) return t;
                    if (rightTool(block)) t /= std::max(held->toolSpeed, 0.01f);
                    else if (blockNeedsTool(block)) t *= 3.33f;   // the wrong tool: much slower
                    return t;
                };
                // A block dug in survival: on_dig_block(x, y, z, block, item) may handle the drops
                // (return true); otherwise it drops itself (or `drops`) into the hotbar.
                // on_block_broken(x, y, z, block, meta): a player broke a block (creative or
                    // survival), after it's gone - e.g. the doors mod removes the other half
                auto blockBroken = [&](const glm::ivec3& pos, const std::string& name, uint8_t meta) {
                    if (g_ClientMods) g_ClientMods->blockDug(pos.x, pos.y, pos.z, name);
                    sol::protected_function hook = lua["on_block_broken"];
                    if (!hook.valid()) return;
                    auto r = hook(pos.x, pos.y, pos.z, name, static_cast<int>(meta), playerName());
                    if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_block_broken: " << err.what() << std::endl; }
                };
                auto survivalDig = [&](const glm::ivec3& pos, uint8_t id) {
                    const BlockProperty block = reg.getBlock(id);
                    const uint8_t brokenMeta = world.getMetadataAt(pos);
                    bool handled = false;
                    sol::protected_function hook = lua["on_dig_block"];
                    if (hook.valid()) {
                        auto r = hook(pos.x, pos.y, pos.z, block.name, activeItemName);
                        if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_dig_block: " << err.what() << std::endl; }
                        else if (r.get_type() == sol::type::boolean && r.get<bool>()) handled = true;
                    }
                    spawnBreakParticles(pos, id);
                    playBlockSound(id, "dug", glm::vec3(pos) + glm::vec3(0.5f));
                    clearBrokenBlock(world, pos);
                    blockBroken(pos, block.name, brokenMeta);
                    if (handled) return;
                    if (blockNeedsTool(block) && !rightTool(block)) return;   // stone by hand: nothing
                    const std::string drop = block.dropsSet ? block.drops : block.name;
                    // a double slab is two slabs
                    const int dropCount = (shape::isSlab(block) && (brokenMeta & 3) == shape::kSlabDouble) ? 2 : 1;
                    // It pops out as an item to pick up (walk over it), like Minecraft
                    if (!drop.empty()) {
                        const glm::vec3 at = glm::vec3(pos) + glm::vec3(0.5f, 0.25f, 0.5f);
                        if (entitiesHere()) {
                            if (!g_EntityLua.drop(drop, dropCount, at)) giveHotbarItem(drop, dropCount);
                        } else {
                            g_Client.sendDropItem(drop, dropCount, at);
                        }
                    }
                };

                g_Dig.cooldown = std::max(0.0f, g_Dig.cooldown - deltaTime);
                bool digging = false;
                if (leftPress && !clickUsed && rayResult.hit) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    const uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = reg.getBlock(targetBlockId);
                        if (targetProp.isSelectable) {
                            // A scythe: on_use_scythe(x, y, z, block) -> true = it reaped something
                            // (the farming mod: ripe crops, replanted). Otherwise - and on anything
                            // else - the scythe digs like any tool. After a reap, holding the button
                            // doesn't then dig the replanted crop (scytheReaped, above).
                            const bool isScythe = activeItemName.find("scythe") != std::string::npos ||
                                                  (held && held->toolType == "scythe");
                            if (isScythe && leftClick) {
                                sol::protected_function scytheFunc = lua["on_use_scythe"];
                                if (scytheFunc.valid()) {
                                    auto r = scytheFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                                    if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_use_scythe: " << err.what() << std::endl; }
                                    else scytheReaped = r.get_type() == sol::type::boolean && r.get<bool>();
                                }
                            }
                            if (scytheReaped) {
                                // (reaped: nothing more for this press)
                            } else if (g_Creative) {
                                if (leftClick) {   // creative: one click, gone
                                    const uint8_t brokenMeta = world.getMetadataAt(hitPos);
                                    spawnBreakParticles(hitPos, targetBlockId);
                                    playBlockSound(targetBlockId, "dug", glm::vec3(hitPos) + glm::vec3(0.5f));
                                    clearBrokenBlock(world, hitPos);
                                    blockBroken(hitPos, targetProp.name, brokenMeta);
                                }
                            } else {
                                // survival: hold until the block breaks
                                digging = true;
                                if (!g_Dig.active || g_Dig.pos != hitPos) {
                                    g_Dig = DigState{true, hitPos, 0.0f, digSeconds(targetProp), g_Dig.cooldown, 0.0f};
                                }
                                if (!g_FirstPerson.swinging()) g_FirstPerson.swing();   // keeps swinging while digging
                                if (g_Dig.seconds >= 0.0f && g_Dig.cooldown <= 0.0f) {
                                    g_Dig.progress = g_Dig.seconds <= 0.0f ? 1.0f
                                        : g_Dig.progress + deltaTime / g_Dig.seconds;
                                    g_Dig.particleTimer -= deltaTime;
                                    if (g_Dig.particleTimer <= 0.0f && g_Dig.progress < 1.0f) {
                                        g_Dig.particleTimer = 0.25f;
                                        playBlockSound(targetBlockId, "dig", glm::vec3(hitPos) + glm::vec3(0.5f), 0.5f);
                                    }
                                    // Minecraft: chips off the face being hit, every tick of digging
                                    static float chipOwed = 0.0f;
                                    chipOwed += deltaTime * 20.0f;
                                    for (; chipOwed >= 1.0f; chipOwed -= 1.0f) {
                                        particles.spawnDigParticles(glm::vec3(hitPos), glm::vec3(rayResult.faceNormal), targetBlockId,
                                                                    static_cast<float>(blockParticleLayer(targetBlockId)));
                                    }
                                    if (g_Dig.progress >= 1.0f) {
                                        survivalDig(hitPos, targetBlockId);
                                        g_Dig.active = false;
                                        g_Dig.progress = 0.0f;
                                        g_Dig.cooldown = 0.25f;   // Minecraft's short pause between blocks
                                    }
                                }
                            }
                        }
                    }
                }
                if (!digging) { g_Dig.active = false; g_Dig.progress = 0.0f; }

                // Items with a use (engine.register_item_use) get the right-click first, even when
                // pointing at water or nothing at all
                bool itemUsed = false;
                if (rightUse && !clickUsed) {
                    const auto handler = g_ItemUseHandlers.find(activeItemName);
                    if (handler != g_ItemUseHandlers.end() && handler->second.valid()) {
                        sol::table pointed = lua.create_table();
                        const HotbarSlot held = g_Hotbar->getSlot(g_Hotbar->activeSlot);
                        pointed["item"] = activeItemName;
                        pointed["slot"] = g_Hotbar->activeSlot + 1;
                        pointed["count"] = held.count;
                        pointed["sneaking"] = sneaking;
                        pointed["repeat"] = rightRepeat;   // from holding the button, not a new click
                        pointed["repeat_click"] = rightRepeat;   // the same, usable as pointed.repeat_click ("repeat" is a Lua keyword)
                        pointed["player"] = lua.create_table_with("x", player.position.x, "y", player.position.y, "z", player.position.z);
                        if (rayResult.hit) {
                            const glm::ivec3 u(rayResult.blockPos);
                            const glm::ivec3 a = u + glm::ivec3(glm::round(glm::vec3(rayResult.faceNormal)));
                            const uint8_t uid = world.getBlockAt(u);
                            pointed["under"] = lua.create_table_with("x", u.x, "y", u.y, "z", u.z,
                                "name", uid ? BlockRegistry::getInstance().getBlock(uid).name : std::string("air"));
                            pointed["above"] = lua.create_table_with("x", a.x, "y", a.y, "z", a.z);
                        }
                        const FluidRayHit fluid = raycastFluid(rayOrigin, rayDir, 6.0f, world);
                        const bool fluidFirst = fluid.hit && (!rayResult.hit ||
                            glm::distance(glm::vec3(fluid.pos) + 0.5f, rayOrigin) <
                            glm::distance(glm::vec3(rayResult.blockPos) + 0.5f, rayOrigin));
                        if (fluidFirst) {
                            const uint8_t fid = world.getBlockAt(fluid.pos);
                            const FluidCell fc = FluidRegistry::get().cell(fid, world.getMetadataAt(fluid.pos));
                            if (fc) {
                                pointed["fluid"] = lua.create_table_with("x", fluid.pos.x, "y", fluid.pos.y, "z", fluid.pos.z,
                                    "name", fc.fluid->name,                                   // "water", "lava"...
                                    "block", BlockRegistry::getInstance().getBlock(fid).name,
                                    "source", fc.source, "falling", fc.falling, "level", fc.level);
                            }
                        }
                        auto res = handler->second(pointed);
                        if (!res.valid()) {
                            sol::error err = res;
                            std::cerr << "[Mod] Item use for '" << activeItemName << "' failed: " << err.what() << std::endl;
                            itemUsed = true;
                        } else {
                            itemUsed = res.get_type() == sol::type::boolean && res.get<bool>();
                        }
                        if (itemUsed) g_FirstPerson.swing();
                    }
                }

                if (rightUse && !clickUsed && rayResult.hit && !itemUsed) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = BlockRegistry::getInstance().getBlock(targetBlockId);
                        // A hoe (tool_type "hoe", or "hoe" in its name): on_use_hoe(x, y, z, block) ->
                        // true = it did something (tilled the ground). Otherwise the right-click
                        // carries on as usual (doors, workbenches... still open with a hoe in hand).
                        bool hoeUsed = false;
                        const BlockProperty& heldProp = BlockRegistry::getInstance().getBlock(g_Hotbar->getActiveBlockId());
                        if (activeItemName.find("hoe") != std::string::npos || heldProp.toolType == "hoe") {
                            sol::protected_function hoeFunc = lua["on_use_hoe"];
                            if (hoeFunc.valid()) {
                                auto r = hoeFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                                if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_use_hoe: " << err.what() << std::endl; }
                                else hoeUsed = r.get_type() == sol::type::boolean && r.get<bool>();
                            }
                            if (hoeUsed) g_FirstPerson.swing();
                        }
                        if (!hoeUsed) {
                            if (g_ClientMods && !rightRepeat) g_ClientMods->rightClickBlock(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                            sol::protected_function clickFunc = lua["on_right_click_block"];
                            bool handled = false;
                            if (clickFunc.valid()) {
                                // (x, y, z, block, held_item, sneaking, repeat): sneaking = place against it
                                // instead of using it; repeat = from holding the button
                                sol::protected_function_result res = clickFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name,
                                                                               activeItemName, sneaking, rightRepeat);
                                // (a hook that returns nothing means "not handled", never a crash)
                                if (res.valid()) handled = res.get_type() == sol::type::boolean && res.get<bool>();
                            }
                            if (handled) g_FirstPerson.swing();   // opened a door, a workbench...

                            if (!handled) {
                                uint8_t selectedBlock = g_Hotbar->getActiveBlockId();
                                // Items (anything with an inventory picture: sticks, tools, buckets)
                                // are never put down as blocks
                                if (selectedBlock != 0 && !BlockRegistry::getInstance().getBlock(selectedBlock).inventoryImage.empty()) {
                                    selectedBlock = 0;
                                }
                                if (selectedBlock != 0) {
                                    bool replaceTarget = false;
                                    if (targetProp.name == "short_grass" || targetProp.name == "flower_red" || targetProp.pattern == "plantlike" || targetProp.name.find("farmland") != std::string::npos) {
                                        replaceTarget = true;
                                    }
                                    // Work in whole-block (integer) coordinates for both cases so the
                                    // ternary isn't mixing glm::ivec3 and glm::vec3.
                                    const glm::ivec3 hitCell(rayResult.blockPos);
                                    const int farmlandLift = (targetProp.name.find("farmland") != std::string::npos) ? 1 : 0;
                                    glm::ivec3 cell = replaceTarget
                                        ? hitCell + glm::ivec3(0, farmlandLift, 0)
                                        : hitCell + glm::ivec3(glm::round(glm::vec3(rayResult.faceNormal)));

                                    // How it's turned / which half (BlockShape.h), from where the
                                    // player looks and the spot they clicked
                                    const BlockProperty& placeProp = BlockRegistry::getInstance().getBlock(selectedBlock);
                                    const glm::ivec3 clickN = rayResult.faceNormal;
                                    // the way the player looks, flat: 0 north, 1 east, 2 south, 3 west
                                    const int lookDir = std::fabs(rayDir.x) > std::fabs(rayDir.z)
                                        ? (rayDir.x > 0.0f ? 1 : 3) : (rayDir.z > 0.0f ? 2 : 0);
                                    // the upper half of a block's side was clicked
                                    const bool upperHalf = clickN.y < 0 ||
                                        (clickN.y == 0 && rayResult.hitPoint.y - std::floor(rayResult.hitPoint.y) > 0.5f);
                                    uint8_t placeMeta = 0;
                                    bool mergeSlab = false;          // a second slab into the same cell: a double slab
                                    if (shape::isSlab(placeProp)) {
                                        auto halfAt = [&](const glm::ivec3& c, uint8_t& half) {
                                            if (world.getBlockAt(c) != selectedBlock) return false;
                                            half = world.getMetadataAt(c) & 3;
                                            return half != shape::kSlabDouble;
                                        };
                                        uint8_t half = 0;
                                        if (!replaceTarget && halfAt(hitCell, half) &&
                                            ((clickN.y > 0 && half == shape::kSlabBottom) || (clickN.y < 0 && half == shape::kSlabTop))) {
                                            cell = hitCell;                // clicked the open side of a slab
                                            mergeSlab = true;
                                        } else if (halfAt(cell, half)) {
                                            mergeSlab = true;              // into a cell that holds a slab already
                                        }
                                        placeMeta = mergeSlab ? shape::kSlabDouble
                                                  : (clickN.y > 0 ? shape::kSlabBottom : upperHalf ? shape::kSlabTop : shape::kSlabBottom);
                                    } else if (shape::isStairs(placeProp)) {
                                        placeMeta = static_cast<uint8_t>(lookDir | (upperHalf && clickN.y <= 0 ? shape::kStairUpsideDown : 0));
                                    } else if (placeProp.rotation == shape::kRotAxis) {
                                        placeMeta = clickN.y != 0 ? 0 : clickN.x != 0 ? 1 : 2;      // along the face it went on
                                    }

                                    // Don't place a solid block where the player is standing (or jumping).
                                    // Player box: 0.6 wide, 1.8 tall, feet at player.position.
                                    constexpr float kHalfWidth = 0.3f;
                                    constexpr float kHeight    = 1.8f;
                                    constexpr float kEps       = 0.001f;   // touching is fine, overlapping isn't
                                    const glm::vec3 pMin(player.position.x - kHalfWidth, player.position.y, player.position.z - kHalfWidth);
                                    const glm::vec3 pMax(player.position.x + kHalfWidth, player.position.y + kHeight, player.position.z + kHalfWidth);
                                    const bool overlapsPlayer =
                                        pMin.x < cell.x + 1 - kEps && pMax.x > cell.x + kEps &&
                                        pMin.y < cell.y + 1 - kEps && pMax.y > cell.y + kEps &&
                                        pMin.z < cell.z + 1 - kEps && pMax.z > cell.z + kEps;
                                    const bool solid = BlockRegistry::getInstance().getBlock(selectedBlock).isWalkable;
                                    // Only into a free space: air, a fluid or a plant (never over a
                                    // door you reached past, or any other block)
                                    bool cellFree = replaceTarget || mergeSlab;
                                    if (!cellFree) {
                                        const uint8_t existing = world.getBlockAt(cell);
                                        const auto& ep = BlockRegistry::getInstance().getBlock(existing);
                                        cellFree = existing == 0 || FluidRegistry::get().forBlock(existing) != nullptr
                                                || ep.pattern == "plantlike";
                                    }

                                    if (cellFree && !(solid && overlapsPlayer && !mergeSlab)) {
                                        // Into a water source: slabs and stairs hold it (waterlogged)
                                        if (!mergeSlab && shape::canWaterlog(placeProp, placeMeta)) {
                                            const FluidCell wc = FluidRegistry::get().cell(world.getBlockAt(cell), world.getMetadataAt(cell));
                                            if (wc && wc.source && wc.fluid->name == "water") placeMeta |= shape::kWaterlogged;
                                        }
                                        if (placeMeta != 0 || mergeSlab) world.setBlockWithMetaAt(cell, selectedBlock, placeMeta);
                                        else world.setBlockAt(cell, selectedBlock);
                                        g_FirstPerson.swing();
                                        // on_block_placed(x, y, z, block): a player put a block down
                                        {
                                            sol::protected_function placedHook = lua["on_block_placed"];
                                            if (placedHook.valid()) {
                                                auto r = placedHook(cell.x, cell.y, cell.z,
                                                                    BlockRegistry::getInstance().getBlock(selectedBlock).name,
                                                                    playerName());
                                                if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_block_placed: " << err.what() << std::endl; }
                                            }
                                        }
                                        playBlockSound(selectedBlock, "place", glm::vec3(cell) + glm::vec3(0.5f));
                                        // Survival: placing uses one from the stack
                                        if (!g_Creative) {
                                            const int slot = g_Hotbar->activeSlot;
                                            const HotbarSlot held = g_Hotbar->getSlot(slot);
                                            const int left = held.count - 1;
                                            g_Hotbar->setSlot(slot, left > 0 ? held.blockId : 0, left > 0 ? left : 0);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            leftMousePressedLast = leftPress;
            rightMousePressedLast = rightPress;

            float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
            glm::mat4 proj = glm::perspective(glm::radians(g_FieldOfView), aspectRatio, 0.1f, 500.0f);
            glm::mat4 view = player.getViewMatrix();

            // Third-person camera (F5): behind the player (or in front, looking back at them), pulled
            // in so it never goes through blocks
            if (g_ThirdPerson && g_CurrentGameState == GameState::PLAYING) {
                const glm::vec3 eye = player.eyePosition();
                const glm::vec3 front = glm::normalize(glm::vec3(
                    cos(glm::radians(player.yaw)) * cos(glm::radians(player.pitch)),
                    sin(glm::radians(player.pitch)),
                    sin(glm::radians(player.yaw)) * cos(glm::radians(player.pitch))));

                // eye -> camera. In front the pitch is mirrored: looking down lifts the camera
                // above the player (instead of into the ground under them)
                const glm::vec3 away = g_ThirdPersonFront ? glm::vec3(front.x, -front.y, front.z) : -front;
                float dist = 4.0f;
                for (float d = 0.25f; d <= 4.0f; d += 0.25f) {
                    const glm::vec3 p = eye + away * d;
                    const uint8_t id = world.getBlockAt(glm::ivec3(static_cast<int>(std::floor(p.x)),
                                                                   static_cast<int>(std::floor(p.y)),
                                                                   static_cast<int>(std::floor(p.z))));
                    if (id != 0 && BlockRegistry::getInstance().getBlock(id).isWalkable) {
                        dist = std::max(0.3f, d - 0.3f);
                        break;
                    }
                }
                view = glm::lookAt(eye + away * dist, eye, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            // The dimension's sky (Dimensions.h): a fixed time of day, and how the sky looks -
            // "overworld" (sun, moon, stars, clouds), "nether" (only fog), "end" (a dark sky)
            const DimensionType& dimType = world.dimensionType();
            {
                static bool fixedApplied = false;
                if (dimType.fixedTime >= 0.0) {
                    g_LightingSystem->customTimeOfDay = static_cast<float>(dimType.fixedTime);
                    fixedApplied = true;
                } else if (fixedApplied) {
                    g_LightingSystem->customTimeOfDay = -1.0f;   // back to the world's clock
                    fixedApplied = false;
                }
            }
            const bool normalSky = dimType.effects == "overworld";
            glm::vec3 sunDir = g_LightingSystem->getSunDirection();
            int moonPhase = g_LightingSystem->getMoonPhase();

            const glm::vec3 eyePos = player.eyePosition();
            const float waterDepth = (g_CurrentGameState == GameState::PLAYING)
                ? underwaterDepth(world, eyePos)
                : -1.0f;
            const bool submerged = waterDepth >= 0.0f;

            {
                const float target   = submerged ? 1.0f : 0.0f;
                const float seconds = submerged ? g_Underwater.fadeInSeconds : g_Underwater.fadeOutSeconds;
                const float step    = deltaTime / glm::max(seconds, 0.01f);
                if (g_UnderwaterBlend < target) g_UnderwaterBlend = glm::min(g_UnderwaterBlend + step, target);
                else                            g_UnderwaterBlend = glm::max(g_UnderwaterBlend - step, target);
            }
            const float blend      = g_UnderwaterBlend;
            // (what a dimension draws overhead: its sky, sun, moon - each can be off)
            // (under water too: the sky is what's seen through the water's surface overhead)
            const bool  skyHidden = !(dimType.showSky || dimType.showSun || dimType.showMoon);

            const WaterColors water = (blend > 0.0f)
                ? queryWaterColors(lua, eyePos)
                : (g_Underwater.colorsFromLua ? WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor } : waterBlockColors());
            const float depthT = submerged
                ? glm::clamp(waterDepth / g_Underwater.fullDepth, 0.0f, 1.0f)
                : 0.0f;
            const glm::vec3 underwaterFog = glm::mix(water.shallow, water.deep, depthT);
            const float underwaterVis = glm::mix(g_Underwater.shallowVisibility, g_Underwater.deepVisibility, depthT);

            // The water's own colour on everything, near or far (brightened so it tints rather than
            // darkens; a little white kept so colours stay readable)
            const glm::vec3 fogHue = underwaterFog / glm::max(glm::max(underwaterFog.r, underwaterFog.g),
                                                              glm::max(underwaterFog.b, 0.001f));
            const glm::vec3 underwaterTint = glm::mix(glm::vec3(0.85f, 0.95f, 1.0f), fogHue, 0.75f);
            const float tintAmount = blend * g_Underwater.tintStrength;

            const glm::vec3 skyClear = normalSky ? glm::vec3(0.5f, 0.8f, 0.95f)
                                     : (dimType.effects == "end" ? dimType.skyColor : dimType.fogColor);
            const glm::vec3 clearCol = glm::mix(skyClear, underwaterFog, blend);
            glClearColor(clearCol.r, clearCol.g, clearCol.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (g_CurrentGameState == GameState::PLAYING) {
                const glm::vec3 aboveFog   = normalSky ? fogColorForSun(g_Fog, sunDir.y) : dimType.fogColor;
                const float loadedEdge     = g_WorldInstance->renderDistanceBlocks();
                const float aboveEnd       = glm::min(effectiveFogEnd(g_Fog, g_LightingSystem->weatherRainIntensity), loadedEdge - 8.0f);
                const float aboveStart     = effectiveFogStart(g_Fog, aboveEnd);

                const glm::vec3 fogCol     = glm::mix(aboveFog, underwaterFog, blend);
                const float     fogStart   = glm::mix(aboveStart, underwaterVis * g_Underwater.fogStartFraction, blend);
                const float     fogEnd     = glm::mix(aboveEnd, underwaterVis, blend);
                const float     fogDensity = g_Fog.density;
                const bool fogExp = g_Fog.exponential && blend <= 0.0f;
                const bool fogOn  = g_Fog.enabled || blend > 0.0f;

                if (!skyHidden) {
                    g_Skybox->sunDirection = sunDir;
                    g_Skybox->brightness = g_LightQuality > 0 ? 0.86f : 1.0f;
                    g_Skybox->sunHalo = g_LightQuality > 0 ? 1.0f : 0.0f;
                    if (dimType.showSky) g_Skybox->render(view, proj, sunDir.y);

                    glDepthMask(GL_FALSE);
                    glUseProgram(sunProgram);

                    glm::vec3 sunWorldPos = player.position + (sunDir * 80.0f);
                    glm::mat4 sunModel = glm::translate(glm::mat4(1.0f), sunWorldPos);
                    sunModel[0][0] = view[0][0]; sunModel[0][1] = view[1][0]; sunModel[0][2] = view[2][0];
                    sunModel[1][0] = view[0][1]; sunModel[1][1] = view[1][1]; sunModel[1][2] = view[2][1];
                    sunModel[2][0] = view[0][2]; sunModel[2][1] = view[1][2]; sunModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(sunModel));

                    if (g_LightingSystem->sunTextureID != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, g_LightingSystem->sunTextureID);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }

                    glBindVertexArray(sunVAO);
                    if (dimType.showSun) glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glm::vec3 moonDir = -sunDir;
                    glm::vec3 moonWorldPos = player.position + (moonDir * 80.0f);
                    glm::mat4 moonModel = glm::translate(glm::mat4(1.0f), moonWorldPos);
                    moonModel[0][0] = view[0][0]; moonModel[0][1] = view[1][0]; moonModel[0][2] = view[2][0];
                    moonModel[1][0] = view[0][1]; moonModel[1][1] = view[1][1]; moonModel[1][2] = view[2][1];
                    moonModel[2][0] = view[0][2]; moonModel[2][1] = view[1][2]; moonModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(moonModel));

                    GLuint activeMoonTex = g_LightingSystem->moonTextureIDs[moonPhase] != 0
                        ? g_LightingSystem->moonTextureIDs[moonPhase]
                        : g_LightingSystem->moonTextureIDs[0];
                    if (activeMoonTex != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, activeMoonTex);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }
                    if (dimType.showMoon) glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glBindVertexArray(0);
                    glDepthMask(GL_TRUE);

                    // Weather: greys/darkens the sky, sun and moon (sky.set_sky_tint)
                    if (dimType.showWeather) g_Weather.drawSkyTint();
                }

                glUseProgram(shaderProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_SunDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_Time"), static_cast<float>(glfwGetTime()));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));

                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogEnabled"), fogOn ? 1 : 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogExp"), fogExp ? 1 : 0);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogStart"), fogStart);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogEnd"), fogEnd);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogDensity"), fogDensity);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_FogColor"), fogCol.r, fogCol.g, fogCol.b);
                // Fog takes the sky's colour behind it (where there's a normal sky, not under
                // water; rain greys it)
                {
                    const float rain = glm::clamp(g_LightingSystem->weatherRainIntensity, 0.0f, 1.0f);
                    // (in bad weather too: the weather's tint below greys both alike)
                    (void)rain;
                    const float skyFog = (normalSky && dimType.showSky && blend <= 0.0f) ? 1.0f : 0.0f;
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_SkyFog"), skyFog);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_SkyBrightness"), g_LightQuality > 0 ? 0.86f : 1.0f);
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_SkyHalo"), g_LightQuality > 0 ? 1.0f : 0.0f);
                    {
                        const glm::vec4 tint = (normalSky && dimType.showWeather && blend <= 0.0f) ? g_Weather.drawnSkyTint() : glm::vec4(0.0f);
                        glUniform4f(glGetUniformLocation(shaderProgram, "u_WeatherTint"), tint.r, tint.g, tint.b, glm::clamp(tint.a, 0.0f, 1.0f));
                    }
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_CamWaterDepth"), submerged ? waterDepth : 0.0f);
                }
                glUniform3f(glGetUniformLocation(shaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_UnderwaterAmount"), tintAmount);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_WeatherLight"), g_Weather.worldLight());

                // Moving lights: the item in the player's hand, then lights added by mods (sky.add_light)
                g_HeldLight.update(world);
                std::vector<PointLight> pointLights;
                for (const PointLight& pl : g_LightingSystem->getPointLights()) {
                    if (pl.active && pointLights.size() < 16) pointLights.push_back(pl);
                }
                glUniform1i(glGetUniformLocation(shaderProgram, "u_NumPointLights"), static_cast<int>(pointLights.size()));
                for (size_t i = 0; i < pointLights.size() && i < 16; ++i) {
                    std::string base = "u_PointLights[" + std::to_string(i) + "].";
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "position").c_str()), pointLights[i].position.x, pointLights[i].position.y, pointLights[i].position.z);
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "color").c_str()), pointLights[i].color.x, pointLights[i].color.y, pointLights[i].color.z);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "radius").c_str()), pointLights[i].radius);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "intensity").c_str()), pointLights[i].intensity);
                }

                glEnable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);

                // Another dimension (the server sent us, or we went ourselves): its chunks, not the
                // old ones' meshes; on_dimension_change(to, from) for this game's mods
                {
                    static std::string lastDim;
                    glm::vec3 to;
                    if (g_Client.takeDimensionChange(to)) {
                        player.position = to;
                        player.velocity = glm::vec3(0.0f);
                        g_Dig = DigState{};
                        dims::currentRef() = g_Client.dimension();
                        g_DimensionSwitched = true;
                    }
                    if (g_DimensionSwitched) {
                        g_DimensionSwitched = false;
                        worldRenderer.clear();
                        const std::string now = world.dimension();
                        if (now != lastDim) {
                            const std::string from = lastDim;
                            lastDim = now;
                            sol::protected_function hook = lua["on_dimension_change"];
                            if (hook.valid()) {
                                auto r = hook(now.empty() ? std::string(dims::kOverworld) : now,
                                              from.empty() ? std::string(dims::kOverworld) : from);
                                if (!r.valid()) { sol::error err = r; std::cerr << "[Lua] on_dimension_change: " << err.what() << std::endl; }
                            }
                        }
                    }
                    if (lastDim != world.dimension() && !g_DimensionSwitched) lastDim = world.dimension();
                }
                worldRenderer.update(world, player.position);   // pick up changed chunks, mesh, upload
                // The blocks around the camera for tracing light (VoxelVolume.h), and spotlights
                {
                    const glm::vec3 camWorld = glm::vec3(glm::inverse(view)[3]);
                    // (lighting quality Off: the volume isn't kept up to date and nothing is traced)
                    const auto meshed = worldRenderer.takeMeshedChunks();
                    if (g_LightQuality > 0) g_Voxels.update(world, camWorld, meshed);
                    else if (!meshed.empty()) g_Voxels.clear();   // (filled again when it's turned back on)
                    g_Voxels.bind(shaderProgram, 3, g_LightQuality > 0);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_TraceSteps"), g_LightQuality >= 2 ? 96 : 48);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_SunSteps"),
                                g_LightQuality >= 2 ? 80 : g_LightQuality == 1 ? 40 : 0);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_Filmic"), g_FilmicOn ? 1 : 0);
                    // (the reflection samplers live on their own units, even when unused: two
                    // sampler types may not share a unit)
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_SceneColor"), 5);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_SceneDepth"), 6);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_SSR"), 0);
                    // The moon's soft shadows: at night, with the moon up, by how full it is (phase 0
                    // full .. 4 new), weaker in rain; none where the dimension has no moon
                    {
                        const glm::vec3 sd = g_LightingSystem->getSunDirection();
                        const glm::vec3 md = g_LightingSystem->getMoonDirection();
                        const int phase = g_LightingSystem->getMoonPhase();
                        const float full = 1.0f - static_cast<float>(std::min(phase, 8 - phase)) / 4.0f;
                        const float night = 1.0f - glm::smoothstep(-0.05f, 0.05f, sd.y);
                        const float moonUp = glm::smoothstep(0.02f, 0.25f, md.y);
                        const float rain = glm::clamp(g_LightingSystem->weatherRainIntensity, 0.0f, 1.0f);
                        const float moonShadow = world.dimensionType().showMoon ? night * moonUp * full * (1.0f - 0.7f * rain) : 0.0f;
                        glUniform3f(glGetUniformLocation(shaderProgram, "u_MoonDir"), md.x, md.y, md.z);
                        glUniform1f(glGetUniformLocation(shaderProgram, "u_MoonShadow"), moonShadow);
                    }
                    g_Spotlights.tracing = g_LightQuality > 0;
                    g_Spotlights.beamSamples = g_LightQuality >= 2 ? 40 : 24;
                    g_Spotlights.beamTraceSteps = g_LightQuality >= 2 ? 48 : 0;
                    g_Spotlights.upload(shaderProgram, camWorld, static_cast<float>(glfwGetTime()));
                    glUniform3f(glGetUniformLocation(shaderProgram, "u_CameraPos"), camWorld.x, camWorld.y, camWorld.z);
                }
                // Block light: chunks use the light in their vertices; everything else drawn with
                // this shader afterwards (players, particles) uses the light where the camera is
                {
                    const uint16_t here = world.getLightAt(glm::ivec3(glm::floor(eyePos)));
                    glUniform4f(glGetUniformLocation(shaderProgram, "u_FallbackLight"), light::sky(here) / 15.0f,
                                light::red(here) / 15.0f, light::green(here) / 15.0f, light::blue(here) / 15.0f);
                    // (a dimension's ambient light: the darkest it gets there)
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_MinLight"),
                                std::max(g_LightSettings.minLight, world.dimensionType().ambientLight));
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_Brightness"), g_LightSettings.effectiveBrightness());
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_Gamma"), g_LightSettings.gammaLift());
                    glUniform1f(glGetUniformLocation(shaderProgram, "u_BlockLightScale"), g_LightSettings.blockLightScale);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 1);
                }
                // Solid blocks and plants now; water, glass and the like at the very end (after
                // mobs, items, players, particles), so whatever is in or behind the water is seen
                // through it rather than drawn over it
                worldRenderer.renderOpaque(shaderProgram, proj * view, eyePos);

                // Text on signs (SignText.h), on the boards just drawn
                if (g_FontRenderer) {
                    static SignTextRenderer signText;
                    signText.draw(world, *g_FontRenderer, proj * view, eyePos,
                                  glm::clamp(sunDir.y * 2.0f + 0.5f, 0.15f, 1.0f));
                    glUseProgram(shaderProgram);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                }

                // Falling blocks (sand, gravel): drawn where they are this frame, as the same
                // textured cube they are in the world, lit by the light where they are
                {
                    const auto falling = world.fallingBlocks();
                    if (!falling.empty()) {
                        static GLuint fallVao = 0, fallVbo = 0;
                        static std::vector<Vertex> verts;
                        const GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
                        glUniform1f(glGetUniformLocation(shaderProgram, "u_ChunkFade"), 1.0f);
                        glDisable(GL_BLEND);
                        for (const auto& fb : falling) {
                            const glm::ivec3 cell(static_cast<int>(std::floor(fb.pos.x)),
                                                  static_cast<int>(std::floor(fb.pos.y + 0.5f)),
                                                  static_cast<int>(std::floor(fb.pos.z)));
                            const uint16_t l = world.getLightAt(cell);
                            const float lv[4] = { light::sky(l) / 15.0f, light::red(l) / 15.0f,
                                                  light::green(l) / 15.0f, light::blue(l) / 15.0f };
                            ChunkMesh::buildSingleBlock(fb.id, lv, verts);
                            if (verts.empty()) continue;
                            ChunkMesh::uploadVertices(fallVao, fallVbo, verts);
                            const glm::mat4 model = glm::translate(glm::mat4(1.0f), fb.pos);
                            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
                            glBindVertexArray(fallVao);
                            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
                        }
                        glBindVertexArray(0);
                        glEnable(GL_BLEND);
                    }
                }
                // Entities this frame (ours or the server's), and dropped items: a small spinning,
                // bobbing copy of the block, or a flat picture for items and plants. Mobs whose
                // model isn't known here (a server-only mod) are a plain box the size of their hitbox.
                const std::vector<EntityView> entityViews = currentEntityViews();
                {
                    static GLuint itemVao = 0, itemVbo = 0;
                    static std::vector<Vertex> verts;
                    const GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
                    auto& breg = BlockRegistry::getInstance();
                    const float now = static_cast<float>(glfwGetTime());
                    const GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
                    bool any = false;
                    for (const EntityView& v : entityViews) {
                        const bool item = v.type == EntityManager::kItemType;
                        if (!item && g_EntityVisuals.count(v.type) && g_EntityVisuals[v.type].model) continue;
                        uint8_t id = item ? breg.getBlockId(v.item) : breg.getBlockId("stone");
                        if (id == 0 && !item) id = 1;
                        if (id == 0) continue;
                        const glm::ivec3 cell(glm::floor(v.pos + glm::vec3(0.0f, 0.3f, 0.0f)));
                        const uint16_t l = world.getLightAt(cell);
                        const float lv[4] = { light::sky(l) / 15.0f, light::red(l) / 15.0f, light::green(l) / 15.0f, light::blue(l) / 15.0f };
                        const BlockProperty& bp = breg.getBlock(id);
                        const bool flat = item && (bp.inventoryLayer >= 0 || bp.pattern == "plantlike");
                        glm::mat4 model(1.0f);
                        if (flat) {
                            int layer = bp.inventoryLayer;
                            bool tint = false;
                            if (layer < 0) for (int f : { 0, 2, 4, 1, 5, 3 }) if (layer < 0 && bp.faces[f].baseLayer >= 0) { layer = bp.faces[f].baseLayer; tint = bp.faces[f].tintBase; }
                            const glm::vec3 c = tint ? bp.defaultTint : glm::vec3(1.0f);
                            auto mk = [&](float x, float y, float u, float w, float shade) {
                                Vertex vx{};
                                vx.posX = x; vx.posY = y; vx.posZ = 0.0f;
                                vx.tintR = c.r; vx.tintG = c.g; vx.tintB = c.b;
                                vx.texCoordX = u; vx.texCoordY = w;
                                vx.baseLayer = static_cast<float>(layer); vx.overlayLayer = -1.0f;
                                vx.tintBase = tint ? 1.0f : 0.0f; vx.tintOverlay = 0.0f;
                                vx.faceShading = shade;
                                vx.lightSky = lv[0]; vx.lightR = lv[1]; vx.lightG = lv[2]; vx.lightB = lv[3];
                                return vx;
                            };
                            Vertex a = mk(-0.5f, 0.0f, 0, 1, 1.0f), b = mk(0.5f, 0.0f, 1, 1, 1.0f),
                                         cc = mk(0.5f, 1.0f, 1, 0, 1.0f), d = mk(-0.5f, 1.0f, 0, 0, 1.0f);
                            Vertex a2 = a, b2 = b, c2 = cc, d2 = d;
                            // the two sides a hair apart: on the same plane they fought over which
                            // one shows, and spinning items flickered
                            for (Vertex* vv : { &a, &b, &cc, &d }) vv->posZ = 0.01f;
                            for (Vertex* vv : { &a2, &b2, &c2, &d2 }) { vv->faceShading = 0.8f; vv->posZ = -0.01f; }
                            verts = { a, b, cc, a, cc, d };
                            verts.insert(verts.end(), { b2, a2, d2, b2, d2, c2 });
                            const float bob = std::sin(now * 2.5f + static_cast<float>(v.id)) * 0.05f;
                            model = glm::translate(glm::mat4(1.0f), v.pos + glm::vec3(0.0f, 0.08f + bob, 0.0f));
                            model = glm::rotate(model, glm::radians(v.yaw + now * 60.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                            model = glm::scale(model, glm::vec3(0.45f));
                        } else {
                            ChunkMesh::buildSingleBlock(id, lv, verts);
                            if (item) {
                                const float bob = std::sin(now * 2.5f + static_cast<float>(v.id)) * 0.05f;
                                model = glm::translate(glm::mat4(1.0f), v.pos + glm::vec3(0.0f, 0.1f + bob, 0.0f));
                                model = glm::rotate(model, glm::radians(v.yaw + now * 60.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                                model = glm::scale(model, glm::vec3(0.25f));
                            } else {
                                model = glm::translate(glm::mat4(1.0f), v.pos);
                                model = glm::rotate(model, glm::radians(-v.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
                                model = glm::scale(model, glm::vec3(v.width, v.height, v.width));
                            }
                            model = glm::translate(model, glm::vec3(-0.5f, 0.0f, -0.5f));
                        }
                        if (verts.empty()) continue;
                        if (!any) {
                            glUniform1f(glGetUniformLocation(shaderProgram, "u_ChunkFade"), 1.0f);
                            glDisable(GL_BLEND);
                            glDisable(GL_CULL_FACE);
                            any = true;
                        }
                        ChunkMesh::uploadVertices(itemVao, itemVbo, verts);
                        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
                        glBindVertexArray(itemVao);
                        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
                        // two or more: a second one, a little apart (like Minecraft's stacks)
                        if (v.itemCount > 1 && item) {
                            const glm::mat4 second = glm::translate(glm::mat4(1.0f), glm::vec3(0.12f, 0.1f, 0.08f)) * model;
                            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(second));
                            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
                        }
                    }
                    if (any) {
                        glBindVertexArray(0);
                        glEnable(GL_BLEND);
                        if (cullWas) glEnable(GL_CULL_FACE);
                    }
                }
                glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 0);

                // Other players (multiplayer)
                const net::PlayerMap& otherPlayers = playersHere();
                if (!otherPlayers.empty()) playerAvatars.render(otherPlayers, shaderProgram, view, proj);

                // Block-break particles (same shader as the world, so they get fog and lighting)
                if (!g_Paused) {
                    runAmbientEffects(world, player.position, deltaTime);   // torches smoking, lava popping...
                    particles.update(deltaTime, &world, player.position);
                }
                particles.render(view, proj, shaderProgram, g_TextureArrayID, &world);

                g_Clouds->update(deltaTime);
                if (blend <= 0.5f && dimType.showClouds) {   // (a dimension may have none)
                    g_Clouds->render(view, proj, player.position,
                                       static_cast<float>(glfwGetTime()),
                                       sunDir.y,
                                       fogCol);
                }

                glUseProgram(modelShaderProgram);
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_LightDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterAmount"), tintAmount);
                glUniform1f(glGetUniformLocation(modelShaderProgram, "u_WeatherLight"), g_Weather.worldLight());

                glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), player.position);
                // Models face -Z; turn that to where the player looks (yaw 0 = +X), so F5 shows its back
                modelMatrix = glm::rotate(modelMatrix, glm::radians(-player.yaw - 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

                // Player model: only in third person (in first person the camera is inside its head)
                {
                    const glm::vec3 lit = modelLightAt(player.position + glm::vec3(0.0f, 1.0f, 0.0f));
                    glUniform3f(glGetUniformLocation(modelShaderProgram, "u_LocalLight"), lit.r, lit.g, lit.b);
                }
                if (g_ThirdPerson) {
                    // Why there's no body to see is said once (in the console), not left a mystery
                    static std::string s_PlayerModelWarned;
                    std::string problem;
                    try {
                        sol::object modelObj = lua["g_PlayerModel"];
                        if (modelObj.is<std::shared_ptr<AnimatedModel>>()) {
                            auto model = modelObj.as<std::shared_ptr<AnimatedModel>>();
                            if (!model) problem = "g_PlayerModel is empty";
                            else if (!model->loaded) problem = "g_PlayerModel did not load (see the [Model] lines above)";
                            else model->draw(modelShaderProgram, modelMatrix);
                        } else {
                            problem = (modelObj.get_type() == sol::type::lua_nil)
                                ? "g_PlayerModel is nil - no mod loaded a player model (the player mod's models/player_model.json missing?)"
                                : "g_PlayerModel is not a model from engine.load_model";
                        }
                    } catch (const std::exception& e) {
                        problem = std::string("drawing it failed: ") + e.what();
                    }
                    if (!problem.empty() && problem != s_PlayerModelWarned) {
                        std::cout << "[Player] Third person: no player model to draw: " << problem << std::endl;
                        s_PlayerModelWarned = problem;
                    }
                }

                // Models placed from Lua with engine.add_model (mobs etc.)
                for (const auto& [id, inst] : g_ModelInstances) {
                    if (!inst.visible || !inst.model) continue;
                    const glm::vec3 lit = modelLightAt(inst.position + glm::vec3(0.0f, 0.5f, 0.0f));
                    glUniform3f(glGetUniformLocation(modelShaderProgram, "u_LocalLight"), lit.r, lit.g, lit.b);
                    glm::mat4 m = glm::translate(glm::mat4(1.0f), inst.position);
                    m = glm::rotate(m, glm::radians(inst.yaw), glm::vec3(0.0f, 1.0f, 0.0f));
                    m = glm::scale(m, glm::vec3(inst.scale));
                    inst.model->draw(modelShaderProgram, m);
                }

                // Mobs with models: each posed with its own animation (the time since it started),
                // lit by the light where it stands, and flashing red for a moment when hit.
                // Models face -z (north) at rest, the way Blockbench makes Bedrock entities; yaw 0
                // = facing +x (east), 90 = +z (south).
                {
                    const double now = glfwGetTime();
                    const GLint localLight = glGetUniformLocation(modelShaderProgram, "u_LocalLight");
                    const GLint tintLoc = glGetUniformLocation(modelShaderProgram, "u_Tint");
                    std::unordered_set<uint32_t> present;
                    for (const EntityView& v : entityViews) {
                        present.insert(v.id);
                        EntityLook& look = g_EntityLooks[v.id];
                        if (look.seen && v.hurtCount != look.hurtCount) {
                            look.hurtUntil = now + 0.35;
                            // Minecraft's hit sparks: a burst of crit stars around its middle
                            const int crit = particleSprite("crit");
                            if (crit >= 0) {
                                ParticleSpec c;
                                c.frames = { crit };
                                c.pos = v.pos + glm::vec3(0.0f, v.height * 0.6f, 0.0f);
                                c.acc = glm::vec3(0.0f, -6.0f, 0.0f);
                                c.drag = 3.0f; c.life = 0.75f; c.size = 0.2f; c.sizeEnd = 0.06f;
                                c.color = glm::vec3(0.9f, 0.9f, 0.95f);
                                c.glow = true;
                                // each one starts just outside its body and flies outwards, so they burst
                                // all round it (inside the body they'd be hidden by the model)
                                const glm::vec3 centre = c.pos;
                                for (int k = 0; k < 18; ++k) {
                                    const float ang = particles.random01() * 6.2831853f;
                                    const float up = particles.randomRange(-0.6f, 0.9f);
                                    const glm::vec3 dir = glm::normalize(glm::vec3(std::cos(ang), up, std::sin(ang)));
                                    ParticleSpec q = c;
                                    q.pos = centre + glm::vec3(dir.x * (v.width * 0.55f + 0.1f), dir.y * v.height * 0.45f,
                                                               dir.z * (v.width * 0.55f + 0.1f));
                                    q.vel = dir * particles.randomRange(1.5f, 3.5f) + glm::vec3(0.0f, 0.6f, 0.0f);
                                    q.life *= particles.randomRange(0.7f, 1.2f);
                                    particles.spawn(q);
                                }
                            }
                        }
                        look.hurtCount = v.hurtCount;
                        look.pos = v.pos; look.width = v.width; look.height = v.height;
                        look.item = v.type == EntityManager::kItemType;
                        if (!look.seen || v.anim != look.anim) { look.anim = v.anim; look.animStart = now; }
                        look.seen = true;
                        auto vis = g_EntityVisuals.find(v.type);
                        if (vis == g_EntityVisuals.end() || !vis->second.model) continue;
                        AnimatedModel& model = *vis->second.model;
                        model.resetPose();
                        if (!v.anim.empty() && vis->second.anims) {
                            // the full name ("animation.cow.walk") or the short one ("walk"), like everywhere else
                            if (const auto a = vis->second.anims->get(v.anim)) {
                                float t = static_cast<float>((now - look.animStart) * v.animSpeed);
                                if (a->length > 0.0f) t = v.animLoop ? std::fmod(std::max(t, 0.0f), a->length) : std::min(t, a->length);
                                a->apply(model, t);
                            }
                        }
                        // light where it stands (sunlight in caves is dark; torches light it)
                        const glm::vec3 lit = modelLightAt(v.pos + glm::vec3(0.0f, std::min(v.height * 0.5f, 1.0f), 0.0f));
                        glUniform3f(localLight, lit.r, lit.g, lit.b);
                        if (now < look.hurtUntil) glUniform4f(tintLoc, 1.0f, 0.15f, 0.1f, 0.45f);
                        else glUniform4f(tintLoc, 0.0f, 0.0f, 0.0f, 0.0f);
                        glm::mat4 m = glm::translate(glm::mat4(1.0f), v.pos);
                        m = glm::rotate(m, glm::radians(-v.yaw - 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                        m = glm::scale(m, glm::vec3(vis->second.scale));
                        model.draw(modelShaderProgram, m);
                    }
                    glUniform3f(localLight, 1.0f, 1.0f, 1.0f);
                    glUniform4f(tintLoc, 0.0f, 0.0f, 0.0f, 0.0f);
                    for (auto it = g_EntityLooks.begin(); it != g_EntityLooks.end();) {
                        if (present.count(it->first)) { ++it; continue; }
                        // A mob gone from near you died (or a mod took it away): Minecraft's white "poof".
                        // (Its last hit is never seen - it's removed the moment it dies; mobs that
                        // despawn or leave your range do it far away, where nothing shows.)
                        const EntityLook& gone = it->second;
                        if (!gone.item && gone.seen && glm::distance(gone.pos, player.position) < 24.0f) {
                            std::vector<int> smoke;
                            for (int f = 0; f < 8; ++f) { const int l = particleSprite(("smoke_" + std::to_string(f)).c_str()); if (l >= 0) smoke.push_back(l); }
                            if (!smoke.empty()) {
                                ParticleSpec pf;
                                pf.frames = smoke;
                                pf.pos = gone.pos + glm::vec3(0.0f, gone.height * 0.5f, 0.0f);
                                pf.vel = glm::vec3(0.0f, 0.4f, 0.0f);
                                pf.acc = glm::vec3(0.0f, 0.3f, 0.0f);
                                pf.drag = 3.0f; pf.life = 0.9f; pf.size = 0.32f; pf.sizeEnd = 0.2f;
                                pf.color = glm::vec3(0.95f);
                                pf.collide = false;
                                particles.burst(pf, 20, glm::vec3(gone.width * 0.5f, gone.height * 0.45f, gone.width * 0.5f),
                                                glm::vec3(1.2f, 0.8f, 1.2f));
                            }
                        }
                        it = g_EntityLooks.erase(it);
                    }
                }

                // See-through blocks last (water, glass...): everything else is already drawn, so
                // the half of a cow under the water is seen through it, tinted like the lake bed
                {
                    // Water reflections (lighting High): a copy of the picture so far, for the
                    // water to look things up in
                    bool ssr = false;
                    if (g_LightQuality >= 2 && blend <= 0.0f) {
                        int fbw = 0, fbh = 0;
                        glfwGetFramebufferSize(window, &fbw, &fbh);
                        ssr = g_SceneCopy.capture(fbw, fbh);
                        if (ssr) g_SceneCopy.bind(5, 6);
                    }
                    glUseProgram(shaderProgram);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_SSR"), ssr ? 1 : 0);
                    {
                        const glm::mat4 vp = proj * view;
                        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "u_ViewProj"), 1, GL_FALSE, glm::value_ptr(vp));
                    }
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 0);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 1);
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                    worldRenderer.renderTransparent(shaderProgram);
                    // Spotlight beams in the air (Spotlights.h), over everything
                    g_Spotlights.drawBeams(proj * view, glm::vec3(glm::inverse(view)[3]), g_Voxels,
                                           static_cast<float>(glfwGetTime()), g_TextureArrayID);
                    // Bloom (Settings; only where the graphics card can take it): bright things glow
                    // Sun shafts (High): light in the air where the sun gets past leaves and edges
                    // (under water too: beams of sunlight in the water)
                    if (g_LightQuality >= 2 && g_SunShaftStrength > 0.0f && sunDir.y > 0.02f &&
                        dimType.showSun) {
                        const float up = glm::smoothstep(0.02f, 0.2f, sunDir.y);
                        const glm::vec3 day(1.0f, 0.98f, 0.92f), sunset(1.0f, 0.62f, 0.32f);
                        const glm::vec3 sunCol = glm::mix(sunset, day, glm::clamp(sunDir.y + 0.15f, 0.0f, 1.0f));
                        const float rain = g_LightingSystem ? g_LightingSystem->weatherRainIntensity : 0.0f;
                        g_SunShafts.apply(width, height, glm::inverse(proj * view), glm::vec3(glm::inverse(view)[3]), sunDir,
                                          sunCol, g_SunShaftStrength * up * (1.0f - 0.7f * glm::clamp(rain, 0.0f, 1.0f)), g_Voxels);
                    }
                    // (fading out through the fog: bright distant fog and sky don't glow)
                    if (g_BloomOn && g_GpuCaps.bloom) {
                        const float fadeEnd = fogOn ? (fogExp ? 2.0f / std::max(fogDensity, 1e-4f) : fogEnd) : loadedEdge;
                        const float fadeStart = fogOn ? (fogExp ? 0.0f : fogStart * 0.6f) : loadedEdge * 0.6f;
                        // (with the realistic look, sunlit sand and snow are already near white: only
                        // what's brighter than that - lamps, glints, the sun's edge - glows)
                        const float thr = g_LightQuality > 0 ? std::max(g_BloomThreshold, 0.88f) : g_BloomThreshold;
                        const float str = g_LightQuality > 0 ? g_BloomStrength * 0.7f : g_BloomStrength;
                        g_Bloom.apply(width, height, str, thr, fadeStart, fadeEnd, 0.1f, 500.0f);
                    }
                    glUseProgram(shaderProgram);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 0);
                    glUniform1i(glGetUniformLocation(shaderProgram, "u_SSR"), 0);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen) && rayResult.hit) {
                    glUseProgram(wireframeProgram);
                    // The outline fits the block's shape: a full cube, or a door's panel (DoorShape.h)
                    glm::vec3 selMin(0.0f), selMax(1.0f);
                    {
                        const uint8_t selId = world.getBlockAt(rayResult.blockPos);
                        const std::string& selPattern = selId ? BlockRegistry::getInstance().getBlock(selId).pattern : std::string();
                        if (selId != 0 && selPattern == "door") {
                            door::box(world.getMetadataAt(rayResult.blockPos), selMin, selMax);
                        } else if (selId != 0 && (selPattern == "sign" || selPattern == "wall_sign")) {
                            sign::bounds(selPattern == "wall_sign", world.getMetadataAt(rayResult.blockPos), selMin, selMax);
                        } else if (selId != 0 && shape::isPartial(BlockRegistry::getInstance().getBlock(selId))) {
                            // slabs and stairs: the box around their parts (BlockShape.h)
                            const glm::ivec3 sp = rayResult.blockPos;
                            auto get = [&](int dx, int dy, int dz, uint8_t& id, uint8_t& m) {
                                id = world.getBlockAt({sp.x + dx, sp.y + dy, sp.z + dz});
                                m = world.getMetadataAt({sp.x + dx, sp.y + dy, sp.z + dz});
                                return true;
                            };
                            shape::bounds(BlockRegistry::getInstance().getBlock(selId), world.getMetadataAt(sp), get, selMin, selMax);
                        }
                    }
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(rayResult.blockPos) + (selMin + selMax) * 0.5f);
                    model = glm::scale(model, (selMax - selMin) + glm::vec3(0.002f));   // a hair bigger: no z-fighting
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                    glDisable(GL_CULL_FACE);
                    glLineWidth(3.0f);

                    static GLuint boxVAO = 0, boxVBO = 0;
                    if (boxVAO == 0) {
                        float vertices[] = {
                            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
                             0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,

                            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
                             0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,

                            -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f
                        };
                        glGenVertexArrays(1, &boxVAO);
                        glGenBuffers(1, &boxVBO);
                        glBindVertexArray(boxVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, boxVBO);
                        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                        glEnableVertexAttribArray(0);
                        glBindVertexArray(0);
                    }

                    glUniform4f(glGetUniformLocation(wireframeProgram, "u_Color"), 0.0f, 0.0f, 0.0f, 0.9f);
                    glBindVertexArray(boxVAO);
                    glDrawArrays(GL_LINES, 0, 24);
                    glBindVertexArray(0);

                    // Survival digging: the crack picture (engine.set_dig_crack_texture) grows stage by
                    // stage over the block; without one, the block darkens in steps
                    if (g_Dig.active && g_Dig.progress > 0.0f && g_Dig.pos == glm::ivec3(rayResult.blockPos)) {
                        const float p = glm::clamp(g_Dig.progress, 0.0f, 0.999f);
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        glDepthMask(GL_FALSE);
                        if (g_CrackTexture != 0) {
                            static GLuint crackProg = 0, texVAO = 0, texVBO = 0;
                            if (crackProg == 0) {
                                const char* vs = "#version 330 core\n"
                                    "layout (location = 0) in vec3 aPos; layout (location = 1) in vec2 aUV;\n"
                                    "uniform mat4 u_MVP; uniform float u_Stage; uniform float u_Stages; out vec2 vUV;\n"
                                    "void main() { vUV = vec2(aUV.x, (u_Stage + aUV.y) / u_Stages); gl_Position = u_MVP * vec4(aPos, 1.0); }\n";
                                const char* fs = "#version 330 core\n"
                                    "in vec2 vUV; out vec4 FragColor; uniform sampler2D u_Tex;\n"
                                    "void main() { vec4 c = texture(u_Tex, vUV); if (c.a < 0.02) discard; FragColor = c; }\n";
                                crackProg = glCreateProgram();
                                glAttachShader(crackProg, compileShader(GL_VERTEX_SHADER, vs));
                                glAttachShader(crackProg, compileShader(GL_FRAGMENT_SHADER, fs));
                                glLinkProgram(crackProg);
                                // 6 faces x 2 triangles, each face with its own 0..1 picture
                                const float h = 0.503f;
                                const float faces[6][4][3] = {
                                    {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}}, {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
                                    {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}}, {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}},
                                    {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}}, {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}} };
                                const float uv[4][2] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };
                                const int order[6] = { 0, 1, 2, 0, 2, 3 };
                                float v[6 * 6 * 5];
                                int n = 0;
                                for (int f = 0; f < 6; ++f) for (int i : order) {
                                    v[n++] = faces[f][i][0]; v[n++] = faces[f][i][1]; v[n++] = faces[f][i][2];
                                    v[n++] = uv[i][0]; v[n++] = uv[i][1];
                                }
                                glGenVertexArrays(1, &texVAO);
                                glGenBuffers(1, &texVBO);
                                glBindVertexArray(texVAO);
                                glBindBuffer(GL_ARRAY_BUFFER, texVBO);
                                glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
                                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
                                glEnableVertexAttribArray(0);
                                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
                                glEnableVertexAttribArray(1);
                            }
                            const glm::mat4 mvp = proj * view * model;
                            glUseProgram(crackProg);
                            glUniformMatrix4fv(glGetUniformLocation(crackProg, "u_MVP"), 1, GL_FALSE, glm::value_ptr(mvp));
                            glUniform1f(glGetUniformLocation(crackProg, "u_Stage"), std::floor(p * g_CrackStages));
                            glUniform1f(glGetUniformLocation(crackProg, "u_Stages"), static_cast<float>(g_CrackStages));
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, g_CrackTexture);
                            glUniform1i(glGetUniformLocation(crackProg, "u_Tex"), 0);
                            glBindVertexArray(texVAO);
                            glDrawArrays(GL_TRIANGLES, 0, 36);
                            glBindVertexArray(0);
                        } else {
                            static GLuint crackVAO = 0, crackVBO = 0;
                            if (crackVAO == 0) {
                                // 36 vertices: the 6 faces of a cube, a hair bigger than the block
                                const float h = 0.502f;
                                const float c[8][3] = { {-h,-h,-h},{h,-h,-h},{h,h,-h},{-h,h,-h},{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h} };
                                const int f[36] = { 0,1,2, 0,2,3,  4,6,5, 4,7,6,  0,4,5, 0,5,1,  3,2,6, 3,6,7,  0,3,7, 0,7,4,  1,5,6, 1,6,2 };
                                float v[108];
                                for (int i = 0; i < 36; ++i) for (int k = 0; k < 3; ++k) v[i * 3 + k] = c[f[i]][k];
                                glGenVertexArrays(1, &crackVAO);
                                glGenBuffers(1, &crackVBO);
                                glBindVertexArray(crackVAO);
                                glBindBuffer(GL_ARRAY_BUFFER, crackVBO);
                                glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
                                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                                glEnableVertexAttribArray(0);
                            }
                            const float stage = std::floor(p * 10.0f) / 10.0f;   // like Minecraft's 10 stages
                            glUniform4f(glGetUniformLocation(wireframeProgram, "u_Color"), 0.05f, 0.04f, 0.03f, 0.08f + stage * 0.55f);
                            glBindVertexArray(crackVAO);
                            glDrawArrays(GL_TRIANGLES, 0, 36);
                            glBindVertexArray(0);
                        }
                        glDepthMask(GL_TRUE);
                    }

                    // Restore the state the rest of the frame expects (culling was off before)
                    glDisable(GL_CULL_FACE);
                    glEnable(GL_BLEND);
                }

                // Weather: rain/snow around the camera (not underwater), then lightning flashes
                // (a dimension with a ceiling, or without the overworld's sky, has no weather)
                const bool weatherHere = dimType.showWeather;
                g_Weather.update(g_Paused ? 0.0f : deltaTime, eyePos, world);
                if (blend < 0.5f && weatherHere) {
                    g_Weather.drawPrecipitation(view, proj, eyePos, glm::clamp(sunDir.y * 2.0f + 0.5f, 0.0f, 1.0f));
                }
                if (blend < 0.5f && weatherHere) g_Weather.drawBolts(view, proj, eyePos);
                if (weatherHere) g_Weather.drawFlash();

                // The world's thumbnail: this frame's picture, before the hand and HUD go on it
                if (!g_ThumbnailChecked && glfwGetTime() - g_WorldOpenedAt > 6.0 && !g_ActiveWorldName.empty()) {
                    g_ThumbnailChecked = true;
                    std::error_code tec;
                    if (!fs::exists(savesRoot() / g_ActiveWorldName / "thumbnail.png", tec)) g_ThumbnailWanted = true;
                }
                if (!g_Paused && glfwGetTime() - g_LastThumbnailAt > 300.0) g_ThumbnailWanted = true;
                if (g_ThumbnailWanted) {
                    g_ThumbnailWanted = false;
                    g_LastThumbnailAt = glfwGetTime();
                    if (g_Client.state() != GameClient::State::Joined) saveWorldThumbnail(width, height);
                }

                // ---------------- First-person hand (FirstPerson.h) ----------------
                {
                    // Walking speed for the bob; "on the ground" by vertical speed, as the grounded
                    // flag flickers frame to frame while standing. Updated in third person too, so
                    // the swing keeps time for the player model (engine.get_swing).
                    const float walkSpeed = g_Paused ? 0.0f : glm::length(glm::vec2(player.velocity.x, player.velocity.z));
                    // (flying counts as off the ground: no bob, like Minecraft - flying level would
                    // otherwise look like walking at flying speed)
                    const bool onGround = !g_Flying && std::fabs(player.velocity.y) < 0.6f;
                    g_FirstPerson.update(deltaTime, g_Hotbar ? g_Hotbar->getActiveBlockId() : 0,
                                         g_Hotbar ? g_Hotbar->activeSlot : 0, walkSpeed, onGround,
                                         player.yaw, player.pitch);
                    std::shared_ptr<AnimatedModel> fpModel;
                    sol::object modelObj = lua["g_PlayerModel"];
                    if (modelObj.is<std::shared_ptr<AnimatedModel>>()) fpModel = modelObj.as<std::shared_ptr<AnimatedModel>>();
                    const uint16_t here = world.getLightAt(glm::ivec3(glm::floor(eyePos)));
                    const glm::vec4 handLight(light::sky(here) / 15.0f, light::red(here) / 15.0f,
                                              light::green(here) / 15.0f, light::blue(here) / 15.0f);
                    if (!g_ThirdPerson && !g_HudHidden) {
                        g_FirstPerson.render(shaderProgram, modelShaderProgram, g_TextureArrayID, width, height,
                                             handLight, fpModel, settingInt("main_hand", 0) == 1);
                    } else if (fpModel) {
                        // F5: what's held, in the player model's hand (placed as the model is drawn)
                        glm::mat4 modelWorld = glm::translate(glm::mat4(1.0f), player.position);
                        modelWorld = glm::rotate(modelWorld, glm::radians(-player.yaw - 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                        g_FirstPerson.renderThirdPerson(shaderProgram, g_TextureArrayID, view, proj, handLight,
                                                        fpModel, modelWorld, settingInt("main_hand", 0) == 1);
                    }
                }

                // ---------------- HUD ----------------
                glDisable(GL_DEPTH_TEST);
                // F1 hides all of this (the crosshair, hotbar and HUDs), like Minecraft
                if (!g_HudHidden) {
                glUseProgram(uiProgram);
                // (no crosshair when the camera faces the player: it would point at nothing)
                if (isCursorDisabled && !g_ThirdPersonFront && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    glBindVertexArray(crosshairVAO);
                    glLineWidth(2.0f);
                    glDrawArrays(GL_LINES, 0, 4);
                    glBindVertexArray(0);
                }

                // 1. Hotbar (background + 3D icons) - drawn BEFORE the inventory so the
                //    creative inventory always sits on top of it.
                g_Hotbar->updateBuffers(width, height);
                g_Hotbar->renderSlots(uiProgram, texturedUIProgram);

                glEnable(GL_DEPTH_TEST);
                glUseProgram(shaderProgram);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

                g_Hotbar->render3DIcons(shaderProgram, g_TextureArrayID);

                // Stack counts (more than 1) at the bottom right of each slot, white with a shadow,
                // like Minecraft
                if (g_FontRenderer) {
                    const Dimensions2D screen{width, height};
                    const Dimensions2D base = g_FontRenderer->baseSize();
                    const float rawCap = std::max(g_FontRenderer->capHeight(1.0f, base), 0.001f);
                    glDisable(GL_DEPTH_TEST);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glUseProgram(fontProgram);
                    for (int i = 0; i < g_Hotbar->slotCount; ++i) {
                        const HotbarSlot slot = g_Hotbar->getSlot(i);
                        if (slot.blockId == 0 || slot.count <= 1) continue;
                        float sx, sy, size;
                        hotbarSlotRect(*g_Hotbar, i, static_cast<float>(width), static_cast<float>(height), sx, sy, size);
                        const std::string text = std::to_string(slot.count);
                        const float capPx = std::max(7.0f, size * 0.3f);
                        const float f = capPx / rawCap;
                        const float scX = f * base.width / std::max(1, width), scY = f * base.height / std::max(1, height);
                        const float tw = g_FontRenderer->textWidth(text, scX, screen);
                        const float tx = std::round(sx + size - tw - size * 0.04f);
                        const float ty = std::round(sy + size - size * 0.04f);           // baseline
                        const float sh = std::max(1.0f, std::round(capPx / 7.0f));      // shadow offset
                        g_FontRenderer->renderText(fontProgram, text, tx + sh, ty + sh, scX, scY, glm::vec4(0.15f, 0.15f, 0.15f, 1.0f), screen);
                        g_FontRenderer->renderText(fontProgram, text, tx, ty, scX, scY, glm::vec4(1.0f), screen);
                    }
                }

                glDisable(GL_DEPTH_TEST);

                // HUD drawn by mods (hearts, hunger, air... see mods/survival): render_hud(W, H)
                {
                    sol::object hud = lua["render_hud"];
                    if (hud.valid() && hud.get_type() == sol::type::function) {
                        g_Inventory.renderLuaScreen(window, lua, "render_hud", static_cast<float>(width),
                                                    static_cast<float>(height), uiProgram, texturedUIProgram);
                        glDisable(GL_DEPTH_TEST);
                    }
                }

                // The player's client mods' HUDs (minimap...), drawn on top of the game's
                if (g_ClientMods && g_ClientMods->active()) {
                    g_Inventory.renderLuaScreen(window, lua, "__client_hud", static_cast<float>(width),
                                                static_cast<float>(height), uiProgram, texturedUIProgram);
                    glDisable(GL_DEPTH_TEST);
                }

                // The held item's name above the hotbar for a moment after switching, like Minecraft
                if (g_ShowBlockNames && g_FontRenderer && !g_Paused && !g_Inventory.isCreativeOpen() && !g_LuaInventoryOpen) {
                    const Dimensions2D screen{width, height};
                    const glm::vec2 sc = uiTextScale(1.0f, screen);
                    auto& names = BlockRegistry::getInstance();
                    auto shadowed = [&](const std::string& t, float x, float y, glm::vec2 s, glm::vec4 c) {
                        g_FontRenderer->renderText(fontProgram, t, x + 2.0f, y + 2.0f, s.x, s.y, glm::vec4(0.1f, 0.1f, 0.1f, c.a * 0.85f), screen);
                        g_FontRenderer->renderText(fontProgram, t, x, y, s.x, s.y, c, screen);
                    };
                    glUseProgram(fontProgram);
                    if (g_Hotbar) {
                        const uint8_t held = g_Hotbar->getActiveBlockId();
                        if (held != g_HeldNameFor || g_Hotbar->activeSlot != g_HeldNameSlot) {
                            g_HeldNameFor = held;
                            g_HeldNameSlot = g_Hotbar->activeSlot;
                            g_HeldNameSince = glfwGetTime();
                        }
                        const double age = glfwGetTime() - g_HeldNameSince;
                        if (held != 0 && age < 2.0) {
                            const float alpha = static_cast<float>(std::min(1.0, (2.0 - age) / 0.5));
                            float hx = 0, hy = 0, hw = 0, hh = 0;
                            hotbarScreenRect(*g_Hotbar, static_cast<float>(width), static_cast<float>(height), hx, hy, hw, hh);
                            const std::string t = blockDisplayName(names.getBlock(held));
                            const float tw = g_FontRenderer->textWidth(t, sc.x, screen);
                            shadowed(t, std::round((width - tw) * 0.5f), std::round(hy - 14.0f), sc, glm::vec4(1.0f, 1.0f, 1.0f, alpha));
                        }
                    }
                }
                }   // !g_HudHidden

                // 2. Creative inventory (drawn by inventory.lua)
                g_Inventory.renderCreativeInventory(window, lua, static_cast<float>(width), static_cast<float>(height), uiProgram, texturedUIProgram);
                glDisable(GL_DEPTH_TEST);

                // 3. A mod's inventory screen (render_inventory), with ui.* drawing available
                {
                    sol::object inv = lua["render_inventory"];
                    if (inv.valid() && inv.get_type() == sol::type::function) {
                        g_Inventory.renderLuaScreen(window, lua, "render_inventory", static_cast<float>(width),
                                                    static_cast<float>(height), uiProgram, texturedUIProgram);
                        glDisable(GL_DEPTH_TEST);
                    }
                }

                // 4. Pause menu (pause_menu.lua), or Settings if opened from it (settings_menu.lua)
                if (g_Paused) {
                    g_Inventory.renderLuaScreen(window, lua, g_SettingsInGame ? "render_settings" : "render_pause_menu",
                                                static_cast<float>(width), static_cast<float>(height),
                                                uiProgram, texturedUIProgram);
                    glDisable(GL_DEPTH_TEST);
                }

                // Name tags above other players (multiplayer)
                {
                    const net::PlayerMap& others = playersHere();
                    if (!others.empty() && g_FontRenderer) {
                        resetUIRenderState();
                        glUseProgram(fontProgram);
                        const Dimensions2D screen{width, height};
                        const glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
                        for (const auto& tag : PlayerAvatars::nameTags(others, view, proj, camPos, width, height)) {
                            const float scale = 0.5f;
                            const float x = tag.x - tag.text.size() * 13.0f * scale * 0.5f;
                            g_FontRenderer->renderText(fontProgram, tag.text, x + 2.0f, tag.y + 2.0f, scale, scale,
                                                       glm::vec4(0.0f, 0.0f, 0.0f, 0.8f), screen);
                            g_FontRenderer->renderText(fontProgram, tag.text, x, tag.y, scale, scale,
                                                       glm::vec4(1.0f), screen);
                        }
                    }
                }

                // Name tags above entities that have one (self:set_nametag), within 24 blocks
                if (g_FontRenderer) {
                    const glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
                    const glm::mat4 vp = proj * view;
                    const Dimensions2D screen{width, height};
                    bool started = false;
                    for (const EntityView& v : entityViews) {
                        if (v.nametag.empty()) continue;
                        const glm::vec3 head = v.pos + glm::vec3(0.0f, v.height + 0.35f, 0.0f);
                        if (glm::distance(head, camPos) > 24.0f) continue;
                        const glm::vec4 clip = vp * glm::vec4(head, 1.0f);
                        if (clip.w <= 0.05f) continue;
                        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                        if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f) continue;
                        if (!started) { resetUIRenderState(); glUseProgram(fontProgram); started = true; }
                        const float scale = 0.5f;
                        const float tw = g_FontRenderer->textWidth(v.nametag, scale, screen);
                        const float x = (ndc.x * 0.5f + 0.5f) * width - tw * 0.5f;
                        const float y = (1.0f - (ndc.y * 0.5f + 0.5f)) * height;
                        g_FontRenderer->renderText(fontProgram, v.nametag, x + 2.0f, y + 2.0f, scale, scale, glm::vec4(0.0f, 0.0f, 0.0f, 0.8f), screen);
                        g_FontRenderer->renderText(fontProgram, v.nametag, x, y, scale, scale, glm::vec4(1.0f), screen);
                    }
                }

                // Player list while the Player List key (Tab) is held (drawn by player_list.lua)
                if (!g_Paused && (!g_ChatSystem || !g_ChatSystem->isOpen) && !g_Inventory.isCreativeOpen() &&
                    isActionDown(window, "player_list")) {
                    resetUIRenderState();
                    g_Inventory.renderLuaScreen(window, lua, "render_player_list",
                                                static_cast<float>(width), static_cast<float>(height),
                                                uiProgram, texturedUIProgram);
                }

                // F3 info: stays on screen even when this window isn't focused, so you can see
                // whether a background window is being slowed down
                if (g_ShowDebugInfo && g_FontRenderer) {
                    resetUIRenderState();
                    glUseProgram(fontProgram);
                    const Dimensions2D screen{width, height};
                    std::vector<std::string> lines;
                    lines.push_back("FPS: " + std::to_string(static_cast<int>(g_Fps)) +
                                    (glfwGetWindowAttrib(window, GLFW_FOCUSED) ? "" : "  (window not focused)"));
                    lines.push_back("GPU: " + g_GpuName);
                    if (g_Server.running()) lines.push_back("Hosting");
                    for (const auto& l : g_Server.running() ? g_Server.statsLines() : g_Client.statsLines()) {
                        if (g_Server.running() || g_Client.active()) lines.push_back(l);
                    }
                    char pos[96];
                    std::snprintf(pos, sizeof(pos), "XYZ: %.1f %.1f %.1f", player.position.x,
                                  player.position.y, player.position.z);
                    lines.push_back(pos);

                    // What the crosshair is on, straight from the world's block data
                    if (rayResult.hit) {
                        const glm::ivec3 b(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                        const uint8_t id = world.getBlockAt(b);
                        const uint8_t meta = world.getMetadataAt(b);
                        const BlockProperty& prop = BlockRegistry::getInstance().getBlock(id);
                        char buf[200];

                        lines.push_back("");
                        std::snprintf(buf, sizeof(buf), "Looking at: %s (id %d)", prop.name.c_str(), static_cast<int>(id));
                        lines.push_back(buf);

                        const glm::ivec3 n(rayResult.faceNormal);
                        const char* face = n.y > 0 ? "top" : n.y < 0 ? "bottom" : n.x > 0 ? "east (+X)"
                                         : n.x < 0 ? "west (-X)" : n.z > 0 ? "south (+Z)" : n.z < 0 ? "north (-Z)" : "?";
                        const glm::vec3 eye = player.eyePosition();
                        const float dist = glm::distance(eye, glm::vec3(b) + glm::vec3(0.5f));
                        std::snprintf(buf, sizeof(buf), "  Block %d %d %d, %s face, %.1f blocks away",
                                      b.x, b.y, b.z, face, dist);
                        lines.push_back(buf);

                        auto floorDiv16 = [](int v) { return v >= 0 ? v / 16 : -((-v + 15) / 16); };
                        const int cx = floorDiv16(b.x), cz = floorDiv16(b.z);
                        std::snprintf(buf, sizeof(buf), "  Chunk %d, %d (inside it: %d %d %d)  metadata %d",
                                      cx, cz, b.x - cx * 16, b.y, b.z - cz * 16, static_cast<int>(meta));
                        lines.push_back(buf);

                        std::string flags;
                        auto flag = [&flags](bool on, const char* name) {
                            if (on) flags += (flags.empty() ? "" : ", ") + std::string(name);
                        };
                        flag(prop.isWalkable, "solid");
                        flag(!prop.isWalkable, "walk-through");
                        flag(prop.isTransparent, "transparent");
                        flag(prop.isCutout, "cutout");
                        flag(prop.hasGravity, "falls");
                        flag(!prop.isSelectable, "not selectable");
                        flag(!prop.in_creative, "hidden from creative");
                        lines.push_back("  " + flags + "  (shape: " + (prop.pattern.empty() ? "cube" : prop.pattern) + ")");
                    } else {
                        lines.push_back("");
                        lines.push_back("Looking at: nothing in reach");
                    }
                    // The first block of any kind on the crosshair ray (even ones that can't be
                    // selected), to tell what an odd-looking block really is
                    {
                        glm::vec3 pnt = rayOrigin;
                        for (float t = 0.0f; t <= 6.0f; t += 0.05f, pnt = rayOrigin + rayDir * t) {
                            const glm::ivec3 c(static_cast<int>(std::floor(pnt.x)), static_cast<int>(std::floor(pnt.y)),
                                               static_cast<int>(std::floor(pnt.z)));
                            const uint8_t rid = world.getBlockAt(c);
                            if (rid == 0) continue;
                            const auto& rp = BlockRegistry::getInstance().getBlock(rid);
                            char rbuf[200];
                            std::snprintf(rbuf, sizeof(rbuf), "Ray hits: %s (id %d) at %d %d %d, metadata %d, water id %d",
                                          rp.name.empty() ? "(no block with this id!)" : rp.name.c_str(),
                                          static_cast<int>(rid), c.x, c.y, c.z, static_cast<int>(world.getMetadataAt(c)),
                                          static_cast<int>(BlockRegistry::getInstance().getBlockId("water")));
                            lines.push_back(rbuf);
                            break;
                        }
                    }
                    // Fluid in front of the block (fluids can't be selected, so they're listed apart)
                    {
                        const FluidRayHit fl = raycastFluid(rayOrigin, rayDir, 6.0f, world);
                        if (fl.hit) {
                            const uint8_t fid = world.getBlockAt(fl.pos);
                            const uint8_t fmeta = world.getMetadataAt(fl.pos);
                            const FluidCell fc = FluidRegistry::get().cell(fid, fmeta);
                            const std::string state = !fc ? "?" : fc.source ? "source" : fc.falling ? "falling"
                                : "flow level " + std::to_string(fc.level) + " of " + std::to_string(fc.fluid->spread);
                            char fbuf[200];
                            std::snprintf(fbuf, sizeof(fbuf), "Fluid: %s (block %s, id %d) at %d %d %d, %s, metadata %d",
                                          fc ? fc.fluid->name.c_str() : "?",
                                          BlockRegistry::getInstance().getBlock(fid).name.c_str(), static_cast<int>(fid),
                                          fl.pos.x, fl.pos.y, fl.pos.z, state.c_str(), static_cast<int>(fmeta));
                            lines.push_back(fbuf);
                            std::snprintf(fbuf, sizeof(fbuf), "  Fluids flowing: %zu cells", world.fluids().activeCount());
                            lines.push_back(fbuf);
                        }
                    }
                    const glm::vec2 sc = uiTextScale(0.8f, screen);
                    float y = 24.0f;
                    for (const auto& l : lines) {
                        g_FontRenderer->renderText(fontProgram, l, 11.0f, y + 1.0f, sc.x, sc.y, glm::vec4(0, 0, 0, 0.8f), screen);
                        g_FontRenderer->renderText(fontProgram, l, 10.0f, y, sc.x, sc.y, glm::vec4(1.0f), screen);
                        y += 20.0f;
                    }
                }

                // 5. Chat always on top
                resetUIRenderState();
                if (g_ChatSystem) g_ChatSystem->renderChatOverlay(g_FontRenderer, fontProgram, width, height);

                glEnable(GL_DEPTH_TEST);
            } else if (g_CurrentGameState == GameState::LOADING) {
                resetUIRenderState();
                drawLoadingScreen(uiProgram, fontProgram, g_LoadingProgress, width, height);
            } else if (g_CurrentGameState == GameState::WORLD_SELECT) {
                resetUIRenderState();
                if (g_MainMenu && g_MainMenu->renderBackdrop(width, height)) resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_world_select",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::MULTIPLAYER) {
                resetUIRenderState();
                if (g_MainMenu && g_MainMenu->renderBackdrop(width, height)) resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_multiplayer",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::MODS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_mods",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_texture_packs",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::SETTINGS) {
                resetUIRenderState();
                // From the title screen: over the game's panorama, turning behind it
                if (g_MainMenu && g_MainMenu->renderBackdrop(width, height)) resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_settings",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else {
                // Gameplay leaves face culling / depth state behind, which hides the
                // font when coming back to the title screen. Reset it every frame.
                resetUIRenderState();

                int sw, sh;
                glfwGetFramebufferSize(window, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(window, &mx, &my);
                scaleCursorToFramebuffer(window, mx, my, sw, sh);

                if (g_MainMenu) {
                    g_MainMenu->updateAndBuildBuffers(mx, my, sw, sh);
                    if (g_FontRenderer) g_MainMenu->render(uiProgram, texturedUIProgram, fontProgram, *g_FontRenderer, sw, sh);
                }
            }

            // The mouse cursor shape a Lua screen asked for this frame (ui.set_cursor)
            {
                static std::string current;
                static std::map<std::string, GLFWcursor*> cursors;
                std::string want = uidraw::takeCursorRequest();
                // (the new-version check: asked in the background; the title screen shows it)
                if (g_Updates) {
                    g_Updates->update(glfwGetTime());
                    if (g_Updates->takeNews()) {
                        const std::string line = g_Updates->summary();
                        // one line (the full details: the server's "update" command / the Lua info)
                        if (!line.empty()) std::cout << "[Update] " << line
                                                     << (g_Updates->info().checked ? "" : " (" + g_Updates->url() + ")") << std::endl;
                    }
                    if (g_MainMenu) {
                        const update::Info& u = g_Updates->info();
                        std::string notice;
                        if (u.required) notice = "Update required: " + u.latest;
                        else if (u.available) notice = "Update available: " + u.latest;
                        if (!notice.empty() && !u.download.empty()) notice += " - click to download";
                        g_MainMenu->setUpdateNotice(notice, u.required);
                    }
                }
                if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_NORMAL || want == "arrow") want.clear();
                if (want != current) {
                    current = want;
                    GLFWcursor* cursor = nullptr;
                    if (!want.empty()) {
                        auto it = cursors.find(want);
                        if (it == cursors.end()) {
                            int shape = 0;
                            if (want == "hand") shape = GLFW_HAND_CURSOR;
                            else if (want == "text") shape = GLFW_IBEAM_CURSOR;
                            else if (want == "crosshair") shape = GLFW_CROSSHAIR_CURSOR;
                            else if (want == "resize_h") shape = GLFW_HRESIZE_CURSOR;
                            else if (want == "resize_v") shape = GLFW_VRESIZE_CURSOR;
#ifdef GLFW_RESIZE_NWSE_CURSOR
                            else if (want == "resize_nwse") shape = GLFW_RESIZE_NWSE_CURSOR;
                            else if (want == "resize_nesw") shape = GLFW_RESIZE_NESW_CURSOR;
                            else if (want == "move") shape = GLFW_RESIZE_ALL_CURSOR;
                            else if (want == "not_allowed") shape = GLFW_NOT_ALLOWED_CURSOR;
#else
                            else if (want == "resize_nwse" || want == "resize_nesw" || want == "move") shape = GLFW_CROSSHAIR_CURSOR;
#endif
                            it = cursors.emplace(want, shape ? glfwCreateStandardCursor(shape) : nullptr).first;
                        }
                        cursor = it->second;
                    }
                    glfwSetCursor(window, cursor);   // nullptr = the normal arrow
                }
            }

            glfwSwapBuffers(window);
        }

        // Closing the window (or Quit Game) while playing saves the world first
        if (g_CurrentGameState == GameState::PLAYING) { world.finishFalling(); saveCurrentWorld(); }
        g_Client.disconnect();
        g_Server.stop("The host closed the game");
        g_DimWorlds.unloadAll();       // (before the generator their chunks come from goes)
        g_DimWorlds.primary = nullptr;
        g_DimWorlds.generator = nullptr;
        dims::currentRef().clear();
        world.setGenerator(nullptr);   // the generator's threads stop at the end of this block
        g_ItemUseHandlers.clear();     // Lua functions: must go before the Lua state does
        g_Pinger.clear();
        g_Lan.stop();
        for (auto& [addr, t] : g_ServerIcons) if (t.tex) glDeleteTextures(1, &t.tex);
        g_ServerIcons.clear();

        // Everything still holding the session's Lua functions lets go while the Lua state exists
        g_BlockMods = BlockModifiers{};
        g_EntityLua = EntityLua{};
        g_Entities = EntityManager{};
        g_ServerApi = ServerLuaApi{};
        g_AmbientEffects.clear();
        g_AmbientByBlock.clear();
        g_AmbientResolvedFor = 0;
        g_Lua = nullptr;

        g_Audio.shutdown();
        // (the objects drawing with OpenGL go at the end of this block: GlShutdown)
        }   // end of the session: its Lua state, world and renderers are gone now

        if (!g_Reload.pending || glfwWindowShouldClose(window)) break;
        resetForReload();
        }   // for (;;): the next session, with the mods of the world being opened
    } catch (const std::exception& e) {
        std::cerr << "\n[CRITICAL EXCEPTION]: " << e.what() << std::endl;
        system("pause");
        return -1;
    }
    return 0;
}
