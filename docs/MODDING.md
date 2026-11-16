# Making games and mods with BlockSmith

BlockSmith is a voxel engine. Everything that makes it a game lives in Lua:
- blocks and items
- world generation
- menus and the HUD
- chat commands
- fluids, weather and sky

You can build a whole new game, or add a mod to an existing one, without touching C++.

> **Always up to date:** at startup the engine writes **`lua_api.txt`** next to the
> executable. It lists every function your build has, which hooks your mods use, the APIs
> other mods offer, and every block and fluid. If this guide and `lua_api.txt` ever
> disagree, trust `lua_api.txt`. Rewrite it at any time with <a id="fn-engine-write_api_reference"></a>`engine.write_api_reference()`.

Contents:

- [Function index](#function-index): every function and hook, A to Z per table
1. [Folders](#1-folders)
2. [Your first mod](#2-your-first-mod)
3. [Blocks and items](#3-blocks-and-items)
4. [Changing the world](#4-changing-the-world)
5. [World generation](#5-world-generation)
6. [Items that do things](#6-items-that-do-things)
7. [Fluids](#7-fluids)
8. [Chat commands](#8-chat-commands)
9. [Sky, weather, fog, clouds, underwater](#9-sky-weather-fog-clouds-underwater)
10. [Survival: health, hunger, digging](#10-survival-health-hunger-digging)
11. [Sound](#11-sound)
12. [Models](#12-models)
13. [UI: menus and screens](#13-ui-menus-and-screens)
14. [Hotbar](#14-hotbar)
15. [Hooks the engine calls](#15-hooks-the-engine-calls)
16. [Multiplayer and servers](#16-multiplayer-and-servers)
17. [Engine screens and settings](#17-engine-screens-and-settings)
18. [Tips](#18-tips)

---

## Function index

Every function and hook, by table, each linking to where it's explained. `lua_api.txt` (written by the engine at startup) lists what your build has.

**engine** (115)  
[`engine.add_model`](#fn-engine-add_model) · [`engine.add_player_velocity`](#fn-engine-add_player_velocity) · [`engine.allow_firewall`](#fn-engine-allow_firewall) · [`engine.back_to_title`](#fn-engine-back_to_title) · [`engine.cancel_join`](#fn-engine-cancel_join) · [`engine.close_settings`](#fn-engine-close_settings) · [`engine.create_server_profile`](#fn-engine-create_server_profile) · [`engine.create_world`](#fn-engine-create_world) · [`engine.delete_server_profile`](#fn-engine-delete_server_profile) · [`engine.delete_world`](#fn-engine-delete_world) · [`engine.draw_text`](#fn-engine-draw_text) · [`engine.find_texture`](#fn-engine-find_texture) · [`engine.get_active_world`](#fn-engine-get_active_world) · [`engine.get_block_def`](#fn-engine-get_block_def) · [`engine.get_block_id`](#fn-engine-get_block_id) · [`engine.get_block_name`](#fn-engine-get_block_name) · [`engine.get_connection_status`](#fn-engine-get_connection_status) · [`engine.get_dig_progress`](#fn-engine-get_dig_progress) · [`engine.get_fluid`](#fn-engine-get_fluid) · [`engine.get_game_info`](#fn-engine-get_game_info) · [`engine.get_keybinds`](#fn-engine-get_keybinds) · [`engine.get_lan_games`](#fn-engine-get_lan_games) · [`engine.get_metadata`](#fn-engine-get_metadata) · [`engine.get_network_info`](#fn-engine-get_network_info) · [`engine.get_player_list`](#fn-engine-get_player_list) · [`engine.get_player_name`](#fn-engine-get_player_name) · [`engine.get_player_position`](#fn-engine-get_player_position) · [`engine.get_player_state`](#fn-engine-get_player_state) · [`engine.get_server_profile`](#fn-engine-get_server_profile) · [`engine.get_server_settings`](#fn-engine-get_server_settings) · [`engine.get_server_status`](#fn-engine-get_server_status) · [`engine.get_shared`](#fn-engine-get_shared) · [`engine.get_storage`](#fn-engine-get_storage) · [`engine.get_texture_pack`](#fn-engine-get_texture_pack) · [`engine.get_video_settings`](#fn-engine-get_video_settings) · [`engine.get_worlds`](#fn-engine-get_worlds) · [`engine.give_item`](#fn-engine-give_item) · [`engine.has_player_password`](#fn-engine-has_player_password) · [`engine.has_server_build`](#fn-engine-has_server_build) · [`engine.has_server_icon`](#fn-engine-has_server_icon) · [`engine.host_game`](#fn-engine-host_game) · [`engine.is_authority`](#fn-engine-is_authority) · [`engine.is_creative`](#fn-engine-is_creative) · [`engine.is_fluid_source`](#fn-engine-is_fluid_source) · [`engine.is_in_game`](#fn-engine-is_in_game) · [`engine.is_inventory_open`](#fn-engine-is_inventory_open) · [`engine.is_outdoors`](#fn-engine-is_outdoors) · [`engine.is_paused`](#fn-engine-is_paused) · [`engine.is_replaceable`](#fn-engine-is_replaceable) · [`engine.is_scanning_lan`](#fn-engine-is_scanning_lan) · [`engine.is_server`](#fn-engine-is_server) · [`engine.is_third_person`](#fn-engine-is_third_person) · [`engine.is_world_generator`](#fn-engine-is_world_generator) · [`engine.join_server`](#fn-engine-join_server) · [`engine.key_name`](#fn-engine-key_name) · [`engine.list_blocks`](#fn-engine-list_blocks) · [`engine.list_fluids`](#fn-engine-list_fluids) · [`engine.list_games`](#fn-engine-list_games) · [`engine.list_mods`](#fn-engine-list_mods) · [`engine.list_mods_for_game`](#fn-engine-list_mods_for_game) · [`engine.list_server_profiles`](#fn-engine-list_server_profiles) · [`engine.list_servers`](#fn-engine-list_servers) · [`engine.list_texture_packs`](#fn-engine-list_texture_packs) · [`engine.list_worlds`](#fn-engine-list_worlds) · [`engine.load_model`](#fn-engine-load_model) · [`engine.move_model`](#fn-engine-move_model) · [`engine.open_mods_folder`](#fn-engine-open_mods_folder) · [`engine.open_server_profile_folder`](#fn-engine-open_server_profile_folder) · [`engine.open_settings`](#fn-engine-open_settings) · [`engine.open_texture_packs_folder`](#fn-engine-open_texture_packs_folder) · [`engine.override_block`](#fn-engine-override_block) · [`engine.pause`](#fn-engine-pause) · [`engine.ping_server`](#fn-engine-ping_server) · [`engine.play_world`](#fn-engine-play_world) · [`engine.quit`](#fn-engine-quit) · [`engine.quit_to_menu`](#fn-engine-quit_to_menu) · [`engine.register_block`](#fn-engine-register_block) · [`engine.register_fluid`](#fn-engine-register_fluid) · [`engine.register_fluid_reaction`](#fn-engine-register_fluid_reaction) · [`engine.register_item_use`](#fn-engine-register_item_use) · [`engine.remove_model`](#fn-engine-remove_model) · [`engine.reset_keybinds`](#fn-engine-reset_keybinds) · [`engine.respawn_player`](#fn-engine-respawn_player) · [`engine.resume`](#fn-engine-resume) · [`engine.save_server_profile`](#fn-engine-save_server_profile) · [`engine.save_servers`](#fn-engine-save_servers) · [`engine.save_world`](#fn-engine-save_world) · [`engine.send_chat`](#fn-engine-send_chat) · [`engine.set_active_world`](#fn-engine-set_active_world) · [`engine.set_block`](#fn-engine-set_block) · [`engine.set_block_meta`](#fn-engine-set_block_meta) · [`engine.set_creative`](#fn-engine-set_creative) · [`engine.set_dig_crack_texture`](#fn-engine-set_dig_crack_texture) · [`engine.set_footstep_distance`](#fn-engine-set_footstep_distance) · [`engine.set_fov`](#fn-engine-set_fov) · [`engine.set_inventory_open`](#fn-engine-set_inventory_open) · [`engine.set_keybind`](#fn-engine-set_keybind) · [`engine.set_mod_enabled`](#fn-engine-set_mod_enabled) · [`engine.set_model_visible`](#fn-engine-set_model_visible) · [`engine.set_moon_texture`](#fn-engine-set_moon_texture) · [`engine.set_player_controls`](#fn-engine-set_player_controls) · [`engine.set_player_name`](#fn-engine-set_player_name) · [`engine.set_player_password`](#fn-engine-set_player_password) · [`engine.set_player_position`](#fn-engine-set_player_position) · [`engine.set_server_setting`](#fn-engine-set_server_setting) · [`engine.set_shared`](#fn-engine-set_shared) · [`engine.set_storage`](#fn-engine-set_storage) · [`engine.set_sun_texture`](#fn-engine-set_sun_texture) · [`engine.set_texture_pack`](#fn-engine-set_texture_pack) · [`engine.set_video_setting`](#fn-engine-set_video_setting) · [`engine.start_game`](#fn-engine-start_game) · [`engine.start_server_profile`](#fn-engine-start_server_profile) · [`engine.stop_hosting`](#fn-engine-stop_hosting) · [`engine.stop_server_profile`](#fn-engine-stop_server_profile) · [`engine.write_api_reference`](#fn-engine-write_api_reference)

**world** (8)  
[`world.get_caves_enabled`](#fn-world-get_caves_enabled) · [`world.get_loaded_chunks`](#fn-world-get_loaded_chunks) · [`world.get_pending_chunks`](#fn-world-get_pending_chunks) · [`world.get_render_distance`](#fn-world-get_render_distance) · [`world.set_caves_enabled`](#fn-world-set_caves_enabled) · [`world.set_load_budget`](#fn-world-set_load_budget) · [`world.set_mesh_budget`](#fn-world-set_mesh_budget) · [`world.set_render_distance`](#fn-world-set_render_distance)

**ui** (12)  
[`ui.draw_block_icon`](#fn-ui-draw_block_icon) · [`ui.draw_image`](#fn-ui-draw_image) · [`ui.draw_rect`](#fn-ui-draw_rect) · [`ui.draw_server_icon`](#fn-ui-draw_server_icon) · [`ui.draw_text`](#fn-ui-draw_text) · [`ui.get_all_blocks`](#fn-ui-get_all_blocks) · [`ui.get_block_info`](#fn-ui-get_block_info) · [`ui.get_mouse`](#fn-ui-get_mouse) · [`ui.get_mouse_pos`](#fn-ui-get_mouse_pos) · [`ui.get_time`](#fn-ui-get_time) · [`ui.text_height`](#fn-ui-text_height) · [`ui.text_width`](#fn-ui-text_width)

**hotbar** (16)  
[`hotbar.get_active_slot`](#fn-hotbar-get_active_slot) · [`hotbar.get_rect`](#fn-hotbar-get_rect) · [`hotbar.get_size`](#fn-hotbar-get_size) · [`hotbar.get_slot`](#fn-hotbar-get_slot) · [`hotbar.get_slot_block`](#fn-hotbar-get_slot_block) · [`hotbar.get_slot_count`](#fn-hotbar-get_slot_count) · [`hotbar.is_creative_open`](#fn-hotbar-is_creative_open) · [`hotbar.set_active_slot`](#fn-hotbar-set_active_slot) · [`hotbar.set_position`](#fn-hotbar-set_position) · [`hotbar.set_selected_texture`](#fn-hotbar-set_selected_texture) · [`hotbar.set_size`](#fn-hotbar-set_size) · [`hotbar.set_slot`](#fn-hotbar-set_slot) · [`hotbar.set_slot_size`](#fn-hotbar-set_slot_size) · [`hotbar.set_texture`](#fn-hotbar-set_texture) · [`hotbar.set_texture_scale`](#fn-hotbar-set_texture_scale) · [`hotbar.toggle_creative`](#fn-hotbar-toggle_creative)

**chat** (2)  
[`chat.register_command`](#fn-chat-register_command) · [`chat.send`](#fn-chat-send)

**sky** (21)  
[`sky.add_light`](#fn-sky-add_light) · [`sky.flash`](#fn-sky-flash) · [`sky.get_light`](#fn-sky-get_light) · [`sky.get_moon_phase`](#fn-sky-get_moon_phase) · [`sky.get_precipitation`](#fn-sky-get_precipitation) · [`sky.get_rain_intensity`](#fn-sky-get_rain_intensity) · [`sky.get_time`](#fn-sky-get_time) · [`sky.remove_light`](#fn-sky-remove_light) · [`sky.reset`](#fn-sky-reset) · [`sky.set_ambient_color`](#fn-sky-set_ambient_color) · [`sky.set_ambient_intensity`](#fn-sky-set_ambient_intensity) · [`sky.set_fog_color`](#fn-sky-set_fog_color) · [`sky.set_light`](#fn-sky-set_light) · [`sky.set_moon_phase`](#fn-sky-set_moon_phase) · [`sky.set_moon_textures`](#fn-sky-set_moon_textures) · [`sky.set_precipitation`](#fn-sky-set_precipitation) · [`sky.set_rain_intensity`](#fn-sky-set_rain_intensity) · [`sky.set_sky_tint`](#fn-sky-set_sky_tint) · [`sky.set_sun_color`](#fn-sky-set_sun_color) · [`sky.set_sun_intensity`](#fn-sky-set_sun_intensity) · [`sky.set_time`](#fn-sky-set_time)

**clouds** (9)  
[`clouds.set_cell_size`](#fn-clouds-set_cell_size) · [`clouds.set_color`](#fn-clouds-set_color) · [`clouds.set_height`](#fn-clouds-set_height) · [`clouds.set_night_color`](#fn-clouds-set_night_color) · [`clouds.set_scroll_speed`](#fn-clouds-set_scroll_speed) · [`clouds.set_texture`](#fn-clouds-set_texture) · [`clouds.set_thickness`](#fn-clouds-set_thickness) · [`clouds.set_threshold`](#fn-clouds-set_threshold) · [`clouds.set_weather`](#fn-clouds-set_weather)

**fog** (8)  
[`fog.set_day_color`](#fn-fog-set_day_color) · [`fog.set_density`](#fn-fog-set_density) · [`fog.set_enabled`](#fn-fog-set_enabled) · [`fog.set_exponential`](#fn-fog-set_exponential) · [`fog.set_night_color`](#fn-fog-set_night_color) · [`fog.set_rain_scale`](#fn-fog-set_rain_scale) · [`fog.set_range`](#fn-fog-set_range) · [`fog.set_sunset_color`](#fn-fog-set_sunset_color)

**underwater** (6)  
[`underwater.set_deep_color`](#fn-underwater-set_deep_color) · [`underwater.set_fade`](#fn-underwater-set_fade) · [`underwater.set_full_depth`](#fn-underwater-set_full_depth) · [`underwater.set_shallow_color`](#fn-underwater-set_shallow_color) · [`underwater.set_tint_strength`](#fn-underwater-set_tint_strength) · [`underwater.set_visibility`](#fn-underwater-set_visibility)

**sound** (14)  
[`sound.exists`](#fn-sound-exists) · [`sound.fade`](#fn-sound-fade) · [`sound.get_music`](#fn-sound-get_music) · [`sound.get_volume`](#fn-sound-get_volume) · [`sound.is_available`](#fn-sound-is_available) · [`sound.is_playing`](#fn-sound-is_playing) · [`sound.play`](#fn-sound-play) · [`sound.play_music`](#fn-sound-play_music) · [`sound.set_gain`](#fn-sound-set_gain) · [`sound.set_pitch`](#fn-sound-set_pitch) · [`sound.set_position`](#fn-sound-set_position) · [`sound.set_volume`](#fn-sound-set_volume) · [`sound.stop`](#fn-sound-stop) · [`sound.stop_music`](#fn-sound-stop_music)

**menu** (3)  
[`menu.get_active_game`](#fn-menu-get_active_game) · [`menu.get_games`](#fn-menu-get_games) · [`menu.set_active_game`](#fn-menu-set_active_game)

**chunk (in generate_chunk)** (6)  
[`chunk:get_block`](#fn-chunk-get_block) · [`chunk:get_metadata`](#fn-chunk-get_metadata) · [`chunk:set_block`](#fn-chunk-set_block) · [`chunk:set_block_with_meta`](#fn-chunk-set_block_with_meta) · [`chunk:set_metadata`](#fn-chunk-set_metadata) · [`chunk:set_tint`](#fn-chunk-set_tint)

**Hooks (functions you define)** (31)  
[`generate_chunk`](#fn-hooks-generate_chunk) · [`get_dig_time`](#fn-hooks-get_dig_time) · [`get_foliage_color`](#fn-hooks-get_foliage_color) · [`get_grass_color`](#fn-hooks-get_grass_color) · [`get_water_color`](#fn-hooks-get_water_color) · [`handle_inventory_click`](#fn-hooks-handle_inventory_click) · [`handle_pause_click`](#fn-hooks-handle_pause_click) · [`on_char_input`](#fn-hooks-on_char_input) · [`on_chat_command`](#fn-hooks-on_chat_command) · [`on_dig_block`](#fn-hooks-on_dig_block) · [`on_hotbar_full`](#fn-hooks-on_hotbar_full) · [`on_player_click`](#fn-hooks-on_player_click) · [`on_right_click_block`](#fn-hooks-on_right_click_block) · [`on_scroll_input`](#fn-hooks-on_scroll_input) · [`on_shared_changed`](#fn-hooks-on_shared_changed) · [`on_update`](#fn-hooks-on_update) · [`on_use_hoe`](#fn-hooks-on_use_hoe) · [`on_use_scythe`](#fn-hooks-on_use_scythe) · [`render_creative_inventory`](#fn-hooks-render_creative_inventory) · [`render_hud`](#fn-hooks-render_hud) · [`render_inventory`](#fn-hooks-render_inventory) · [`render_mods`](#fn-hooks-render_mods) · [`render_multiplayer`](#fn-hooks-render_multiplayer) · [`render_pause_menu`](#fn-hooks-render_pause_menu) · [`render_player_list`](#fn-hooks-render_player_list) · [`render_settings`](#fn-hooks-render_settings) · [`render_texture_packs`](#fn-hooks-render_texture_packs) · [`render_world_select`](#fn-hooks-render_world_select) · [`set_world_seed`](#fn-hooks-set_world_seed) · [`toggle_main_inventory`](#fn-hooks-toggle_main_inventory) · [`update_player_animation`](#fn-hooks-update_player_animation)

**weather (mods/weather)** (11)  
[`weather.get`](#fn-weather-get) · [`weather.get_def`](#fn-weather-get_def) · [`weather.is_cold`](#fn-weather-is_cold) · [`weather.is_cycling`](#fn-weather-is_cycling) · [`weather.list`](#fn-weather-list) · [`weather.on_change`](#fn-weather-on_change) · [`weather.on_lightning`](#fn-weather-on_lightning) · [`weather.register`](#fn-weather-register) · [`weather.set`](#fn-weather-set) · [`weather.set_cycle`](#fn-weather-set_cycle) · [`weather.time_left`](#fn-weather-time_left)

**survival (mods/survival)** (17)  
[`survival.damage`](#fn-survival-damage) · [`survival.feed`](#fn-survival-feed) · [`survival.get_air`](#fn-survival-get_air) · [`survival.get_armor`](#fn-survival-get_armor) · [`survival.get_food`](#fn-survival-get_food) · [`survival.get_hp`](#fn-survival-get_hp) · [`survival.get_mode`](#fn-survival-get_mode) · [`survival.heal`](#fn-survival-heal) · [`survival.is_dead`](#fn-survival-is_dead) · [`survival.on_damage`](#fn-survival-on_damage) · [`survival.on_death`](#fn-survival-on_death) · [`survival.on_respawn`](#fn-survival-on_respawn) · [`survival.register_food`](#fn-survival-register_food) · [`survival.respawn`](#fn-survival-respawn) · [`survival.set_armor`](#fn-survival-set_armor) · [`survival.set_mode`](#fn-survival-set_mode) · [`survival.settings`](#fn-survival-settings)

**survival.inventory** (6)  
[`survival.inventory.add`](#fn-survival-inventory-add) · [`survival.inventory.clear`](#fn-survival-inventory-clear) · [`survival.inventory.get`](#fn-survival-inventory-get) · [`survival.inventory.is_open`](#fn-survival-inventory-is_open) · [`survival.inventory.set`](#fn-survival-inventory-set) · [`survival.inventory.size`](#fn-survival-inventory-size)

**buckets (mods/buckets)** (1)  
[`buckets.register`](#fn-buckets-register)

**default_sounds (mods/default_sounds)** (3)  
[`default_sounds.material`](#fn-default_sounds-material) · [`default_sounds.rules`](#fn-default_sounds-rules) · [`default_sounds.sets`](#fn-default_sounds-sets)

---


## 1. Folders

```
BlockSmith.exe
BlockSmithServer.exe            dedicated server (see section 16)
lua_api.txt                     written at startup: what this build can do
games/
  craft_and_ruin/               a game: pick it on the title screen
    game.conf                   title = ..., author = ..., description = ...
    mods/                       the game's own mods (always loaded with it)
      ...
mods/                           global mods: loaded with any game, can be switched off
  buckets/
    mod.conf                    title, author, description, version, depends, required = true|false
    init.lua                    if present, the ONLY file the engine runs (it loads the rest)
    textures/  models/
texturepacks/
  MyPack/pack.conf              any image with the same file name replaces a game texture
saves/<world>/                  worlds
servers/<profile>/              dedicated server setups (made with My Servers)
```

**How a mod's files run:**
- If a folder contains `init.lua`, only that file runs. Load your other files from it with `dofile(g_CurrentModPath .. "/other.lua")`.
- Otherwise every `.lua` file in the folder runs.
- While a file runs, `g_CurrentModPath` is its mod's folder. Textures and models are looked for there first, then in its `textures/` or `models/` folder.
- Players can switch global mods off on the Mods screen. A mod with `required = true` in `mod.conf` can't be switched off.

**To make a new game:**
1. Make a folder in `games/` with a `game.conf`.
2. Put its mods in `games/<yours>/mods/`.
3. Pick it on the title screen.

Worlds remember which game made them.

## 2. Your first mod

`mods/hello/init.lua`:

<a id="fn-chat-register_command"></a><a id="fn-chat-send"></a><a id="fn-engine-register_block"></a><a id="fn-hooks-on_chat_command"></a>
```lua
-- A block
engine.register_block("ruby_block", 1, 1, 1, "ruby_block.png")

-- A chat command
chat.register_command("hello", "/hello", "Says hello")
local previous = on_chat_command          -- keep any other mod's handler
function on_chat_command(text)
    if text == "hello" then
        chat.send("Hello from my mod!", 1, 0.8, 0.2)
        return true                       -- handled
    end
    return previous and previous(text) or false
end

print("[Hello] loaded")
```

Put `ruby_block.png` (16×16) in `mods/hello/textures/` and start the game. The block appears
in the creative inventory, and `/hello` works in chat.

## 3. Blocks and items

```lua
engine.register_block(name, r, g, b, texture [, overlay [, properties]])
```

**Colour (`r, g, b`):** a tint, from 0 to 1 (or 0 to 255). It only applies to faces marked for tinting, like grass.

**`texture`** is one of:
- `"stone.png"`: the same picture on every face
- `{ top = "log_top.png", bottom = "log_top.png", sides = "log.png" }`

**`overlay`** (optional) is a picture drawn over the side faces, like the grass edge on dirt: `"grass_side_overlay.png"` or `{ sides = "..." }`.

**`properties`** (all optional):

| key | default | meaning |
|---|---|---|
| `walkable` | true | solid: you collide with it |
| `selectable` | true | the crosshair can target and break it |
| `transparent` | false | see-through; drawn blended (glass, water) |
| `cutout` | false | see-through pixels cut out, no blending (leaves) |
| `pattern` | `"cube"` | `"plantlike"` = two crossed pictures (flowers, grass) |
| `gravity` | false | falls like sand |
| `tint` | false | tint all faces with r, g, b |
| `in_creative` | true | listed in the creative inventory |
| `inventory_image` | none | shows a flat picture in the inventory and hotbar. The block becomes an **item**: right-click never places it |

**Rules:**
- Up to 255 blocks.
- Registering the same name twice keeps its first id and updates its settings. The console says so.
- Worlds save blocks by **name**, so the order you register them in doesn't matter.

**Metadata:** each block has one extra byte, 0–255. Water uses it for its flow level. Plants use bit 1 to mean "waterlogged". Your blocks can use it for anything.

## 4. Changing the world

| function | |
|---|---|
| <a id="fn-engine-get_block_id"></a>`engine.get_block_id(name)` | block id, or 0 if there's no such block |
| <a id="fn-engine-get_block_name"></a>`engine.get_block_name(x, y, z)` | `"stone"`, `"air"`... |
| <a id="fn-engine-set_block"></a>`engine.set_block(x, y, z, name)` | changes a block (it is saved, sent to other players, and fluids react) |
| <a id="fn-engine-get_metadata"></a>`engine.get_metadata(x, y, z)` | 0–255 |
| <a id="fn-engine-set_block_meta"></a>`engine.set_block_meta(x, y, z, name, meta)` | block and metadata in one change |
| <a id="fn-engine-is_replaceable"></a>`engine.is_replaceable(x, y, z)` | air, a fluid or a plant: there's room to put something |
| <a id="fn-world-get_render_distance"></a><a id="fn-world-set_render_distance"></a>`world.set_render_distance(chunks)` / `get_render_distance()` | |
| <a id="fn-world-get_caves_enabled"></a><a id="fn-world-set_caves_enabled"></a>`world.set_caves_enabled(bool)` / `get_caves_enabled()` | built-in caves (turn them off if your mapgen makes its own) |
| <a id="fn-world-get_loaded_chunks"></a><a id="fn-world-get_pending_chunks"></a>`world.get_loaded_chunks()` / `get_pending_chunks()` | |
| <a id="fn-world-set_load_budget"></a><a id="fn-world-set_mesh_budget"></a>`world.set_load_budget(ms)` / `set_mesh_budget(ms)` | how much time per frame goes to loading and meshing |

## 5. World generation

Define these globals. The engine calls them.

<a id="fn-hooks-generate_chunk"></a><a id="fn-hooks-set_world_seed"></a>
```lua
function set_world_seed(seed)          -- once per world, before any chunk
    my_noise_seed = seed
end

function generate_chunk(chunk, cx, cz) -- every new chunk (16 x 256 x 16)
    local stone = engine.get_block_id("stone")
    for x = 0, 15 do
        for z = 0, 15 do
            local wx, wz = cx * 16 + x, cz * 16 + z
            local height = 60 + math.floor(math.sin(wx * 0.05) * 4)
            for y = 0, height do chunk:set_block(x, y, z, stone) end
        end
    end
end
```

<a id="fn-chunk-get_block"></a><a id="fn-chunk-get_metadata"></a><a id="fn-chunk-set_block"></a><a id="fn-chunk-set_block_with_meta"></a><a id="fn-chunk-set_metadata"></a>`chunk` provides `set_block(x, y, z, id)`, `get_block(x, y, z)`,
`set_block_with_meta(x, y, z, id, meta)`, `set_metadata(x, y, z, meta)` and
`get_metadata(x, y, z)`. Coordinates are 0–15 across and 0–255 up.

**Important:**
- `generate_chunk` runs on **background threads**, each with its own copy of your mod files.
- <a id="fn-engine-is_world_generator"></a>On those threads only world-generation functions work. `engine.get_block_id` and math work. Drawing, chat, `engine.set_block` and the hotbar do nothing. `engine.is_world_generator()` returns true there, if a mod needs to know.
- Don't rely on global variables changed elsewhere at runtime. Compute everything from the seed and the coordinates.
- With no `generate_chunk`, the engine makes simple built-in terrain.
- After your terrain, the engine carves caves unless you turn them off.

### Biome colours (grass and leaves)

Grass and leaves can change colour from place to place, like Minecraft. Define these in your mapgen; they return `r, g, b` (0–255):

<a id="fn-hooks-get_foliage_color"></a><a id="fn-hooks-get_grass_color"></a>
```lua
function get_grass_color(x, z)   -- e.g. from your biome temperature and humidity
    local _, temp, hum = get_biome_parameters(x, z)
    return 80 + 110 * temp * (1 - hum), 190 + 25 * hum, 120 - 60 * temp
end
function get_foliage_color(x, z) ... end   -- optional, for leaves
```

- **When it's asked:** the engine asks every 4 blocks of each new chunk, on the generator threads, and blends smoothly in between and from block to block.
- **Where colours are kept:** with the chunk, so they're saved and sent to players.
- <a id="fn-chunk-set_tint"></a>**Setting colours directly:** a mapgen can instead call `chunk:set_tint(x, z, r, g, b [, "foliage"])` inside `generate_chunk`.
- **Which blocks use them:** blocks with `biome_tint = "grass"`, `"foliage"` or `"none"`. Tinted blocks named *grass* use grass colours and tinted *leaves* use foliage colours automatically. The colour replaces the block's tint colour only where the block is tinted (for grass: the top and the side overlay).

## 6. Items that do things

<a id="fn-engine-register_item_use"></a>
```lua
engine.register_block("magic_wand", 1, 1, 1, "wand.png", nil, { inventory_image = "wand.png" })

engine.register_item_use("magic_wand", function(pointed)
    -- pointed.item, pointed.slot, pointed.count, pointed.player = {x,y,z}
    -- pointed.under = {x,y,z,name}: the block looked at (nil if none)
    -- pointed.above = {x,y,z}: the space in front of that face
    -- pointed.fluid = {x,y,z,name,block,source,falling,level}: fluid looked at first (or nil)
    if pointed.under then
        engine.set_block(pointed.under.x, pointed.under.y, pointed.under.z, "gold_block")
    end
    return true        -- the click was used
end)
```

<a id="fn-engine-give_item"></a>`engine.give_item(name [, count])` adds items to the hotbar and returns false if they didn't fit. `mods/buckets` is a complete example.

Older hooks still work:
- <a id="fn-hooks-on_right_click_block"></a>`on_right_click_block(x, y, z, block, held_item)`: return true to stop placing
- <a id="fn-hooks-on_use_hoe"></a>`on_use_hoe(x, y, z, block)`
- <a id="fn-hooks-on_use_scythe"></a>`on_use_scythe(x, y, z, block)`

## 7. Fluids

Any fluid (lava, molten metal, oil) flows like Minecraft water, with its own spread distance, speed, reactions and bucket:

<a id="fn-engine-register_fluid"></a><a id="fn-engine-register_fluid_reaction"></a>
```lua
engine.register_fluid{ name = "lava", source = "lava", flowing = "flowing_lava",
                       spread = 3, tick = 1.5, infinite = false }
engine.register_fluid_reaction{ fluid = "lava", touching = "water",
                                source_becomes = "obsidian", flowing_becomes = "cobblestone" }
```

<a id="fn-engine-get_fluid"></a><a id="fn-engine-is_fluid_source"></a><a id="fn-engine-list_fluids"></a>`engine.get_fluid(x, y, z)`, `engine.is_fluid_source(x, y, z)` and `engine.list_fluids()` read fluids. The full guide is **`docs/FLUIDS.md`**. `mods/lava` is a complete example.

## 8. Chat commands

<a id="fn-engine-send_chat"></a>
```lua
chat.register_command(name, usage, description)   -- listed in /help
chat.send(text [, r, g, b])                       -- a message in your own chat
engine.send_chat(text)                            -- a message to everyone (multiplayer)
function on_chat_command(text) ... end            -- "/give stone 5" arrives as "give stone 5"
```

In multiplayer, commands the client doesn't handle go to the server. Server operators have their own commands there (see section 16).

## 9. Sky, weather, fog, clouds, underwater

**Weather** is decided entirely in Lua by `mods/weather`. It's Minecraft-like by default: long clear spells, rain, thunderstorms, and snow where your game says it's cold. The engine only draws what that mod asks for:

| function | |
|---|---|
| <a id="fn-sky-set_precipitation"></a>`sky.set_precipitation{ type, intensity, fall_speed, size, length, sway, wind_x, wind_z, color = {r,g,b,a}, texture, radius, height, max_particles, streaks }` | rain, snow or anything else (ash, leaves) with your own picture. `type = "rain"` or `"snow"` starts from a preset; other fields change only what you give. Particles stop on roofs and water. |
| <a id="fn-sky-get_precipitation"></a>`sky.get_precipitation()` | `{ type, intensity }` |
| <a id="fn-sky-set_sky_tint"></a>`sky.set_sky_tint(r, g, b, a)` | colour laid over the sky (grey rain, dark storm) |
| <a id="fn-sky-get_light"></a><a id="fn-sky-set_light"></a>`sky.set_light(level)` / `get_light()` | sunlight on the world: 1 = normal, 0.5 = stormy |
| <a id="fn-sky-flash"></a>`sky.flash(strength, seconds)` | a lightning flash |
| <a id="fn-engine-is_outdoors"></a>`engine.is_outdoors(x, y, z)` | nothing above, so rain reaches it |

Using the weather mod:

<a id="fn-weather-get"></a><a id="fn-weather-is_cold"></a><a id="fn-weather-list"></a><a id="fn-weather-on_change"></a><a id="fn-weather-on_lightning"></a><a id="fn-weather-register"></a><a id="fn-weather-set"></a><a id="fn-weather-set_cycle"></a><a id="fn-weather-is_cycling"></a><a id="fn-weather-time_left"></a><a id="fn-weather-get_def"></a>
```lua
weather.register("sandstorm", { light = 0.7, sky_tint = { 0.8, 0.65, 0.45, 0.6 },
    precipitation = { type = "dust", texture = "sand.png", intensity = 0.7, streaks = false },
    wind = { 6, 1 }, chance = 5, duration = { 120, 300 } })
weather.set("rain", 10)          -- fade over 10 s (singleplayer, host or server only)
weather.get(), weather.list(), weather.set_cycle(false)
weather.is_cycling(), weather.time_left()   -- seconds until the next change
weather.get_def("rain")          -- the table it was registered with
weather.on_change(function(old, new) end)
weather.on_lightning(function(x, z) end)
weather.is_cold = function(x, z) return my_temperature(x, z) < 0.25 end   -- rain becomes snow
```

The `/weather [type [seconds]] | list | cycle on|off` command works in singleplayer and for operators on servers.

**Sky:**
- <a id="fn-sky-get_time"></a><a id="fn-sky-set_time"></a>`sky.set_time(t)` / `get_time()`. Time runs from 0 to 1 over a whole day: `0.75` is midday and `0.25` is midnight (the base mod's `/day` and `/night` use these).
- <a id="fn-sky-get_moon_phase"></a><a id="fn-sky-set_moon_phase"></a><a id="fn-sky-set_moon_textures"></a>`sky.set_moon_phase(n)` / `get_moon_phase()`, `sky.set_moon_textures({ ... })`
- <a id="fn-engine-set_moon_texture"></a><a id="fn-engine-set_sun_texture"></a>`engine.set_sun_texture(path)`, `engine.set_moon_texture(path)`
- <a id="fn-sky-set_ambient_color"></a><a id="fn-sky-set_fog_color"></a><a id="fn-sky-set_sun_color"></a>`sky.set_sun_color(r,g,b)`, `set_ambient_color(r,g,b)`, `set_fog_color(r,g,b)`
- <a id="fn-sky-set_ambient_intensity"></a><a id="fn-sky-set_sun_intensity"></a>`sky.set_sun_intensity(v)`, `set_ambient_intensity(v)`
- <a id="fn-sky-reset"></a>`sky.reset()`

<a id="fn-sky-get_rain_intensity"></a><a id="fn-sky-set_rain_intensity"></a>**Rain:** `sky.set_rain_intensity(0..1)` / `get_rain_intensity()`

<a id="fn-sky-add_light"></a><a id="fn-sky-remove_light"></a>**Lights:** `sky.add_light(x, y, z, r, g, b, radius, intensity)` returns an id. Remove it with `sky.remove_light(id)`. Torches, glowing ores, lava lamps.

**Clouds:**
- <a id="fn-clouds-set_color"></a><a id="fn-clouds-set_night_color"></a>`clouds.set_color(r,g,b)`, `set_night_color(r,g,b)`
- <a id="fn-clouds-set_cell_size"></a><a id="fn-clouds-set_height"></a><a id="fn-clouds-set_scroll_speed"></a><a id="fn-clouds-set_thickness"></a><a id="fn-clouds-set_threshold"></a>`clouds.set_height(y)`, `set_thickness(t)`, `set_cell_size(c)`, `set_threshold(t)`, `set_scroll_speed(v)`
- <a id="fn-clouds-set_texture"></a>`clouds.set_texture(path)`
- <a id="fn-clouds-set_weather"></a>`clouds.set_weather(name, seconds)` fades to a weather over that many seconds

**Fog:**
- <a id="fn-fog-set_enabled"></a><a id="fn-fog-set_exponential"></a>`fog.set_enabled(bool)`, `set_exponential(bool)`
- <a id="fn-fog-set_density"></a><a id="fn-fog-set_rain_scale"></a><a id="fn-fog-set_range"></a>`fog.set_range(start, end)`, `set_density(d)`, `set_rain_scale(s)`
- <a id="fn-fog-set_day_color"></a><a id="fn-fog-set_night_color"></a><a id="fn-fog-set_sunset_color"></a>`fog.set_day_color(r,g,b)`, `set_sunset_color(r,g,b)`, `set_night_color(r,g,b)`

**Underwater:**
- <a id="fn-underwater-set_deep_color"></a><a id="fn-underwater-set_shallow_color"></a>`underwater.set_shallow_color(r,g,b)`, `set_deep_color(r,g,b)`
- <a id="fn-underwater-set_full_depth"></a><a id="fn-underwater-set_visibility"></a>`underwater.set_visibility(shallow_blocks, deep_blocks)`, `set_full_depth(blocks)`
- <a id="fn-underwater-set_fade"></a><a id="fn-underwater-set_tint_strength"></a>`underwater.set_fade(in_seconds, out_seconds)`, `set_tint_strength(0..1)`

<a id="fn-hooks-get_water_color"></a>Define `get_water_color(x, z)` to return `{ r, g, b, deep_r, deep_g, deep_b }` per place, for example by biome.

<a id="fn-engine-is_third_person"></a><a id="fn-engine-set_fov"></a>**Camera:** `engine.set_fov(degrees)`, `engine.is_third_person()`

## 10. Survival: health, hunger, digging

The engine has the mechanics; **`mods/survival`** has Minecraft's rules, and a game can tune or replace them:
- 10 hearts and 10 hunger (with saturation)
- fall damage, drowning (with air bubbles) and lava damage
- regeneration and starvation
- no sprinting when hungry
- eating, a death screen and respawning
- `/gamemode survival|creative`

<a id="fn-engine-set_creative"></a>**Creative or survival:** `engine.set_creative(bool)`.
- Creative: blocks break with one click, placing is free, and the creative menu opens.
- Survival: you **hold** to dig, with a crack overlay that darkens in stages. Placing uses one from the stack, and dug blocks go into the hotbar.

<a id="fn-engine-override_block"></a>**How long a block takes to dig**, as `register_block` properties or with `engine.override_block(name, {...})` for blocks another mod made:

| property | meaning |
|---|---|
| `hardness` | seconds to dig by hand (0 = instant, -1 = can't be dug). If it's not set, the engine guesses from the name: dirt 0.6, logs 2, stone 1.5... |
| `tool` | the tool type that digs it fastest: `"pickaxe"`, `"axe"`, `"shovel"` |
| `needs_tool` | without that tool it's 3.3× slower and drops nothing (stone, ores) |
| `drops` | what it gives (`""` = nothing). By default it drops itself. |
| `tool_type`, `tool_speed` | on an **item**: it's a tool of that type, and digs that many times faster |

```lua
engine.register_block("iron_pickaxe", 1, 1, 1, "iron_pickaxe.png", nil,
    { inventory_image = "iron_pickaxe.png", tool_type = "pickaxe", tool_speed = 6 })
engine.override_block("grass", { drops = "dirt" })           -- like Minecraft
```

**Hooks:**
- <a id="fn-hooks-get_dig_time"></a>`get_dig_time(block, item)` returns seconds, to replace the rule.
- <a id="fn-hooks-on_dig_block"></a>`on_dig_block(x, y, z, block, item)` returns true if you handled the drops (random drops, XP...).
- <a id="fn-hooks-on_player_click"></a>`on_player_click(button)` returns true to use a click before digging or placing.

**Player functions:**
- <a id="fn-engine-get_player_state"></a>`engine.get_player_state()` gives position, velocity, `on_ground`, `sprinting`, `sneaking`, `jumping`, and `feet_fluid` / `head_fluid`.
- <a id="fn-engine-set_player_controls"></a>`engine.set_player_controls{ move, jump, sprint, sneak }` blocks keys. Prediction and the server stay in sync.
- <a id="fn-engine-add_player_velocity"></a><a id="fn-engine-set_player_position"></a>`engine.set_player_position(x, y, z)`, `engine.add_player_velocity(...)`.
- <a id="fn-engine-respawn_player"></a>`engine.respawn_player(x, y, z)`. On a server, the server's spawn is used.
- <a id="fn-engine-get_player_position"></a>`engine.get_player_position()` returns x, y, z of the feet (a quicker form of `get_player_state`).

**Game mode and blocks:**
- <a id="fn-engine-is_creative"></a>`engine.is_creative()` says which mode is on (`engine.set_creative(bool)` changes it).
- <a id="fn-engine-get_dig_progress"></a>`engine.get_dig_progress()` returns progress (0..1), x, y, z while a block is being dug in survival, or nil.
- <a id="fn-engine-get_block_def"></a>`engine.get_block_def(name)` returns `{ name, hardness, tool, needs_tool, drops, tool_type, tool_speed, solid, item, has_sounds }`.
- <a id="fn-engine-list_blocks"></a>`engine.list_blocks()` returns the names of every registered block and item.

**Saving and drawing:**
- <a id="fn-engine-get_storage"></a><a id="fn-engine-set_storage"></a>`engine.get_storage(key)` / `set_storage(key, text)` keep text with the world, like Luanti's mod storage.
- <a id="fn-engine-find_texture"></a>`engine.find_texture(name)` returns a picture's full path (or nil) without loading it.
- <a id="fn-hooks-render_hud"></a><a id="fn-hotbar-get_rect"></a><a id="fn-ui-draw_image"></a>`render_hud(W, H)` draws your HUD. `ui.draw_image(path, x, y, w, h [, u0, v0, u1, v1])` draws a picture, and `hotbar.get_rect(W, H)` says where the hotbar is.

**Your own inventory screen:**
- <a id="fn-hooks-toggle_main_inventory"></a>`toggle_main_inventory()` returns true when it's now open. While it's open the engine frees the mouse, and Escape closes it.
- <a id="fn-engine-is_inventory_open"></a><a id="fn-hooks-render_inventory"></a>`render_inventory(W, H)` draws it, with all the `ui.*` functions. `engine.is_inventory_open()` says whether it's showing.
- <a id="fn-hooks-handle_inventory_click"></a>Clicks arrive in `handle_inventory_click(mx, my, W, H, button, shift)`.
- <a id="fn-engine-set_inventory_open"></a>`engine.set_inventory_open(bool)` closes it from Lua.
- <a id="fn-hooks-on_hotbar_full"></a>`on_hotbar_full(item, count)` takes items that don't fit in the hotbar.

<a id="fn-survival-inventory-add"></a>The survival mod has a complete Minecraft-style one: 27 slots plus the hotbar, click / right-click / shift-click. Other mods can use `survival.inventory.add(item, count)` (returns how many didn't fit), <a id="fn-survival-inventory-get"></a><a id="fn-survival-inventory-set"></a><a id="fn-survival-inventory-clear"></a><a id="fn-survival-inventory-size"></a><a id="fn-survival-inventory-is_open"></a>`get(i)`, `set(i, item, count)`, `clear()`, `size()` and `is_open()`.

<a id="fn-engine-set_dig_crack_texture"></a>**The crack picture:** `engine.set_dig_crack_texture(file [, stages])` takes a vertical strip of squares. Leave out `stages` to count them from the picture. A game's `crack_anylength.png`, Luanti's name for it, is used automatically.

**Pictures are found by file name, Luanti style.** At startup the engine indexes every `.png` in the game's folder and the mods folders, so `"heart_bg.png"` works wherever that file is: any mod, any subfolder, any capitalisation. A mod's own folder is checked first, so if two mods have a file with the same name, give yours a name only your mod uses (the survival mod's defaults are `survival_*.png`).

**Survival mod API:**
- <a id="fn-survival-damage"></a><a id="fn-survival-feed"></a><a id="fn-survival-heal"></a>`survival.damage(amount, reason)`, `heal`, `feed`
- <a id="fn-survival-register_food"></a>`survival.register_food(item, food, saturation)`
- <a id="fn-survival-on_damage"></a><a id="fn-survival-on_death"></a><a id="fn-survival-on_respawn"></a>`survival.on_damage`, `on_death`, `on_respawn`
- <a id="fn-survival-set_mode"></a><a id="fn-survival-settings"></a>`survival.set_mode`, and `survival.settings` (max health, fall distance, keep inventory, lava damage...)
- <a id="fn-survival-get_hp"></a><a id="fn-survival-get_food"></a><a id="fn-survival-get_air"></a><a id="fn-survival-is_dead"></a><a id="fn-survival-get_mode"></a>`survival.get_hp()`, `get_food()`, `get_air()`, `is_dead()`, `get_mode()`: read the player's state
- <a id="fn-survival-set_armor"></a><a id="fn-survival-get_armor"></a>`survival.set_armor(points)`, `get_armor()`: armour points for an armour mod; each point blocks 4% of damage, like Minecraft
- <a id="fn-survival-respawn"></a>`survival.respawn()`: bring the player back at the world's spawn

## 11. Sound

The engine plays sound through OpenAL Soft: 3D positional effects, streamed music, and volume groups that the Settings sliders control. With no sound device, the game simply runs silently.

**Finding sounds:**
- `"my_click"` plays `my_click.ogg` or `my_click.wav` (16-bit) from any mod's folder or its `sounds/` folder, the game's `sounds/`, or `./sounds/`.
- Add `my_click.1.ogg`, `my_click.2.ogg`... and one is picked at random each time, as in Luanti.

<a id="fn-engine-set_footstep_distance"></a>**Block sounds** are played by the engine for you: footsteps every 1.3 blocks walked (`engine.set_footstep_distance(blocks)` changes it; quieter when sneaking), landings, each dig hit, breaking and placing. Other players' building in multiplayer plays too.

<a id="fn-default_sounds-sets"></a>
```lua
engine.register_block("marble", 1, 1, 1, "marble.png", nil, { sounds = {
    footstep = "my_marble_step",                     -- a name, or
    dig      = { name = "my_marble_hit", gain = 0.7, pitch = 1.1 },
    dug      = "my_marble_break",
    place    = "default_place",
} })
engine.override_block("stone", { sounds = default_sounds.sets.stone })   -- change existing blocks
```

If a block has no dig sound its footstep is used, and if it has no dug sound its dig sound is used. The `default_sounds` mod gives every block without sounds a set by material (stone, dirt, wood, sand, leaves), plus water splashes. <a id="fn-default_sounds-material"></a><a id="fn-default_sounds-rules"></a>`default_sounds.material(name)` returns the material it picks for a block name, using the `default_sounds.rules` list (`{ "leaves", "leaves" }`, `{ "sandstone", "stone" }`...), which you can add to. Its sounds are placeholders: replace the files in `mods/default_sounds/sounds/` with your own.

**From Lua:**

| function | |
|---|---|
| <a id="fn-sound-play"></a>`sound.play(name [, { pos = {x,y,z}, gain, pitch, loop, group, max_distance }])` | returns a handle. With `pos` the sound comes from that place and fades out by `max_distance` (default 24 blocks). |
| <a id="fn-sound-fade"></a><a id="fn-sound-set_gain"></a><a id="fn-sound-stop"></a>`sound.stop(h)`, `sound.fade(h, gain, seconds)`, `sound.set_gain(h, gain)`, `sound.set_pitch(h, pitch)`, <a id="fn-sound-set_pitch"></a><a id="fn-sound-set_position"></a>`sound.set_position(h, x, y, z)` | control a playing sound |
| <a id="fn-sound-exists"></a><a id="fn-sound-is_available"></a><a id="fn-sound-is_playing"></a>`sound.is_playing(h)`, `sound.exists(name)`, `sound.is_available()` | |
| <a id="fn-sound-get_music"></a><a id="fn-sound-play_music"></a><a id="fn-sound-stop_music"></a>`sound.play_music(name [, { fade, loop, gain }])`, `sound.stop_music([fade])`, `sound.get_music()` | one streamed track at a time, crossfading |
| <a id="fn-sound-get_volume"></a><a id="fn-sound-set_volume"></a>`sound.set_volume(group, 0..1)`, `sound.get_volume(group)` | groups: `master`, `music`, `effects`, `ambient`, `ui` |

**Weathers** can loop a sound: `weather.register("rain", { ..., sound = "weather_rain", sound_gain = 0.55 })`. It fades with the weather, and is quieter under a roof.

On a dedicated server, `sound.*` does nothing; each player's game plays its own sounds.

## 12. Models

<a id="fn-engine-add_model"></a><a id="fn-engine-load_model"></a><a id="fn-engine-move_model"></a><a id="fn-engine-remove_model"></a><a id="fn-engine-set_model_visible"></a>
```lua
local cow = engine.load_model("cow.b3d" [, "cow.png" [, scale]])   -- .b3d (Luanti) or .json (Bedrock)
local id = engine.add_model(cow, x, y, z [, yaw [, scale]])          -- x, y, z = feet
engine.move_model(id, x, y, z [, yaw])
engine.set_model_visible(id, bool)
engine.remove_model(id)
```

<a id="fn-hooks-on_update"></a><a id="fn-hooks-update_player_animation"></a>Use `on_update(dt)` to move them every frame. The player model is the global `g_PlayerModel`, and the engine calls `update_player_animation(dt, is_moving, pitch)`.

## 13. UI: menus and screens

Screens are drawn in Lua. The engine calls `render_<screen>(W, H)` every frame while that screen is open. `W` and `H` are the window's pixel size. Input arrives in `handle_<screen>_click(mx, my, W, H)`, `_key(key, action, mods)`, `_char(codepoint, char)` and `_scroll(dy)`.

**Drawing:**
- <a id="fn-ui-draw_rect"></a>`ui.draw_rect(x, y, w, h, r, g, b [, a])`
- <a id="fn-ui-draw_text"></a>`ui.draw_text(text, x, y [, scale] [, r, g, b [, a]])`. Text is UTF-8 and supports Latin-1 accents.
  <a id="fn-engine-draw_text"></a>`engine.draw_text(text, x, y)` is an older shortcut (white, scale 1); use `ui.draw_text` in new code.
- <a id="fn-ui-text_height"></a><a id="fn-ui-text_width"></a>`ui.text_width(text [, scale])` and `ui.text_height([scale])` give exact sizes, for centring.
- <a id="fn-ui-draw_block_icon"></a>`ui.draw_block_icon(name, x, y, size)`
- <a id="fn-ui-draw_server_icon"></a>`ui.draw_server_icon(address, x, y, size)`

**Input and info:**
- <a id="fn-ui-get_mouse"></a><a id="fn-ui-get_mouse_pos"></a><a id="fn-ui-get_time"></a>`ui.get_mouse()` returns x, y, left_down, right_down. `ui.get_mouse_pos()` and `ui.get_time()` are also there.
- <a id="fn-ui-get_all_blocks"></a><a id="fn-ui-get_block_info"></a>`ui.get_all_blocks()` and `ui.get_block_info(name)` describe blocks.
- `ui.keys.ESCAPE` / `ENTER` / `BACKSPACE` / `UP` / `DOWN`..., `ui.actions.PRESS` / `RELEASE` / `REPEAT`.

**Screens you can replace:**
- <a id="fn-hooks-handle_pause_click"></a><a id="fn-hooks-render_pause_menu"></a>`render_pause_menu` (with `handle_pause_click`)
- <a id="fn-hooks-render_mods"></a><a id="fn-hooks-render_multiplayer"></a><a id="fn-hooks-render_settings"></a><a id="fn-hooks-render_texture_packs"></a><a id="fn-hooks-render_world_select"></a>`render_settings`, `render_world_select`, `render_multiplayer`, `render_mods`, `render_texture_packs`
- <a id="fn-hooks-render_player_list"></a>`render_player_list` (hold Tab)
- <a id="fn-hooks-render_creative_inventory"></a>`render_creative_inventory`, `render_inventory`
- `on_<screen>_opened()` runs when one opens

The base mod's files (`pause_menu.lua`, `settings_menu.lua`, `multiplayer.lua` and so on) are complete examples. Copy one to start a new screen.

## 14. Hotbar

Slots are numbered from 1:
- <a id="fn-hotbar-get_slot"></a><a id="fn-hotbar-set_slot"></a>`hotbar.get_slot(i)` returns name, count. `hotbar.set_slot(i, name [, count])` sets a slot.
- <a id="fn-hotbar-get_active_slot"></a><a id="fn-hotbar-set_active_slot"></a>`hotbar.get_active_slot()` / `set_active_slot(i)`
- <a id="fn-hotbar-get_size"></a><a id="fn-hotbar-get_slot_count"></a><a id="fn-hotbar-set_position"></a><a id="fn-hotbar-set_size"></a><a id="fn-hotbar-set_slot_size"></a>`hotbar.get_slot_count()` (same as `get_size()`), `set_size(n)`, `set_slot_size(px)`, `set_position(x, y)`
- <a id="fn-hotbar-get_slot_block"></a>`hotbar.get_slot_block(i)` returns just the name in a slot ("air" if empty)
- <a id="fn-hotbar-set_selected_texture"></a><a id="fn-hotbar-set_texture"></a><a id="fn-hotbar-set_texture_scale"></a>`hotbar.set_texture(path)`, `set_selected_texture(path)`, `set_texture_scale(s)`
- <a id="fn-hotbar-is_creative_open"></a><a id="fn-hotbar-toggle_creative"></a>`hotbar.is_creative_open()`, `toggle_creative()`

## 15. Hooks the engine calls

Define any of these as globals. Only the ones you need. If another mod may already define one, keep a reference to the old function and call it, as `on_chat_command` does in section 2.

| hook | when |
|---|---|
| `set_world_seed(seed)`, `generate_chunk(chunk, cx, cz)` | world generation (section 5) |
| `on_update(dt)` | every frame |
| `on_chat_command(text, player, is_operator, reply)` → bool | a `/command` (same arguments in the game and on servers) |
| <a id="fn-hooks-on_shared_changed"></a>`on_shared_changed(key, value)` | a shared value changed (section 16) |
| `on_right_click_block(x, y, z, block, item)` → bool | right-click on a block |
| `on_use_hoe(...)`, `on_use_scythe(...)` | tool use |
| `update_player_animation(dt, moving, pitch)` | every frame in game |
| `get_water_color(x, z)` | underwater colour |
| <a id="fn-hooks-on_char_input"></a><a id="fn-hooks-on_scroll_input"></a>`on_char_input(codepoint)`, `on_scroll_input(dx, dy)` | text and mouse wheel |
| `toggle_main_inventory()`, `handle_inventory_click(...)`, `render_inventory(W, H)` | your own inventory |
| `render_*` / `handle_*` / `on_*_opened` | screens (section 13) |

`lua_api.txt` lists them all and marks the ones your loaded mods define.

## 16. Multiplayer and servers

**Shared values** let a mod decide something and have every player see it, the way the weather mod does:
- <a id="fn-engine-set_shared"></a>`engine.set_shared(key, text)` sets a value. Only whoever runs the world can: singleplayer, the host, or the dedicated server.
- <a id="fn-engine-get_shared"></a>`engine.get_shared(key)` reads one.
- `on_shared_changed(key, value)` runs on every player's game when a value changes, and when they join.
- <a id="fn-engine-is_authority"></a>`engine.is_authority()` tells your mod whether it's the one deciding.

On the dedicated server, `on_update(dt)` runs every tick. `/commands` the server doesn't know go to `on_chat_command(text, player, is_operator, reply)`, and `reply(text)` answers only that player. Commands added with `chat.register_command` show up in the server's `help`.

Mods run the same way in singleplayer, when hosting a LAN game, and on the dedicated `BlockSmithServer`. Blocks are sent to players by **name**, so the server and its players need the same mods. The server runs world generation, fluids and block changes; players see the results.

- <a id="fn-engine-is_server"></a>**On the server:** drawing, hotbar and sky functions do nothing. `engine.is_server()` is true there.
- **Server setup:** run `BlockSmithServer --setup` once, or use My Servers in the game.
- **Settings:** in `server.conf` (name, description and colours, icon, game, world, port, max players, whitelist, passwords, disabled mods, tick rate, world generation threads).
- **Operator commands:** `/op`, `/kick`, `/ban`, `/whitelist`, `/tps`, `/save`, `/stop` and more. Type `help` in the server console.

Lua functions for multiplayer menus:
- <a id="fn-engine-host_game"></a><a id="fn-engine-join_server"></a><a id="fn-engine-stop_hosting"></a>`engine.join_server`, `host_game`, `stop_hosting`
- <a id="fn-engine-get_connection_status"></a><a id="fn-engine-get_network_info"></a><a id="fn-engine-get_player_list"></a>`engine.get_connection_status`, `get_network_info`, `get_player_list`
- <a id="fn-engine-get_lan_games"></a><a id="fn-engine-get_server_status"></a><a id="fn-engine-ping_server"></a>`engine.ping_server`, `get_server_status`, `get_lan_games`
- <a id="fn-engine-list_servers"></a><a id="fn-engine-save_servers"></a>`engine.list_servers`, `save_servers`
- <a id="fn-engine-get_player_name"></a><a id="fn-engine-has_player_password"></a><a id="fn-engine-set_player_name"></a><a id="fn-engine-set_player_password"></a>`engine.get_player_name`, `set_player_name`, `set_player_password`, `has_player_password`
- <a id="fn-engine-cancel_join"></a><a id="fn-engine-is_scanning_lan"></a>`engine.cancel_join` stops connecting; `engine.is_scanning_lan` is true while looking for LAN games
- <a id="fn-engine-get_server_settings"></a><a id="fn-engine-has_server_icon"></a><a id="fn-engine-set_server_setting"></a>`engine.get_server_settings`, `set_server_setting` (Settings > Server: description, max players, port), `has_server_icon`
- <a id="fn-engine-allow_firewall"></a>`engine.allow_firewall()` asks Windows again to let other PCs connect (for a "friends can't join" button)

## 17. Engine screens and settings

Mostly used by the base mod's menus. You'll only need these to replace those menus:
- <a id="fn-engine-create_world"></a><a id="fn-engine-delete_world"></a><a id="fn-engine-get_active_world"></a><a id="fn-engine-get_game_info"></a><a id="fn-engine-get_worlds"></a><a id="fn-engine-list_worlds"></a><a id="fn-engine-play_world"></a><a id="fn-engine-save_world"></a><a id="fn-engine-set_active_world"></a><a id="fn-engine-start_game"></a>**Worlds:** `engine.list_worlds`, `create_world`, `play_world`, `delete_world`, `save_world`, `get_game_info`, and the older `get_worlds`, `get_active_world`, `set_active_world`, `start_game`
- <a id="fn-engine-get_keybinds"></a><a id="fn-engine-get_video_settings"></a><a id="fn-engine-key_name"></a><a id="fn-engine-reset_keybinds"></a><a id="fn-engine-set_keybind"></a><a id="fn-engine-set_video_setting"></a>**Settings:** `engine.get_keybinds`, `set_keybind`, `reset_keybinds`, `key_name`, `get_video_settings`, `set_video_setting`
- <a id="fn-engine-back_to_title"></a><a id="fn-engine-close_settings"></a><a id="fn-engine-is_in_game"></a><a id="fn-engine-is_paused"></a><a id="fn-engine-open_settings"></a><a id="fn-engine-pause"></a><a id="fn-engine-quit"></a><a id="fn-engine-quit_to_menu"></a><a id="fn-engine-resume"></a>**Game flow:** `engine.open_settings`, `close_settings`, `pause`, `resume`, `is_paused`, `is_in_game`, `quit_to_menu`, `back_to_title`, `quit`
- <a id="fn-engine-get_texture_pack"></a><a id="fn-engine-list_mods"></a><a id="fn-engine-list_texture_packs"></a><a id="fn-engine-open_mods_folder"></a><a id="fn-engine-open_texture_packs_folder"></a><a id="fn-engine-set_mod_enabled"></a><a id="fn-engine-set_texture_pack"></a>**Mods and texture packs:** `engine.list_mods`, `set_mod_enabled`, `open_mods_folder`, `list_texture_packs`, `set_texture_pack`, `get_texture_pack`, `open_texture_packs_folder`
- <a id="fn-engine-create_server_profile"></a><a id="fn-engine-delete_server_profile"></a><a id="fn-engine-get_server_profile"></a><a id="fn-engine-has_server_build"></a><a id="fn-engine-list_games"></a><a id="fn-engine-list_mods_for_game"></a><a id="fn-engine-list_server_profiles"></a><a id="fn-engine-open_server_profile_folder"></a><a id="fn-engine-save_server_profile"></a><a id="fn-engine-start_server_profile"></a><a id="fn-engine-stop_server_profile"></a>**Dedicated servers (My Servers):** `engine.list_games`, `list_server_profiles`, `get_server_profile`, `save_server_profile`, `create_server_profile`, `delete_server_profile`, `start_server_profile`, `stop_server_profile`, `list_mods_for_game`, `open_server_profile_folder`, `has_server_build`
- <a id="fn-menu-get_active_game"></a><a id="fn-menu-get_games"></a><a id="fn-menu-set_active_game"></a>**Choosing a game:** `menu.get_games`, `get_active_game`, `set_active_game`

## 18. Tips

- **Errors** go to the console with the file and line, and to the Mods screen. A broken mod doesn't stop the others.
- **F3** in-game shows the block and fluid you're looking at, with its id and metadata.
- **Many changes at once:** use `engine.set_block` freely, because changes are batched to other players each tick.
- <a id="fn-buckets-register"></a>**Mods working together:** a mod can offer functions to others in a global table, like `buckets.register(...)`. Check it exists before using it (`if buckets then ... end`), since load order isn't guaranteed. These tables appear in `lua_api.txt`.
