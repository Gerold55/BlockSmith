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
> disagree, trust `lua_api.txt`. Rewrite it at any time with `engine.write_api_reference()`.

Contents:

1. [Folders: games, mods, texture packs](#1-folders)
2. [Your first mod](#2-your-first-mod)
3. [Blocks and items](#3-blocks-and-items)
4. [Changing the world](#4-changing-the-world)
5. [World generation](#5-world-generation)
6. [Items that do things](#6-items-that-do-things)
7. [Fluids](#7-fluids)
8. [Chat commands](#8-chat-commands)
9. [Sky, weather, fog, clouds, underwater](#9-sky-weather-fog-clouds-underwater)
10. [Models](#10-models)
11. [UI: menus and screens](#11-ui-menus-and-screens)
12. [Hotbar](#12-hotbar)
13. [Hooks the engine calls](#13-hooks-the-engine-calls)
14. [Multiplayer and dedicated servers](#14-multiplayer-and-servers)
15. [Engine screens and settings (advanced)](#15-engine-screens-and-settings)
16. [Tips](#16-tips)

---

## 1. Folders

```
BlockSmith.exe
BlockSmithServer.exe            dedicated server (see section 14)
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
| `engine.get_block_id(name)` | block id, or 0 if there's no such block |
| `engine.get_block_name(x, y, z)` | `"stone"`, `"air"`... |
| `engine.set_block(x, y, z, name)` | changes a block (it is saved, sent to other players, and fluids react) |
| `engine.get_metadata(x, y, z)` | 0–255 |
| `engine.set_block_meta(x, y, z, name, meta)` | block and metadata in one change |
| `engine.is_replaceable(x, y, z)` | air, a fluid or a plant: there's room to put something |
| `world.set_render_distance(chunks)` / `get_render_distance()` | |
| `world.set_caves_enabled(bool)` / `get_caves_enabled()` | built-in caves (turn them off if your mapgen makes its own) |
| `world.get_loaded_chunks()` / `get_pending_chunks()` | |
| `world.set_load_budget(ms)` / `set_mesh_budget(ms)` | how much time per frame goes to loading and meshing |

## 5. World generation

Define these globals. The engine calls them.

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

`chunk` provides `set_block(x, y, z, id)`, `get_block(x, y, z)`,
`set_block_with_meta(x, y, z, id, meta)`, `set_metadata(x, y, z, meta)` and
`get_metadata(x, y, z)`. Coordinates are 0–15 across and 0–255 up.

**Important:**
- `generate_chunk` runs on **background threads**, each with its own copy of your mod files.
- On those threads only world-generation functions work. `engine.get_block_id` and math work. Drawing, chat, `engine.set_block` and the hotbar do nothing.
- Don't rely on global variables changed elsewhere at runtime. Compute everything from the seed and the coordinates.
- With no `generate_chunk`, the engine makes simple built-in terrain.
- After your terrain, the engine carves caves unless you turn them off.

## 6. Items that do things

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

`engine.give_item(name [, count])` adds items to the hotbar and returns false if they didn't fit. `mods/buckets` is a complete example.

Older hooks still work:
- `on_right_click_block(x, y, z, block, held_item)`: return true to stop placing
- `on_use_hoe(x, y, z, block)`
- `on_use_scythe(x, y, z, block)`

## 7. Fluids

Any fluid (lava, molten metal, oil) flows like Minecraft water, with its own spread distance, speed, reactions and bucket:

```lua
engine.register_fluid{ name = "lava", source = "lava", flowing = "flowing_lava",
                       spread = 3, tick = 1.5, infinite = false }
engine.register_fluid_reaction{ fluid = "lava", touching = "water",
                                source_becomes = "obsidian", flowing_becomes = "cobblestone" }
```

`engine.get_fluid(x, y, z)`, `engine.is_fluid_source(x, y, z)` and `engine.list_fluids()` read fluids. The full guide is **`docs/FLUIDS.md`**. `mods/lava` is a complete example.

## 8. Chat commands

```lua
chat.register_command(name, usage, description)   -- listed in /help
chat.send(text [, r, g, b])                       -- a message in your own chat
engine.send_chat(text)                            -- a message to everyone (multiplayer)
function on_chat_command(text) ... end            -- "/give stone 5" arrives as "give stone 5"
```

In multiplayer, commands the client doesn't handle go to the server. Server operators have their own commands there (see section 14).

## 9. Sky, weather, fog, clouds, underwater

**Weather** is decided entirely in Lua by `mods/weather`. It's Minecraft-like by default: long clear spells, rain, thunderstorms, and snow where your game says it's cold. The engine only draws what that mod asks for:

| function | |
|---|---|
| `sky.set_precipitation{ type, intensity, fall_speed, size, length, sway, wind_x, wind_z, color = {r,g,b,a}, texture, radius, height, max_particles, streaks }` | rain, snow or anything else (ash, leaves) with your own picture. `type = "rain"` or `"snow"` starts from a preset; other fields change only what you give. Particles stop on roofs and water. |
| `sky.get_precipitation()` | `{ type, intensity }` |
| `sky.set_sky_tint(r, g, b, a)` | colour laid over the sky (grey rain, dark storm) |
| `sky.set_light(level)` / `get_light()` | sunlight on the world: 1 = normal, 0.5 = stormy |
| `sky.flash(strength, seconds)` | a lightning flash |
| `engine.is_outdoors(x, y, z)` | nothing above, so rain reaches it |

Using the weather mod:

```lua
weather.register("sandstorm", { light = 0.7, sky_tint = { 0.8, 0.65, 0.45, 0.6 },
    precipitation = { type = "dust", texture = "sand.png", intensity = 0.7, streaks = false },
    wind = { 6, 1 }, chance = 5, duration = { 120, 300 } })
weather.set("rain", 10)          -- fade over 10 s (singleplayer, host or server only)
weather.get(), weather.list(), weather.set_cycle(false)
weather.on_change(function(old, new) end)
weather.on_lightning(function(x, z) end)
weather.is_cold = function(x, z) return my_temperature(x, z) < 0.25 end   -- rain becomes snow
```

The `/weather [type [seconds]] | list | cycle on|off` command works in singleplayer and for operators on servers.

**Sky:**
- `sky.set_time(t)` / `get_time()`. Time runs from 0 to 1 over a whole day: `0.75` is midday and `0.25` is midnight (the base mod's `/day` and `/night` use these).
- `sky.set_moon_phase(n)` / `get_moon_phase()`, `sky.set_moon_textures({ ... })`
- `engine.set_sun_texture(path)`, `engine.set_moon_texture(path)`
- `sky.set_sun_color(r,g,b)`, `set_ambient_color(r,g,b)`, `set_fog_color(r,g,b)`
- `sky.set_sun_intensity(v)`, `set_ambient_intensity(v)`
- `sky.reset()`

**Rain:** `sky.set_rain_intensity(0..1)` / `get_rain_intensity()`

**Lights:** `sky.add_light(x, y, z, r, g, b, radius, intensity)` returns an id. Remove it with `sky.remove_light(id)`. Torches, glowing ores, lava lamps.

**Clouds:**
- `clouds.set_color(r,g,b)`, `set_night_color(r,g,b)`
- `clouds.set_height(y)`, `set_thickness(t)`, `set_cell_size(c)`, `set_threshold(t)`, `set_scroll_speed(v)`
- `clouds.set_texture(path)`
- `clouds.set_weather(name, seconds)` fades to a weather over that many seconds

**Fog:**
- `fog.set_enabled(bool)`, `set_exponential(bool)`
- `fog.set_range(start, end)`, `set_density(d)`, `set_rain_scale(s)`
- `fog.set_day_color(r,g,b)`, `set_sunset_color(r,g,b)`, `set_night_color(r,g,b)`

**Underwater:**
- `underwater.set_shallow_color(r,g,b)`, `set_deep_color(r,g,b)`
- `underwater.set_visibility(shallow_blocks, deep_blocks)`, `set_full_depth(blocks)`
- `underwater.set_fade(in_seconds, out_seconds)`, `set_tint_strength(0..1)`

Define `get_water_color(x, z)` to return `{ r, g, b, deep_r, deep_g, deep_b }` per place, for example by biome.

**Camera:** `engine.set_fov(degrees)`, `engine.is_third_person()`

## Survival: health, hunger, digging

The engine has the mechanics; **`mods/survival`** has Minecraft's rules, and a game can tune or replace them:
- 10 hearts and 10 hunger (with saturation)
- fall damage, drowning (with air bubbles) and lava damage
- regeneration and starvation
- no sprinting when hungry
- eating, a death screen and respawning
- `/gamemode survival|creative`

**Creative or survival:** `engine.set_creative(bool)`.
- Creative: blocks break with one click, placing is free, and the creative menu opens.
- Survival: you **hold** to dig, with a crack overlay that darkens in stages. Placing uses one from the stack, and dug blocks go into the hotbar.

**How long a block takes to dig**, as `register_block` properties or with `engine.override_block(name, {...})` for blocks another mod made:

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
- `get_dig_time(block, item)` returns seconds, to replace the rule.
- `on_dig_block(x, y, z, block, item)` returns true if you handled the drops (random drops, XP...).
- `on_player_click(button)` returns true to use a click before digging or placing.

**Player functions:**
- `engine.get_player_state()` gives position, velocity, `on_ground`, `sprinting`, `sneaking`, `jumping`, and `feet_fluid` / `head_fluid`.
- `engine.set_player_controls{ move, jump, sprint, sneak }` blocks keys. Prediction and the server stay in sync.
- `engine.set_player_position(x, y, z)`, `engine.add_player_velocity(...)`.
- `engine.respawn_player(x, y, z)`. On a server, the server's spawn is used.

**Saving and drawing:**
- `engine.get_storage(key)` / `set_storage(key, text)` keep text with the world, like Luanti's mod storage.

**Saving and drawing:**
- `engine.get_storage(key)` / `set_storage(key, text)` keep text with the world, like Luanti's mod storage.
- `render_hud(W, H)` draws your HUD. `ui.draw_image(path, x, y, w, h [, u0, v0, u1, v1])` draws a picture, and `hotbar.get_rect(W, H)` says where the hotbar is.

**Your own inventory screen:**
- `toggle_main_inventory()` returns true when it's now open. While it's open the engine frees the mouse, and Escape closes it.
- `render_inventory(W, H)` draws it, with all the `ui.*` functions.
- Clicks arrive in `handle_inventory_click(mx, my, W, H, button, shift)`.
- `engine.set_inventory_open(bool)` closes it from Lua.
- `on_hotbar_full(item, count)` takes items that don't fit in the hotbar.

The survival mod has a complete Minecraft-style one: 27 slots plus the hotbar, click / right-click / shift-click. Other mods can use `survival.inventory.add / get / set / clear`.

**The crack picture:** `engine.set_dig_crack_texture(file [, stages])` takes a vertical strip of squares. Leave out `stages` to count them from the picture. A game's `crack_anylength.png`, Luanti's name for it, is used automatically.

**HUD pictures** are drawn long after mods load, so give `ui.draw_image` a full path to anything in your mod's folder (the survival mod does this for you). Plain names are looked for in the game's textures folder.

**Survival mod API:**
- `survival.damage(amount, reason)`, `heal`, `feed`
- `survival.register_food(item, food, saturation)`
- `survival.on_damage`, `on_death`, `on_respawn`
- `survival.set_mode`, and `survival.settings` (max health, fall distance, keep inventory, lava damage...)

## 10. Models

```lua
local cow = engine.load_model("cow.b3d" [, "cow.png" [, scale]])   -- .b3d (Luanti) or .json (Bedrock)
local id = engine.add_model(cow, x, y, z [, yaw [, scale]])          -- x, y, z = feet
engine.move_model(id, x, y, z [, yaw])
engine.set_model_visible(id, bool)
engine.remove_model(id)
```

Use `on_update(dt)` to move them every frame. The player model is the global `g_PlayerModel`, and the engine calls `update_player_animation(dt, is_moving, pitch)`.

## 11. UI: menus and screens

Screens are drawn in Lua. The engine calls `render_<screen>(W, H)` every frame while that screen is open. `W` and `H` are the window's pixel size. Input arrives in `handle_<screen>_click(mx, my, W, H)`, `_key(key, action, mods)`, `_char(codepoint, char)` and `_scroll(dy)`.

**Drawing:**
- `ui.draw_rect(x, y, w, h, r, g, b [, a])`
- `ui.draw_text(text, x, y [, scale] [, r, g, b [, a]])`. Text is UTF-8 and supports Latin-1 accents.
- `ui.text_width(text [, scale])` and `ui.text_height([scale])` give exact sizes, for centring.
- `ui.draw_block_icon(name, x, y, size)`
- `ui.draw_server_icon(address, x, y, size)`

**Input and info:**
- `ui.get_mouse()` returns x, y, left_down, right_down. `ui.get_mouse_pos()` and `ui.get_time()` are also there.
- `ui.get_all_blocks()` and `ui.get_block_info(name)` describe blocks.
- `ui.keys.ESCAPE` / `ENTER` / `BACKSPACE` / `UP` / `DOWN`..., `ui.actions.PRESS` / `RELEASE` / `REPEAT`.

**Screens you can replace:**
- `render_pause_menu` (with `handle_pause_click`)
- `render_settings`, `render_world_select`, `render_multiplayer`, `render_mods`, `render_texture_packs`
- `render_player_list` (hold Tab)
- `render_creative_inventory`, `render_inventory`
- `on_<screen>_opened()` runs when one opens

The base mod's files (`pause_menu.lua`, `settings_menu.lua`, `multiplayer.lua` and so on) are complete examples. Copy one to start a new screen.

## 12. Hotbar

Slots are numbered from 1:
- `hotbar.get_slot(i)` returns name, count. `hotbar.set_slot(i, name [, count])` sets a slot.
- `hotbar.get_active_slot()` / `set_active_slot(i)`
- `hotbar.get_slot_count()`, `set_size(n)`, `set_slot_size(px)`, `set_position(x, y)`
- `hotbar.set_texture(path)`, `set_selected_texture(path)`, `set_texture_scale(s)`
- `hotbar.is_creative_open()`, `toggle_creative()`

## 13. Hooks the engine calls

Define any of these as globals. Only the ones you need. If another mod may already define one, keep a reference to the old function and call it, as `on_chat_command` does in section 2.

| hook | when |
|---|---|
| `set_world_seed(seed)`, `generate_chunk(chunk, cx, cz)` | world generation (section 5) |
| `on_update(dt)` | every frame |
| `on_chat_command(text, player, is_operator, reply)` → bool | a `/command` (same arguments in the game and on servers) |
| `on_shared_changed(key, value)` | a shared value changed (section 14) |
| `on_right_click_block(x, y, z, block, item)` → bool | right-click on a block |
| `on_use_hoe(...)`, `on_use_scythe(...)` | tool use |
| `update_player_animation(dt, moving, pitch)` | every frame in game |
| `get_water_color(x, z)` | underwater colour |
| `on_char_input(codepoint)`, `on_scroll_input(dx, dy)` | text and mouse wheel |
| `toggle_main_inventory()`, `handle_inventory_click(...)`, `render_inventory(W, H)` | your own inventory |
| `render_*` / `handle_*` / `on_*_opened` | screens (section 11) |

`lua_api.txt` lists them all and marks the ones your loaded mods define.

## 14. Multiplayer and servers

**Shared values** let a mod decide something and have every player see it, the way the weather mod does:
- `engine.set_shared(key, text)` sets a value. Only whoever runs the world can: singleplayer, the host, or the dedicated server.
- `engine.get_shared(key)` reads one.
- `on_shared_changed(key, value)` runs on every player's game when a value changes, and when they join.
- `engine.is_authority()` tells your mod whether it's the one deciding.

On the dedicated server, `on_update(dt)` runs every tick. `/commands` the server doesn't know go to `on_chat_command(text, player, is_operator, reply)`, and `reply(text)` answers only that player. Commands added with `chat.register_command` show up in the server's `help`.

Mods run the same way in singleplayer, when hosting a LAN game, and on the dedicated `BlockSmithServer`. Blocks are sent to players by **name**, so the server and its players need the same mods. The server runs world generation, fluids and block changes; players see the results.

- **On the server:** drawing, hotbar and sky functions do nothing. `engine.is_server()` is true there.
- **Server setup:** run `BlockSmithServer --setup` once, or use My Servers in the game.
- **Settings:** in `server.conf` (name, description and colours, icon, game, world, port, max players, whitelist, passwords, disabled mods, tick rate, world generation threads).
- **Operator commands:** `/op`, `/kick`, `/ban`, `/whitelist`, `/tps`, `/save`, `/stop` and more. Type `help` in the server console.

Lua functions for multiplayer menus:
- `engine.join_server`, `host_game`, `stop_hosting`
- `engine.get_connection_status`, `get_network_info`, `get_player_list`
- `engine.ping_server`, `get_server_status`, `get_lan_games`
- `engine.list_servers`, `save_servers`
- `engine.get_player_name`, `set_player_name`, `set_player_password`

## 15. Engine screens and settings

Mostly used by the base mod's menus. You'll only need these to replace those menus:
- **Worlds:** `engine.list_worlds`, `create_world`, `play_world`, `delete_world`, `save_world`, `get_game_info`
- **Settings:** `engine.get_keybinds`, `set_keybind`, `reset_keybinds`, `key_name`, `get_video_settings`, `set_video_setting`
- **Game flow:** `engine.open_settings`, `close_settings`, `pause`, `resume`, `is_paused`, `is_in_game`, `quit_to_menu`, `back_to_title`, `quit`
- **Mods and texture packs:** `engine.list_mods`, `set_mod_enabled`, `open_mods_folder`, `list_texture_packs`, `set_texture_pack`, `get_texture_pack`
- **Dedicated servers (My Servers):** `engine.list_games`, `list_server_profiles`, `get_server_profile`, `save_server_profile`, `create_server_profile`, `delete_server_profile`, `start_server_profile`, `stop_server_profile`, `list_mods_for_game`
- **Choosing a game:** `menu.get_games`, `get_active_game`, `set_active_game`

## 16. Tips

- **Errors** go to the console with the file and line, and to the Mods screen. A broken mod doesn't stop the others.
- **F3** in-game shows the block and fluid you're looking at, with its id and metadata.
- **Many changes at once:** use `engine.set_block` freely, because changes are batched to other players each tick.
- **Mods working together:** a mod can offer functions to others in a global table, like `buckets.register(...)`. Check it exists before using it (`if buckets then ... end`), since load order isn't guaranteed. These tables appear in `lua_api.txt`.
