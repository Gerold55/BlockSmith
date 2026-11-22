# Making games and mods with BlockSmith

BlockSmith is a voxel engine. Everything that makes it a *game* is written in Lua: blocks and items, world generation, menus and the HUD, chat commands, fluids, weather and the sky. You can build a whole new game, or add a mod to an existing one, without touching C++.

**This guide has three parts:**

| Part | Read it when | What's in it |
|---|---|---|
| [Part 1: Learn](#part-1-learn) | you're new to BlockSmith modding | how mods run, a first mod step by step, hooks and chaining, blocks and metadata, multiplayer, debugging, client mods |
| [Part 2: Cookbook](#part-2-cookbook) | you want to make something specific | 19 complete mods to copy: blocks, ores, lamps, food, tools, armour, items, commands, HUD, screens, weather, mobs, terrain, timers, a client mod |
| [Part 3: Reference](#part-3-reference) | you need the details of a function | every table and function, the hooks, and an A–Z function index |

**New to Lua?** You need only the basics: variables (`local x = 5`), tables (`{ a = 1 }`), `if`, `for` and `function`. The free book [*Programming in Lua*](https://www.lua.org/pil/contents.html) covers them in its first chapters. BlockSmith uses Lua 5.4.

> **Always up to date:** at startup the engine writes **`lua_api.txt`** next to the
> executable. It lists every function your build has, which hooks your mods use, the APIs
> other mods offer, and every block and fluid. If this guide and `lua_api.txt` ever
> disagree, trust `lua_api.txt`. Rewrite it at any time with <a id="fn-engine-write_api_reference"></a>`engine.write_api_reference()`.

> **Reading this nicely:** open **`MODDING.html`** (next to this file) in any web browser. It has a contents sidebar, a function search box and a dark theme.

## Contents

1. [Part 1: Learn](#part-1-learn)
   - [L1. How BlockSmith runs your mod](#l1-how-blocksmith-runs-your-mod)
   - [L2. Your first mod, step by step](#l2-your-first-mod-step-by-step)
   - [L3. Hooks, and why you must chain them](#l3-hooks-and-why-you-must-chain-them)
   - [L4. Blocks, items, names and metadata](#l4-blocks-items-names-and-metadata)
   - [L5. Coordinates and chunks](#l5-coordinates-and-chunks)
   - [L6. Pictures, sounds and models: how files are found](#l6-pictures-sounds-and-models-how-files-are-found)
   - [L7. Who decides? Singleplayer, LAN and servers](#l7-who-decides-singleplayer-lan-and-servers)
   - [L8. Mods working together, and saving data](#l8-mods-working-together-and-saving-data)
   - [L9. Debugging when something goes wrong](#l9-debugging-when-something-goes-wrong)
   - [L10. Client mods: scripts for your own game](#l10-client-mods-scripts-for-your-own-game)
   - [L11. Screens and menus: how BlockSmith UI works](#l11-screens-and-menus-how-blocksmith-ui-works)
2. [Part 2: Cookbook](#part-2-cookbook)
   - [C1. A decorative block with different faces](#c1-a-decorative-block-with-different-faces)
   - [C2. A glowing ore that generates underground](#c2-a-glowing-ore-that-generates-underground)
   - [C3. A lamp you switch on and off](#c3-a-lamp-you-switch-on-and-off)
   - [C4. Food](#c4-food)
   - [C5. A tool](#c5-a-tool)
   - [C6. Armour](#c6-armour)
   - [C7. Crafting recipes](#c7-crafting-recipes)
   - [C8. A jump pad](#c8-a-jump-pad)
   - [C9. An item that does something](#c9-an-item-that-does-something)
   - [C10. A chat command with arguments and permissions](#c10-a-chat-command-with-arguments-and-permissions)
   - [C11. A HUD piece](#c11-a-hud-piece)
   - [C12. Your own screen](#c12-your-own-screen)
   - [C13. A new weather](#c13-a-new-weather)
   - [C14. A mob with a model and an animation](#c14-a-mob-with-a-model-and-an-animation)
   - [C15. Your own terrain](#c15-your-own-terrain)
   - [C16. Doing something later (timers)](#c16-doing-something-later-timers)
   - [C17. Growing plants (world changes over time)](#c17-growing-plants-world-changes-over-time)
   - [C18. A light that follows something](#c18-a-light-that-follows-something)
   - [C19. A client mod: light level and coordinates](#c19-a-client-mod-light-level-and-coordinates)
   - [C20. A rearrangeable inventory (drag and drop)](#c20-a-rearrangeable-inventory-drag-and-drop)
   - [C21. A laptop with its own desktop (windows)](#c21-a-laptop-with-its-own-desktop-windows)
3. [Part 3: Reference](#part-3-reference)
   - [Function index](#function-index): every function and hook, A to Z per table
   - [1. Folders](#1-folders)
   - [2. Your first mod](#2-your-first-mod)
   - [3. Blocks and items](#3-blocks-and-items)
   - [4. Changing the world](#4-changing-the-world) (and [random ticks, ABMs, LBMs](#abm))
   - [5. World generation](#5-world-generation)
   - [6. Items that do things](#6-items-that-do-things)
   - [7. Fluids](#7-fluids)
   - [8. Chat commands](#8-chat-commands)
   - [9. Sky, weather, fog, clouds, underwater](#9-sky-weather-fog-clouds-underwater)
   - [10. Light](#10-light)
   - [11. Survival: health, hunger, digging](#11-survival-health-hunger-digging)
   - [12. Sound](#12-sound)
   - [13. Models](#13-models)
   - [13a. Mobs and other entities](#13a-mobs-and-other-entities)
   - [14. UI: menus and screens](#14-ui-menus-and-screens)
   - [15. Hotbar](#15-hotbar)
   - [16. Hooks the engine calls](#16-hooks-the-engine-calls)
   - [17. Multiplayer and servers](#17-multiplayer-and-servers)
   - [18. Engine screens and settings](#18-engine-screens-and-settings)
   - [19. Tips](#19-tips)
   - [20. Client mods](#20-client-mods)
   - [21. Online content](#21-online-content)
   - [22. Glossary](#22-glossary)

---

# Part 1: Learn

This part explains how modding works, one idea at a time. Read it once from top to bottom. Each chapter builds on the one before.

## L1. How BlockSmith runs your mod

BlockSmith is split in two:

- **The engine** (C++) handles drawing, physics, light, sound, networking and saving. It provides *mechanics*: "a block can give off light", "a player can take damage", "a picture can be drawn here".
- **Lua mods** decide what the game *is*: which blocks exist, how much a fall hurts, what the HUD looks like, how terrain is shaped. Craft & Ruin, the default game, is just a set of Lua mods.

When you start a world, this happens in order:

1. The engine starts with no blocks and no rules.
2. It runs the **global mods** in `mods/` that are switched on for this world (see [Which mods a world runs](#which-mods-a-world-runs)), in alphabetical order by folder name.
3. It runs the **game's mods** in `games/<game>/mods/`, also alphabetically.
   - Folder names are sorted the same way on every computer, ignoring capitals.
   - Mods with an `init.lua` run first, then single-file mods (a loose `something.lua` in `mods/`).
   - A mod whose `mod.conf` says `depends = other_mod` runs after `other_mod`, wherever that is.
4. It writes `lua_api.txt` next to the executable, listing everything that's loaded.
5. The world loads, and the engine starts calling your **hooks** (the functions you defined) as things happen.

A mod's code runs at two different times, and it matters which one you're in:

| When | What you do there | Example |
|---|---|---|
| **Load time:** the top level of your file, running once at startup | *Register* things: blocks, recipes, commands, sounds, settings | `engine.register_block(...)` |
| **Run time:** inside hooks the engine calls later | *React* to things: a click, a frame, a new chunk | `function on_update(dt) ... end` |

> **Rule of thumb:** register at load time, act at run time. Calling `engine.set_block` at the top level of your file does nothing useful, because there's no world yet.

<a id="which-mods-a-world-runs"></a>
### Which mods a world runs

Like Luanti, each world has its own mod list; like Minecraft, a game sets the defaults for its worlds.

- **A game's own mods** (`games/<game>/mods/`) always run with that game.
- **Mods in the `mods/` folder** run only where they're switched on:
  - **For a game:** tick it on the **Mods** screen on the title screen. Every world of that game runs it, unless the world chose otherwise. Saved in `mods/game_mods_<game>.txt`.
  - **For one world:** pick the world under **Singleplayer** and click **Mods**. Tick or untick mods for that world alone; **Use Game's Mods** clears its own choices. Saved in `saves/<world>/world_mods.txt`. A mod the world doesn't mention follows the game.
  - A mod neither list mentions is **off**, so installing a new mod changes no world until you switch it on.
  - `base`, and any mod whose `mod.conf` says `required = true`, always runs.
- **No restart.** Opening a world whose mods (or game) differ from the ones loaded reloads everything behind the loading screen, then opens the world. All mods start again from scratch, exactly as at startup: the Lua state, blocks, items, textures, sounds and entities are made anew, so a mod needs nothing special to support it. Worlds save blocks by name, so a world opened with a mod missing just shows air where its blocks were.
- Upgrading from an older BlockSmith: the first time a game's list is needed it starts from `mods/mod_settings.txt` (every mod on unless switched off there), so existing worlds keep the mods they had.
- Client mods (`clientmods/`) are separate: they're yours, not the world's (see [L10](#l10-client-mods-scripts-for-your-own-game)).

## L2. Your first mod, step by step

We'll make a mod that adds a ruby block and a `/ruby` command that gives you some.

**Step 1: make the folder.** Create `mods/ruby/`. The folder name is the mod's name: use lowercase letters, digits and underscores.

**Step 2: describe it.** Create `mods/ruby/mod.conf`:

```
title = Ruby
author = You
description = A shiny red block.
version = 1.0
```

**Step 3: add a picture.** Draw a 16×16 PNG called `ruby_block.png` and put it in `mods/ruby/textures/`.

**Step 4: write the code.** Create `mods/ruby/init.lua`:

```lua
-- Load time: register the block
engine.register_block("ruby_block", 1, 1, 1, "ruby_block.png", nil, {
    hardness = 3,          -- seconds to dig by hand
    tool = "pickaxe",      -- a pickaxe digs it faster
})

-- Load time: list the command in /help
chat.register_command("ruby", "/ruby [count]", "Gives you ruby blocks")

-- Run time: answer the command when someone types it
local previous = on_chat_command
function on_chat_command(text, player, is_operator, reply)
    local count = text:match("^ruby%s*(%d*)$")
    if count then
        engine.give_item("ruby_block", tonumber(count) or 8)
        chat.send("Here you go!", 1, 0.3, 0.3)
        return true
    end
    if previous then return previous(text, player, is_operator, reply) end
    return false
end

print("[Ruby] loaded")
```

**Step 5: try it.**
1. Start BlockSmith and open a world.
2. Look in the console for `[Ruby] loaded`.
3. Type `/ruby 5` in chat. Five ruby blocks appear in your hotbar.
4. In creative mode, the block is also in the creative inventory.

**Step 6: if it doesn't work,** see [Debugging](#l9-debugging-when-something-goes-wrong). A typo in Lua shows up in the console with the file name and line number, and on the Mods screen.

What you just used:
- `engine.register_block`: see [Reference §3](#3-blocks-and-items)
- `chat.register_command`, `on_chat_command`: see [Reference §8](#8-chat-commands)
- `engine.give_item`: see [Reference §6](#6-items-that-do-things)
- The `previous` pattern: explained in the next chapter. **It is the most important thing in this guide.**

## L3. Hooks, and why you must chain them

A **hook** is a global function with a special name, like `on_update` or `on_right_click_block`. The engine calls it when something happens.

Every mod shares the same global names, so there is only one `on_right_click_block` at a time. If two mods both write

```lua
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click)
    -- this mod's code
end
```

the second one **replaces** the first, and the first mod silently stops working.

> **A real bug this caused:** the farming mod defined `on_right_click_block` for its hoe without keeping the old one. The doors mod had loaded first, so after farming loaded, doors could no longer be placed or opened. Nothing printed an error; doors just did nothing.

**The fix: chaining.** Before you define a hook, save the one that's already there. Then call it when your mod isn't interested:

```lua
local previous = on_right_click_block               -- 1. remember whoever was there
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if block == "my_button" then                    -- 2. handle what's yours
        chat.send("Click!")
        return true                                 --    true = handled, stop here
    end
    if previous then                                -- 3. pass everything else on
        return previous(x, y, z, block, held, sneaking, repeat_click, ...)
    end
    return false
end
```

Copy this shape for **every** hook. A few details:

- **Pass every argument on, plus `...`.** If the engine adds a new argument later, your chain still forwards it.
- **Hooks that return something** (`true` = handled) should return what `previous` returned.
- **Hooks that just do work** (`on_update`, `render_hud`, `on_block_broken`) should call `previous` *and* do their own work:

```lua
local previous_update = on_update
function on_update(dt)
    if previous_update then previous_update(dt) end
    -- your per-frame work here
end
```

- **Errors in one mod shouldn't break the others.** For hooks that run every frame, you can wrap the old one: `if previous_update then pcall(previous_update, dt) end`.

**Load order matters for chaining.** Mods load alphabetically, global mods first. The *last* mod to load gets called *first*, and it decides whether to pass the call on. If your mod must load after another one, list it in `mod.conf` and the engine will load it after those mods. Use `optional_depends` for mods that may not be installed:

```
depends = survival
optional_depends = doors, weather
```

The full list of hooks is in [Reference §16](#16-hooks-the-engine-calls).

## L4. Blocks, items, names and metadata

**Everything in a hotbar is a block.** An *item* (a pickaxe, an apple, a bucket) is just a block registered with `inventory_image`. That makes it show as a flat picture, and right-clicking never places it.

```lua
engine.register_block("apple", 1, 1, 1, "apple.png", nil, { inventory_image = "apple.png" })
```

**Names and ids.**
- You always refer to blocks by **name**, such as `"stone"` or `"ruby_block"`.
- Inside, each block also gets an **id** from 1 to 255 (`"air"` is 0). `engine.get_block_id(name)` returns it, or 0 if there's no such block.
- Worlds are saved by name, and multiplayer sends names, so ids can change between runs without breaking anything. Never save an id; save the name.
- Use ids only where speed matters, mainly in `generate_chunk`.

**Metadata.** Every block in the world also has one extra number from 0 to 255, its *metadata*. The block decides what it means:

| Block | What its metadata means |
|---|---|
| Water | flow level |
| Plants | bit 1 = waterlogged |
| Doors | facing (bits 0–1), open (4), upper half (8), hinge side (16) |
| Leaves | bit 128 = placed by a player (never decays) |
| Your blocks | anything you like: a growth stage, a colour, a direction |

Read it with `engine.get_metadata(x, y, z)` and set it with `engine.set_block_meta(x, y, z, name, meta)`. To use parts of the number as separate on/off flags ("bits"), Lua 5.4 has bit operators:

```lua
local meta = engine.get_metadata(x, y, z)
local facing = meta & 3            -- bits 0-1: a number 0..3
local is_open = (meta & 4) ~= 0    -- bit 4: true/false
meta = meta | 4                    -- turn "open" on
meta = meta & ~4                   -- turn "open" off
```

## L5. Coordinates and chunks

- **x** runs east and west, **z** north and south, and **y** is **up**.
- The world is 256 blocks tall, from y = 0 (bedrock) to y = 255.
- A whole block at `(x, y, z)` fills the space from `x` to `x + 1`, and so on. The player's position is the point between their feet.
- The world is made of **chunks**: columns 16 × 256 × 16. Chunk `(cx, cz)` covers x from `cx * 16` to `cx * 16 + 15`, and the same for z.
- In `generate_chunk`, coordinates are **inside the chunk**: x and z from 0 to 15. Add `cx * 16` and `cz * 16` to get world coordinates.

```lua
local cx = math.floor(x / 16)      -- which chunk a world x is in
local lx = x - cx * 16             -- position inside that chunk (0..15)
```

## L6. Pictures, sounds and models: how files are found

You give only a **file name**, never a path.

- **Pictures** (`.png`): at startup the engine indexes every PNG in the game's folder and in every mod folder, including subfolders, ignoring capitals. `"ruby_block.png"` works wherever that file is. Your own mod's folder is checked first.
- **Sounds** (`.ogg` or 16-bit `.wav`): `"ruby_hit"` finds `ruby_hit.ogg` in your mod's folder or its `sounds/` folder, the game's `sounds/` folder, or `./sounds/`. Add `ruby_hit.1.ogg`, `ruby_hit.2.ogg`... and one is picked at random each time.
- **Models** (`.b3d` from Luanti, or Bedrock `.json` from Blockbench) and **animations** (`.animation.json`): found in your mod's folder or its `models/` folder.
- **Texture packs** replace any picture with one of the same file name.

> **Name clashes:** if two mods both have `icon.png`, each finds its own while loading, but anything looked up later (like a HUD picture drawn every frame) could find the other one. Give files names only your mod uses: `ruby_icon.png`, not `icon.png`.

Look a picture up while your mod loads, and keep the result:

```lua
local HEART = engine.find_texture("ruby_heart.png") or "ruby_heart.png"   -- full path, found now
```

## L7. Who decides? Singleplayer, LAN and servers

The same mod runs in three situations:

| Situation | Who runs the world | Where your mod runs |
|---|---|---|
| Singleplayer | your game | your game |
| Hosting a LAN game | your game (the host) | the host and every player who joined |
| Dedicated server | `BlockSmithServer` | the server **and** every player's game |

So in multiplayer **your code runs in more than one place at once**. If your mod changes the world from a hook, every copy would make the change. The rule:

> **Only the authority changes the world.** `engine.is_authority()` is true in singleplayer, for a LAN host, and on the dedicated server. It's false for players who joined.

```lua
local previous = on_block_broken
function on_block_broken(x, y, z, block, meta, ...)
    if block == "ruby_ore" and engine.is_authority() then
        engine.set_block(x, y + 1, z, "fire")         -- happens once, on the authority
    end
    if previous then previous(x, y, z, block, meta, ...) end
end
```

What runs where:

| Thing | Runs on |
|---|---|
| Drawing (`ui.*`, `render_hud`), sound, sky, hotbar, the hand | each player's game. Does nothing on a dedicated server (`engine.is_server()` is true there). |
| World changes (`engine.set_block`), weather decisions, mob logic | the authority. It sends the results to everyone. |
| World generation (`generate_chunk`) | the authority, on background threads |
| Light | each player's game works it out from the blocks |

**Sharing a decision with everyone.** When the authority decides something every player must see (the weather, a boss's health), use **shared values**:

```lua
if engine.is_authority() then engine.set_shared("ruby:bossfight", "on") end

local previous = on_shared_changed
function on_shared_changed(key, value, ...)
    if key == "ruby:bossfight" then sky.set_sky_tint(0.6, 0, 0, 0.3) end
    if previous then previous(key, value, ...) end
end
```

Put your mod's name in front of keys (`"ruby:..."`) so they don't clash with other mods.

Players who join a server must have the **same mods** as the server, because blocks are sent by name.

## L8. Mods working together, and saving data

**Offering functions to other mods.** Put them in one global table named after your mod:

```lua
ruby = {}
function ruby.is_gem(name) return name == "ruby_block" or name == "ruby" end
```

**Using another mod's functions.** It might not be installed, so check first:

```lua
if survival then survival.register_food("ruby_candy", 2) end
```

The engine itself offers no gameplay tables: they come from whichever game and mods are loaded. Craft & Ruin's own mods (`games/craft_and_ruin/mods/`) offer `survival`, `survival.inventory`, `weather`, `doors`, `leafdecay` and `default_sounds`. The example `buckets` mod in the `mods/` folder offers `buckets`. Another game may offer none of these, so check before using one (`if survival then ... end`). `lua_api.txt` lists every table your loaded mods offer.

**Saving data with the world.** `engine.set_storage(key, text)` and `engine.get_storage(key)` keep text with the world, the same idea as Luanti's mod storage. Store numbers and lists as text:

```lua
-- save
engine.set_storage("ruby:spawn", string.format("%d,%d,%d", x, y, z))

-- load
local s = engine.get_storage("ruby:spawn")
if s then
    local x, y, z = s:match("(-?%d+),(-?%d+),(-?%d+)")
    x, y, z = tonumber(x), tonumber(y), tonumber(z)
end
```

## L9. Debugging when something goes wrong

1. **Read the console.** Lua errors show the file and line: `mods/ruby/init.lua:12: attempt to call a nil value (field 'regsiter_block')`. The Mods screen shows them too. One broken mod doesn't stop the others.
2. **Use `print`.** `print("[Ruby] clicked", x, y, z, block)` writes to the console.
3. **Press F3** in the game to see the block you're looking at, with its name, id and metadata.
4. **Check `lua_api.txt`.** If a function you're calling isn't listed there, your build doesn't have it. It also shows which hooks your mods define, which helps you find two mods fighting over one.
5. **Something stopped working after you added a mod?** It's almost always an unchained hook. Search your mods for `function on_` and check each one keeps `previous`.

Common error messages:

| Message | What it usually means |
|---|---|
| `attempt to call a nil value (field 'x')` | a typo in a function name, or a function your build doesn't have |
| `attempt to index a nil value (global 'survival')` | the other mod isn't loaded (yet). Check `if survival then` or add `depends`. |
| `attempt to perform arithmetic on a nil value` | a function returned nothing; for example, `engine.get_player_state()` before the player exists |
| `'end' expected near <eof>` | a missing `end`. Count your `function`, `if` and `for` lines. |
| Nothing happens, no error | an unchained hook in another mod, or your hook's name is misspelled, so the engine never calls it |

## L10. Client mods: scripts for your own game

Everything so far has been about **game mods**: they decide how the game works, and every player on a server needs the same ones. Some things aren't the game's business at all, though: a minimap, waypoints, a light-level display, a chat helper. In Minecraft you'd add those to your own copy of the game. BlockSmith has **client mods** for this, like Luanti's client-side mods (CSM).

| | Game mods (`mods/`, `games/<game>/mods/`) | Client mods (`clientmods/`) |
|---|---|---|
| Who has them | everyone on a server (the server's own) | only you |
| Where they run | the server and every player's game | only your game |
| What they can do | anything: blocks, rules, the world | look and show: draw on screen, read the world near you, read chat, add `.commands` |
| Can they change the world? | yes | no |
| Lua | the full game API (`engine.*`...) | a sandbox with the `client.*` API, `ui.*` and `sound.*` |
| Who switches them on | always on (the server owner chooses) | **you**, on the Mods screen: client mods are off until you switch them on. **The server** then decides what they may do there. |

**Your first client mod.** Make `clientmods/hello/init.lua` (the `clientmods` folder sits next to `mods`):

```lua
client.register_chat_command("hello", {
    description = "Say hello to yourself",
    func = function(param)
        return true, "Hello, " .. client.get_player_name() .. "!"
    end,
})

client.register_on_hud(function(W, H)
    ui.draw_text("Client mods work!", 10, H - 30, 0.3, 1, 1, 0.5)
end)
```

Open **Mods** on the title screen, find *hello* (marked *Client mod*), press **Enable** and restart the game. Client mods never run until you switch them on, so nothing you download runs by surprise. Then start a world and type `.hello`. Client mod commands start with a **dot**, so they never clash with the game's `/commands`, and they're never sent to the server. `.help` lists them.

**What's different from game mods:**
- **Register, don't define.** Client mods use `client.register_on_update(fn)`, `client.register_on_hud(fn)` and so on, instead of global hooks. Every mod's function runs, so there's nothing to chain.
- **A sandbox.** There's no `io`, no `os.execute` and no `require`. `client.dofile("file.lua")` loads files from the mod's own folder, while it loads. Saved data goes through `client.get_mod_storage()`.
- **Mistakes are contained.** If a callback errors, that callback stops, a red line in chat says which mod and why, and everything else carries on.

**The server decides.** Servers set four rules in `server.conf`, and your game keeps to them while you play there:

| Rule | Default | Meaning |
|---|---|---|
| `client_mods` | `true` | client mods run at all |
| `client_mods_chat` | `false` | they may send chat messages and commands for you (`client.send_chat_message`) |
| `client_mods_players` | `true` | they may see other players' names and positions (`client.get_players`) |
| `client_mods_block_range` | `64` | how far from you they can read blocks; 0 = any distance |

In singleplayer there are no limits. A LAN host has no limits either, and the friends who join get the host's `lan_client_mods...` settings (no limits unless changed). `client.get_rules()` tells your mod what's allowed right now. Plan for `nil`: when a block is out of range, `client.get_block` returns nil.

> **Honest games only.** The server can't see what runs in your game, so it can't *force* these rules. Like Luanti's, they're kept by the game itself, which stops honest client mods from becoming accidental cheats. Servers that need real protection must check things on the server, as BlockSmith already does for digging and game modes.

The complete client API is in [Reference §20](#20-client-mods). The example client mod `clientmods/minimap` is a complete one to copy from. Switch it on on the Mods screen, then press **M** to show or hide it, and use `.wp add home` to set a waypoint.

## L11. Screens and menus: how BlockSmith UI works

If you've modded Luanti, you know **formspecs**: you write a description of a window (`size[8,9] button[1,1;2,1;ok;OK]`), send it, and wait for a "fields" message when the player clicks something. BlockSmith works differently, and once you've seen it, it's simpler.

### Drawn every frame, by you

BlockSmith UI is **immediate mode**. While your screen is open, the engine calls your draw function every frame (60 times a second), and your function draws the whole screen from scratch: rectangles, text, pictures, buttons. There's no window object to create, update or destroy. To change what's shown, change your variables; the next frame draws it.

```lua
local count = 0
function draw_my_screen(W, H)                     -- the engine calls this every frame
    ui.draw_rect(100, 100, 300, 120, 0.1, 0.1, 0.1, 0.9)
    ui.draw_text("Clicked " .. count .. " times", 120, 150, 0.5, 1, 1, 1)
end
-- ...and when a click arrives, you change `count`. That's all "updating the UI" means.
```

Why this is nice:
- **What you see is always your data.** A stack count, a timer, a player list: draw it from your variables, and it's never out of date.
- **No messages back and forth.** Clicks arrive as a function call with the mouse position. You check what's there, and do something.
- **Anything goes.** Animations, hover effects and live previews are just more drawing.

### Three places UI can go

| | what | how |
|---|---|---|
| **HUD** | always on screen while playing: hearts, a compass, a timer | define `render_hud(W, H)`. It's only drawn, never clicked. [C11](#c11-a-hud-piece) |
| **In-game screen** | a window over the world that takes the mouse: a chest, a shop, a notice board | one shared slot: `render_inventory` + `handle_inventory_click` + friends. **Easiest: the `ui_kit` library**, below. [C12](#c12-your-own-screen) |
| **Menus** | the title screen's pages and the pause menu: Settings, Singleplayer, Multiplayer, Mods | replace the engine's own: `render_pause_menu`, `render_settings`... [§14](#14-ui-menus-and-screens) |

### The quickest way: `ui_kit` (BlockSmith's formspecs)

BlockSmith comes with `lualib/ui_kit.lua`, a small library that gives you formspec-style building blocks (buttons, labels, text fields, checkboxes, sliders, lists, item slots, pictures, tooltips) and does all the plumbing for you: opening, closing, scaling, typing, scrolling, and sharing the screen slot with other mods.

```lua
local kit = require("ui_kit")

local greeting = kit.form{
    id = "hello:greeting", width = 10, height = 4, title = "Hello!",
    draw = function(f, state)
        f:label(0, 0, "What's your name?")
        state.name = f:field("name", 0, 1, 10, 1, state.name, { placeholder = "Type here" })
        if f:button("ok", 6, 2.75, 4, 1, "Say hi") then
            engine.send_chat("Hi, I'm " .. (state.name or "?"))
            f:close()
        end
    end,
}

-- Open it with /hello
chat.register_command("hello", "/hello", "Says hello")
local previous = on_chat_command
function on_chat_command(text, ...)
    if text == "hello" then greeting:open({}) return true end
    if previous then return previous(text, ...) end
end
```

Here's what happens:
- `kit.form{...}` describes the form once: its size in **units** (here 10 × 4) and a `draw` function.
- Each frame while it's open, `draw(f, state)` runs. `f:label`, `f:field` and `f:button` draw a widget **and** report what the player did with it: the field returns its current text, and the button returns `true` on the frame it's clicked.
- `state` is your table. It's kept while the form is open, and it's where typed text and choices live.
- **Units, not pixels.** The kit picks a unit size that fits the window, so the form looks the same at any resolution. `(0, 0)` is the top-left below the title.

Formspec elements and their `ui_kit` equivalents:

| Luanti formspec | ui_kit |
|---|---|
| `size[w,h]` | `kit.form{ width = w, height = h }` |
| `label[x,y;text]` | `f:label(x, y, text)` (and `f:text(x, y, w, text)` for wrapped paragraphs) |
| `button[x,y;w,h;name;label]` | `if f:button("name", x, y, w, h, "label") then ... end` |
| `field[x,y;w,h;name;label;default]` | `state.v = f:field("name", x, y, w, h, state.v)` |
| `pwdfield[...]` | `f:field(..., { password = true })` |
| `checkbox[x,y;name;label;selected]` | `state.on = f:checkbox("name", x, y, "label", state.on)` |
| `scrollbar` | `state.v = f:slider("name", x, y, w, state.v, { min = 0, max = 100 })` |
| `textlist[...]` | `state.i = f:list("name", x, y, w, h, items, state.i)` |
| `item_image[x,y;w,h;item]` / `list[...]` | `f:item(x, y, size, "item_name", { count = 5 })` |
| `image[x,y;w,h;file]` | `f:image(x, y, w, h, "file.png")` |
| `tooltip[...]` | `{ tooltip = "..." }` on most widgets, or `f:tooltip(x, y, w, h, text)` |
| receiving `fields` | the widget's return value, on the same line |

### Going further: windows, drag and drop, any screen

The same kit also has what you need for UI that behaves like a computer's:
- **windows** the player moves, resizes, stacks, minimizes and closes, plus frameless panels for taskbars;
- **drag and drop**: an inventory becomes rearrangeable by giving its slots `drag` and `drop`;
- **scroll areas, desktop icons (double-click), right-click menus**, the right mouse cursor over each thing, copy and paste in text fields;
- **`kit.embed`**, to use all of it in any screen: the HUD, the creative inventory, menus.

See [§14.6](#146-ui_kit-windows-drag-and-drop-and-the-kit-in-any-screen), the `ui_showcase` mod (type `/uikit` in game: every feature in one desktop), and two complete examples: [C20](#c20-a-rearrangeable-inventory-drag-and-drop) (a backpack) and [C21](#c21-a-laptop-with-its-own-desktop-windows) (a laptop with a desktop). Underneath, the engine gives every screen shapes and clipping ([§14.2](#142-drawing)) and the mouse, wheel, keys and typing of each frame ([§14.3](#143-mouse-and-keyboard)), if you'd rather build your own.

### Doing it by hand

`ui_kit` is plain Lua on top of the engine's `ui.*` functions, and you can use those directly for anything it doesn't cover: a custom layout, a mini-game, a map. Three things to know:
1. **One function decides the layout, and both drawing and clicking use it.** Then a click always lands on what's drawn.
2. **`ui.draw_text`'s `y` is the text's baseline**, the line the letters sit on, not its top. To put text with its top at `y`, draw it at `y + ui.text_height(scale)`.
3. **The in-game screen slot is shared** with the survival inventory and other mods, so chain every hook and only act while your screen is open. [C12](#c12-your-own-screen) shows it.

The full list of drawing functions, input, keys and hooks is in [Reference §14](#14-ui-menus-and-screens).

---

# Part 2: Cookbook

Complete, working examples. Each one is a whole mod: make a folder in `mods/`, paste the code into `init.lua`, and add the pictures it names. Swap in your own names and numbers.

All of them follow the rules from Part 1: they register at load time, chain every hook, and check `engine.is_authority()` before changing the world on their own.

## C1. A decorative block with different faces

A carved stone block with its own top, bottom and sides, stone sounds, and a recipe.

```lua
engine.register_block("carved_stone", 1, 1, 1, {
    top    = "carved_stone_top.png",
    bottom = "carved_stone_top.png",
    sides  = "carved_stone_side.png",
}, nil, {
    hardness = 1.5, tool = "pickaxe", needs_tool = true,   -- like stone
})

-- Sounds: borrow the stone set from default_sounds, if it's installed
if default_sounds then
    engine.override_block("carved_stone", { sounds = default_sounds.sets.stone })
end

-- 4 stone in a square make 4 carved stone (needs the survival mod)
if survival then
    survival.register_craft({ output = "carved_stone", count = 4,
        recipe = { { "stone", "stone" },
                   { "stone", "stone" } } })
end
```

**Variations:**
- **A see-through block** (glass): add `transparent = true`.
- **Leaves-style cut-out holes:** use `cutout = true` instead. It's faster and has no blending.
- **A flower:** `pattern = "plantlike", walkable = false, transparent = true`.
- **Falls like sand:** `gravity = true`.
- **Hidden from the creative menu:** `in_creative = false`.

## C2. A glowing ore that generates underground

An ore that gives off a dim red light, needs a pickaxe, drops a gem, and appears in stone below y = 40.

```lua
engine.register_block("ruby_ore", 1, 1, 1, "ruby_ore.png", nil, {
    hardness = 3, tool = "pickaxe", needs_tool = true,
    drops = "ruby",
    light = 5, light_color = { 255, 60, 60 },      -- a faint red glow in caves
})
engine.register_block("ruby", 1, 1, 1, "ruby.png", nil, { inventory_image = "ruby.png" })

-- World generation: sprinkle ore into the stone after the game's terrain is made.
local seed = 0
local previous_seed = set_world_seed
function set_world_seed(s, ...)
    seed = s
    if previous_seed then previous_seed(s, ...) end
end

local previous_generate = generate_chunk
function generate_chunk(chunk, cx, cz, ...)
    if previous_generate then previous_generate(chunk, cx, cz, ...) end   -- terrain first

    local stone = engine.get_block_id("stone")
    local ore = engine.get_block_id("ruby_ore")
    if stone == 0 or ore == 0 then return end

    -- The same chunk must always get the same ore, so seed the dice from the world seed
    -- and the chunk's position (never from the clock).
    math.randomseed(math.floor(seed) ~ (cx * 73856093) ~ (cz * 19349663))

    for _ = 1, 6 do                                   -- 6 veins per chunk
        local x, y, z = math.random(0, 15), math.random(5, 40), math.random(0, 15)
        for _ = 1, math.random(3, 6) do               -- 3 to 6 blocks per vein
            if chunk:get_block(x, y, z) == stone then chunk:set_block(x, y, z, ore) end
            x = math.min(15, math.max(0, x + math.random(-1, 1)))
            y = math.min(255, math.max(1, y + math.random(-1, 1)))
            z = math.min(15, math.max(0, z + math.random(-1, 1)))
        end
    end
end
```

**Things to know:**
- `generate_chunk` runs on background threads. Use only `engine.get_block_id`, the `chunk` functions and maths there.
- Chunks that were already generated keep what they had. Explore new land to see your ore.
- Caves are carved *after* `generate_chunk`, so some ore ends up in cave walls, as in Minecraft.

## C3. A lamp you switch on and off

Right-click a lamp to toggle it. "On" and "off" are two separate blocks, and the "on" one gives off light.

```lua
engine.register_block("ruby_lamp", 1, 1, 1, "ruby_lamp_off.png", nil, { hardness = 0.3 })
engine.register_block("ruby_lamp_on", 1, 1, 1, "ruby_lamp_on.png", nil, {
    hardness = 0.3,
    light = 15, light_color = "#ff8080",
    drops = "ruby_lamp",            -- breaking a lit lamp gives the normal lamp
    in_creative = false,
})

local SWITCH = { ruby_lamp = "ruby_lamp_on", ruby_lamp_on = "ruby_lamp" }

local previous = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    local other = SWITCH[block]
    -- Like Minecraft: sneak + right-click with a block in hand places against the lamp instead
    local holding = held and held ~= "" and held ~= "air"
    if other and not (sneaking and holding) then
        if not repeat_click then                  -- one toggle per click, not every 0.2 s
            engine.set_block(x, y, z, other)
            sound.play("default_place", { pos = { x + 0.5, y + 0.5, z + 0.5 } })
        end
        return true                               -- handled: don't place the held block
    end
    if previous then return previous(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end
```

This works in multiplayer as written: a change made in response to the player's own click is sent to the server like their own building, and the server checks it. Light updates by itself.

**Variation: one block, metadata for the state.** If the off and on states don't need different light, keep one block and store the state in its metadata with `engine.set_block_meta(x, y, z, "my_block", meta ~ 1)` (`~` flips bit 1). The doors mod works this way.

## C4. Food

An apple pie that restores 4 hunger points (two drumsticks). The survival mod does the eating: right-click with it in hand.

```lua
engine.register_block("apple_pie", 1, 1, 1, "apple_pie.png", nil, { inventory_image = "apple_pie.png" })

if survival then
    survival.register_food("apple_pie", 4, 3)     -- food points, saturation
    survival.register_craft({ type = "shapeless", output = "apple_pie",
        recipe = { "apple", "apple", "wheat" } })
end
```

- **Food points:** 2 = one drumstick. A full bar is 20.
- **Saturation:** how long it keeps you full. Leave it out for food × 0.6.
- You can't eat when your hunger bar is full, as in Minecraft.

## C5. A tool

A ruby pickaxe that digs pickaxe blocks 8 times faster than a hand.

```lua
engine.register_block("ruby_pickaxe", 1, 1, 1, "ruby_pickaxe.png", nil, {
    inventory_image = "ruby_pickaxe.png",
    tool_type = "pickaxe",      -- digs blocks whose `tool` is "pickaxe" faster...
    tool_speed = 8,             -- ...this many times faster than a hand
})

if survival then
    survival.register_craft({ output = "ruby_pickaxe", recipe = {
        { "ruby", "ruby",  "ruby" },
        { "",     "stick", ""     },
        { "",     "stick", ""     },
    } })                        -- 3x3: craft it at a workbench
end
```

- A block's `tool` says which tool type helps. With `needs_tool = true`, digging it without that tool is 3.3 times slower and drops nothing.
- Other tool types: `"axe"`, `"shovel"`, and `"shears"` (shears make leaves drop themselves).
- For complete control of dig times, define the `get_dig_time(block, item)` hook. See [Reference §11](#11-survival-health-hunger-digging).

## C6. Armour

A ruby helmet that goes in the head slot of the survival inventory and blocks 12% of damage.

```lua
engine.register_block("ruby_helmet", 1, 1, 1, "ruby_helmet.png", nil, { inventory_image = "ruby_helmet.png" })

if survival then
    survival.register_equipment("ruby_helmet", "head", { armor = 3 })   -- 3 points x 4% = 12%
    survival.register_craft({ output = "ruby_helmet", recipe = {
        { "ruby", "ruby", "ruby" },
        { "ruby", "",     "ruby" },
    } })

    survival.on_equip(function(slot, item)
        if slot == "head" and item == "ruby_helmet" then chat.send("You feel protected.") end
    end)
end
```

Slots: `pet`, `head`, `glove_left`, `chest`, `glove_right`, `feet`, `legs`, `backpack`.

## C7. Crafting recipes

```lua
if not survival then return end      -- no survival mod, no crafting

-- Shaped: the pattern must match (mirrored also works). "" = an empty square.
survival.register_craft({ output = "torch", count = 4, recipe = {
    { "coal"  },
    { "stick" },
} })

-- Shapeless: the ingredients can be anywhere in the grid
survival.register_craft({ type = "shapeless", output = "mushroom_stew",
    recipe = { "bowl", "mushroom_red", "mushroom_brown" } })
```

- Recipes up to 2×2 work in the inventory. Bigger ones (up to 3×3) need a workbench.
- Make your own crafting table: `survival.register_workbench("ruby_table")`. Right-clicking that block then opens the 3×3 grid.
- Shift-click the output to craft as many as you can.

## C8. A jump pad

Step on it and you're launched into the air. This uses `on_update`, which runs every frame, to check what's under the player.

```lua
engine.register_block("jump_pad", 1, 1, 1, { top = "jump_pad_top.png", bottom = "jump_pad_side.png",
                                             sides = "jump_pad_side.png" }, nil, { hardness = 1 })

local cooldown = 0
local previous_update = on_update
function on_update(dt, ...)
    if previous_update then previous_update(dt, ...) end

    cooldown = math.max(0, cooldown - dt)
    local p = engine.get_player_state()
    if not p or not p.on_ground or cooldown > 0 then return end

    local under = engine.get_block_name(math.floor(p.x), math.floor(p.y - 0.1), math.floor(p.z))
    if under == "jump_pad" then
        engine.add_player_velocity(0, 18, 0)
        sound.play("default_place", { pitch = 1.6 })
        cooldown = 0.5
    end
end
```

`engine.add_player_velocity` works in singleplayer and for a LAN host. When you've joined a server, your movement is checked by the server, so it returns `false` and nothing happens.

## C9. An item that does something

A teleport wand: right-click a block to jump on top of it, with a sound and a hand swing.

```lua
engine.register_block("teleport_wand", 1, 1, 1, "teleport_wand.png", nil, {
    inventory_image = "teleport_wand.png",
})

engine.register_item_use("teleport_wand", function(pointed)
    if not pointed.under or pointed.repeat_click then return false end   -- no block, or button held
    local u = pointed.under
    engine.set_player_position(u.x + 0.5, u.y + 1, u.z + 0.5)
    engine.swing_hand()
    sound.play("ruby_teleport")                                     -- ruby_teleport.ogg
    return true
end)
```

What `pointed` holds:
- `pointed.under` is the block you're looking at, `{ x, y, z, name }`, or nil.
- `pointed.above` is the empty space in front of the face you clicked, `{ x, y, z }`. Place blocks there.
- `pointed.fluid` is the fluid you're looking at, if any.
- `pointed.slot` and `pointed.count` are the hotbar slot and how many you have. Use `hotbar.set_slot(pointed.slot, name, count - 1)` to use one up.

## C10. A chat command with arguments and permissions

`/setwarp name` saves a place and `/warp name` takes you back to it. Only operators may set warps on a server.

```lua
chat.register_command("warp", "/warp <name>", "Go to a saved place")
chat.register_command("setwarp", "/setwarp <name>", "Save this place (operators)")

local function key(name) return "warps:" .. name end

local previous = on_chat_command
function on_chat_command(text, player, is_operator, reply, ...)
    local say = reply or function(t) chat.send(t, 0.6, 0.9, 1) end
    local cmd, arg = text:match("^(%S+)%s*(%S*)")

    if cmd == "setwarp" then
        if not is_operator then say("Only operators can set warps.") return true end
        if arg == "" then say("Usage: /setwarp <name>") return true end
        local x, y, z = engine.get_player_position()
        engine.set_storage(key(arg), string.format("%.1f,%.1f,%.1f", x, y, z))
        say("Warp '" .. arg .. "' saved.")
        return true
    elseif cmd == "warp" then
        local s = engine.get_storage(key(arg))
        if not s then say("No warp called '" .. arg .. "'.") return true end
        local x, y, z = s:match("([^,]+),([^,]+),([^,]+)")
        engine.set_player_position(tonumber(x), tonumber(y), tonumber(z))
        return true
    end

    if previous then return previous(text, player, is_operator, reply, ...) end
    return false
end
```

- `text` arrives without the `/`: `"setwarp home"`.
- `is_operator` is always true in singleplayer.
- `reply` answers just the player who typed the command (on a server). In singleplayer it can be nil, hence `say`.
- In multiplayer, commands your game doesn't handle (`return false`) are sent on to the server. Its mods get the same call there.

## C11. A HUD piece

A compass in the top-right corner that shows which way you're facing and your coordinates.

```lua
local DIRECTIONS = { "East", "South-east", "South", "South-west", "West", "North-west", "North", "North-east" }

local previous_hud = render_hud
function render_hud(W, H, ...)
    if previous_hud then previous_hud(W, H, ...) end    -- keep the hearts, hunger and other HUD pieces

    local p = engine.get_player_state()
    if not p then return end
    local i = math.floor(((p.yaw % 360) + 22.5) / 45) % 8 + 1   -- yaw 0 = east (+x)
    local line1 = DIRECTIONS[i]
    local line2 = string.format("%d, %d, %d", math.floor(p.x), math.floor(p.y), math.floor(p.z))

    local scale = 0.35
    local w = math.max(ui.text_width(line1, scale), ui.text_width(line2, scale)) + 16
    local h = ui.text_height(scale) * 2 + 14
    local x, y = W - w - 10, 10
    ui.draw_rect(x, y, w, h, 0, 0, 0, 0.45)
    ui.draw_text(line1, x + 8, y + 5, scale, 1, 1, 0.6)
    ui.draw_text(line2, x + 8, y + 9 + ui.text_height(scale), scale, 1, 1, 1)
end
```

- `render_hud` runs every frame while you're in the game. Keep it light: no file reads, no `engine.find_texture`.
- `W` and `H` are the window's size in pixels. Place things relative to them so the HUD fits any window.
- To place something next to the hotbar, `local bx, by, bw, bh = hotbar.get_rect(W, H)`.
- Pictures: `ui.draw_image("compass.png", x, y, 32, 32)`. Look up its path while your mod loads, as in [L6](#l6-pictures-sounds-and-models-how-files-are-found).

## C12. Your own screen

A notice board you can write on: right-click it to open a screen showing the notes. Each note needs a name and a message, and the board shows the newest notes first. The mouse is freed while it's open, and E or Escape closes it.

### With `ui_kit` (recommended)

```lua
local kit = require("ui_kit")
engine.register_block("notice_board", 1, 1, 1, "notice_board.png", nil, { hardness = 1, tool = "axe" })

local notes = {}                      -- { { name = ..., text = ... }, ... }, newest first

local board = kit.form{
    id = "notice:board", width = 14, height = 8, title = "Town notice board",
    draw = function(f, state)
        -- The notes, as a scrolling list
        local rows = {}
        for i, n in ipairs(notes) do rows[i] = n.name .. ": " .. n.text end
        if #rows == 0 then rows[1] = { text = "(nothing yet - be the first!)", color = { 0.6, 0.6, 0.65 } } end
        f:list("notes", 0, 0, 14, 4.5, rows, nil)

        -- Writing one
        state.name = f:field("name", 0, 5, 4, 1, state.name, { placeholder = "Your name", max_length = 16 })
        local submitted
        state.text, submitted = f:field("text", 4.5, 5, 9.5, 1, state.text, { placeholder = "Your note", max_length = 80 })
        local ready = (state.name or "") ~= "" and (state.text or "") ~= ""
        if f:button("pin", 9.5, 6.75, 4.5, 1, "Pin it", { disabled = not ready, tooltip = "Adds your note" })
           or (submitted and ready) then
            table.insert(notes, 1, { name = state.name, text = state.text })
            state.text = ""
        end
        if f:button("close", 0, 6.75, 3, 1, "Close") then f:close() end
    end,
}

local previous_right_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if block == "notice_board" and not sneaking then
        if not repeat_click then board:open({}) end
        return true
    end
    if previous_right_click then return previous_right_click(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end
```

That's the whole mod. The kit opens the screen, frees the mouse, sends typing to the focused field (so typing "e" doesn't close it), scrolls the list with the wheel, and closes on E or Escape. It also passes everything on to the survival inventory and other mods when the board isn't open.

To keep the notes when the game restarts, save them with `engine.set_storage("notice:notes", ...)` (one string; join the notes with a separator) and load them when the mod starts. In multiplayer, each player's game has its own `notes`; sharing a board between players needs the server to keep it (see [§17](#17-multiplayer-and-servers)).

### By hand, without the kit

This is what the kit does for you, shown for a simple board with a Close button. Use it to understand the engine's screen slot, or for a screen the kit can't make.

```lua
engine.register_block("notice_board", 1, 1, 1, "notice_board.png", nil, { hardness = 1, tool = "axe" })

local open = false
local function set_open(on)
    open = on
    engine.set_inventory_open(on)          -- the engine frees or captures the mouse
end

-- Where the panel and button are, for drawing and for clicks alike
local function layout(W, H)
    local w, h = 360, 200
    local x, y = (W - w) / 2, (H - h) / 2
    return { x = x, y = y, w = w, h = h, bx = x + w / 2 - 60, by = y + h - 44, bw = 120, bh = 30 }
end

local previous_render = render_inventory
function render_inventory(W, H, ...)
    if not open then
        if previous_render then return previous_render(W, H, ...) end
        return
    end
    local L = layout(W, H)
    ui.draw_rect(0, 0, W, H, 0, 0, 0, 0.5)                          -- dim the world
    ui.draw_rect(L.x, L.y, L.w, L.h, 0.22, 0.16, 0.10, 0.95)         -- the board
    -- (text y is the baseline: top + the capital height puts the text's top at `top`)
    ui.draw_text("Town notice", L.x + 16, L.y + 14 + ui.text_height(0.5), 0.5, 1, 0.9, 0.6)
    ui.draw_text("No digging under the church.", L.x + 16, L.y + 60 + ui.text_height(0.35), 0.35, 1, 1, 1)

    local mx, my = ui.get_mouse()
    local hover = mx >= L.bx and mx < L.bx + L.bw and my >= L.by and my < L.by + L.bh
    ui.draw_button(L.bx, L.by, L.bw, L.bh, hover and "hover" or "normal")
    local r, g, b = ui.get_button_text_color(hover)
    local label, s = "Close", 0.35
    ui.draw_text(label, L.bx + (L.bw - ui.text_width(label, s)) / 2,
                 L.by + (L.bh + ui.text_height(s)) / 2, s, r, g, b)    -- centred: baseline below the middle
end

local previous_click = handle_inventory_click
function handle_inventory_click(mx, my, W, H, ...)
    if not open then
        if previous_click then return previous_click(mx, my, W, H, ...) end
        return false
    end
    local L = layout(W, H)
    if mx >= L.bx and mx < L.bx + L.bw and my >= L.by and my < L.by + L.bh then set_open(false) end
    return true
end

-- E and Escape: close ours if it's open, otherwise let the survival inventory have the key
local previous_toggle = toggle_main_inventory
function toggle_main_inventory(...)
    if open then set_open(false) return false end
    if previous_toggle then return previous_toggle(...) end
    return false
end

local previous_right_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if block == "notice_board" and not sneaking then
        if not repeat_click then set_open(true) end
        return true
    end
    if previous_right_click then return previous_right_click(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end
```

A hand-made screen that takes typing also chains `handle_inventory_key` (returning `true` while a box has focus) and `on_char_input`; one with a scrolling list chains `handle_inventory_scroll`. See [§14.4](#144-where-ui-goes) and [§14.6](#147-patterns-and-mistakes).

**Menus** (the pause menu, Settings and the like) are replaced instead: define `render_pause_menu` and its `handle_pause_click`. See [Reference §14](#14-ui-menus-and-screens).

## C13. A new weather

A sandstorm: orange sky, dim light, sideways dust and wind. The weather mod decides when it comes; you only describe it.

```lua
if not weather then return end

weather.register("sandstorm", {
    light = 0.7,                              -- sunlight: 1 = normal
    sky_tint = { 0.8, 0.6, 0.4, 0.6 },        -- r, g, b, strength
    precipitation = {
        type = "dust", texture = "sand.png",  -- any picture as particles
        intensity = 0.8, streaks = false,
        fall_speed = 2, size = 0.08,
    },
    wind = { 8, 1 },                          -- x, z
    chance = 5,                               -- how often it's picked (rain is about 20)
    duration = { 120, 300 },                  -- seconds, picked at random between these
    sound = "sandstorm_loop", sound_gain = 0.5,
})

weather.on_change(function(old, new)
    if new == "sandstorm" then chat.send("A sandstorm is coming!", 1, 0.7, 0.3) end
end)
```

Try it with `/weather sandstorm`. The weather mod already sends the change to everyone in multiplayer.

## C14. A mob with a model and an animation

A cow that wanders around, plays its walk animation, runs away when you hit it, and drops beef when it dies. Make the model and animation in Blockbench: export the model as *Bedrock Geometry* (`cow.geo.json`) and the animations with *Export Bedrock Animations* (`cow.animation.json`). Put both in your mod's `models/` folder and the skin in `textures/`. Build it facing north (-z), the way Blockbench starts a Bedrock entity.

```lua
entities.register("mymod:cow", {
    hitbox = { 0.9, 1.4 },                      -- width, height in blocks
    health = 10,
    model = "cow.geo.json", texture = "cow.png", animations = "cow.animation.json",
    drops = { { "beef", 1, 3 } },               -- item, fewest, most

    on_spawn = function(self)
        self.timer = 0                           -- your own fields: self stays the same table
    end,

    on_step = function(self, dt)                 -- every tick
        if self.panic and self.panic > 0 then    -- just hit: run
            self.panic = self.panic - dt
            if self:path_state() ~= "following" then self:wander(8, 3) end   -- run somewhere
            self:set_animation("walk", 2)        -- twice as fast
            return
        end
        self.timer = self.timer - dt
        if self.timer <= 0 and self:path_state() ~= "following" then
            self.timer = math.random(3, 7)       -- every few seconds: go somewhere else
            if math.random() < 0.5 then self:wander(10, 1) end   -- a spot it can reach, round trees
        end
        self:set_animation(self:path_state() == "following" and "walk" or "idle")
    end,

    on_punch = function(self, player, item, damage)
        self.panic = 3
        self:stop()
        -- return true here to stop the engine taking the damage and knocking it back
    end,
})

-- Cows spawn by themselves on grass in daylight, a few at a time
entities.register_spawn({ name = "mymod:cow", on = { "grass" }, min_light = 9, sky = true,
                          chance = 0.25, group = { 2, 4 }, max_nearby = 6 })
```

**Things to know:**
- **Yaw** is in degrees. Yaw 0 faces +x (east), 90 faces +z (south).
- **The engine does the hard parts:** finding a way round trees and walls (`wander`, `go_to`), gravity, bumping into blocks, bumping into each other and into players (overlapping mobs are nudged apart, as in Minecraft), jumping 1-block steps, floating in water, knockback, the red flash when hit, saving, and showing it to every player in multiplayer.
- **Each cow animates on its own.** Animations play from when you set them, so cows walk out of step.
- **Spawn one yourself:** `entities.spawn("mymod:cow", x, y, z)`.
- The example `mobs` mod in the `mods/` folder has a full cow and a zombie (one that chases you and burns in the sun) to copy from. Everything is in [Reference §13a](#13a-mobs-and-other-entities).

## C15. Your own terrain

A complete world generator: rolling grass hills, sand beaches and a sea. It replaces the game's terrain, so put it in a new game, or in a mod that loads after the game's terrain mod.

For a much bigger example, read Craft & Ruin's own generator, `games/craft_and_ruin/mods/terrain_gen/mapgen.lua`. Smooth noise picks each column's continentalness, erosion, temperature and humidity, and the biome follows from those. Its biomes are oceans, beaches, rivers, plains, forests, birch forests, taiga, snowy plains and taiga, hills, mountains with snowy peaks, savannas, deserts, badlands mesas, jungles, swamps and basalt hills. The basalt hills are volcanoes with their tops blown off, a lava pool in the crater and a lava river down one side. While that game runs, other mods can call `get_biome(x, z)` (the biome's name), `get_surface_height(x, z)` and `find_volcano(x, z)` (the nearest volcano's `x, z`, rim height and radius, or nil).

```lua
local SEA = 62
local seed = 0
function set_world_seed(s) seed = s end

-- Smooth noise from sine waves: fine for a start; real mapgens use Perlin or simplex noise
local function height_at(x, z)
    local h = math.sin((x + seed % 1000) * 0.02) * 8 + math.cos((z - seed % 777) * 0.025) * 8
            + math.sin((x + z) * 0.07) * 3
    return math.floor(SEA + 2 + h)
end

function generate_chunk(chunk, cx, cz)
    local id = engine.get_block_id
    local bedrock, stone, dirt = id("bedrock"), id("stone"), id("dirt")
    local grass, sand, water = id("grass"), id("sand"), id("water")

    for x = 0, 15 do
        for z = 0, 15 do
            local top = height_at(cx * 16 + x, cz * 16 + z)
            local beach = top <= SEA + 1
            chunk:set_block(x, 0, z, bedrock)
            for y = 1, top do
                local b = stone
                if y > top - 4 then b = beach and sand or dirt end
                if y == top and not beach then b = grass end
                chunk:set_block(x, y, z, b)
            end
            for y = top + 1, SEA do chunk:set_block(x, y, z, water) end
        end
    end
end
```

This version doesn't chain `generate_chunk` on purpose: it *replaces* the terrain. Caves are still carved afterwards. Switch them off with `world.set_caves_enabled(false)`, or tune them with `world.set_cave_settings{...}`. See [Reference §5](#5-world-generation).

## C16. Doing something later (timers)

Lua has no built-in "wait". Keep a list of things to do and count down in `on_update`:

```lua
local jobs = {}

function after(seconds, fn, ...)            -- a global: other files of your mod can use it
    jobs[#jobs + 1] = { left = seconds, fn = fn, args = { ... } }
end

local previous_update = on_update
function on_update(dt, ...)
    if previous_update then previous_update(dt, ...) end
    local due = {}
    for i = #jobs, 1, -1 do                 -- backwards, so removing is safe
        local j = jobs[i]
        j.left = j.left - dt
        if j.left <= 0 then due[#due + 1] = j; table.remove(jobs, i) end
    end
    for _, j in ipairs(due) do              -- run them after the loop: a job may add new jobs
        local ok, err = pcall(j.fn, table.unpack(j.args))
        if not ok then print("[Timers] " .. tostring(err)) end
    end
end

-- Example: a bomb block that explodes 3 seconds after you place it
local previous_placed = on_block_placed
function on_block_placed(x, y, z, block, ...)
    if block == "bomb" and engine.is_authority() then
        after(3, function()
            for dx = -1, 1 do for dy = -1, 1 do for dz = -1, 1 do
                if engine.get_block_name(x + dx, y + dy, z + dz) ~= "bedrock" then
                    engine.set_block(x + dx, y + dy, z + dz, "air")
                end
            end end end
        end)
    end
    if previous_placed then previous_placed(x, y, z, block, ...) end
end
```

Timers are lost when the game closes. For something that must survive a restart, such as a crop growing over an hour, save it with `engine.set_storage`.

## C17. Growing plants (world changes over time)

Saplings that grow into a small tree after a while, on the authority only, so it happens exactly once in multiplayer. It uses `after` from C16.

```lua
local function grow_tree(x, y, z)
    if engine.get_block_name(x, y, z) ~= "sapling_oak" then return end   -- dug up meanwhile
    for i = 0, 4 do engine.set_block(x, y + i, z, "log_oak") end
    for dx = -2, 2 do for dz = -2, 2 do for dy = 3, 5 do
        if math.abs(dx) + math.abs(dz) + (dy - 3) <= 3 and engine.is_replaceable(x + dx, y + dy, z + dz) then
            engine.set_block(x + dx, y + dy, z + dz, "leaves_oak")
        end
    end end end
end

local previous_placed = on_block_placed
function on_block_placed(x, y, z, block, ...)
    if block == "sapling_oak" and engine.is_authority() then
        after(math.random(60, 180), grow_tree, x, y, z)
    end
    if previous_placed then previous_placed(x, y, z, block, ...) end
end
```

`engine.is_replaceable` is true for air, fluids and plants, so the tree never deletes solid blocks.

## C18. A light that follows something

Point lights glow without being a block: a magic missile, a glowing mob, a lantern you carry. Here, a soft blue light floats above the player at night.

```lua
local glow = nil
local previous_update = on_update
function on_update(dt, ...)
    if previous_update then previous_update(dt, ...) end
    local x, y, z = engine.get_player_position()
    local night = sky.get_time() < 0.5        -- time 0..1: 0.25 is midnight, 0.75 is midday
    if night and not glow then
        glow = sky.add_light(x, y + 2.5, z, 0.4, 0.6, 1.0, 8, 1.0)     -- r, g, b, radius, intensity
    elseif not night and glow then
        sky.remove_light(glow); glow = nil
    end
    if glow then sky.move_light(glow, x, y + 2.5, z) end
end
```

Up to 16 point lights show at once, and they shine through walls. For lights that stay in one place, use a block with `light = ...` instead (C2, C3).

## C19. A client mod: light level and coordinates

A small HUD that shows your coordinates and the light level where you stand, red when it's dark enough for monsters (7 or less, like Minecraft). Press **L** or type `.light` to show or hide it. It's a **client mod**: put it in `clientmods/lightlevel/init.lua`, then switch it on on the Mods screen. It works on any server that allows client mods, because it only reads and draws.

```lua
local storage = client.get_mod_storage()
local shown = storage:get("shown") ~= "false"

local function toggle()
    shown = not shown
    storage:set("shown", tostring(shown))           -- remembered next time
    return true, shown and "Light level on" or "Light level off"
end

client.register_chat_command("light", { description = "Show or hide the light level", func = toggle })

client.register_on_key(function(key, action)
    if key == ui.keys.L and action == ui.actions.PRESS then toggle() end
end)

client.register_on_hud(function(W, H)
    if not shown then return end
    local x, y, z = client.get_player_position()
    local bx, by, bz = math.floor(x), math.floor(y), math.floor(z)
    local level = client.get_light_level(bx, by, bz)      -- nil if the server limits the range
    local s = 0.32
    local line1 = string.format("XYZ  %d  %d  %d", bx, by, bz)
    local line2 = level and ("Light  " .. level) or "Light  ?"
    local w = math.max(ui.text_width(line1, s), ui.text_width(line2, s)) + 16
    local h = ui.text_height(s) * 2 + 18
    ui.draw_rect(10, 10, w, h, 0, 0, 0, 0.45)
    ui.draw_text(line1, 18, 16, s, 1, 1, 1)
    local dark = level and level <= 7
    ui.draw_text(line2, 18, 22 + ui.text_height(s), s, 1, dark and 0.35 or 1, dark and 0.35 or 0.6)
end)
```

**Ideas for more:**
- **Chest tracker:** `client.register_on_right_click_block` sees every block you right-click. Remember where your chests are in the mod storage, and list them with a `.chests` command.
- **Waypoints and minimap:** see `clientmods/minimap`.
- **Chat highlighter:** `client.register_on_chat_message(function(text) ... end)` sees every chat line. Play a sound when someone says your name.

## C20. A rearrangeable inventory (drag and drop)

A backpack with 27 slots that opens with **B**. Drag items between the backpack and the hotbar; the same item stacks (up to 64), anything else swaps; Shift+click sends an item to the other side. It's saved with the world. Uses [drag and drop](#drag-and-drop) and [frame input](#everything-that-happened-this-frame).

```lua
-- mods/backpack/init.lua
local kit = require("ui_kit")

-- The backpack's 27 slots, kept with the world: one "name*count" per line
local SLOTS = 27
local pack = {}

local function load_pack()
    pack = {}
    local i = 0
    for line in ((engine.get_storage("backpack:slots") or "") .. "\n"):gmatch("(.-)\n") do
        i = i + 1
        local name, count = line:match("^(.-)%*(%d+)$")
        if name and name ~= "" then pack[i] = { name = name, count = tonumber(count) } end
    end
end

local function save_pack()
    local lines = {}
    for i = 1, SLOTS do
        local s = pack[i]
        lines[i] = s and (s.name .. "*" .. s.count) or ""
    end
    engine.set_storage("backpack:slots", table.concat(lines, "\n"))
end

-- One way to read and write any slot, wherever it lives
local function get(where, i)
    if where == "pack" then return pack[i] end
    local name, count = hotbar.get_slot(i)
    if name == "" then return nil end
    return { name = name, count = count }
end

local function put(where, i, s)
    if where == "pack" then pack[i] = s
    else hotbar.set_slot(i, s and s.name or "air", s and s.count or 0) end
end

-- Dropping slot A on slot B: the same item stacks (up to 64), anything else swaps
local function move(from, to)
    if from.where == to.where and from.i == to.i then return end
    local a, b = get(from.where, from.i), get(to.where, to.i)
    if a and b and a.name == b.name then
        local n = math.min(64 - b.count, a.count)
        b.count, a.count = b.count + n, a.count - n
        put(to.where, to.i, b)
        put(from.where, from.i, a.count > 0 and a or nil)
    else
        put(to.where, to.i, a)
        put(from.where, from.i, b)
    end
    save_pack()
end

local backpack = kit.form{
    id = "backpack:main", width = 9.8, height = 5.2, title = "Backpack",
    on_open = load_pack,
    draw = function(f)
        local function slot(where, i, x, y)
            local s = get(where, i)
            local here = { where = where, i = i }
            local click, dropped = f:item(x, y, 1, s and s.name, {
                id = where .. i,
                count = s and s.count,
                drag = here,          -- this slot can be picked up...
                drop = true,          -- ...and things can be put down on it
            })
            if dropped then move(dropped, here) end
            if click and click.shift and s then           -- Shift+click: to the first free slot on the other side
                local other = where == "pack" and "hotbar" or "pack"
                local n = other == "pack" and SLOTS or hotbar.get_slot_count()
                for j = 1, n do
                    if not get(other, j) then move(here, { where = other, i = j }) break end
                end
            end
        end
        for i = 1, SLOTS do
            slot("pack", i, ((i - 1) % 9) * 1.1, math.floor((i - 1) / 9) * 1.1)
        end
        f:label(0, 3.55, "Hotbar", { color = kit.theme.muted })
        for i = 1, math.min(9, hotbar.get_slot_count()) do
            slot("hotbar", i, (i - 1) * 1.1, 4.2)
        end
    end,
}

-- Open it with B (key events only arrive while chat is closed, so typing "b" in chat is safe)
local previous_update = on_update
function on_update(dt, ...)
    for _, k in ipairs(ui.get_key_events()) do
        if k.key == ui.keys.B and k.action == "press" and not engine.is_inventory_open() then
            backpack:open()
        end
    end
    if previous_update then return previous_update(dt, ...) end
end
```

How it works:
- **Every slot is both a thing you can pick up and a place you can drop on** (`drag = here, drop = true`). The payload says where the slot is, `{ where = "pack" | "hotbar", i = n }`, so one `move(from, to)` handles every combination: backpack to hotbar, hotbar to backpack, within either.
- **`get` and `put` hide where slots live.** The backpack is a Lua table, saved with `engine.set_storage`; the hotbar is the engine's (`hotbar.get_slot` / `set_slot`). Add a chest, armour slots or a crafting grid the same way: a new `where` in `get` and `put`.
- **The kit tells drags and clicks apart:** pressing and letting go without moving is a click (`click.shift` for Shift+click); moving a few pixels while held is a drag.
- **Making any existing inventory rearrangeable** is the same few lines: in its draw function, give each `f:item` a `drag` payload naming the slot and `drop = true`, and when `dropped` comes back, swap (or stack) the two slots in your data. If the screen is drawn by hand rather than as a form, wrap it with [`kit.embed`](#the-kit-in-any-screen-kitembed).

Ideas: a slot that only takes some items (`accept = function(p) return is_armour(get(p.where, p.i)) end`); right-click to split a stack in half (`click.button == "right"`); a trash slot that's only a `f:drop_target`.

## C21. A laptop with its own desktop (windows)

Right-click a laptop block and its screen fills yours: a wallpaper, desktop icons (double-click to open), windows you can move, resize, minimize, maximize and close, a taskbar with a Start menu, one button per open window and a clock. Two apps come with it, Notes and Paint, and adding one is adding a function. Uses [windows](#windows), [icons and menus](#scroll-areas-icons-and-menus) and the new [shapes](#142-drawing).

```lua
-- mods/laptop/init.lua
local kit = require("ui_kit")
engine.register_block("laptop", 1, 1, 1, "laptop.png", nil, { hardness = 1 })

local apps = {
    { id = "notes", name = "Notes", icon = "laptop_notes.png", w = 10, h = 8 },
    { id = "paint", name = "Paint", icon = "laptop_paint.png", w = 11, h = 9 },
    { id = "about", name = "About", icon = "laptop_about.png", w = 8, h = 4 },
}

-- What each app draws inside its window. wf is the window's frame: (0, 0) is its top-left.
local draw_app = {}

function draw_app.notes(wf, win, s)
    s.notes = s.notes or {}
    wf:list("lines", 0.3, 0.3, win.w - 0.6, win.h - 2, s.notes, nil)
    local submitted
    s.draft, submitted = wf:field("new", 0.3, win.h - 1.4, win.w - 0.6, 1, s.draft,
                                  { placeholder = "Type a note, press Enter" })
    if submitted and s.draft ~= "" then
        s.notes[#s.notes + 1] = s.draft
        s.draft = ""
    end
end

local palette = { { 0, 0, 0, 1 }, { 1, 1, 1, 1 }, { 0.9, 0.2, 0.2, 1 }, { 0.2, 0.7, 0.3, 1 }, { 0.2, 0.4, 0.9, 1 }, { 1, 0.85, 0.2, 1 } }
function draw_app.paint(wf, win, s)
    s.pixels = s.pixels or {}
    s.color = s.color or 1
    for i, c in ipairs(palette) do
        wf:box(0.3 + (i - 1) * 0.9, 0.3, 0.7, 0.7, c, s.color == i and kit.theme.focus_border or nil)
        if wf:take_click(0.3 + (i - 1) * 0.9, 0.3, 0.7, 0.7) then s.color = i end
    end
    local cols, rows = 24, 14
    local cell = math.min((win.w - 0.6) / cols, (win.h - 1.5) / rows)
    local ox, oy = 0.3, 1.2
    wf:rect(ox, oy, cols * cell, rows * cell, { 1, 1, 1, 1 })
    -- Start painting with a click on the canvas; keep painting while the button is held
    if wf:take_click(ox, oy, cols * cell, rows * cell) then s.painting = true end
    if not ui.is_mouse_down("left") then s.painting = false end
    for y = 0, rows - 1 do
        for x = 0, cols - 1 do
            local key = x .. "," .. y
            if s.painting and wf:hovered(ox + x * cell, oy + y * cell, cell, cell) then s.pixels[key] = s.color end
            if s.pixels[key] then wf:rect(ox + x * cell, oy + y * cell, cell, cell, palette[s.pixels[key]]) end
        end
    end
    if wf:hovered(ox, oy, cols * cell, rows * cell) then ui.set_cursor("crosshair") end
end

function draw_app.about(wf, win)
    wf:text(0.4, 0.4, win.w - 0.8, "BlockOS 1.0\nMade with ui_kit windows.")
end

local desktop = kit.form{
    id = "laptop:desktop", fullscreen = true,
    draw = function(f, s)
        local W, H = f.w_units, f.h_units
        -- wallpaper
        local x0, y0 = f:px(0, 0)
        ui.draw_gradient(x0, y0, f:pw(W), f:pw(H), { 0.10, 0.25, 0.45, 1 }, { 0.30, 0.10, 0.35, 1 })
        f:set_work_area(0, 0, W, H - 1.2)      -- maximized windows stop above the taskbar

        -- desktop icons: double-click opens the app (a second open just brings it to the front)
        for i, app in ipairs(apps) do
            local what = f:icon(app.id, 1, 1 + (i - 1) * 3, 1.6, app.icon, app.name, { selected = s.selected == app.id })
            if what == "select" then s.selected = app.id end
            if what == "open" then s.open[app.id] = true; f:focus_window(app.id) end
        end

        -- one window per open app
        for _, app in ipairs(apps) do
            if s.open[app.id] then
                local still_open = f:window(app.id, { title = app.name, x = 5, y = 2, w = app.w, h = app.h }, function(wf, win)
                    s.apps[app.id] = s.apps[app.id] or {}
                    draw_app[app.id](wf, win, s.apps[app.id])
                end)
                if not still_open then s.open[app.id] = false end
            end
        end

        -- the taskbar: a frameless window that's always on top
        f:window("taskbar", { frame = false, topmost = true, x = 0, y = H - 1.2, w = W, h = 1.2,
                              background = { 0.08, 0.08, 0.10, 0.95 } }, function(tb)
            if tb:button("start", 0.1, 0.1, 3, 1, "Start") then tb:open_popup("start", 0.1, -3.7) end
            local choice = tb:popup("start", { "Notes", "Paint", "About", "-", "Shut down" })
            if choice == 5 then f:close()
            elseif choice then s.open[apps[choice].id] = true; f:focus_window(apps[choice].id) end
            for i, w in ipairs(f:window_list()) do               -- a button per window
                if tb:button("task_" .. w.id, 3.3 + (i - 1) * 4.1, 0.1, 4, 1, w.title, { selected = w.focused }) then
                    if w.focused then f:minimize_window(w.id) else f:focus_window(w.id) end
                end
            end
            local minutes = math.floor((sky.get_time() - 0.25) * 24 * 60) % (24 * 60)   -- 0.25 is midnight
            tb:label(W - 3, 0.4, string.format("%02d:%02d", minutes // 60, minutes % 60))
        end)
    end,
}

-- Each laptop keeps its own files while the game runs
local laptops = {}
local previous_right_click = on_right_click_block
function on_right_click_block(x, y, z, block, held, sneaking, repeat_click, ...)
    if block == "laptop" and not sneaking then
        if not repeat_click then
            local key = x .. "," .. y .. "," .. z
            laptops[key] = laptops[key] or { open = {}, apps = {} }
            desktop:open(laptops[key])
        end
        return true
    end
    if previous_right_click then return previous_right_click(x, y, z, block, held, sneaking, repeat_click, ...) end
    return false
end
```

How it works:
- **The desktop is a `fullscreen` form.** Everything is in units, and `f.w_units`, `f.h_units` say how many fit on this player's screen, so the layout works at any window size.
- **An app is a window whose content is a function.** `s.open[app]` says which apps are open; each frame, every open app calls `f:window`, and when its close button is clicked, `f:window` returns `false` and the app is marked closed. Window positions, sizes and stacking are kept by the kit.
- **The taskbar is a window too**, frameless (`frame = false`) and always on top (`topmost = true`), so other windows go under it. `f:set_work_area` makes maximized windows stop above it. `f:window_list()` gives it one button per window: click to bring a window forward, or click the front one to minimize it, as on a real desktop.
- **Paint** starts painting on a click in the canvas (`take_click`) and keeps going while the button is held (`ui.is_mouse_down`), for every cell the mouse passes over. Because input goes to the window on top, painting never happens through another window.
- **Each laptop has its own state** (`laptops[x,y,z]`): open apps, notes and drawings, while the game runs. Save it with `engine.set_storage` to keep it across restarts.

Ideas: a file manager (icons + [drag and drop](#drag-and-drop) onto folder icons, right-click menus); a terminal (a `f:scroll_area` of lines and a field); a web browser that shows pages from your GitHub repository with [`remote.fetch`](#21-online-content) (JSON and pictures only: nothing downloaded is ever run); a chat app that talks to other players' laptops through server-side code.

---

# Part 3: Reference

Every function, grouped by what it's for. Each section is complete on its own, so you can jump straight in from the index.

## Function index

Type in the search box (in `MODDING.html`) or use your browser's find (Ctrl+F) to jump to any function.

Every function and hook, by table, each linking to where it's explained. `lua_api.txt` (written by the engine at startup) lists what your build has.

**engine** (131)  
[`engine.add_model`](#fn-engine-add_model) · [`engine.add_player_velocity`](#fn-engine-add_player_velocity) · [`engine.allow_firewall`](#fn-engine-allow_firewall) · [`engine.back_to_title`](#fn-engine-back_to_title) · [`engine.cancel_join`](#fn-engine-cancel_join) · [`engine.close_settings`](#fn-engine-close_settings) · [`engine.create_server_profile`](#fn-engine-create_server_profile) · [`engine.create_world`](#fn-engine-create_world) · [`engine.delete_server_profile`](#fn-engine-delete_server_profile) · [`engine.delete_world`](#fn-engine-delete_world) · [`engine.draw_text`](#fn-engine-draw_text) · [`engine.find_texture`](#fn-engine-find_texture) · [`engine.get_active_world`](#fn-engine-get_active_world) · [`engine.get_block_def`](#fn-engine-get_block_def) · [`engine.get_block_description`](#fn-engine-get_block_description) · [`engine.set_block_name_display`](#fn-engine-set_block_name_display) · [`engine.get_block_id`](#fn-engine-get_block_id) · [`engine.get_block_name`](#fn-engine-get_block_name) · [`engine.get_connection_status`](#fn-engine-get_connection_status) · [`engine.get_dig_progress`](#fn-engine-get_dig_progress) · [`engine.get_fluid`](#fn-engine-get_fluid) · [`engine.get_game_info`](#fn-engine-get_game_info) · [`engine.get_keybinds`](#fn-engine-get_keybinds) · [`engine.get_lan_games`](#fn-engine-get_lan_games) · [`engine.get_light`](#fn-engine-get_light) · [`engine.get_light_level`](#fn-engine-get_light_level) · [`engine.get_metadata`](#fn-engine-get_metadata) · [`engine.get_network_info`](#fn-engine-get_network_info) · [`engine.get_online_players`](#fn-engine-get_online_players) · [`engine.get_player_info`](#fn-engine-get_player_info) · [`engine.get_player_list`](#fn-engine-get_player_list) · [`engine.get_player_mode`](#fn-engine-get_player_mode) · [`engine.get_player_name`](#fn-engine-get_player_name) · [`engine.get_player_position`](#fn-engine-get_player_position) · [`engine.get_player_state`](#fn-engine-get_player_state) · [`engine.get_server_game_mode`](#fn-engine-get_server_game_mode) · [`engine.get_server_info`](#fn-engine-get_server_info) · [`engine.get_server_profile`](#fn-engine-get_server_profile) · [`engine.get_server_settings`](#fn-engine-get_server_settings) · [`engine.get_server_status`](#fn-engine-get_server_status) · [`engine.get_shared`](#fn-engine-get_shared) · [`engine.get_storage`](#fn-engine-get_storage) · [`engine.get_swing`](#fn-engine-get_swing) · [`engine.get_texture_pack`](#fn-engine-get_texture_pack) · [`engine.get_video_settings`](#fn-engine-get_video_settings) · [`engine.get_worlds`](#fn-engine-get_worlds) · [`engine.give_item`](#fn-engine-give_item) · [`engine.has_player_password`](#fn-engine-has_player_password) · [`engine.has_server_build`](#fn-engine-has_server_build) · [`engine.has_server_icon`](#fn-engine-has_server_icon) · [`engine.host_game`](#fn-engine-host_game) · [`engine.is_authority`](#fn-engine-is_authority) · [`engine.is_creative`](#fn-engine-is_creative) · [`engine.is_fluid_source`](#fn-engine-is_fluid_source) · [`engine.is_flying`](#fn-engine-is_flying) · [`engine.is_in_game`](#fn-engine-is_in_game) · [`engine.is_inventory_open`](#fn-engine-is_inventory_open) · [`engine.is_left_handed`](#fn-engine-is_left_handed) · [`engine.is_opaque`](#fn-engine-is_opaque) · [`engine.is_operator`](#fn-engine-is_operator) · [`engine.is_player_operator`](#fn-engine-is_player_operator) · [`engine.is_outdoors`](#fn-engine-is_outdoors) · [`engine.is_paused`](#fn-engine-is_paused) · [`engine.is_replaceable`](#fn-engine-is_replaceable) · [`engine.is_scanning_lan`](#fn-engine-is_scanning_lan) · [`engine.is_server`](#fn-engine-is_server) · [`engine.is_third_person`](#fn-engine-is_third_person) · [`engine.get_camera_mode`](#fn-engine-get_camera_mode) · [`engine.set_camera_mode`](#fn-engine-set_camera_mode) · [`engine.set_player_look`](#fn-engine-set_player_look) · [`engine.get_player_look`](#fn-engine-get_player_look) · [`engine.set_hud_visible`](#fn-engine-set_hud_visible) · [`engine.is_hud_visible`](#fn-engine-is_hud_visible) · [`engine.is_world_generator`](#fn-engine-is_world_generator) · [`engine.join_server`](#fn-engine-join_server) · [`engine.key_name`](#fn-engine-key_name) · [`engine.kick_player`](#fn-engine-kick_player) · [`engine.list_blocks`](#fn-engine-list_blocks) · [`engine.list_fluids`](#fn-engine-list_fluids) · [`engine.list_games`](#fn-engine-list_games) · [`engine.list_mods`](#fn-engine-list_mods) · [`engine.list_mods_for_game`](#fn-engine-list_mods_for_game) · [`engine.get_world_mods`](#fn-engine-get_world_mods) · [`engine.set_world_mod`](#fn-engine-set_world_mod) · [`engine.list_server_profiles`](#fn-engine-list_server_profiles) · [`engine.list_servers`](#fn-engine-list_servers) · [`engine.list_texture_packs`](#fn-engine-list_texture_packs) · [`engine.list_worlds`](#fn-engine-list_worlds) · [`engine.load_animations`](#fn-engine-load_animations) · [`engine.message_player`](#fn-engine-message_player) · [`engine.player_set_block`](#fn-engine-player_set_block) · [`engine.load_model`](#fn-engine-load_model) · [`engine.move_model`](#fn-engine-move_model) · [`engine.open_mods_folder`](#fn-engine-open_mods_folder) · [`engine.open_server_profile_folder`](#fn-engine-open_server_profile_folder) · [`engine.open_settings`](#fn-engine-open_settings) · [`engine.open_texture_packs_folder`](#fn-engine-open_texture_packs_folder) · [`engine.override_block`](#fn-engine-override_block) · [`engine.pause`](#fn-engine-pause) · [`engine.ping_server`](#fn-engine-ping_server) · [`engine.play_world`](#fn-engine-play_world) · [`engine.quit`](#fn-engine-quit) · [`engine.quit_to_menu`](#fn-engine-quit_to_menu) · [`engine.register_abm`](#fn-engine-register_abm) · [`engine.register_random_tick`](#fn-engine-register_random_tick) · [`engine.drop_item`](#fn-engine-drop_item) · [`engine.register_block`](#fn-engine-register_block) · [`engine.register_lbm`](#fn-engine-register_lbm) · [`engine.register_fluid`](#fn-engine-register_fluid) · [`engine.register_fluid_reaction`](#fn-engine-register_fluid_reaction) · [`engine.register_item_use`](#fn-engine-register_item_use) · [`engine.remove_model`](#fn-engine-remove_model) · [`engine.request_harvest`](#fn-engine-request_harvest) · [`engine.reset_keybinds`](#fn-engine-reset_keybinds) · [`engine.respawn_player`](#fn-engine-respawn_player) · [`engine.resume`](#fn-engine-resume) · [`engine.save_server_profile`](#fn-engine-save_server_profile) · [`engine.save_servers`](#fn-engine-save_servers) · [`engine.save_world`](#fn-engine-save_world) · [`engine.send_chat`](#fn-engine-send_chat) · [`engine.set_active_world`](#fn-engine-set_active_world) · [`engine.set_ambient_occlusion`](#fn-engine-set_ambient_occlusion) · [`engine.set_block`](#fn-engine-set_block) · [`engine.set_block_meta`](#fn-engine-set_block_meta) · [`engine.set_creative`](#fn-engine-set_creative) · [`engine.set_dig_crack_texture`](#fn-engine-set_dig_crack_texture) · [`engine.set_first_person_arm`](#fn-engine-set_first_person_arm) · [`engine.set_first_person_hand`](#fn-engine-set_first_person_hand) · [`engine.set_third_person_item`](#fn-engine-set_third_person_item) · [`engine.set_fly_allowed`](#fn-engine-set_fly_allowed) · [`engine.set_flying`](#fn-engine-set_flying) · [`engine.set_footstep_distance`](#fn-engine-set_footstep_distance) · [`engine.set_fov`](#fn-engine-set_fov) · [`engine.set_inventory_open`](#fn-engine-set_inventory_open) · [`engine.set_keybind`](#fn-engine-set_keybind) · [`engine.set_mod_enabled`](#fn-engine-set_mod_enabled) · [`engine.set_model_visible`](#fn-engine-set_model_visible) · [`engine.set_moon_texture`](#fn-engine-set_moon_texture) · [`engine.set_place_repeat`](#fn-engine-set_place_repeat) · [`engine.set_player_controls`](#fn-engine-set_player_controls) · [`engine.set_player_name`](#fn-engine-set_player_name) · [`engine.set_player_mode`](#fn-engine-set_player_mode) · [`engine.set_player_password`](#fn-engine-set_player_password) · [`engine.set_player_position`](#fn-engine-set_player_position) · [`engine.set_server_setting`](#fn-engine-set_server_setting) · [`engine.set_shared`](#fn-engine-set_shared) · [`engine.set_smooth_lighting`](#fn-engine-set_smooth_lighting) · [`engine.set_storage`](#fn-engine-set_storage) · [`engine.set_sun_texture`](#fn-engine-set_sun_texture) · [`engine.set_texture_pack`](#fn-engine-set_texture_pack) · [`engine.set_video_setting`](#fn-engine-set_video_setting) · [`engine.start_game`](#fn-engine-start_game) · [`engine.start_server_profile`](#fn-engine-start_server_profile) · [`engine.stop_hosting`](#fn-engine-stop_hosting) · [`engine.stop_server_profile`](#fn-engine-stop_server_profile) · [`engine.swing_hand`](#fn-engine-swing_hand) · [`engine.take_world_error`](#fn-engine-take_world_error) · [`engine.teleport_player`](#fn-engine-teleport_player) · [`engine.write_api_reference`](#fn-engine-write_api_reference)

**remote** (8)  
[`remote.check_url`](#fn-remote-check_url) · [`remote.fetch`](#fn-remote-fetch) · [`remote.get_cached`](#fn-remote-get_cached) · [`remote.is_available`](#fn-remote-is_available) · [`remote.null`](#fn-remote-null) · [`remote.refresh`](#fn-remote-refresh) · [`remote.unwatch`](#fn-remote-unwatch) · [`remote.watch`](#fn-remote-watch)

**creative** (the creative inventory, from the game's inventory mod)  
[`creative.get_held`](#fn-creative-hold) · [`creative.hold`](#fn-creative-hold) · [`creative.register_category`](#fn-creative-register_category) · [`creative.register_tab`](#fn-creative-register_tab) · [`creative.set_category`](#fn-creative-set_category)

**ui_kit** (`require("ui_kit")`)  
[`kit.embed`](#fn-uikit-embed) · [`kit.form`](#fn-uikit-form) · [`kit.theme`](#fn-uikit-theme) · [`form:open`](#fn-uikit-open) · [`f:button`](#fn-uikit-button) · [`f:button_width`](#fn-uikit-button_width) · [`f:text_width`](#fn-uikit-text_width) · [`f:checkbox`](#fn-uikit-checkbox) · [`f:draggable`](#fn-uikit-draggable) · [`f:dragging`](#fn-uikit-dragging) · [`f:drop_target`](#fn-uikit-drop_target) · [`f:field`](#fn-uikit-field) · [`f:focus_window`](#fn-uikit-focus_window) · [`f:icon`](#fn-uikit-icon) · [`f:image`](#fn-uikit-image) · [`f:item`](#fn-uikit-item) · [`f:label`](#fn-uikit-label) · [`f:list`](#fn-uikit-list) · [`f:maximize_window`](#fn-uikit-maximize_window) · [`f:window_state`](#fn-uikit-window_state) · [`f:popup`](#fn-uikit-popup) · [`f:scroll_area`](#fn-uikit-scroll_area) · [`f:set_work_area`](#fn-uikit-set_work_area) · [`f:slider`](#fn-uikit-slider) · [`f:take_click`](#fn-uikit-take_click) · [`f:take_scroll`](#fn-uikit-take_scroll) · [`f:text`](#fn-uikit-text) · [`f:window`](#fn-uikit-window) · [`f:window_list`](#fn-uikit-window_list) · [`kit.selection`](#fn-uikit-selection) · [`kit.icon_grid`](#fn-uikit-icon_grid) · [`kit.overlaps`](#fn-uikit-overlaps) · [`f:marquee`](#fn-uikit-marquee) · [`f:selected_windows`](#fn-uikit-selected_windows)

**world** (11)  
[`world.get_cave_settings`](#fn-world-get_cave_settings) · [`world.get_caves_enabled`](#fn-world-get_caves_enabled) · [`world.get_falling_blocks`](#fn-world-get_falling_blocks) · [`world.get_loaded_chunks`](#fn-world-get_loaded_chunks) · [`world.get_pending_chunks`](#fn-world-get_pending_chunks) · [`world.get_render_distance`](#fn-world-get_render_distance) · [`world.set_cave_settings`](#fn-world-set_cave_settings) · [`world.set_caves_enabled`](#fn-world-set_caves_enabled) · [`world.set_load_budget`](#fn-world-set_load_budget) · [`world.set_mesh_budget`](#fn-world-set_mesh_budget) · [`world.set_render_distance`](#fn-world-set_render_distance)

**ui** (37)  
[`ui.draw_block_icon`](#fn-ui-draw_block_icon) · [`ui.draw_button`](#fn-ui-draw_button) · [`ui.draw_circle`](#fn-ui-draw_circle) · [`ui.draw_gradient`](#fn-ui-draw_gradient) · [`ui.draw_image`](#fn-ui-draw_image) · [`ui.draw_line`](#fn-ui-draw_line) · [`ui.draw_pie`](#fn-ui-draw_pie) · [`ui.draw_polygon`](#fn-ui-draw_polygon) · [`ui.draw_rect`](#fn-ui-draw_rect) · [`ui.draw_rect_outline`](#fn-ui-draw_rect_outline) · [`ui.draw_ring`](#fn-ui-draw_ring) · [`ui.draw_rounded_rect`](#fn-ui-draw_rounded_rect) · [`ui.draw_server_icon`](#fn-ui-draw_server_icon) · [`ui.draw_text`](#fn-ui-draw_text) · [`ui.draw_triangle`](#fn-ui-draw_triangle) · [`ui.get_all_blocks`](#fn-ui-get_all_blocks) · [`ui.get_block_info`](#fn-ui-get_block_info) · [`ui.get_button_text_color`](#fn-ui-get_button_text_color) · [`ui.get_clipboard`](#fn-ui-get_clipboard) · [`ui.get_image_size`](#fn-ui-get_image_size) · [`ui.get_key_events`](#fn-ui-get_key_events) · [`ui.get_mouse`](#fn-ui-get_mouse) · [`ui.get_mouse_events`](#fn-ui-get_mouse_events) · [`ui.get_mouse_pos`](#fn-ui-get_mouse_pos) · [`ui.get_scroll`](#fn-ui-get_scroll) · [`ui.get_time`](#fn-ui-get_time) · [`ui.get_typed_text`](#fn-ui-get_typed_text) · [`ui.is_key_down`](#fn-ui-is_key_down) · [`ui.is_mouse_down`](#fn-ui-is_mouse_down) · [`ui.keys`](#fn-ui-keys) · [`ui.mods`](#fn-ui-mods) · [`ui.pop_clip`](#fn-ui-pop_clip) · [`ui.push_clip`](#fn-ui-push_clip) · [`ui.set_clipboard`](#fn-ui-set_clipboard) · [`ui.set_cursor`](#fn-ui-set_cursor) · [`ui.text_height`](#fn-ui-text_height) · [`ui.text_width`](#fn-ui-text_width)

**hotbar** (17)  
[`hotbar.get_active_slot`](#fn-hotbar-get_active_slot) · [`hotbar.get_rect`](#fn-hotbar-get_rect) · [`hotbar.get_size`](#fn-hotbar-get_size) · [`hotbar.get_slot_block`](#fn-hotbar-get_slot_block) · [`hotbar.get_slot_count`](#fn-hotbar-get_slot_count) · [`hotbar.get_slot`](#fn-hotbar-get_slot) · [`hotbar.is_creative_open`](#fn-hotbar-is_creative_open) · [`hotbar.set_active_slot`](#fn-hotbar-set_active_slot) · [`hotbar.set_creative_open`](#fn-hotbar-set_creative_open) · [`hotbar.set_position`](#fn-hotbar-set_position) · [`hotbar.set_selected_texture`](#fn-hotbar-set_selected_texture) · [`hotbar.set_size`](#fn-hotbar-set_size) · [`hotbar.set_slot_size`](#fn-hotbar-set_slot_size) · [`hotbar.set_slot`](#fn-hotbar-set_slot) · [`hotbar.set_texture_scale`](#fn-hotbar-set_texture_scale) · [`hotbar.set_texture`](#fn-hotbar-set_texture) · [`hotbar.toggle_creative`](#fn-hotbar-toggle_creative)

**chat** (2)  
[`chat.register_command`](#fn-chat-register_command) · [`chat.send`](#fn-chat-send)

**entities** (13, and the `self` functions)  
[`entities.register`](#fn-entities-register) · [`entities.spawn`](#fn-entities-spawn) · [`entities.get`](#fn-entities-get) · [`entities.get_all`](#fn-entities-get) · [`entities.in_radius`](#fn-entities-in_radius) · [`entities.nearest_player`](#fn-entities-nearest_player) · [`entities.get_players`](#fn-entities-get_players) · [`entities.damage_player`](#fn-entities-damage_player) · [`entities.drop_item`](#fn-entities-drop_item) · [`entities.get_attack_damage`](#fn-entities-get_attack_damage) · [`entities.register_spawn`](#fn-entities-register_spawn) · [`entities.line_of_sight`](#fn-entities-line_of_sight) · [`entities.find_path`](#fn-entities-find_path) · [`self:go_to`](#fn-entity-go_to) · [`self:wander`](#fn-entity-wander) · [`self:path_state`](#fn-entity-path_state) · [`self:can_see`](#fn-entity-can_see) · [`self:find_land`](#fn-entity-find_land) · [`self:is_blocked`](#fn-entity-is_blocked) · [`self:walk`](#fn-entity-walk) · [`self:walk_to`](#fn-entity-walk_to) · [`self:set_animation`](#fn-entity-set_animation) · [`self:damage`](#fn-entity-damage) · [`self:get_pos`](#fn-entity-get_pos)

**sky** (35)  
[`sky.add_light`](#fn-sky-add_light) · [`sky.add_time`](#fn-sky-add_time) · [`sky.flash`](#fn-sky-flash) · [`sky.lightning_bolt`](#fn-sky-lightning_bolt) · [`sky.get_clock`](#fn-sky-get_clock) · [`sky.get_day`](#fn-sky-get_day) · [`sky.get_day_length`](#fn-sky-get_day_length) · [`sky.get_light`](#fn-sky-get_light) · [`sky.get_moon_phase`](#fn-sky-get_moon_phase) · [`sky.get_precipitation`](#fn-sky-get_precipitation) · [`sky.get_rain_intensity`](#fn-sky-get_rain_intensity) · [`sky.get_stars`](#fn-sky-get_stars) · [`sky.get_time`](#fn-sky-get_time) · [`sky.get_time_speed`](#fn-sky-get_time_speed) · [`sky.move_light`](#fn-sky-move_light) · [`sky.remove_light`](#fn-sky-remove_light) · [`sky.reset`](#fn-sky-reset) · [`sky.set_ambient_color`](#fn-sky-set_ambient_color) · [`sky.set_ambient_intensity`](#fn-sky-set_ambient_intensity) · [`sky.set_block_light_strength`](#fn-sky-set_block_light_strength) · [`sky.set_brightness`](#fn-sky-set_brightness) · [`sky.set_day_length`](#fn-sky-set_day_length) · [`sky.set_fog_color`](#fn-sky-set_fog_color) · [`sky.set_held_light`](#fn-sky-set_held_light) · [`sky.set_light`](#fn-sky-set_light) · [`sky.set_min_light`](#fn-sky-set_min_light) · [`sky.set_moon_phase`](#fn-sky-set_moon_phase) · [`sky.set_moon_textures`](#fn-sky-set_moon_textures) · [`sky.set_precipitation`](#fn-sky-set_precipitation) · [`sky.set_rain_intensity`](#fn-sky-set_rain_intensity) · [`sky.set_sky_tint`](#fn-sky-set_sky_tint) · [`sky.set_stars`](#fn-sky-set_stars) · [`sky.set_sun_color`](#fn-sky-set_sun_color) · [`sky.set_sun_intensity`](#fn-sky-set_sun_intensity) · [`sky.set_time`](#fn-sky-set_time) · [`sky.set_time_speed`](#fn-sky-set_time_speed)

**clouds** (9)  
[`clouds.set_cell_size`](#fn-clouds-set_cell_size) · [`clouds.set_clump`](#fn-clouds-set_clump) · [`clouds.set_color`](#fn-clouds-set_color) · [`clouds.set_height`](#fn-clouds-set_height) · [`clouds.set_night_color`](#fn-clouds-set_night_color) · [`clouds.set_scroll_speed`](#fn-clouds-set_scroll_speed) · [`clouds.set_texture`](#fn-clouds-set_texture) · [`clouds.set_thickness`](#fn-clouds-set_thickness) · [`clouds.set_threshold`](#fn-clouds-set_threshold) · [`clouds.set_weather`](#fn-clouds-set_weather)

**fog** (8)  
[`fog.set_day_color`](#fn-fog-set_day_color) · [`fog.set_density`](#fn-fog-set_density) · [`fog.set_enabled`](#fn-fog-set_enabled) · [`fog.set_exponential`](#fn-fog-set_exponential) · [`fog.set_night_color`](#fn-fog-set_night_color) · [`fog.set_rain_scale`](#fn-fog-set_rain_scale) · [`fog.set_range`](#fn-fog-set_range) · [`fog.set_sunset_color`](#fn-fog-set_sunset_color)

**underwater** (6)  
[`underwater.set_deep_color`](#fn-underwater-set_deep_color) · [`underwater.set_fade`](#fn-underwater-set_fade) · [`underwater.set_full_depth`](#fn-underwater-set_full_depth) · [`underwater.set_shallow_color`](#fn-underwater-set_shallow_color) · [`underwater.set_tint_strength`](#fn-underwater-set_tint_strength) · [`underwater.set_visibility`](#fn-underwater-set_visibility)

**sound** (14)  
[`sound.exists`](#fn-sound-exists) · [`sound.fade`](#fn-sound-fade) · [`sound.get_music`](#fn-sound-get_music) · [`sound.get_volume`](#fn-sound-get_volume) · [`sound.is_available`](#fn-sound-is_available) · [`sound.is_playing`](#fn-sound-is_playing) · [`sound.play`](#fn-sound-play) · [`sound.play_music`](#fn-sound-play_music) · [`sound.set_gain`](#fn-sound-set_gain) · [`sound.set_pitch`](#fn-sound-set_pitch) · [`sound.set_position`](#fn-sound-set_position) · [`sound.set_volume`](#fn-sound-set_volume) · [`sound.stop`](#fn-sound-stop) · [`sound.stop_music`](#fn-sound-stop_music)

**menu** (3)  
[`menu.get_active_game`](#fn-menu-get_active_game) · [`menu.get_games`](#fn-menu-get_games) · [`menu.set_active_game`](#fn-menu-set_active_game)

**Block light properties** (4)  
[`light`, `light_color`, `light_filter`, `light_absorb`](#fn-block-light)

**chunk (in generate_chunk)** (6)  
[`chunk:get_block`](#fn-chunk-get_block) · [`chunk:get_metadata`](#fn-chunk-get_metadata) · [`chunk:set_block`](#fn-chunk-set_block) · [`chunk:set_block_with_meta`](#fn-chunk-set_block_with_meta) · [`chunk:set_metadata`](#fn-chunk-set_metadata) · [`chunk:set_tint`](#fn-chunk-set_tint)

**model, bone and animation objects** (12)  
[`model:get_bone`](#fn-model-get_bone) · [`model:reset_pose`](#fn-model-reset_pose) · [`model:is_loaded`](#fn-model-is_loaded) · [`bone:get_rotation`](#fn-bone-get_rotation) · [`bone:set_position`](#fn-bone-set_position) · [`bone:set_rotation`](#fn-bone-set_rotation) · [`anims:get`](#fn-animationset-get) · [`anims:names`](#fn-animationset-names) · [`anim:apply`](#fn-animation-apply) · [`anim:get_length`](#fn-animation-get_length) · [`anim:get_name`](#fn-animation-get_name) · [`anim:is_looping`](#fn-animation-is_looping) · [`anim:set_loop`](#fn-animation-set_loop)

**Hooks (functions you define)** (36)  
[`can_edit_block`](#fn-hooks-can_edit_block) · [`generate_chunk`](#fn-hooks-generate_chunk) · [`get_dig_time`](#fn-hooks-get_dig_time) · [`get_foliage_color`](#fn-hooks-get_foliage_color) · [`get_grass_color`](#fn-hooks-get_grass_color) · [`get_water_color`](#fn-hooks-get_water_color) · [`handle_inventory_click`](#fn-hooks-handle_inventory_click) · [`handle_inventory_key`](#fn-hooks-handle_inventory_key) · [`handle_inventory_scroll`](#fn-hooks-handle_inventory_scroll) · [`handle_pause_click`](#fn-hooks-handle_pause_click) · [`handle_settings_mouse`](#fn-hooks-handle_settings_mouse) · [`on_block_broken`](#fn-hooks-on_block_broken) · [`on_block_placed`](#fn-hooks-on_block_placed) · [`on_char_input`](#fn-hooks-on_char_input) · [`can_pick_up_item`](#fn-hooks-can_pick_up_item) · [`on_chat_command`](#fn-hooks-on_chat_command) · [`on_player_drop`](#fn-hooks-on_player_drop) · [`on_chat_complete`](#fn-hooks-on_chat_complete) · [`on_dig_block`](#fn-hooks-on_dig_block) · [`on_game_mode`](#fn-hooks-on_game_mode) · [`on_hotbar_full`](#fn-hooks-on_hotbar_full) · [`on_pick_block`](#fn-hooks-on_pick_block) · [`on_player_chat`](#fn-hooks-on_player_chat) · [`on_player_click`](#fn-hooks-on_player_click) · [`on_player_harvest`](#fn-hooks-on_player_harvest) · [`on_player_join`](#fn-hooks-on_player_join) · [`on_player_leave`](#fn-hooks-on_player_leave) · [`on_right_click_block`](#fn-hooks-on_right_click_block) · [`on_scroll_input`](#fn-hooks-on_scroll_input) · [`on_server_start`](#fn-hooks-on_server_start) · [`on_server_stop`](#fn-hooks-on_server_stop) · [`on_shared_changed`](#fn-hooks-on_shared_changed) · [`on_update`](#fn-hooks-on_update) · [`on_use_hoe`](#fn-hooks-on_use_hoe) · [`on_use_scythe`](#fn-hooks-on_use_scythe) · [`render_creative_inventory`](#fn-hooks-render_creative_inventory) · [`render_hud`](#fn-hooks-render_hud) · [`render_inventory`](#fn-hooks-render_inventory) · [`render_mods`](#fn-hooks-render_mods) · [`render_multiplayer`](#fn-hooks-render_multiplayer) · [`render_pause_menu`](#fn-hooks-render_pause_menu) · [`render_player_list`](#fn-hooks-render_player_list) · [`render_settings`](#fn-hooks-render_settings) · [`render_texture_packs`](#fn-hooks-render_texture_packs) · [`render_world_select`](#fn-hooks-render_world_select) · [`set_world_seed`](#fn-hooks-set_world_seed) · [`toggle_main_inventory`](#fn-hooks-toggle_main_inventory) · [`update_player_animation`](#fn-hooks-update_player_animation)

**weather (mods/weather)** (11)  
[`weather.get`](#fn-weather-get) · [`weather.get_def`](#fn-weather-get_def) · [`weather.is_cold`](#fn-weather-is_cold) · [`weather.is_cycling`](#fn-weather-is_cycling) · [`weather.list`](#fn-weather-list) · [`weather.on_change`](#fn-weather-on_change) · [`weather.on_lightning`](#fn-weather-on_lightning) · [`weather.register`](#fn-weather-register) · [`weather.set`](#fn-weather-set) · [`weather.set_cycle`](#fn-weather-set_cycle) · [`weather.time_left`](#fn-weather-time_left)

**survival (mods/survival)** (21)  
[`survival.damage`](#fn-survival-damage) · [`survival.feed`](#fn-survival-feed) · [`survival.get_air`](#fn-survival-get_air) · [`survival.get_armor`](#fn-survival-get_armor) · [`survival.get_food`](#fn-survival-get_food) · [`survival.get_hp`](#fn-survival-get_hp) · [`survival.get_mode`](#fn-survival-get_mode) · [`survival.heal`](#fn-survival-heal) · [`survival.is_dead`](#fn-survival-is_dead) · [`survival.on_damage`](#fn-survival-on_damage) · [`survival.on_death`](#fn-survival-on_death) · [`survival.on_equip`](#fn-survival-on_equip) · [`survival.on_respawn`](#fn-survival-on_respawn) · [`survival.register_craft`](#fn-survival-register_craft) · [`survival.register_equipment`](#fn-survival-register_equipment) · [`survival.register_food`](#fn-survival-register_food) · [`survival.register_workbench`](#fn-survival-register_workbench) · [`survival.respawn`](#fn-survival-respawn) · [`survival.set_armor`](#fn-survival-set_armor) · [`survival.set_mode`](#fn-survival-set_mode) · [`survival.settings`](#fn-survival-settings)

**survival.inventory** (7)  
[`survival.inventory.add`](#fn-survival-inventory-add) · [`survival.inventory.clear`](#fn-survival-inventory-clear) · [`survival.inventory.get`](#fn-survival-inventory-get) · [`survival.inventory.is_open`](#fn-survival-inventory-is_open) · [`survival.inventory.pick`](#fn-survival-inventory-pick) · [`survival.inventory.set`](#fn-survival-inventory-set) · [`survival.inventory.size`](#fn-survival-inventory-size)

**buckets (mods/buckets)** (1)  
[`buckets.register`](#fn-buckets-register)

**leafdecay (mods/leafdecay)** (1)  
[`leafdecay.on_decay`](#fn-leafdecay-on_decay)

**doors (mods/doors)** (4)  
[`doors.is_door`](#fn-doors-is_door) · [`doors.is_open`](#fn-doors-is_open) · [`doors.register`](#fn-doors-register) · [`doors.set_open`](#fn-doors-set_open)

**client (client mods)** (36)  
[`client.display_chat_message`](#fn-client-display_chat_message) · [`client.dofile`](#fn-client-dofile) · [`client.get_active_slot`](#fn-client-get_active_slot) · [`client.get_block`](#fn-client-get_block) · [`client.get_block_color`](#fn-client-get_block_color) · [`client.get_block_def`](#fn-client-get_block_def) · [`client.get_current_modname`](#fn-client-get_current_modname) · [`client.get_dig_progress`](#fn-client-get_dig_progress) · [`client.get_hotbar_size`](#fn-client-get_hotbar_size) · [`client.get_hotbar_slot`](#fn-client-get_hotbar_slot) · [`client.get_light_level`](#fn-client-get_light_level) · [`client.get_mod_names`](#fn-client-get_mod_names) · [`client.get_mod_storage`](#fn-client-get_mod_storage) · [`client.get_modpath`](#fn-client-get_modpath) · [`client.get_player_name`](#fn-client-get_player_name) · [`client.get_player_position`](#fn-client-get_player_position) · [`client.get_player_state`](#fn-client-get_player_state) · [`client.get_players`](#fn-client-get_players) · [`client.get_rules`](#fn-client-get_rules) · [`client.get_server_info`](#fn-client-get_server_info) · [`client.get_surface`](#fn-client-get_surface) · [`client.get_time`](#fn-client-get_time) · [`client.is_creative`](#fn-client-is_creative) · [`client.is_left_handed`](#fn-client-is_left_handed) · [`client.key_name`](#fn-client-key_name) · [`client.list_blocks`](#fn-client-list_blocks) · [`client.register_chat_command`](#fn-client-register_chat_command) · [`client.register_on_chat_message`](#fn-client-register_on_chat_message) · [`client.register_on_dig_block`](#fn-client-register_on_dig_block) · [`client.register_on_hud`](#fn-client-register_on_hud) · [`client.register_on_join`](#fn-client-register_on_join) · [`client.register_on_key`](#fn-client-register_on_key) · [`client.register_on_leave`](#fn-client-register_on_leave) · [`client.register_on_right_click_block`](#fn-client-register_on_right_click_block) · [`client.register_on_update`](#fn-client-register_on_update) · [`client.send_chat_message`](#fn-client-send_chat_message)

**default_sounds (mods/default_sounds)** (3)  
[`default_sounds.material`](#fn-default_sounds-material) · [`default_sounds.rules`](#fn-default_sounds-rules) · [`default_sounds.sets`](#fn-default_sounds-sets)

---

## 1. Folders

```
BlockSmith.exe
BlockSmithServer.exe            dedicated server (see section 17)
lua_api.txt                     written at startup: what this build can do
games/
  craft_and_ruin/               a game: pick it on the title screen
    game.conf                   title = ..., author = ..., description = ...
    mods/                       the game's own mods (always loaded with it)
    lualib/                     this game's shared Lua modules (optional)
      ...
lualib/                         shared Lua modules for require() (json.lua, inspect.lua...) - never run on their own
mods/                           global mods: loaded with any game, can be switched off
  buckets/
    mod.conf                    title, author, description, version, depends, optional_depends, required = true|false,
                                remote_sources = https://... (online content, section 21)
    init.lua                    if present, the ONLY file the engine runs (it loads the rest)
    textures/  models/
texturepacks/
  MyPack/pack.conf              any image with the same file name replaces a game texture
saves/<world>/                  worlds
servers/<profile>/              dedicated server setups (engine.create_server_profile)
```

**How a mod's files run:**
- Mods run in a fixed order: global mods first, then the game's mods, each alphabetically by folder name. Mods with an `init.lua` run before loose `.lua` files. A mod whose `mod.conf` has `depends = a, b` (or `optional_depends`) runs after those mods.
- If a folder contains `init.lua`, only that file runs. Load your other files from it with `require("other")` or `dofile(g_CurrentModPath .. "/other.lua")`. See [Lua modules: require](#lua-modules-require).
- Otherwise every `.lua` file in the folder runs.
- While a file runs, `g_CurrentModPath` is its mod's folder. Textures and models are looked for there first, then in its `textures/` or `models/` folder.
- Players can switch global mods off on the Mods screen. A mod with `required = true` in `mod.conf` can't be switched off.

### Lua modules: require

<a id="fn-require"></a>`require("name")` loads a Lua file **once** and returns what it returns (later calls get the same value). It's the tidy way to split a mod into files, share code between mods, and use libraries other people wrote. It's looked for in this order:

| `require(...)` | finds |
|---|---|
| `require("helpers")` | `helpers.lua` (or `helpers/init.lua`) next to **the file that calls it**, so in your own mod |
| `require("util.vec")` | `util/vec.lua` in your mod: dots are folders |
| `require("json")` | a shared library: `lualib/json.lua` or `lualib/json/init.lua`, next to `mods/` (or the game's own `games/<game>/lualib/`) |
| `require("doors.api")` | `api.lua` in the **doors** mod: one mod can use another mod's files |

```lua
-- mods/claims/init.lua
local regions = require("regions")      -- mods/claims/regions.lua
local json    = require("json")         -- lualib/json.lua (a library you downloaded)

-- mods/claims/regions.lua
local M = {}
function M.inside(x, z) return math.abs(x) < 16 and math.abs(z) < 16 end
return M
```

- Put downloaded pure-Lua libraries (json.lua, inspect.lua, serpent...) in `lualib/`. Files there never run by themselves, only when a mod asks for them. Libraries that need a compiled DLL can't be used.
- `require("doors")` doesn't run another mod's `init.lua` again; ask for one of its files (`require("doors.api")`), and list the mod in your `depends` so it has loaded first.
- Module names are letters, digits, `_`, `-` and dots. `require` returns the module and the file it came from.

**What mods may load** (the same in the game, on LAN games and on the dedicated server):
- `require`, `dofile` and `loadfile` only read files inside the game's folder, the `mods/` folder and the `lualib/` folders. `..`, other drives and links pointing outside are refused. So a mod from the internet can't read your other files.
- `load` only takes Lua source text, never precompiled bytecode (which can crash the game).
- There's no `io`, `os` or `debug` library, and no way to load native DLLs (`package.loadlib` and C modules are gone). `coroutine` and `utf8` are there.
- Client mods have their own, stricter rules: see [Client mods](#20-client-mods).

**To make a new game:**
1. Make a folder in `games/` with a `game.conf`.
2. Put its mods in `games/<yours>/mods/`.
3. Pick it on the title screen.

Worlds remember which game made them.

## 2. Your first mod

A step-by-step version with explanations is in [Your first mod, step by step](#l2-your-first-mod-step-by-step).


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
| `gravity` | false | falls like sand: with nothing solid below, it drops smoothly (speeding up, as in Minecraft) and lands on the first solid block. It falls through air, fluids and things you walk through (grass, flowers, torches are crushed). See [`world.get_falling_blocks`](#fn-world-get_falling_blocks). |
| `tint` | false | tint all faces with r, g, b |
| `in_creative` | true | listed in the creative inventory |
| `description` | made from the name | the name players see, like Luanti's: `"Oak Log"`. It's shown above the hotbar for a moment when it's picked, in the inventory, and in `/give`'s answer. Without one, `log_oak` shows as "Log Oak". |
| `inventory_image` | none | shows a flat picture in the inventory and hotbar. The block becomes an **item**: right-click never places it |
| `pick` | the block itself | what [pick block](#pick-block) gives for this block, e.g. `"wheat_seeds"` for a crop, or `"ruby_lamp"` for a lit lamp |

**Rules:**
- Up to 255 blocks.
- Registering the same name twice keeps its first id and updates its settings. The console says so.
- Worlds save blocks by **name**, so the order you register them in doesn't matter.

**Metadata:** each block has one extra byte, 0–255. Water uses it for its flow level. Plants use bit 1 to mean "waterlogged". Your blocks can use it for anything.

### Doors

<a id="fn-doors-register"></a>The `doors` mod adds doors: two blocks tall, opened and closed with right-click, swinging toward their hinge. Place two side by side and they make a double door that opens from the middle. Add your own:

```lua
doors.register("door_birch", {
    top = "birch_door_top.png", bottom = "birch_door_bottom.png",   -- upper / lower half
    inventory_image = "door_birch_item.png",                         -- picture in the hotbar
    hardness = 3, tool = "axe",                                      -- optional
    sound_open = "default_wood_dig", sound_close = "default_wood_dig",
})
```

<a id="fn-doors-is_door"></a><a id="fn-doors-is_open"></a><a id="fn-doors-set_open"></a>`doors.is_door(name)`, `doors.is_open(x, y, z)` and `doors.set_open(x, y, z, open)` let other mods work doors, for example a pressure plate or a redstone-like mod.

**How it works:** the engine draws any block registered with `pattern = "door"` as a thin panel (3 pixels thick) and collides with only that panel, on the game and the server alike. You can aim through an open door's empty part. Transparent pixels in the picture (windows) show through. The block's metadata says where the panel is:

| Bit | Meaning |
|---|---|
| 0-1 (`meta % 4`) | facing: 0 north, 1 east, 2 south, 3 west (the way the placer looked) |
| 4 | open |
| 8 | upper half |
| 16 | hinge on the right |

The mod sets these with `engine.set_block_meta`, so any mod can make door-like blocks such as trapdoors, gates or shutters the same way.

## 4. Changing the world

| function | |
|---|---|
| <a id="fn-engine-get_block_id"></a>`engine.get_block_id(name)` | block id, or 0 if there's no such block |
| <a id="fn-engine-get_block_name"></a>`engine.get_block_name(x, y, z)` | `"stone"`, `"air"`... |
| <a id="fn-engine-set_block"></a>`engine.set_block(x, y, z, name)` | changes a block (it is saved, sent to other players, and fluids react) |
| <a id="fn-engine-get_metadata"></a>`engine.get_metadata(x, y, z)` | 0–255 |
| <a id="fn-engine-set_block_meta"></a>`engine.set_block_meta(x, y, z, name, meta)` | block and metadata in one change |
| <a id="fn-engine-is_replaceable"></a>`engine.is_replaceable(x, y, z)` | air, a fluid or a plant: there's room to put something |
| <a id="fn-engine-is_opaque"></a>`engine.is_opaque(x, y, z)` | a solid block light can't get through (stone, dirt, planks); false for air, water, plants, leaves and glass |
| <a id="fn-world-get_render_distance"></a><a id="fn-world-set_render_distance"></a>`world.set_render_distance(chunks)` / `get_render_distance()` | |
| <a id="fn-world-get_falling_blocks"></a>`world.get_falling_blocks()` | the blocks falling right now (sand, gravel...): `{ { name, x, y, z }, ... }`. `x, y, z` is the block's bottom corner; `y` has a fraction while it falls. A falling block isn't in the world (`engine.get_block_name` says air) until it lands. |
| <a id="fn-world-get_caves_enabled"></a><a id="fn-world-set_caves_enabled"></a>`world.set_caves_enabled(bool)` / `get_caves_enabled()` | built-in caves (turn them off if your mapgen makes its own) |
| <a id="fn-world-get_loaded_chunks"></a><a id="fn-world-get_pending_chunks"></a>`world.get_loaded_chunks()` / `get_pending_chunks()` | |
| <a id="fn-world-set_load_budget"></a><a id="fn-world-set_mesh_budget"></a>`world.set_load_budget(ms)` / `set_mesh_budget(ms)` | how much time per frame goes to loading and meshing |

<a id="abm"></a>### Blocks that change by themselves: random ticks, ABMs and LBMs

There are three ways, and all of them run only where the world is decided: singleplayer, the game when hosting, and the dedicated server. Everyone else just sees the blocks change.

<a id="fn-engine-register_random_tick"></a>**Random ticks (Minecraft's way): the smoothest.** Every game tick (20 a second), the engine picks 3 random blocks in every 16×16×16 section of the chunks near players. If a picked block is a kind with a random tick, its action runs. On average each block is picked about every 68 seconds. Because the work is spread evenly over every tick, grass creeps outwards a block at a time and crops grow steadily, rather than everything changing at once every few seconds. This is how Minecraft's grass, crops, saplings, leaves and ice work. The `randomTickSpeed` game rule (3 by default, 0 = off) sets how many blocks are picked: `/gamerule randomTickSpeed 10` makes everything grow about three times faster.

```lua
engine.register_random_tick({
    label = "grass spreads and dies",
    nodenames = { "grass" },
    action = function(x, y, z, name, meta)
        if engine.is_opaque(x, y + 1, z) then engine.set_block(x, y, z, "dirt") return end   -- covered
        if engine.get_light_level(x, y + 1, z) < 9 then return end
        for _ = 1, 4 do                                       -- Minecraft: 4 tries nearby
            local tx, ty, tz = x + math.random(-1, 1), y + math.random(-3, 1), z + math.random(-1, 1)
            if engine.get_block_name(tx, ty, tz) == "dirt" and not engine.is_opaque(tx, ty + 1, tz) then
                engine.set_block(tx, ty, tz, "grass")
            end
        end
    end,
})
```
Use random ticks for slow, natural change. Use an ABM (below) when a block should check its surroundings every few seconds, or when porting a Luanti mod.

<a id="fn-engine-register_abm"></a>**ABM (Active Block Modifier):** every `interval` seconds, each block of the given kinds near a player (within 3 chunks) has a 1-in-`chance` chance of running your action. Grass spreading, crops growing, fire, lava cooling and moss creeping are typical jobs.

```lua
engine.register_abm({
    label = "grass spreads onto dirt",       -- shown if the action has an error
    nodenames = { "dirt" },                  -- the blocks it acts on
    neighbors = { "grass" },                 -- only if one of these is among the 26 around it (optional)
    without_neighbors = { "lava" },          -- ...and none of these (optional)
    interval = 6,                            -- seconds between rounds
    chance = 15,                             -- 1 in 15 matching blocks each round
    min_y = 0, max_y = 255,                  -- (optional)
    action = function(x, y, z, name, meta)
        if not engine.is_opaque(x, y + 1, z) and engine.get_light_level(x, y + 1, z) >= 9 then
            engine.set_block(x, y, z, "grass")
        end
    end,
})
```
The engine looks through the area around players a few chunks per tick, so lots of blocks and lots of ABMs don't cause a lag spike. A block changed by one action is checked again before the next runs on it.

<a id="fn-engine-register_lbm"></a>**LBM (Loading Block Modifier):** runs on blocks of the given kinds when their chunk is **loaded from its save file**.
- With `run_at_every_load = false` (the default) it runs **once per chunk, ever**: for upgrading old worlds, such as replacing a block your mod renamed. Chunks generated after the LBM existed never need it, so they're only noted. Which chunks it has done is kept with the world (`lbm_done.txt`), so give each LBM a lasting, unique `name`.
- With `run_at_every_load = true` it runs every time such a chunk is loaded from disk: to catch up on things like timers.

```lua
engine.register_lbm({
    name = "mymod:old_lamps",                -- unique, and never change it
    nodenames = { "mymod:lamp_old" },
    run_at_every_load = false,
    action = function(x, y, z, name, meta)
        engine.set_block(x, y, z, "mymod:lamp")
    end,
})
```

Craft & Ruin's `grass` mod (`games/craft_and_ruin/mods/grass`) is Minecraft's grass, on a random tick:
- a grass block with something solid (or water, or lava) on top turns back to dirt;
- with light 9 or more above it, it tries 4 random spots nearby (one block each way, from 3 below to 1 above), and dirt there with an open top and light 4 or more becomes grass.

On a dedicated server, which keeps no light, `engine.get_light_level` is open sky (sunlight for the time of day) or dark under cover.

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

<a id="terrain-version"></a>**When you change the terrain: `terrain_version`.** Set a global number in your world generator, and raise it whenever the terrain it makes changes (heights, biomes, rivers...):

```lua
terrain_version = 2   -- 1: first version; 2: smoother hills
```

Saved chunks remember which version made them, and whether anything but nature changed them since. A player digging or building, a bucket, a mod placing a structure: all of those count as building in it. Flowing fluids, falling blocks, ABMs, LBMs and random ticks (grass spreading, leaves decaying, crops growing) don't. When a world opens, a saved chunk **nobody built in** that an **older version** made is generated again, so old terrain doesn't stand next to new as steps and pits. A chunk anyone built in is always kept exactly as it was. Chunks saved before the engine recorded this (BlockSmith 0.1-alpha) are kept too. Without `terrain_version`, nothing is ever regenerated.

**Opening another world in the same session.** `set_world_seed` is called again with the new
seed. The background threads start from fresh copies of your mods for it, but the main thread's
copy (it makes the chunks around the spawn and answers `get_grass_color`, `get_biome` and so
on) carries on. So if you keep anything worked out from the seed (a cache of where structures
or volcanoes are, a noise table), clear it in `set_world_seed`, or the new world's chunks won't
match each other.

**If `set_world_seed` raises an error.** New terrain is never generated without a seed: those
chunks wait. The world generator runs on background threads, each with its own copy of your
mods. When the hook fails on one, that copy is thrown away and rebuilt from scratch, exactly as
it was before the first attempt. A retry never sees tables your failed call half-filled. The
game tries again after 0.5 s, then 1, 2, 4 and 8 s. Saved chunks keep loading from disk
meanwhile. After 6 failed attempts it gives up. The game closes the world and shows your error
on the world list, and a dedicated server stops with the error in its console. So:

- keep `set_world_seed` quick and only derive state from `seed`: it can run several times
  (once per thread, and again after a failure);
- don't count on globals it set in an earlier, failed call: a retry starts from your mod's
  freshly loaded files;
- an error that persists (a typo, a missing dependency) closes the world with that message:
  fix it and open the world again.

<a id="fn-chunk-get_block"></a><a id="fn-chunk-get_metadata"></a><a id="fn-chunk-set_block"></a><a id="fn-chunk-set_block_with_meta"></a><a id="fn-chunk-set_metadata"></a>`chunk` provides `set_block(x, y, z, id)`, `get_block(x, y, z)`,
`set_block_with_meta(x, y, z, id, meta)`, `set_metadata(x, y, z, meta)` and
`get_metadata(x, y, z)`. Coordinates are 0–15 across and 0–255 up.

**Important:**
- `generate_chunk` runs on **background threads**, each with its own copy of your mod files.
- <a id="fn-engine-is_world_generator"></a>On those threads only world-generation functions work. `engine.get_block_id` and math work. Drawing, chat, `engine.set_block` and the hotbar do nothing. `engine.is_world_generator()` returns true there, if a mod needs to know.
- Don't rely on global variables changed elsewhere at runtime. Compute everything from the seed and the coordinates.
- With no `generate_chunk`, the engine makes simple built-in terrain.
- After your terrain, the engine carves caves unless you turn them off (see [Caves](#caves) below).

### Caves

After `generate_chunk`, the engine carves Minecraft-style caves into stone, dirt and other solid ground:

- **Tunnels:** Minecraft's classic winding caves. They wander, change width, split into branches and open into round rooms. Some climb to the surface as cave entrances, and the dirt at the mouth turns to grass.
- **Ravines:** rare, deep, narrow canyons with jagged walls.
- **Caverns:** big open caves deep down, with stone pillars.
- **Noodles:** thin twisty tunnels in some regions.
- **Lava:** open cave space at y 10 and below fills with lava, if the game has a `lava` block.

Caves never cut through water (so no holes in the sea floor), logs, leaves, planks, glass or bedrock (y 0–4). Plants and other non-solid blocks (`walkable = false`) left standing over a hole are removed, so no grass or flowers float above cave entrances. Where a cave passes under a tree, the ground under its trunk is left in place (a pillar down to the cave floor), so trees never hang in the air. Blocks named *log*, *leaves*, *trunk* or *cactus* count as trees. They come from the world seed, so a seed always gives the same caves. The game and the dedicated server make exactly the same ones.

<a id="fn-world-get_cave_settings"></a><a id="fn-world-set_cave_settings"></a>Change them from your mod while it loads (only the keys you give change):

```lua
world.set_cave_settings({
    tunnels = true,  tunnel_frequency = 1,  tunnel_width = 1,   -- 2 = twice as many / wide
    ravines = true,  ravine_frequency = 1,
    caverns = true,  cavern_size = 1,
    noodles = true,
    lava_level = 10,  -- -1 = no lava
    max_y = 200,      -- nothing carved above this height
})
local s = world.get_cave_settings()   -- the current settings, plus `enabled`
world.set_caves_enabled(false)        -- no built-in caves at all (your mapgen makes its own)
```

Put these in a mod that both the game and the server load (any game mod is), so both make the same caves. Chunks already saved keep the caves they had.

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
- <a id="fn-hooks-on_right_click_block"></a>`on_right_click_block(x, y, z, block, held_item, sneaking, repeat)`: return true to stop placing. `sneaking` is true while the player holds Sneak; like Minecraft, a block you can use, such as a door, should then let the held block be placed against it. `repeat` is true when the call comes from holding the button rather than a new click (see below).
- <a id="fn-engine-set_place_repeat"></a>**Holding right-click** keeps placing blocks and using items, every 0.2 s like Minecraft. `engine.set_place_repeat(seconds)` changes the interval (0 = one per click). Item uses get `pointed.repeat_click` and `pointed.sneaking` too (`repeat` is a Lua keyword, so `pointed.repeat` can't be written; `pointed["repeat"]` is the same value). While sneaking, players don't walk off edges, so they can stand at the edge of a block and keep building out from it.
- <a id="fn-hooks-on_block_broken"></a>`on_block_broken(x, y, z, block, meta, player)`: a player broke a block, in creative or survival. It runs after the block is gone; `meta` is what its metadata was (on a server: 0). `player` is who did it. The doors mod uses it to remove the other half.
- <a id="fn-hooks-on_block_placed"></a>`on_block_placed(x, y, z, block, player)`: a player placed a block. To stop players changing blocks somewhere, use [`can_edit_block`](#fn-hooks-can_edit_block).

Both run where the world is decided too: in singleplayer, for every player's edits when you host a LAN game, and on the dedicated server. A mod that changes the world from them, like leaf decay, should check `engine.is_authority()` so it only acts once.

<a id="fn-leafdecay-on_decay"></a>**Leaf decay** (the `leafdecay` mod): when a tree is cut down, its leaves fade away over the next few seconds. A leaf stays while a log is within 6 steps of it through other leaves, and leaves a player placed never decay. You can change `leafdecay.settings.distance` and `leafdecay.settings.delay`, or which blocks count with `leafdecay.is_leaves` and `leafdecay.is_log`. `leafdecay.on_decay(function(x, y, z, leaves) end)` runs when a leaf goes.

Leaves break instantly by hand. Like Minecraft, broken or decaying leaves can drop a sapling (5%), 1–2 sticks (2%), and from oak leaves an apple (0.5%), but only items the game has. The leaves themselves drop only with shears (a tool with `tool_type = "shears"`). Change the drops with `leafdecay.register_drops("leaves_oak", { { item = "sapling_oak", chance = 0.05 }, { item = "stick", chance = 0.02, min = 1, max = 2 } })`. Decaying leaves give their drops to the player if they're within 32 blocks.

<a id="farming"></a>### Farming: hoes, crops and the scythe

Two hooks drive the tools. A hoe or scythe is any item whose `tool_type` is `"hoe"` or `"scythe"`, or whose name contains the word:
- <a id="fn-hooks-on_use_hoe"></a>`on_use_hoe(x, y, z, block)`: right-click with a hoe. Return true if it did something (tilled the ground); otherwise the right-click carries on as usual, so doors still open with a hoe in hand.
- <a id="fn-hooks-on_use_scythe"></a>`on_use_scythe(x, y, z, block)`: left-click with a scythe. Return true if it reaped something. Otherwise the scythe digs the block like any tool.

On a server, a player's game must not hand out a harvest itself: dropping the items and asking for the block change separately would give the items even when the change is refused, for example on a protected field. So when joined, call <a id="fn-engine-request_harvest"></a>`engine.request_harvest(item, x, y, z)`. It sends only the item name and the block position, as data. The server's copy of your mod then gets <a id="fn-hooks-on_player_harvest"></a>`on_player_harvest(name, item, x, y, z)`. The server has already checked that the block is loaded, within the player's reach, and that requests come no faster than 10 a second. It works out what happens from its own world. Use [`engine.player_set_block`](#fn-engine-player_set_block) for each change, and create the reward only when that returns true, so the change and its reward happen together. In singleplayer, or for the host's own player, `engine.request_harvest` returns false and nothing is sent: do the work right there, as before. Craft & Ruin's farming mod does exactly this:

```lua
-- in on_use_scythe
if engine.is_authority() then return reap(x, y, z) > 0 end   -- singleplayer / host: here
engine.request_harvest("scythe", x, y, z)                     -- joined: the server decides
return true

-- on the server
function on_player_harvest(name, item, x, y, z)
    -- for each ripe crop in the 3x3 patch:
    local ok, why = engine.player_set_block(name, cx, y, cz, first_stage)
    if ok then --[[ drop its harvest ]] end
end
```

Craft & Ruin's `farming` mod is Minecraft's farming:
- **Tilling:** a hoe (wooden or stone) right-clicked on grass or dirt with air above makes farmland.
- **Farmland:** it's wet (darker) with water within 4 blocks and dries out without. Bare dry farmland turns back to dirt, and jumping or falling onto farmland tramples it to dirt.
- **Seeds:** they plant only on farmland. You get them by breaking tall grass (1 in 8).
- **Growing:** crops grow on [random ticks](#abm) with Minecraft's formula. They grow faster on wet farmland and in fields, need light 9 or more, and pop off if the farmland under them goes.
- **Harvesting by hand:** breaking a ripe crop gives its harvest (wheat: 1 wheat and 0–3 seeds); an unripe one gives its seed back.
- **The scythe:** left-click a ripe crop and the scythe reaps every ripe crop in the 3×3 patch around it, then replants them. Each one keeps back a seed from its harvest to go back in the ground. On anything else the scythe is an ordinary tool. On a server, the server does the reaping. Spawn protection and `can_edit_block` are checked for each crop, and a crop you may not touch stays ripe and gives nothing. A reap's harvest is dropped as one batch. If the world can't take an item yet (it's at its entity limit), the item waits and is dropped again every second until it arrives. Items that were already dropped are never given twice.
- **Recipes:**
  - wooden and stone hoes are Minecraft's shape;
  - the scythe is 3 stone across the top, with a stick under the right end and one in the middle of the bottom row;
  - bread is 3 wheat in a row (5 food).
- **Creative:** everything is in the "Farming" tab.

<a id="fn-farming-register_crop"></a>Add your own crops with `farming.register_crop`:
```lua
farming.register_crop({
    name = "carrot",
    stages = { "mymod:carrot_1", "mymod:carrot_2", "mymod:carrot_3", "mymod:carrot_ripe" },  -- block names
    textures = { "carrot_1.png", "carrot_2.png", "carrot_3.png", "carrot_4.png" },  -- registers the stages
    seed = "mymod:carrot",                         -- what plants it (an item: give it an inventory_image)
    harvest = { { "mymod:carrot", 2, 4 } },        -- a ripe one: item, fewest, most
    scythe_replants = true,                        -- a scythe reaps and replants it
})
```
The last stage is the ripe one. It grows the same way as wheat. `farming.is_crop(name)` and `farming.is_ripe(name)` tell you about a block.

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

<a id="tanks"></a>### Liquids in tanks (like Tinkers' Construct)

The engine draws blocks with <a id="fn-block-tank"></a>`pattern = "tank"` as a see-through box with liquid inside. The block's metadata says how full it is, from 0 (empty) to 16 (full). The liquid is drawn with the fluid block's own pictures, so a water tank looks like water and a lava tank like lava, and it rises and falls as the metadata changes:

```lua
engine.register_block("mymod:vat_water", 1, 1, 1, "vat.png", nil, {
    pattern = "tank",
    tank = {
        fluid = "water",                  -- the block whose pictures fill it ("" or nothing: empty)
        box = { 0, 0, 0, 16, 16, 16 },    -- the outer box, in pixels (0..16): any size, e.g. a small vat
        inset = 1,                        -- wall thickness in pixels: the liquid sits this far inside
        stream = false,                   -- true: a thin stream falls out of the bottom (a spout pouring)
        floor = 1,                        -- where the liquid starts, in pixels (default: the inset); 13 = a
                                          --   shallow mould near the top, like a casting table
        shell = true,                     -- false: draw only the liquid, no box (the inside of a smeltery)
        fluid2 = nil,                     -- a second liquid layered on top of `fluid` (see below)
    },
    light = 13, light_color = "#ff9a40",  -- optional: a lava tank glows
})
engine.set_block_meta(x, y, z, "mymod:vat_water", 8)    -- half full
```

The box's picture is cut to size like Minecraft's non-cube blocks, and see-through pixels show the liquid. Give the block one entry per liquid ("vat_water", "vat_lava"...) and change block and metadata together with `engine.set_block_meta`. On the server, block and metadata are saved and sent like any other block.

<a id="fn-block-tank-layers"></a>**Two liquids in one block (layers):** with `fluid2`, the block shows `fluid` at the bottom and `fluid2` on top of it. The metadata then holds both heights, in 16ths of the block:
- the low 4 bits are where the lower liquid ends (1 to 15);
- the high 4 bits are where the upper liquid ends, minus 1 (0 to 15, meaning 1 to 16).

For example, `engine.set_block_meta(x, y, z, "mymod:layer_gold__iron", 6 + 16 * 15)` puts gold up to 6/16, then iron up to the top. A container taller than one block uses one block per height, so any number of layers can be stacked. Each block shows the one or two that meet inside it.

**The `tanks` mod** (`mods/tanks`) is a complete example:
- **Capacity:** a tank holds 4 buckets (4000 mB) of one liquid.
- **Buckets:** right-click a tank with a full bucket to pour it in, and with an empty bucket to take a bucket out. With an empty hand you see how much is in it.
- **Spout:** under a tank, a spout pours it into the tank below, 250 mB at a time, while you watch the stream. Right-click the spout to start or stop it.
- **Liquids:** lava tanks glow, and any fluid with a bucket gets tanks.
- **For other mods:** <a id="fn-tanks-get"></a><a id="fn-tanks-set"></a><a id="fn-tanks-register_fluid"></a><a id="fn-tanks-start_pouring"></a>`tanks.get(x, y, z)` returns the fluid and mB, `tanks.set(x, y, z, fluid, mB)` changes it, `tanks.register_fluid(fluid, source_block [, light, light_color])` adds a liquid, and `tanks.start_pouring(x, y, z)` starts a spout.

<a id="smeltery"></a>**The `smeltery` mod** (`mods/smeltery`) is a working smeltery like Tinkers' Construct's, built from these blocks:
- **Building it:** a floor of Seared Bricks, and walls of Seared Bricks or Seared Glass around an empty space from 1×1 to 7×7, up to 8 high. A Smeltery Controller goes in a wall, and a lava tank in a wall (or a lava bucket on the controller) is the fuel.
- **Melting:** right-click the controller with ore, ingots or metal blocks to put them in. Iron, gold and copper ores, ingots and blocks come with the mod, and ore gives 2 ingots. The metals melt into **layers**, stacked in the order they melted, and you can see them through Seared Glass.
- **Alloying:** copper and gold make rose gold, 3 to 1.
- **The controller's screen:** right-click it with an empty hand. It shows what's melting, the layers drawn to scale, and the fuel. Click a layer to move it to the bottom.
- **Casting:** a Smeltery Drain in a wall, with a Faucet outside it, pours the bottom layer into a Casting Table (one ingot, 144 mB) or a Casting Basin (one block, 1296 mB) under the faucet. It cools solid and you right-click to take it. Sneak + right-click tips out a part-filled cast.
- **How the layers are drawn:** the inside of the smeltery is filled with shell-less tank blocks, each with one liquid or two layers.
- **For other mods:** <a id="fn-smeltery-add_melting"></a><a id="fn-smeltery-fill"></a><a id="fn-smeltery-insert"></a>
  - `smeltery.add_melting(item, metal, mB, seconds)` makes another item meltable;
  - `smeltery.fill(x, y, z, { {metal, mB}, ... } [, fuel])` sets what's inside;
  - `smeltery.insert(x, y, z, item [, count])` puts items in to melt;
  - `smeltery.open_screen(x, y, z)` opens the controller's screen.
- **Where it runs:** singleplayer, or when you host a LAN game.

<a id="fn-buckets-register_target"></a>**Buckets on your own blocks:** the buckets mod passes clicks on other mods' blocks to them. `buckets.register_target(function(pointed, bucket_item, fluid) ... end)` is asked first: `fluid` is the bucket's fluid, or nil for an empty bucket, and returning true means you used the click. Helpers:
- <a id="fn-buckets-swap_held"></a>`buckets.swap_held(pointed, new_item)` turns the held bucket into another item, so a full bucket becomes `buckets.EMPTY` and the other way round;
- `buckets.fluid_of(bucket_item)` and `buckets.bucket_of(fluid)` convert between them.

## 8. Chat commands

<a id="fn-engine-send_chat"></a>
```lua
chat.register_command(name, usage, description)   -- listed in /help
chat.send(text [, r, g, b])                       -- a message in your own chat
engine.send_chat(text)                            -- a message to everyone (multiplayer)
function on_chat_command(text) ... end            -- "/give stone 5" arrives as "give stone 5"
```

In multiplayer, commands the client doesn't handle go to the server. Server operators have their own commands there (see section 17).

**Links.** Web addresses in chat (`https://...`, `http://...` or `www....`) are drawn as blue, underlined links: a server's voting site, its rules or its forum. With the chat open, clicking one asks first: *Open this link in your browser?*, with the website's name shown on its own so players can see where it really goes. **Open** (Enter) hands it to the player's own browser, **Copy** puts it on the clipboard, **Cancel** (Esc) does nothing. The game itself never downloads or runs anything from a link.

That works for anything that ends up in chat: players' messages, the server's `say` and message of the day, and mods' `chat.send` lines and command replies. For example, a server mod for voting:

```lua
chat.register_command("vote", "/vote", "Where to vote for this server")
local previous = on_chat_command
function on_chat_command(text, player, is_operator, reply, ...)
    if text == "vote" then
        reply("Vote for us every day at https://vote.example.com/server/123 - thanks!")
        return true
    end
    if previous then return previous(text, player, is_operator, reply, ...) end
    return false
end
```

Only plain `http`/`https` addresses with an ordinary site name become links. Addresses with spaces, quotes or other unusual characters, and ones like `https://real.site@other.site` (which hide where they really go), are left as plain text.

**Scrolling back.** With the chat open, the mouse wheel or Page Up / Page Down scroll through the last 200 lines; the bar at the right shows where you are. New messages don't move what you're reading. Closing the chat goes back to the newest lines.

<a id="fn-hooks-on_chat_complete"></a>**Tab completion.** In chat, typing `/` lists the commands that fit (with their usage) and Tab fills in the highlighted one; Up/Down pick another (the list scrolls, so every match can be reached). After the command, Tab completes its arguments: game rules, item names, players, `~`. In ordinary chat, Tab completes the names of players online (the first Tab shows the list, or fills the name in if only one fits). On a server, the server is asked as well, so its commands and their arguments show up too, but only the ones your operator level lets you use.

Your commands' arguments come from `on_chat_complete`. Return everything that could go where the word is being typed; the game keeps only what starts with what's been typed so far, so you don't need to filter:

```lua
-- words: the words before the one being typed ("/warp ho" -> { "warp" }, word = "ho")
local previous_complete = on_chat_complete              -- keep the other mods' completions
function on_chat_complete(words, word, player)
    local out = previous_complete and previous_complete(words, word, player) or {}
    if words[1] == "warp" and #words == 1 then
        for name in pairs(warps) do out[#out + 1] = name end
    end
    return out
end
```

The same hook runs in server mods, where `player` is who pressed Tab (in the game it's `nil`). A server mod's `chat.register_command` commands are listed in players' chat with their usage and description, like the game's own, to players whose operator level allows them (commands only a mod knows about are listed for everyone; check `is_operator` in `on_chat_command` as usual). The base mod completes `/time`, `/gamerule`, `/give` and `/tp`, and the weather mod completes `/weather`.

## 9. Sky, weather, fog, clouds, underwater

**Weather** is decided entirely in Lua by `mods/weather`. It's Minecraft-like by default: long clear spells, rain, thunderstorms, and snow where your game says it's cold. The engine only draws what that mod asks for:

| function | |
|---|---|
| <a id="fn-sky-set_precipitation"></a>`sky.set_precipitation{ type, intensity, fall_speed, size, length, sway, wind_x, wind_z, color = {r,g,b,a}, texture, radius, height, max_particles, streaks }` | rain, snow or anything else (ash, leaves) with your own picture. `type = "rain"` or `"snow"` starts from a preset; other fields change only what you give. Particles stop on roofs and water. |
| <a id="fn-sky-get_precipitation"></a>`sky.get_precipitation()` | `{ type, intensity }` |
| <a id="fn-sky-set_sky_tint"></a>`sky.set_sky_tint(r, g, b, a)` | colour laid over the sky (grey rain, dark storm) |
| <a id="fn-sky-get_light"></a><a id="fn-sky-set_light"></a>`sky.set_light(level)` / `get_light()` | sunlight on the world: 1 = normal, 0.5 = stormy |
| <a id="fn-sky-flash"></a>`sky.flash(strength, seconds)` | a lightning flash |
| <a id="fn-sky-lightning_bolt"></a>`sky.lightning_bolt(x, z [, top_y [, seconds]])` | a visible lightning bolt from the clouds (or `top_y`) down to the ground at x, z. It is jagged and branching and flickers for `seconds` (default 0.45). It is only drawn: it starts no fire and does no damage. The weather mod's thunderstorms use it with `sky.flash`. |
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
- <a id="fn-sky-get_time"></a><a id="fn-sky-set_time"></a>**The time of day.** `sky.get_time()` goes from 0 to 1 over a whole day: `0.25` midnight, `0.5` sunrise, `0.75` midday, `0` sunset. `sky.set_time(t [, forward])` jumps there and the clock keeps running; with `forward = true` it goes on to the *next* time it's `t` (so the day count goes up, like Minecraft's `/time set`).
  - <a id="fn-sky-get_day"></a><a id="fn-sky-get_clock"></a>`sky.get_day()`: days since the world began (0 is the first; it goes up at midnight). `sky.get_clock()`: the time as `"07:15"`.
  - <a id="fn-sky-add_time"></a>`sky.add_time(hours)` moves the clock on (or back).
  - <a id="fn-sky-set_time_speed"></a><a id="fn-sky-get_time_speed"></a>`sky.set_time_speed(x)` / `get_time_speed()`: 1 normal, 2 twice as fast, 0 stops time.
  - <a id="fn-sky-set_day_length"></a><a id="fn-sky-get_day_length"></a>`sky.set_day_length(seconds)` / `get_day_length()`: a whole day lasts 1200 seconds (20 minutes) unless changed.

  The time is saved with the world and carries on where it was. **On a server, the server's clock is the one everyone sees.** It sends it to players when they join and every few seconds, and at once when it jumps. Server mods use the same `sky.get_time` / `set_time` / `add_time` / `set_time_speed` (the rest of `sky.*` does nothing on a server). A player's own mods can't change the time there: the next update from the server puts it back. When you host a LAN game, your game is the server. The moon's phase (`sky.get_moon_phase`) changes every day, so it's the same for everyone too.

  In chat: `/time` (what time and day it is), `/time set day|noon|night|midnight|sunrise|sunset|<hh:mm>`, `/time add <hours>`, `/time speed <x>`. On a dedicated server these go to the server (operators only); its console has the same `time` command.
- <a id="fn-sky-get_moon_phase"></a><a id="fn-sky-set_moon_phase"></a><a id="fn-sky-set_moon_textures"></a>`sky.set_moon_phase(n)` / `get_moon_phase()`, `sky.set_moon_textures({ ... })`
- <a id="fn-sky-set_stars"></a><a id="fn-sky-get_stars"></a>`sky.set_stars(density [, brightness])` / `get_stars()`: how starry the night is. `density` 0–1 (default 0.06), `brightness` 0 = no stars (default 0.9). Stars are fixed, steady dots that fade in at dusk.
- <a id="fn-engine-set_moon_texture"></a><a id="fn-engine-set_sun_texture"></a>`engine.set_sun_texture(path)`, `engine.set_moon_texture(path)`
- <a id="fn-sky-set_ambient_color"></a><a id="fn-sky-set_fog_color"></a><a id="fn-sky-set_sun_color"></a>`sky.set_sun_color(r,g,b)`, `set_ambient_color(r,g,b)`, `set_fog_color(r,g,b)`
- <a id="fn-sky-set_ambient_intensity"></a><a id="fn-sky-set_sun_intensity"></a>`sky.set_sun_intensity(v)`, `set_ambient_intensity(v)`
- <a id="fn-sky-reset"></a>`sky.reset()`

<a id="fn-sky-get_rain_intensity"></a><a id="fn-sky-set_rain_intensity"></a>**Rain:** `sky.set_rain_intensity(0..1)` / `get_rain_intensity()`

**Lights:** torches, lamps, coloured light and moving lights are in [section 10](#10-light).

**Clouds:**
- <a id="fn-clouds-set_color"></a><a id="fn-clouds-set_night_color"></a>`clouds.set_color(r,g,b)`, `set_night_color(r,g,b)`
- <a id="fn-clouds-set_cell_size"></a><a id="fn-clouds-set_height"></a><a id="fn-clouds-set_scroll_speed"></a><a id="fn-clouds-set_thickness"></a><a id="fn-clouds-set_threshold"></a>`clouds.set_height(y)`, `set_thickness(t)`, `set_cell_size(c)`, `set_threshold(t)`, `set_scroll_speed(v)`
- <a id="fn-clouds-set_texture"></a>`clouds.set_texture(path)`
- <a id="fn-clouds-set_weather"></a>`clouds.set_weather(name, seconds)` fades to a weather's clouds over that many seconds, and returns false for an unknown name. During the change, clouds form or melt away cell by cell. The weathers:
  - `"clear"`: no clouds at all.
  - `"cloudy"`: scattered white clouds.
  - `"rain"`: thicker grey clouds.
  - `"thunder"` (or `"storm"`): one huge, dark, low, thick storm cloud over everything.
  - `"snow"`: clouds bunched together into big soft-grey masses.
  - `"blizzard"`: like snow, thicker and darker.

  The weather mod does this for you: each weather it registers names its clouds (`clouds = "rain"`).
- <a id="fn-clouds-set_clump"></a>`clouds.set_clump(0..1)`: how bunched together the clouds are, from 0 (scattered puffs) to 1 (big masses).

**Fog:**
- <a id="fn-fog-set_enabled"></a><a id="fn-fog-set_exponential"></a>`fog.set_enabled(bool)`, `set_exponential(bool)`
- <a id="fn-fog-set_density"></a><a id="fn-fog-set_rain_scale"></a><a id="fn-fog-set_range"></a>`fog.set_range(start, end)`, `set_density(d)`, `set_rain_scale(s)`
- <a id="fn-fog-set_day_color"></a><a id="fn-fog-set_night_color"></a><a id="fn-fog-set_sunset_color"></a>`fog.set_day_color(r,g,b)`, `set_sunset_color(r,g,b)`, `set_night_color(r,g,b)`

**Underwater:**
- <a id="fn-underwater-set_deep_color"></a><a id="fn-underwater-set_shallow_color"></a>`underwater.set_shallow_color(r,g,b)`, `set_deep_color(r,g,b)`
- <a id="fn-underwater-set_full_depth"></a><a id="fn-underwater-set_visibility"></a>`underwater.set_visibility(shallow_blocks, deep_blocks)`, `set_full_depth(blocks)`
- <a id="fn-underwater-set_fade"></a><a id="fn-underwater-set_tint_strength"></a>`underwater.set_fade(in_seconds, out_seconds)`, `set_tint_strength(0..1)`. The tint colours everything like the water, however close, so a small pool looks underwater too. The fog only shows on things far away. The default strength is 0.42.

<a id="particles"></a>**Particles** (Minecraft style). The engine makes these on its own:
- **Blocks:** bits when a block breaks (also when another player breaks one), and chips flying off the face you're digging.
- **Movement:** dust kicked up while sprinting, and a puff when you land from a fall (more the harder you land).
- **Water:** a splash when you drop in, and bubbles while your head is under.
- **Mobs:** crit sparks bursting round a mob when it's hit, and a white "poof" when one dies.

The pictures are part of Craft & Ruin's `core_blocks` (`games/craft_and_ruin/mods/core_blocks/textures/`: `particle_smoke_0.png` to `particle_smoke_7.png`, `particle_flame.png`, `_lava`, `_spark`, `_bubble`, `_splash`, `_drip`, `_crit`, `_heart`, `_happy`, `_note`), so a texture pack can replace them, and another game can ship its own with the same names. `core_blocks/particles.lua` also gives blocks effects of their own: torches flicker and smoke, lava pops, leaves drift down, and water drips through ceilings. Particles are drawn by each player's game only: they're lit by the light where they are (coloured light too) unless they glow, and fog and the underwater tint apply to them.

<a id="fn-particles-spawn"></a>`particles.spawn{ ... }` makes some. Everything but `pos` and a picture is optional:

```lua
particles.spawn{
    pos = { x, y, z },
    velocity = { 0, 1, 0 },        -- blocks a second
    gravity = 12,                  -- or acceleration = { x, y, z }
    drag = 0,                      -- speed lost a second (air resistance)
    life = 1, size = 0.1, size_end = 0.03,          -- seconds, blocks across
    color = "#ff8800", color_end = { 0.2, 0.2, 0.2 },
    texture = "particle_flame.png",                 -- or { frames... } played over its life
    -- block = "stone",            -- instead of a texture: bits of that block
    glow = false,                  -- full bright, whatever the light
    collide = true, bounce = false,                 -- stop on (or bounce off) blocks
    amount = 1, spread = { 0.3, 0, 0.3 }, velocity_spread = { 1, 1, 1 },   -- several, scattered
}
```

<a id="fn-particles-add_spawner"></a><a id="fn-particles-remove_spawner"></a>`particles.add_spawner{ ... }` keeps making them: the same fields, plus `rate` (per second), `time` (seconds, 0 means until removed), `follow_player`, and `life` and `size` as `{ min, max }`. It returns an id for `particles.remove_spawner(id)`.

<a id="fn-particles-register_ambient"></a>`particles.register_ambient{ nodenames = { "mymod:candle" }, action = function(x, y, z, block) ... end }` is Minecraft's "animate tick". Each frame, random blocks near the player are looked at, roughly 3 times a second for each block within 16. For the listed kinds, `action` runs and can spawn particles (a candle's flame, a cauldron's bubbles). Use `math.random` in it to make effects rarer.

<a id="fn-particle_effects"></a>Ready-made effects from `core_blocks/particles.lua`: `particle_effects.hearts(x, y, z)`, `.happy`, `.smoke`, `.flame`, `.notes`, `.sparks`.

<a id="fn-hooks-get_water_color"></a>Define `get_water_color(x, z)` to return `{ r, g, b, deep_r, deep_g, deep_b }` per place, for example by biome.

<a id="fn-engine-is_third_person"></a><a id="fn-engine-set_fov"></a><a id="fn-engine-get_camera_mode"></a><a id="fn-engine-set_camera_mode"></a>**Camera:** `engine.set_fov(degrees)`, `engine.is_third_person()` (true behind or in front). F5 cycles like Minecraft: first person, then behind the player, then in front of them looking back (no crosshair in that view). `engine.get_camera_mode()` returns `"first"`, `"back"` or `"front"`. `engine.set_camera_mode(mode)` switches to one of those and returns false for any other name.
<a id="fn-engine-set_player_look"></a><a id="fn-engine-get_player_look"></a><a id="fn-engine-set_hud_visible"></a><a id="fn-engine-is_hud_visible"></a>`engine.set_player_look(yaw, pitch)` turns the player's view, in degrees: yaw −90 looks north (−Z), 0 east (+X); pitch 90 is straight up, kept within ±89.9. `engine.get_player_look()` returns `yaw, pitch`. **F1** hides the HUD, crosshair and hand for screenshots, like Minecraft. `engine.set_hud_visible(on)` and `engine.is_hud_visible()` do the same from a mod.

## 10. Light

The engine has Minecraft-style block light, with colour. Every block space holds four light levels from 0 to 15: **sunlight**, and **red**, **green** and **blue** block light.

- **Sunlight** comes straight down from the sky at full strength, then spreads sideways into caves and under overhangs, losing 1 per block. It follows the time of day and the weather (`sky.set_light`).
- **Block light** spreads out from light sources the same way, each colour on its own. A red lamp and a blue lamp side by side make purple where their light meets. A torch fades from white-yellow to orange, because its blue runs out first.
- Solid blocks stop light. Water and leaves dim sunlight by 1 level per block, so it reaches about 15 blocks down into the sea. Glass lets light through, and stained glass can colour it.
- Light updates as soon as a block changes: dig into a cave and the sun shines in; place a torch and the room lights up.
- Chunks are drawn with **smooth lighting** (light blends from corner to corner) and **ambient occlusion** (soft shadows where blocks meet).
- In multiplayer, every player works out light from the blocks, so it never needs to be sent.

### Light sources and see-through blocks

Give a block these properties in `register_block` (or change one with `engine.override_block`):

<a id="fn-block-light"></a>
```lua
engine.register_block("torch", 1, 1, 1, "torch.png", nil, {
    pattern = "plantlike", transparent = true, walkable = false,
    light = 14,                          -- 0..15, how far it reaches
    light_color = { 255, 220, 160 },     -- 0..255 (or 0..1), or "#ffdca0"
})
engine.register_block("ruby_lamp", 1, 1, 1, "ruby_lamp.png", nil, {
    light = { 15, 2, 4 },                -- or red, green and blue levels directly
})
engine.register_block("red_stained_glass", 1, 1, 1, "red_glass.png", nil, {
    transparent = true,
    light_filter = { 1, 0.15, 0.15 },    -- the colour let through (0..1 or 0..255)
})
engine.register_block("murky_water", 1, 1, 1, "murky.png", nil, {
    transparent = true,
    light_absorb = 3,                    -- light lost per block passing through (water is 1)
})
```

| Property | What it does |
|---|---|
| `light` | Light the block gives off: a level 0–15 (coloured by `light_color`), or `{ r, g, b }` levels. `light = 0` turns off a guessed light (below). |
| `light_color` | Colour of `light`: `{ r, g, b }` (0–255 or 0–1) or `"#rrggbb"`. Default white. |
| `light_filter` | Colour of light let through, for stained glass: `{ r, g, b }`. Default `{ 1, 1, 1 }`. |
| `tint_view` | Looking through the block tints everything behind it in its picture's colour, like stained glass. Blocks, water, mobs and the sky are all tinted, at any time of day, and several panes in a row multiply. On by default for a see-through (`transparent`) block with a `light_filter`; `tint_view = false` turns it off and `true` turns it on without a filter. How strong the tint is depends on how opaque the picture is: a half-transparent picture tints fully, and white or fully clear pixels let everything through. Light passing through is only coloured by `light_filter`; sunlight isn't coloured. |
| `light_absorb` | Light lost per block passing through, like Minecraft's light opacity. Sunlight going straight down loses only this; sideways, at least 1 is always lost. Default 1 for water, other fluids and leaves (sunlight reaches about 15 blocks deep), otherwise 0. Set `0` for crystal-clear water. |

**Which blocks stop light:** solid, non-transparent blocks with `pattern = "cube"`. Transparent blocks, cutout blocks (leaves), plants, fluids, doors and models let light through.

**Guessed lights:** blocks registered without `light` glow if their name says so: *torch* (14, warm), *lantern*, *glowstone*, *fire* (15), *lava* (15, orange), *sea_lantern* (15, cool). Set `light = 0` to stop that.

### Reading light

- <a id="fn-engine-get_light"></a>`engine.get_light(x, y, z)` returns `sun, red, green, blue` (0–15 each), the light stored at that block.
- <a id="fn-engine-get_light_level"></a>`engine.get_light_level(x, y, z)` returns 0–15: how bright it is there right now. It takes the brightest of the block light and the sunlight, with sunlight 11 lower at night, like Minecraft. Use it for mob spawning, crops and so on:

```lua
if engine.get_light_level(x, y + 1, z) <= 7 then spawn_zombie(x, y + 1, z) end
```

### How light looks

- <a id="fn-sky-set_brightness"></a>`sky.set_brightness(v)`: 0 = moody (dark nights and caves), 1 = bright, like Minecraft's brightness slider. Default 0.35. This is the *game's* brightness. Players also have a **Brightness** slider (Settings > Video & Sound, saved as `brightness` in settings.txt): in the middle ("Default") they see exactly the game's look; to the left it goes down to fully moody; to the right it lifts the final light the way Minecraft's brightness does, so caves and nights get brighter too (a pitch-black cave is about 28% lit at "Bright", a night about 73%), while daylight stays the same. So a dark, spooky game can set a low value, and a player who finds it too dark can still brighten it.
- <a id="fn-sky-set_min_light"></a>`sky.set_min_light(v)`: the darkest anything gets, 0–1. Default 0.02; 0 makes sealed caves pitch black.
- <a id="fn-sky-set_block_light_strength"></a>`sky.set_block_light_strength(v)`: how strong light sources look (default 1; 1.3 for a cosier game, 0.7 for horror).
- <a id="fn-engine-set_ambient_occlusion"></a><a id="fn-engine-set_smooth_lighting"></a>`engine.set_smooth_lighting(on)`, `engine.set_ambient_occlusion(on)`: both on by default. Changing one redraws every chunk.

### Moving lights

Lights that aren't a block (a glowing mob, a magic spell, a lightning bolt) are point lights. Up to 16 show at once.

- <a id="fn-sky-add_light"></a>`sky.add_light(x, y, z, r, g, b, radius, intensity)` returns an id (`r, g, b` are 0–1, `radius` in blocks).
- <a id="fn-sky-move_light"></a>`sky.move_light(id, x, y, z [, r, g, b, radius, intensity])` moves it, and changes anything else you pass.
- <a id="fn-sky-remove_light"></a>`sky.remove_light(id)` removes it. Ids never change while other lights come and go.
- <a id="fn-sky-set_held_light"></a>`sky.set_held_light(on)`: holding a light source in the hotbar lights up around the player as they walk (on by default).

Point lights shine through walls, so keep them small, and use blocks for lights that stay put.

## 11. Survival: health, hunger, digging

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
- <a id="fn-engine-get_player_state"></a>`engine.get_player_state()` gives position, velocity, `on_ground`, `sprinting`, `sneaking`, `jumping`, `flying`, and `feet_fluid` / `head_fluid`.
- **Flying (creative):** like Minecraft, double-tap Jump to fly. Jump rises, Sneak sinks, Sprint flies faster (21.6 blocks/s instead of 10.9), and landing or double-tapping again stops it. <a id="fn-engine-is_flying"></a><a id="fn-engine-set_flying"></a><a id="fn-engine-set_fly_allowed"></a>`engine.is_flying()`, `engine.set_flying(on)` (false outside creative) and `engine.set_fly_allowed(false)` (no flying, e.g. on a parkour map; `true` allows it again). On a server, only players in creative mode can fly: the server ignores flying from anyone else.
- <a id="fn-engine-set_player_controls"></a>`engine.set_player_controls{ move, jump, sprint, sneak }` blocks keys. Prediction and the server stay in sync.
- <a id="fn-engine-add_player_velocity"></a><a id="fn-engine-set_player_position"></a>`engine.set_player_position(x, y, z)`, `engine.add_player_velocity(...)`.
- <a id="fn-engine-respawn_player"></a>`engine.respawn_player(x, y, z)`. On a server, the server's spawn is used.
- <a id="fn-engine-get_player_position"></a>`engine.get_player_position()` returns x, y, z of the feet (a quicker form of `get_player_state`).

**Game mode and blocks:**
- <a id="fn-engine-is_creative"></a>`engine.is_creative()` says which mode is on (`engine.set_creative(bool)` changes it).
- <a id="fn-engine-get_dig_progress"></a>`engine.get_dig_progress()` returns progress (0..1), x, y, z while a block is being dug in survival, or nil.
- <a id="fn-engine-get_block_def"></a>`engine.get_block_def(name)` returns `{ name, hardness, tool, needs_tool, drops, pick, tool_type, tool_speed, solid, item, has_sounds }`.
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

<a id="fn-survival-inventory-add"></a>The survival mod has a complete Minecraft-style one, drawn on the game's inventory picture (`survival_inventory.png`, or `inventory.png` to use your own). It has 8 equipment slots, a 2×2 crafting grid with its output, a bin, 27 storage slots and the hotbar, with click, right-click and shift-click. Other mods can use `survival.inventory.add(item, count)` (returns how many didn't fit), <a id="fn-survival-inventory-get"></a><a id="fn-survival-inventory-set"></a><a id="fn-survival-inventory-clear"></a><a id="fn-survival-inventory-size"></a><a id="fn-survival-inventory-is_open"></a>`get(i)`, `set(i, item, count)`, `clear()`, `size()` and `is_open()`. <a id="fn-survival-inventory-pick"></a>`survival.inventory.pick(item)` puts an item from the inventory in the player's hand, by the pick block rules below (returns false if they don't have it).

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
- <a id="fn-survival-register_craft"></a>`survival.register_craft({ output = "planks", count = 4, recipe = { { "log_oak" } } })`: a crafting recipe (rows of item names; mirrored shapes match too). Recipes up to 2×2 work in the inventory; anything up to 3×3 needs a workbench. Use `type = "shapeless", recipe = { "a", "b" }` for any arrangement. Shift-clicking the output crafts as many as possible.
- <a id="fn-survival-register_workbench"></a>`survival.register_workbench("my_table")`: right-clicking that block opens the 3×3 crafting screen (`survival_workbench.png`, or `workbench_gui.png` to use your own). Craft & Ruin's `workbench` is made from 4 planks. Sneak + right-click with a block in hand places it against the workbench instead.
- <a id="fn-survival-register_equipment"></a><a id="fn-survival-on_equip"></a>`survival.register_equipment("iron_helmet", "head", { armor = 2 })`: which items go in which equipment slot. Slots are `pet`, `head`, `glove_left`, `chest`, `glove_right`, `feet`, `legs` and `backpack`. Armour from everything worn is added up automatically. `survival.on_equip(function(slot, item) end)` runs when something is put on or taken off, and `survival.inventory.get_equipment(slot)` reads a slot.

## 12. Sound

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

## 13. Models

<a id="fn-engine-add_model"></a><a id="fn-engine-load_model"></a><a id="fn-engine-move_model"></a><a id="fn-engine-remove_model"></a><a id="fn-engine-set_model_visible"></a>
```lua
local cow = engine.load_model("cow.b3d" [, "cow.png" [, scale]])   -- .b3d (Luanti) or .json (Bedrock)
local id = engine.add_model(cow, x, y, z [, yaw [, scale]])          -- x, y, z = feet
engine.move_model(id, x, y, z [, yaw])
engine.set_model_visible(id, bool)
engine.remove_model(id)
```

- **Units:** `yaw` is in degrees; 0 faces +x (east) and 90 faces +z.
- **One pose per model:** every copy placed with `add_model` shares the loaded model's pose. Load the model again for copies that animate separately.

**Moving bones:**
- <a id="fn-model-get_bone"></a>`model:get_bone(name)` returns a bone, or nil.
- <a id="fn-bone-set_rotation"></a><a id="fn-bone-get_rotation"></a>`bone:set_rotation(x, y, z)` / `bone:get_rotation()`: degrees, relative to the model file's pose. For a Blockbench/Bedrock model these are Blockbench's numbers and behave exactly as Blockbench shows them:
  - an arm or leg at -X swings forward;
  - a knee (lower leg) at +X bends back, and an elbow (lower arm) at -X bends forward;
  - a head or body at +X leans or looks down;
  - the right arm goes out to the side at +Z, the left at -Z.
- <a id="fn-bone-set_position"></a>`bone:set_position(x, y, z)`: an offset from where the model file puts the bone, in the model's own coordinates, so the model's scale applies to it. For a Blockbench/Bedrock `.json` model, 1 is one block (its cubes' pixels are already divided by 16). For a `.b3d` model it's the file's units, shrunk by the model's scale (0.1 by default, so 10 units = 1 block). `model:reset_pose()` sets it back to 0; the file's own positions are never lost.
- <a id="fn-model-reset_pose"></a>`model:reset_pose()` puts every bone back to the model file's pose.
- <a id="fn-model-is_loaded"></a>`model:is_loaded()` is false when the file was missing or couldn't be read. `engine.load_model` still returns a model then, which draws nothing, so check this before relying on it. `g_PlayerModel` is the model drawn as the player in third person (F5). If it's nil or not loaded, the console says so once: `[Player] Third person: no player model to draw: ...`.

<a id="fn-hooks-on_update"></a><a id="fn-hooks-update_player_animation"></a>Use `on_update(dt)` to move them every frame. The player model is the global `g_PlayerModel`, and the engine calls `update_player_animation(dt, is_moving, pitch)`.

### First-person hand

In first person the engine shows what you're holding at the bottom of the screen. A block is a little cube; an item with an `inventory_image` (and plants) is a flat picture. With an empty hand it shows your arm, taken from the player model (`g_PlayerModel`) or a plain skin-coloured arm if the model has no arm bone. The hand sways as you walk, swings when you dig, place or use something, and dips and comes back up when you switch hotbar slots. The **Main Hand** setting (Settings > Video & Sound) moves it to the left for left-handed players.

- <a id="fn-engine-set_first_person_arm"></a>`engine.set_first_person_arm({ right = "upper_arm_right", left = "upper_arm_left", color = { 224, 176, 140 } })`: which bone of the player model is the arm (it's drawn with everything attached to it, in the model file's pose), and the plain arm's colour. Without this, common names such as `right_arm` and `arm_right` are tried.
- <a id="fn-engine-set_first_person_hand"></a>`engine.set_first_person_hand(false)` hides it, for example for a cutscene.
- <a id="fn-engine-set_third_person_item"></a>In third person (F5), what you're holding shows in the player model's hand and moves with the arm through walking, swings and every other animation. A block is held as a small cube in the fist; an item picture (a tool, a stick) is held by its handle, pointing forward. The hand is the bottom of the model's forearm bone: the first of `lower_arm_right`, `right_hand`, `hand_right`, `forearm_right`, then the arm names `set_first_person_arm` accepts (the left side for left-handed players). `engine.set_third_person_item(false)` hides it. `engine.set_third_person_item(true, "my_right_hand", "my_left_hand")` names the bones of a model whose hands are called something else.
- <a id="fn-engine-swing_hand"></a>`engine.swing_hand()` plays the swing, for example when your mod's item is used.
- <a id="fn-engine-get_swing"></a>`engine.get_swing()` returns how far through the swing you are (0–1), or -1 when you're not swinging. The player mod uses it to swing the model's arm in third person.
- <a id="fn-engine-is_left_handed"></a>`engine.is_left_handed()` returns the player's Main Hand setting.

### Animations

The engine plays **Bedrock / Blockbench animation files** (`.animation.json`): export from Blockbench with *File > Export > Export Bedrock Animations*. Put them in your mod's folder or its `models/` folder.

<a id="fn-engine-load_animations"></a><a id="fn-animation-apply"></a><a id="fn-animation-set_loop"></a><a id="fn-animationset-get"></a><a id="fn-animationset-names"></a>
```lua
local anims = engine.load_animations("mob.animation.json")
print(table.concat(anims:names(), ", "))     -- every animation in the file
local walk = anims:get("animation.mob.walk")  -- or the short name "walk", or anims:get() for the first
walk:set_loop(true)                           -- the file's "loop": true does the same

local t = 0
function on_update(dt)
    t = t + dt
    model:reset_pose()                        -- bones back to the model file's pose
    walk:apply(model, t)                      -- pose it at t seconds
    -- walk:apply(model, t, 0.5)              -- half strength: blend with what's there
end
```

- **What's supported:** rotation (degrees), position (pixels, 16 per block) and scale per bone, as a constant or keyframes. Keyframes can be `linear`, `catmullrom` (smooth) or `step`, with `pre`/`post` values.
- **Blending:** call `reset_pose()`, then `apply` each animation with a weight. For example idle at `1 - w` and walk at `w`, raising `w` while moving, gives smooth starts and stops.
- **Molang** (expressions like `"math.sin(query.anim_time * 360) * 20"`) isn't supported yet. Those values count as 0, and the console says so. Use keyframes instead.
- <a id="fn-animation-get_length"></a><a id="fn-animation-is_looping"></a><a id="fn-animation-get_name"></a>`walk:get_length()`, `is_looping()` and `get_name()` tell you about an animation.

**Exactly as in Blockbench.** Bedrock models and animations look in the game just as they do in Blockbench: the same side is the right, and rotations turn the same way. Bedrock files store X mirrored and Blockbench flips it back when it shows them; the engine does the same.

<a id="player-animations"></a>**The Craft & Ruin player** (`mods/player/`) has a model with two joints in every limb, so knees and elbows bend. Its animations are in `models/player.animation.json`:
- **Movement:** `idle` (breathing), `idle_look` (glancing about now and then), `walk`, `sprint`, `sneak`, `sneak_walk`, `jump`, `fall`, `land`, `swim`, `tread_water`, `fly` and `fly_move`.
- **Actions:** `attack` (every swing: digging, hitting, placing), `use`, `eat`, `hurt`, `death`, `wave` and `blink`.
- **Poses:** `sit` (as on a chair or seat), `sit_ground` (on the floor: leaning back on the hands, one knee up) and `sleep` (lying on the back, eyes shut, breathing slowly).

`animations.lua` picks the movement animation from what the player is doing and crossfades from one to the next. Walking and sprinting speed up or slow down with your speed. On top of that it plays the swing, `hurt` when you lose health, `eat` when the survival mod feeds you, `death` while you're dead, `land` after a fall, and a blink every few seconds. The head follows the camera.

<a id="fn-player_anim"></a>Other mods can play any of the actions on the player:
- `player_anim.play("wave")` plays one once;
- `player_anim.play("sit", true)` holds one until `player_anim.stop("sit")`;
- `player_anim.is_playing(name)` and `player_anim.list()` tell you what's playing and what's available.

<a id="fn-player_anim-sit"></a><a id="fn-player_anim-sleep"></a><a id="fn-player_anim-stand"></a>Sitting and sleeping are poses: the whole body takes them, with no walking legs underneath, and they fade in. Only one pose plays at a time.
- `player_anim.sit()` sits on the floor; `player_anim.sit("chair")` sits as on a seat. Put the player on the seat's top yourself.
- `player_anim.sleep()` lies down on the back, centred where the player stands. The head doesn't follow the camera and the eyes stay shut.
- `player_anim.stand()` gets up again. `player_anim.get_pose()` returns `"sit"`, `"sit_ground"`, `"sleep"` or nil.
- Started like this, a pose ends by itself when the player walks, jumps, flies or swims. Pass `true` as the last argument (`player_anim.sleep(true)`, `player_anim.sit("chair", true)`) to keep it until `player_anim.stand()`, for a bed or chair mod that holds the player in place. `player_anim.play("sleep")` also holds the pose.
- Players can type `/sit`, `/sit chair`, `/sleep` (or `/lay`) and `/stand`.

The pose only changes how the model looks in third person (F5). The first-person camera stays at eye height.

For mobs, use `entities` instead ([§13a](#13a-mobs-and-other-entities)). The engine places them, animates each one separately, and shows them to everyone in multiplayer.

## 13a. Mobs and other entities

An **entity** is anything that moves around the world by itself: mobs, and items dropped on the ground. You describe a kind of entity with `entities.register`; the engine moves it (gravity, collisions with blocks, walking, water), draws it, saves it with the world, and lets players hit and right-click it. Your functions decide what it does.

**Who runs them.** Your game in singleplayer and when you host a LAN game; the server on a dedicated server. That's where `on_step`, `on_punch` and the other functions run, and where `entities.spawn` works. Everyone else's game only receives *data* about each entity it can see (its kind, where it is, its animation, its name tag) and draws it. No Lua is ever sent. So register your entities in a mod that **both** the server and the players have: the server needs the behaviour, players need the model. A kind of entity a player's game doesn't know (a server-only mod) is drawn as a plain box of its hitbox's size.

<a id="fn-entities-register"></a>
```lua
entities.register("mymod:goat", {
    -- shape and physics
    hitbox = { 0.9, 1.3 },       -- width (square footprint) and height, in blocks
    health = 10,
    step_height = 0.6,           -- walks up ledges this high (0.6 = slabs, not full blocks)
    auto_jump = true,            -- walking into a 1-block step jumps it
    gravity = true,              -- false: it floats where you put it (set_velocity moves it)
    collides = true,             -- false: passes through blocks
    knockback = true,            -- pushed back when hit
    solid = true,                -- bumps into other mobs and players instead of walking through them
                                 --   (false: passes through, e.g. ghosts or tiny critters)
    despawn = false,             -- true: removed when no player is near, and never saved (monsters)
    -- looks (the games that draw it)
    model = "goat.geo.json",     -- .geo.json (Bedrock / Blockbench) or .b3d, from your mod's models/
    texture = "goat.png",
    animations = "goat.animation.json",
    scale = 1,
    -- when it dies
    drops = { { "mutton", 1, 2 }, { "wool", 0, 1 } },   -- item, fewest, most
    -- what it does (the authority only)
    on_spawn = function(self, saved) end,            -- saved: what on_save returned last time (or nil)
    on_step = function(self, dt) end,                -- every tick while a player is within 128 blocks
    on_punch = function(self, player, item, damage) end,   -- return true: no damage, no knockback
    on_right_click = function(self, player, item) end,
    on_death = function(self, killer) end,           -- return true: it stays alive after all
    on_save = function(self) return "some text" end, -- kept with the world
})
```

Names need a `mymod:` prefix. Names starting with `__` are the engine's (`__item` is a dropped item).

**`self`** is a table that stays the same for the whole life of the entity: put your own fields on it (`self.target = name`). `self.id` and `self.name` (its kind) are set for you. Its functions:

| | |
|---|---|
| <a id="fn-entity-get_pos"></a>`self:get_pos()` / `set_pos(x, y, z)` | its feet, centred |
| <a id="fn-entity-get_velocity"></a>`self:get_velocity()` / `set_velocity(x, y, z)` / `add_velocity(x, y, z)` | blocks per second (set_velocity stops walking) |
| <a id="fn-entity-get_yaw"></a>`self:get_yaw()` / `set_yaw(degrees)` | which way it faces: 0 = +x (east), 90 = +z (south) |
| <a id="fn-entity-go_to"></a>`self:go_to(x, y, z [, speed])` | **finds a way there and follows it** (below). Fine to call every tick; false if there's no way at all |
| <a id="fn-entity-wander"></a>`self:wander([radius [, speed]])` | walks to a random spot it can reach within `radius` (8) blocks, never into water. In water it heads for the nearest land instead. False if nowhere was found |
| <a id="fn-entity-find_land"></a>`self:find_land([radius])` | the nearest dry land it can get to: x, y, z (or nil) |
| <a id="fn-entity-path_state"></a>`self:path_state()` | `"following"` (on its way), `"arrived"`, `"partial"` (got as near as it could), `"stuck"` or `"none"` |
| <a id="fn-entity-can_see"></a>`self:can_see(x, y, z)` / `can_see_player(name)` | a ray from its eyes through the blocks: true if nothing solid is in the way |
| <a id="fn-entity-walk"></a>`self:walk(speed)` / `stop()` | walks straight ahead at `speed` blocks/s (no path), jumping a 1-block step only if its body fits on top; `stop()` also ends a path |
| <a id="fn-entity-is_blocked"></a>`self:is_blocked()` | walked into something it couldn't step or jump over this tick |
| <a id="fn-entity-walk_to"></a>`self:walk_to(x, z [, speed [, near]])` | turns and walks straight towards a point (no path); true once within `near` (1) blocks |
| <a id="fn-entity-look_at"></a>`self:look_at(x, z)` | turns to face a point |
| <a id="fn-entity-set_animation"></a>`self:set_animation(name [, speed [, loop]])` / `get_animation()` | plays an animation from the `animations` file, from its start. The same name again carries on. `loop = false` plays it once and holds the end. |
| <a id="fn-entity-set_nametag"></a>`self:set_nametag(text)` | shown above it ("" = none) |
| <a id="fn-entity-get_health"></a>`self:get_health()` / `set_health(h)` / `get_max_health()` | 0 health = it dies |
| <a id="fn-entity-damage"></a>`self:damage(amount [, attacker])` | hurts it like a hit: red flash, knockback away from the attacker (a player name) |
| <a id="fn-entity-remove"></a>`self:remove()` / `is_alive()` | gone straight away (no drops, no on_death) |
| <a id="fn-entity-is_on_ground"></a>`self:is_on_ground()` / `is_in_water()` | |
| <a id="fn-entity-distance_to"></a>`self:distance_to(x, y, z)` / `nearest_player([max])` | |

Everything else:

| | |
|---|---|
| <a id="fn-entities-spawn"></a>`entities.spawn(name, x, y, z [, data])` | a new one; `data` goes to its `on_spawn`. Returns its `self` (nil on a player's game when joined to a server) |
| <a id="fn-entities-get"></a>`entities.get(id)`, `entities.get_all([name])` | `self` tables |
| <a id="fn-entities-in_radius"></a>`entities.in_radius(x, y, z, r [, name])` | entities within `r` blocks |
| <a id="fn-entities-nearest_player"></a>`entities.nearest_player(x, y, z [, max])` | name, x, y, z, distance (or nil) |
| <a id="fn-entities-get_players"></a>`entities.get_players()` | `{ {name=, x=, y=, z=}, ... }`, everyone in the world |
| <a id="fn-entities-line_of_sight"></a>`entities.line_of_sight(x1, y1, z1, x2, y2, z2)` | true if a straight line between the points passes no solid block |
| <a id="fn-entities-find_path"></a>`entities.find_path(name, x1, y1, z1, x2, y2, z2)` | the route an entity of kind `name` would take: `{ {x=,y=,z=}, ... }` (feet positions), and whether it reaches the end |
| <a id="fn-entities-damage_player"></a>`entities.damage_player(name, amount [, reason])` | hurts a player (their game's survival mod takes it, armour and all). `reason` is shown when they die: `"was slain by a zombie"` |
| <a id="fn-entities-drop_item"></a>`entities.drop_item(item, count, x, y, z)` | an item on the ground that players pick up by walking over it |
| <a id="fn-entities-get_attack_damage"></a>`entities.get_attack_damage(item, player)` | how hard a hit with `item` is: swords 4 + material (stone 5, iron 6, diamond 7), axes 3 + material, a hand 1. Replace the function to change it. |
| <a id="fn-entities-register_spawn"></a>`entities.register_spawn{ ... }` | spawning by itself (below) |

<a id="entity-paths"></a>**Finding the way.** `self:go_to` plans a route over the blocks (A*) for that entity's size, then walks it:
- It goes **around** trees, walls and holes instead of into them.
- It never tries a gap lower than its height: a 1.9-tall zombie won't jump at a 1-block opening.
- It climbs a 1-block step only where there's room to jump and to stand on top.
- It walks off ledges of up to 3 blocks.
- It swims across water only when there's no reasonable way round. Swimming costs a lot more than walking, so it goes around a pond rather than through it. In water it swims along the surface (it doesn't dive) and climbs out onto the bank.
- It never walks into lava or fire.
- The route is straightened where it can, so it doesn't zig-zag along the grid.

If the target can't be reached it goes as near as it can (`"partial"`). If something gets in the way it plans again, and after a couple of tries it gives up (`"stuck"`). Chasing is cheap: calling `go_to` every tick only plans again when the target moves more than a block, and at most about 3 times a second. A search looks at up to 1500 blocks, enough for routes of about 30–40 blocks; further targets get a partial route towards them.

```lua
on_step = function(self, dt)
    local name, px, py, pz = entities.nearest_player(self:get_pos())
    if name and self:can_see(px, py + 1.6, pz) then
        self:go_to(px, py, pz, 2.4)                 -- chase: round obstacles, through doorways
    elseif self:path_state() ~= "following" then
        self:wander(10, 1)                          -- wander somewhere reachable
    end
    self:set_animation(self:path_state() == "following" and "walk" or "idle")
end
```

<a id="entity-water"></a>**In water.** A mob floats with about half its body under the surface (legs down, back and head out) and swims at about half its walking speed. In water shallower than that it just stands on the bottom. Dropped items float on top. A mob that ends up in water (knocked in, or spawned there) and calls `wander` heads for the nearest land first, which is what the cows in the `mobs` mod do.

<a id="entity-spawning"></a>**Spawning by itself.** Every couple of seconds the engine tries a few spots 24–48 blocks from each player, in loaded chunks, and spawns there when all of a rule's conditions hold. There are never more than 40 spawned (`despawn = true`) mobs around one player.

```lua
entities.register_spawn({
    name = "mymod:goat",
    on = { "grass", "stone" },   -- standing on one of these (left out: anything solid)
    min_light = 9, max_light = 15,   -- engine.get_light_level there (monsters: max_light = 7)
    sky = true,                  -- only where the sky is overhead
    when = "day",                -- "day", "night" or "any"
    min_y = 40, max_y = 200,
    chance = 0.3,                -- of spawning when a spot qualifies
    group = { 2, 4 },            -- how many together (or one number)
    max_nearby = 6,              -- not if there are this many of its kind within 48 blocks
})
```

The game rule `doMobSpawning` (`/gamerule doMobSpawning false`) turns natural spawning off. On a dedicated server, which keeps no light data, spawning uses the same light as `engine.get_light_level` there. Under open sky that's sunlight for the time of day; under anything solid it's dark. `sky = true` means nothing solid is overhead. So monsters spawn in caves and under cover, and animals only in the open.

**Where they stop.** An entity more than 128 blocks from every player, or in a chunk that isn't loaded, waits where it is: no physics, no `on_step`. Ones with `despawn = true` are removed when no player is within 128 blocks, or sometimes after 30 seconds with nobody within 32, like Minecraft's monsters. Those are never saved. Everything else is saved with the world (`entities.txt` in its folder) and comes back with `on_spawn(self, saved)`. If a mod isn't loaded, its entities are kept in the file untouched until it is.

<a id="dropped-items"></a>**Dropped items.** Blocks dug in survival pop out as items, and so do mobs' `drops`. They bob and spin, join into stacks, and a player picks them up by walking over them. If your inventory is full they stay on the ground, and they disappear after 5 minutes.

Players drop things the way they do in Minecraft:
- **Q** (Settings > Controls, "Drop Item") throws one of what's in your hand the way you're looking. **Ctrl+Q** throws the whole stack. Holding Q keeps dropping.
- In an inventory screen, **clicking outside the window** while holding a stack drops it: left click the whole stack, right click one. This works in both the survival and the creative inventory.
- **When you die**, everything you carried scatters on the ground, unless `keepInventory` is on (Craft & Ruin's survival mod keeps it by default).
- Thrown items can't be picked up for 2 seconds, so you don't catch them straight back.
- You can't pick anything up while dead.

<a id="fn-engine-drop_item"></a>From a mod: `engine.drop_item(item, count [, x, y, z [, vx, vy, vz [, pickup_delay]]])`. Without a position, the item is thrown from the player's eyes like Q does. It works when joined to a server too, where the server makes the item. `entities.drop_item` (the authority only) makes one with a little pop instead.

<a id="fn-hooks-on_player_drop"></a><a id="fn-hooks-can_pick_up_item"></a>Two hooks:
- `on_player_drop(item, count)`: return true to stop a drop, for items that can't be dropped.
- `can_pick_up_item(item, count)`: return false and the player doesn't pick items up. The survival mod uses it while you're dead.

**Players and entities.** Left-click hits the entity you're looking at (within 4.5 blocks), right-click uses it. Nothing is dug or placed behind it. A hit runs `on_punch`; unless that returns true, the engine takes `damage` off its health, knocks it back, makes it flash red, and stops it being hit again for half a second. At 0 health `on_death` runs, its `drops` fall, and it's gone. On a server, hits are checked: the player has to be within reach, and no more than 10 a second.

**Models.** Build them facing north (-z), as Blockbench does for Bedrock entities. They look and move in the game exactly as in Blockbench. Animations are played by name, the full name or the short one (`"walk"` finds `animation.cow.walk`), and each entity runs its own from when you set it. Mobs are lit by the light where they stand: dark in caves and at night, lit by torches.

The example `mobs` mod (`mods/mobs`) is a complete example:
- **Cows** wander on grass, run when hit, and drop beef and leather.
- **Zombies** come out in the dark, chase the nearest player they can see (and where they last saw you), hit for 3, and burn in sunlight.
- **Spawn eggs** are in the creative inventory's Mobs tab.
- **`/summon cow|zombie [count]`** works for operators.

## 14. UI: menus and screens

How BlockSmith UI works, compared with Luanti's formspecs, is explained in [L11](#l11-screens-and-menus-how-blocksmith-ui-works). This section is the complete reference:
- 14.1 sizes and positions
- 14.2 everything you can draw
- 14.3 mouse and keyboard
- 14.4 where UI goes (HUD, in-game screens, menus)
- 14.5 the `ui_kit` library
- 14.6 `ui_kit` windows, drag and drop, and the kit in any screen
- 14.7 patterns and common mistakes

### 14.1 Sizes and positions

- Everything is in **pixels of the window**, measured from the **top-left** corner. `x` goes right, `y` goes down. Your draw functions get `W, H`, the window's size, every frame: always lay out from them, never assume 1920 × 1080.
- **Text `y` is the baseline**: the line the letters sit on. Letters go *up* from it (and `g`, `p`, `y` hang a little below it). To place text by its top, add the capital height: `ui.draw_text(t, x, top + ui.text_height(s), s, ...)`.
- **Text size** is a scale: 1.0 makes capitals about 14 pixels tall. Measure, don't guess: `ui.text_width(text, scale)` and `ui.text_height(scale)` give the exact pixels `ui.draw_text` will use.
- **Scale with the window.** The engine's menus use a whole-number GUI scale so pixel art stays crisp:
  ```lua
  local function gui_scale(W, H) return math.max(1, math.floor(math.min(W / 480, H / 360) * 2) / 2) end
  local S = gui_scale(W, H)          -- 1, 1.5, 2, 2.5 ... ; multiply your sizes by it
  ```
  `ui_kit` does this for you with units.
- **Drawing order is stacking order:** what you draw later is on top. Draw backgrounds first and tooltips last.

### 14.2 Drawing

| function | draws |
|---|---|
| <a id="fn-ui-draw_rect"></a>`ui.draw_rect(x, y, w, h, r, g, b [, a])` | a filled rectangle. Colours are 0..1; `a` is opacity (default 1). A border is a slightly bigger rectangle drawn first. |
| <a id="fn-ui-draw_text"></a>`ui.draw_text(text, x, y, scale, r, g, b [, a])` | text, UTF-8, with Latin-1 accents (é, ü, ñ, ©). `y` is the baseline. The older form `ui.draw_text(text, x, y, r, g, b)` (scale 1) still works. Characters the font doesn't have show as `?`. |
| <a id="fn-ui-text_height"></a><a id="fn-ui-text_width"></a>`ui.text_width(text [, scale])`, `ui.text_height([scale])` | exact width of `text`, and height of capital letters, in pixels |
| [`ui.draw_image(path, x, y, w, h [, u0, v0, u1, v1])`](#fn-ui-draw_image) | a PNG/JPEG picture, stretched to `w × h`. The file is found like block textures (your mod's folder, then `textures/`), or is a path from `remote.fetch`. `u0, v0, u1, v1` (0..1) pick part of it, e.g. one icon from a sheet: `ui.draw_image("icons.png", x, y, 32, 32, 0.25, 0, 0.5, 0.25)` is the second icon of a 4 × 4 sheet. Loaded once, then cached. |
| [`ui.draw_button(x, y, w, h [, state [, danger]])`](#fn-ui-draw_button) | a button using the game's button picture (the same one as the title screen). `state`: `"normal"`, `"hover"`, `"disabled"` or `"selected"` (a lit tab). `danger = true` tints it red, for Delete. Draw the label on top yourself. |
| [`ui.get_button_text_color([hovered])`](#fn-ui-get_button_text_color) | `r, g, b`: the label colour the game chose for its buttons |
| <a id="fn-ui-draw_block_icon"></a>`ui.draw_block_icon(name, x, y, size)` | a block or item as it looks in inventories: a 3D cube, a flat sprite, or the item's picture, chosen automatically |
| <a id="fn-ui-draw_server_icon"></a>`ui.draw_server_icon(address, x, y, size)` | a server's icon (after `engine.ping_server`); `false` if it has none |
| <a id="fn-engine-draw_text"></a>`engine.draw_text(text, x, y)` | old shortcut (white, scale 1); use `ui.draw_text` |

**Shapes.** Colours are 0..1 and `a` (opacity) is optional, as for `ui.draw_rect`. Thickness and radius are pixels.

| function | draws |
|---|---|
| <a id="fn-ui-draw_rect_outline"></a>`ui.draw_rect_outline(x, y, w, h, thickness, r, g, b [, a])` | a rectangle's border only (the border is inside `x, y, w, h`) |
| <a id="fn-ui-draw_rounded_rect"></a>`ui.draw_rounded_rect(x, y, w, h, radius, r, g, b [, a])` | a filled rectangle with round corners |
| <a id="fn-ui-draw_line"></a>`ui.draw_line(x1, y1, x2, y2, thickness, r, g, b [, a])` | a straight line, at any angle |
| <a id="fn-ui-draw_circle"></a>`ui.draw_circle(cx, cy, radius, r, g, b [, a])` | a filled circle around `cx, cy` |
| <a id="fn-ui-draw_ring"></a>`ui.draw_ring(cx, cy, radius, thickness, r, g, b [, a])` | a circle's outline |
| <a id="fn-ui-draw_pie"></a>`ui.draw_pie(cx, cy, radius, from, to, r, g, b [, a])` | a slice of a circle, from `from` to `to` degrees (0 = right, going clockwise). A cooldown or progress dial: `ui.draw_pie(cx, cy, 20, -90, -90 + 360 * done, ...)` |
| <a id="fn-ui-draw_triangle"></a>`ui.draw_triangle(x1, y1, x2, y2, x3, y3, r, g, b [, a])` | a filled triangle (arrows, play buttons) |
| <a id="fn-ui-draw_polygon"></a>`ui.draw_polygon({ x1, y1, x2, y2, ... }, r, g, b [, a])` | a filled convex shape through the points, in order |
| <a id="fn-ui-draw_gradient"></a>`ui.draw_gradient(x, y, w, h, top, bottom [, "horizontal"])` | a rectangle fading from one colour to another. `top` and `bottom` are `{ r, g, b [, a] }`; with `"horizontal"` they're left and right. |

Shapes are collected and sent to the graphics card together, so a screen can draw thousands of them (a pixel-art canvas, a chart) without slowing down. Order is still kept: text, pictures and icons drawn after a shape appear on top of it.

<a id="fn-ui-push_clip"></a><a id="fn-ui-pop_clip"></a>**Clipping: drawing only inside a rectangle.** `ui.push_clip(x, y, w, h)` hides everything drawn outside that rectangle until the matching `ui.pop_clip()`. Clips nest: inside another clip, only the overlap of the two shows. This is how a scrolling list hides the rows that have scrolled out of view, and how a window's contents stay inside the window. Always pair each `push_clip` with a `pop_clip`; anything left over is cleared when your draw function ends.
```lua
ui.push_clip(x, y, w, h)                       -- the list's box
for i, row in ipairs(rows) do
    ui.draw_text(row, x + 4, y + i * 20 - scroll, 1, 1, 1, 1)   -- rows above or below the box don't show
end
ui.pop_clip()
```

**More from `ui.draw_image`.** Instead of the four numbers, the sixth argument can be a table:
```lua
ui.draw_image("compass_needle.png", x, y, 64, 64, {
    rotation = angle,                  -- degrees, around the picture's centre
    tint = { 1, 0.5, 0.5 },            -- multiply its colours (reddish here); { r, g, b, a }
    alpha = 0.8,                       -- opacity
    flip_x = true,                     -- mirror left-right (flip_y: upside down)
    u0 = 0, v0 = 0, u1 = 0.5, v1 = 1,  -- part of the picture, as before
})
```
<a id="fn-ui-get_image_size"></a>`ui.get_image_size(path)` returns the picture's `width, height` in pixels (or `nil` if it can't be loaded), e.g. to keep its shape when you scale it.

**Text with a shadow** (how the engine's menus make text readable over pictures):
```lua
local function text(str, x, top, s, r, g, b)
    local y = top + ui.text_height(s)
    ui.draw_text(str, x + 2, y + 2, s, r * 0.25, g * 0.25, b * 0.25)   -- shadow
    ui.draw_text(str, x, y, s, r, g, b)
end
```

**Centred in a box:**
```lua
local function centred(str, bx, by, bw, bh, s, r, g, b)
    local x = bx + (bw - ui.text_width(str, s)) / 2
    local y = by + (bh + ui.text_height(s)) / 2          -- baseline: half the box, plus half the letters
    ui.draw_text(str, x, y, s, r, g, b)
end
```

**Blocks and items:**
- <a id="fn-ui-get_all_blocks"></a>`ui.get_all_blocks()` returns every block and item meant for the creative inventory, as a list of names.
- <a id="fn-ui-get_block_info"></a>`ui.get_block_info(name)` returns `{ id, name, pattern, has_texture, in_creative, inventory_image, item, tool_type, tool, light, walkable, transparent, mod }`, or `nil`. `mod` is the folder name of the mod that registered it (`""` for the engine's own). `description` is the name players see, and `full_name` is `"mod:name"`, for example `"core_blocks:basalt"`.
- <a id="fn-engine-get_block_description"></a><a id="fn-engine-set_block_name_display"></a>`engine.get_block_description(name)` returns the name players see (`"Oak Log"`, or `""` for no such block). `engine.get_block_def(name)` has `description` and `full_name` too. `engine.set_block_name_display(false)` hides the held item's name above the hotbar.

### 14.3 Mouse and keyboard

- <a id="fn-ui-get_mouse"></a>`ui.get_mouse()` returns `x, y, left_down, right_down`: where the mouse is, in the same pixels as drawing and clicks. Use it for hover effects (`if mx >= x and mx < x + w and ...`) and dragging.
- <a id="fn-ui-get_mouse_pos"></a>`ui.get_mouse_pos()` returns only `x, y`.
- <a id="fn-ui-get_time"></a>`ui.get_time()` gives seconds since the game started, for animations and a blinking text cursor: `math.floor(ui.get_time() * 2) % 2 == 0`.
- <a id="fn-ui-keys"></a>**`ui.keys`** holds the key codes of every key on a keyboard:
  - `A`–`Z` (`ui.keys.M`), and the digits as `ui.keys["0"]`–`ui.keys["9"]`;
  - `F1`–`F25`;
  - `ESCAPE`, `ENTER`, `KP_ENTER`, `BACKSPACE`, `DELETE`, `INSERT`, `TAB`, `SPACE`;
  - `UP`, `DOWN`, `LEFT`, `RIGHT`, `HOME`, `END`, `PAGE_UP`, `PAGE_DOWN`;
  - punctuation: `MINUS`, `EQUAL`, `LEFT_BRACKET`, `RIGHT_BRACKET`, `BACKSLASH`, `SEMICOLON`, `APOSTROPHE`, `COMMA`, `PERIOD`, `SLASH`, `GRAVE_ACCENT` (the key left of 1);
  - modifiers: `LEFT_SHIFT`, `RIGHT_SHIFT`, `LEFT_CONTROL`, `RIGHT_CONTROL`, `LEFT_ALT`, `RIGHT_ALT`, `LEFT_SUPER`, `RIGHT_SUPER` (the Windows / Command key);
  - the number pad: `KP_0`–`KP_9`, `KP_DECIMAL`, `KP_DIVIDE`, `KP_MULTIPLY`, `KP_SUBTRACT`, `KP_ADD`, `KP_EQUAL`;
  - `CAPS_LOCK`, `NUM_LOCK`, `SCROLL_LOCK`, `PRINT_SCREEN`, `PAUSE`, `MENU`.
- **`ui.actions`**: `PRESS`, `RELEASE`, `REPEAT` (sent while a key is held: use it for Backspace and arrows).
- <a id="fn-ui-mods"></a>**`mods`** (in key hooks) is a sum of bits, named in `ui.mods`: `SHIFT` = 1, `CONTROL` = 2, `ALT` = 4, `SUPER` = 8. For example, `mods % 2 == 1` is "Shift held".
- **Typed text** arrives separately from keys, as Unicode characters: for in-game screens in `on_char_input(codepoint)`, and for menus in `handle_<menu>_char(codepoint, char)`. Use `utf8.char(codepoint)` to turn the code into text. **Keys** are for Enter, Backspace, arrows and shortcuts; **characters** are for what the player types (so other keyboard layouts work).

#### Everything that happened this frame

The hooks above are tied to one screen each. These functions work **anywhere**: in any `render_*` function, in `on_update`, on the HUD, in a menu. Each returns what happened since the last frame, so read them once per frame (every call in the same frame returns the same thing).

| function | returns |
|---|---|
| <a id="fn-ui-get_mouse_events"></a>`ui.get_mouse_events()` | a list of `{ button, pressed, released, x, y, clicks, shift, ctrl, alt }`, oldest first. `button` is `"left"`, `"right"` or `"middle"`. `pressed`/`released` say which it was. `clicks` is 2 for a double-click and 3 for a triple-click (presses less than half a second apart, in the same place). `x, y` are where it happened, in drawing pixels. |
| <a id="fn-ui-get_scroll"></a>`ui.get_scroll()` | `dx, dy`: how far the wheel turned (`dy > 0` is up; `dx` is sideways scrolling on a touchpad) |
| <a id="fn-ui-get_typed_text"></a>`ui.get_typed_text()` | everything typed, as a UTF-8 string (`""` if nothing) |
| <a id="fn-ui-get_key_events"></a>`ui.get_key_events()` | a list of `{ key, action, shift, ctrl, alt }`. `action` is `"press"`, `"repeat"` or `"release"`; compare `key` with `ui.keys`. |
| <a id="fn-ui-is_key_down"></a>`ui.is_key_down(key)` | is that key held right now? (`ui.is_key_down(ui.keys.LEFT_SHIFT)`) |
| <a id="fn-ui-is_mouse_down"></a>`ui.is_mouse_down([button])` | is that button held right now? `"left"` (default), `"right"` or `"middle"` |

- Nothing is recorded while chat is open, so a key you watch for (B for a backpack, say) doesn't fire while the player types it in a message.
- `ctrl` is also true for the Command key on a Mac, so Ctrl+C shortcuts work there too.
- Press and release are separate events. That's what drag and drop needs: something is picked up on the press and put down on the release.

```lua
-- Open my map with M, from anywhere in the game
local previous_update = on_update
function on_update(dt, ...)
    for _, k in ipairs(ui.get_key_events()) do
        if k.key == ui.keys.M and k.action == "press" and not engine.is_inventory_open() then open_map() end
    end
    if previous_update then return previous_update(dt, ...) end
end
```

#### The mouse cursor

<a id="fn-ui-set_cursor"></a>`ui.set_cursor(name)` shows a different mouse pointer for this frame: `"hand"` (something clickable), `"text"` (a text box), `"crosshair"`, `"move"`, `"resize_h"` (left-right), `"resize_v"` (up-down), `"resize_nwse"` and `"resize_nesw"` (corners), `"not_allowed"`, or `"arrow"` (the normal one). Call it every frame you want it, usually while the mouse is over something; when nobody asks, the normal arrow comes back by itself. (`ui_kit` sets it for you: a hand over buttons, a text bar over fields, arrows on window edges.)

#### The clipboard

<a id="fn-ui-get_clipboard"></a><a id="fn-ui-set_clipboard"></a>`ui.get_clipboard()` returns the text the player copied, and `ui.set_clipboard(text)` puts text there. To protect players, a mod can only do this **right after the player presses Ctrl+V** (read) or **Ctrl+C / Ctrl+X** (write): a mod can't quietly read passwords the player copied elsewhere, or swap what they're about to paste. At any other time `get_clipboard` returns `nil, reason` and `set_clipboard` returns `false, reason`. Text is limited to 64 KB. So call them when you see the shortcut:
```lua
for _, k in ipairs(ui.get_key_events()) do
    if k.ctrl and k.action == "press" and k.key == ui.keys.V then text = text .. (ui.get_clipboard() or "") end
    if k.ctrl and k.action == "press" and k.key == ui.keys.C then ui.set_clipboard(text) end
end
```
(`ui_kit` text fields already do this.)

### 14.4 Where UI goes

#### The HUD

[`render_hud(W, H)`](#fn-hooks-render_hud) is drawn every frame while playing, under any open screen. It's only drawn, never clicked. Chain it: the survival mod draws hearts and hunger there. See [C11](#c11-a-hud-piece).

#### In-game screens

The engine has **one** in-game screen slot for mods. The survival inventory uses it, and so does every mod's chest, shop or board, so they take turns: each mod chains the hooks below and only acts while its own screen is open.

| hook / function | when | what to do |
|---|---|---|
| [`engine.set_inventory_open(true\|false)`](#fn-engine-set_inventory_open), [`engine.is_inventory_open()`](#fn-engine-is_inventory_open) | you call it | Opens or closes the slot. While open, the mouse is free (no looking around or walking), and the hooks below go to mods. |
| [`render_inventory(W, H)`](#fn-hooks-render_inventory) | every frame while playing | Draw your screen if it's open; otherwise call the previous one. |
| [`handle_inventory_click(mx, my, W, H, button, shift)`](#fn-hooks-handle_inventory_click) | a click while the slot is open | `button` is `"left"` or `"right"`; `shift` is true with Shift held |
| <a id="fn-hooks-handle_inventory_key"></a>`handle_inventory_key(key, action, mods)` → `true` to keep it | every key while the slot is open, **before** E and Escape | Return `true` to stop the engine using it, e.g. while typing in a text box, so E types an "e" instead of closing the screen |
| [`on_char_input(codepoint)`](#fn-hooks-on_char_input) | a typed character | for your text boxes |
| <a id="fn-hooks-handle_inventory_scroll"></a>`handle_inventory_scroll(dy)` | the mouse wheel while the slot is open (instead of changing the hotbar slot) | `dy > 0` is up |
| [`toggle_main_inventory()`](#fn-hooks-toggle_main_inventory) | E (the inventory key) or Escape | If your screen is open, close it and return `false`. Otherwise pass it on: in survival, the survival inventory opens and returns `true`. |

How to open your screen:
- From a block: `on_right_click_block`, as in [C12](#c12-your-own-screen).
- From an item: `engine.register_item_use`.
- From a chat command, or from a key you watch for in `on_update`.

The creative inventory is drawn by `render_creative_inventory` in the game's inventory mod. Mods can add their own tabs to it without replacing it: see [Creative inventory: tabs for your mod](#creative-inventory-tabs-for-your-mod).

#### Menus (replacing the engine's screens)

The title screen's pages and the pause menu are drawn by Lua files in the base mod. Define the same function in your mod (loaded later), and yours is used instead:

| screen | draw | input | opened |
|---|---|---|---|
| <a id="fn-hooks-render_pause_menu"></a><a id="fn-hooks-handle_pause_click"></a>Pause menu (Escape) | `render_pause_menu(W, H)` | `handle_pause_click(mx, my, W, H)` | |
| <a id="fn-hooks-render_settings"></a>Settings | `render_settings(W, H)` | `handle_settings_click(mx, my, W, H)`, `_key(key, action, mods)`, `_char(codepoint, char)`, `_scroll(dy)`, `_mouse(code)` | `on_settings_opened()` |
| <a id="fn-hooks-render_world_select"></a>Singleplayer | `render_world_select(W, H)` | `handle_world_select_click/_key/_char/_scroll` | `on_world_select_opened()` |
| <a id="fn-hooks-render_multiplayer"></a>Multiplayer | `render_multiplayer(W, H)` | `handle_multiplayer_click/_key/_char/_scroll` | `on_multiplayer_opened()` |
| <a id="fn-hooks-render_mods"></a>Mods | `render_mods(W, H)` | `handle_mods_click/_key/_scroll` | `on_mods_opened()` |
| <a id="fn-hooks-render_texture_packs"></a>Texture packs | `render_texture_packs(W, H)` | `handle_texture_packs_click/_key/_scroll` | `on_texture_packs_opened()` |
| <a id="fn-hooks-render_player_list"></a>Player list (hold Tab) | `render_player_list(W, H)` | | |
| <a id="fn-hooks-render_creative_inventory"></a>Creative inventory | `render_creative_inventory(W, H)` | `handle_creative_click/_key/_char/_scroll` | `on_creative_opened()`, `on_creative_closed()` |

- `_key` handlers return `true` when they used the key; Escape then doesn't go back.
- Menus that change the engine's state use `engine.*` functions: `engine.play_world`, `engine.join_server`, `engine.resume`, `engine.quit_to_menu` and so on.
- The base mod's files (`pause_menu.lua`, `settings_menu.lua`, `world_select.lua`, `multiplayer.lua`, `mods_menu.lua`) are complete, working examples. Copy one to start.

### 14.5 `ui_kit`: forms the easy way

<a id="fn-uikit-form"></a>`local kit = require("ui_kit")` loads the library from `lualib/` (see [Lua modules](#lua-modules-require)). Every mod shares one copy, so forms from different mods never fight over the screen slot.

**Making a form:** `kit.form{ ... }`

| field | |
|---|---|
| `id` | a unique name, e.g. `"mymod:shop"` |
| `width`, `height` | the content area, in units (the title bar and padding are added around it) |
| `title` | optional, shown at the top |
| `draw = function(f, state)` | **required.** Runs every frame while open; draws the widgets. |
| `on_open(state)`, `on_close(state)` | optional. Close happens with E, Escape, `f:close()`, `form:close()`, or leaving the world. |
| `dim` | darken the world behind (default `true`) |
| `padding` | space around the content, in units (default 0.5) |
| `close_on_click_outside` | `true`: a click outside the panel closes it |
| `fullscreen` | `true`: no panel or title; the form is the whole window, for desktops and big maps. Units are then about 1/40 of the window's width (set `unit` for a fixed number of pixels per unit), and `f.w_units`, `f.h_units` tell you how many fit. |

<a id="fn-uikit-open"></a>`form:open(state)` opens it (any other kit form closes first). `state` is your table: draw gets it every frame, so keep typed text and choices in it. `form:close()` closes it, and `form:is_open()` tells you if it's open. `kit.current()` is the open form, if any.

**Widgets** (`f` is the frame; `x, y, w, h` are units from the content's top-left):

| widget | returns |
|---|---|
| <a id="fn-uikit-label"></a>`f:label(x, y, text [, { size, color, w, align }])` | its width. `align` = `"center"` or `"right"` within `w`. `size` is the capital height in units (default 0.42). |
| <a id="fn-uikit-text"></a>`f:text(x, y, w, text [, opts])` | the height used: a paragraph wrapped to `w` (`\n` starts a new line) |
| <a id="fn-uikit-button"></a>`f:button(id, x, y, w, h, label [, { disabled, danger, selected, tooltip }])` | `true` on the frame it's clicked. The label always stays inside the button: if it's too long it's drawn smaller (down to 70%), then cut short with "...", and the full text goes in the tooltip |
| <a id="fn-uikit-button_width"></a>`f:button_width(label [, h])` | how wide a button of height `h` needs to be for `label` (units), to make buttons that fit their text |
| <a id="fn-uikit-text_width"></a>`f:text_width(text [, size])` / `f:fit_text(text, size, max_w)` | text width in units; text and size that fit `max_w` (smaller, then cut with "...") |
| <a id="fn-uikit-field"></a>`f:field(id, x, y, w, h, text [, { placeholder, max_length, password, numeric }])` | `text, submitted`: the current text, and `true` when Enter is pressed in it. Click it to type; Tab moves to the next field; Escape leaves it. |
| <a id="fn-uikit-checkbox"></a>`f:checkbox(id, x, y, label, value)` | the new value |
| <a id="fn-uikit-slider"></a>`f:slider(id, x, y, w, value [, { min, max, step }])` | the new value (click or drag) |
| <a id="fn-uikit-list"></a>`f:list(id, x, y, w, h, items, selected [, { row_height }])` | `selected, clicked, opened`. `items` are strings or `{ text = ..., color = {r,g,b} }`; the wheel scrolls it; `opened` is `true` on a double-click. |
| <a id="fn-uikit-item"></a>`f:item(x, y, size, name [, { count, tooltip, selected, drag, drop, accept, id }])` | `click, dropped`. `click` is `{ button = "left"\|"right", shift, clicks }` on the frame it's clicked, else `nil`. An inventory slot with an item. With `drag` and `drop` the slot can be dragged and dropped on: see [drag and drop](#drag-and-drop). |
| <a id="fn-uikit-image"></a>`f:image(x, y, w, h, path [, u0, v0, u1, v1 or { options }])` | (a picture; the options are those of [`ui.draw_image`](#142-drawing)) |
| `f:rect(x, y, w, h, {r,g,b,a})`, `f:box(x, y, w, h, fill [, border])` | (plain shapes) |
| `f:tooltip(x, y, w, h, text)` | shows `text` by the mouse while it's over that area |
| `f:hovered(x, y, w, h)` | is the mouse over it? |
| <a id="fn-uikit-take_click"></a>`f:take_click(x, y, w, h [, button])` | this frame's click, if it was in that area, nothing on top of it (a window, a menu) and no widget used it yet: `{ x, y, button, shift, ctrl, clicks }`. `button` is `"left"` (default), `"right"`, `"middle"` or `"any"`. For your own widgets. |
| <a id="fn-uikit-take_scroll"></a>`f:take_scroll(x, y, w, h)` | the wheel (`dy`, up is positive) if the mouse is over that area, else 0 |
| `f:px(x, y)`, `f.u` | units to pixels, and pixels per unit, for drawing with `ui.*` yourself |
| `f:close()` | closes the form |

`id`s must be unique within a form: they're how a text field keeps its focus, and how a list keeps its scroll position. (Inside a window or scroll area, ids only need to be unique there.)

Text fields also do **Ctrl+C** (copy all), **Ctrl+X** (cut all) and **Ctrl+V** (paste); `password` fields don't copy.

<a id="fn-uikit-theme"></a>**Restyling:** change `kit.theme` (colours: `panel`, `border`, `title`, `text`, `muted`, `field`, `field_focus`, `focus_border`, `accent`, `row_hover`, `row_selected`, `slot`...) and `kit.text_size`. It changes every kit form, so a game can give all its mods one look.

**A complete example: a shop.**
```lua
local kit = require("ui_kit")
local goods = { { item = "torch", price = 1 }, { item = "planks", price = 2 }, { item = "glass", price = 4 } }

local shop = kit.form{
    id = "shop:main", width = 12, height = 7, title = "General store",
    draw = function(f, state)
        f:label(0, 0, "Coins: " .. state.coins, { color = { 1, 0.85, 0.3 } })
        local names = {}
        for i, g in ipairs(goods) do names[i] = g.item .. "  (" .. g.price .. " coins)" end
        state.pick = f:list("goods", 0, 1, 7, 4.5, names, state.pick)
        state.amount = math.floor(f:slider("amount", 0, 6, 5, state.amount or 1, { min = 1, max = 16, step = 1 }))
        f:label(5.4, 6.1, "x" .. state.amount)

        local g = goods[state.pick or 0]
        if g then
            f:item(8.5, 1, 2.5, g.item, { count = state.amount })
            local cost = g.price * state.amount
            if f:button("buy", 8, 4.5, 4, 1, "Buy (" .. cost .. ")", { disabled = cost > state.coins }) then
                state.coins = state.coins - cost
                engine.give_item(g.item, state.amount)
            end
        end
        if f:button("close", 8, 6, 4, 1, "Close") then f:close() end
    end,
}
-- open it: shop:open({ coins = 20 })
```

### 14.6 `ui_kit` windows, drag and drop, and the kit in any screen

These turn a form into a small desktop: windows you can move, resize, stack, minimize and close; things you can drag from one place to another; areas that scroll; icons; menus. They're built from the same input as everything else, so they work together: a slot inside a window can be dragged to a slot outside it, a click on a window never reaches the button underneath, a menu sits on top of everything.

#### Windows

<a id="fn-uikit-window"></a>`f:window(id, opts, draw)` → `still_open`

```lua
if s.notes_open then
    s.notes_open = f:window("notes", { title = "Notes", x = 4, y = 2, w = 10, h = 7 }, function(wf, win)
        -- wf is a frame inside the window: (0, 0) is the window's top-left, under the title bar.
        -- win.w, win.h: the window's current size (it can be resized); win.focused
        s.text = wf:field("text", 0.3, 0.3, win.w - 0.6, 1, s.text)
        if wf:button("save", 0.3, win.h - 1.3, 3, 1, "Save") then save(s.text) end
    end)
end
```

- Call it every frame for as long as the window should exist, like any widget. It returns `false` on the frame its close button is clicked: stop calling it then (above, `s.notes_open` becomes `false`). A window that isn't called for a frame is gone; call it again and it opens fresh.
- `x, y, w, h` (units) are where it *starts*; after that, the player moves and resizes it, and the kit remembers. `w, h` are the content's size, without the title bar.
- The player can:
  - **move** it by its title bar;
  - **resize** it from the right edge, bottom edge or bottom-right corner;
  - **bring it to the front** by clicking anywhere in it (the front window has a coloured title bar);
  - **maximize** it with the □ button or a double-click on the title, **minimize** it with the _ button, **close** it with ×.
- Anything a widget inside does (a click, typing, the wheel) only happens when that window is the one on top at the mouse.

| option | default | |
|---|---|---|
| `title` | none | the text in the title bar |
| `x`, `y`, `w`, `h` | 1, 1, 8, 6 | where it starts, in units of the form |
| `min_w`, `min_h` | 4, 2 | the smallest size it can be resized to |
| `resizable`, `movable` | `true` | |
| `closable`, `minimizable`, `maximizable` | `true` | which title bar buttons it has |
| `frame` | `true` | `false`: no title bar or border, can't be moved or resized by the player, and doesn't take the focus. For taskbars, docks and panels. |
| `topmost` | `false` | `true`: always above normal windows (taskbars, notifications) |
| `background` | `kit.theme.window` | `{ r, g, b, a }` |

Managing windows from your code (for a taskbar, a "Window" menu, keyboard shortcuts):

| function | |
|---|---|
| <a id="fn-uikit-window_list"></a>`f:window_list()` | the open windows (not frameless ones), each `{ id, title, minimized, focused }`, in a steady order: one button per window in a taskbar |
| <a id="fn-uikit-focus_window"></a>`f:focus_window(id)` | brings it to the front (and back from minimized) |
| `f:minimize_window(id)` | minimizes it |
| <a id="fn-uikit-window_state"></a>`f:window_state(id)` | `{ x, y, w, h, maximized, minimized, focused }`, or `nil` if it isn't open: to save a layout, or to notice what the player did with a window |
| <a id="fn-uikit-maximize_window"></a>`f:maximize_window(id [, false])` | maximizes it (`false`: back to its normal size); works on the frame you open it, too |
| `f:move_window(id, x, y [, w, h])` | moves (and resizes) it |
| <a id="fn-uikit-set_work_area"></a>`f:set_work_area(x, y, w, h)` | where a maximized window goes, e.g. the screen minus the taskbar |

Windows don't go inside windows; inside a window, use a [scroll area](#scroll-areas-icons-and-menus) for a part that scrolls.

#### Drag and drop

Two pieces: something you can pick up, and somewhere you can put it down.

<a id="fn-uikit-draggable"></a>`f:draggable(id, x, y, w, h, payload [, { ghost, button }])` makes an area draggable. `payload` is anything you want to hand over (usually a small table saying *what* is dragged and *where from*). It returns `true` while this thing is being dragged, so you can draw it faded where it was. The drag starts once the mouse has moved a few pixels with the button held, so a plain click is still a click. While dragging, a faded box follows the mouse; `ghost = function(px, py, u) ... end` draws your own instead (pixels; `u` is the unit size).

<a id="fn-uikit-drop_target"></a>`f:drop_target(id, x, y, w, h [, accept])` returns the payload on the frame something is dropped on it, else `nil`. While something is dragged over it, it lights up. `accept(payload)` returning `false` refuses it (a slot that only takes armour, a trash can that takes anything).

<a id="fn-uikit-dragging"></a>`f:dragging()` is the payload being dragged right now, or `nil`: to highlight every place it could go, or to show a trash can only during a drag.

**Inventory slots do both.** `f:item` takes `drag = payload` (the slot can be picked up when it has an item) and `drop = true` (things can be dropped on it), and returns `click, dropped`. A rearrangeable grid is then:
```lua
for i = 1, 27 do
    local slot = { i = i }                                  -- "the thing dragged is slot i"
    local click, dropped = f:item((i - 1) % 9 * 1.1, (i - 1) // 9 * 1.1, 1, items[i] and items[i].name,
                                  { count = items[i] and items[i].count, drag = slot, drop = true, id = "slot" .. i })
    if dropped then items[i], items[dropped.i] = items[dropped.i], items[i] end   -- swap the two slots
end
```
Dropping on the slot it came from gives back its own payload: swapping a slot with itself changes nothing, so the code above needs no special case. Give each slot an `id` when slots can move around on screen. [C20](#c20-a-rearrangeable-inventory-drag-and-drop) is a complete backpack with stacking and Shift+click.

Drag and drop isn't only for items: payloads can be anything. Reorder a list (drop row 5 on row 2), drag files onto a folder icon, drag a card onto a pile, a quest onto a player's name.

#### Scroll areas, icons and menus

<a id="fn-uikit-scroll_area"></a>`f:scroll_area(id, x, y, w, h, content_h, function(sf, offset) ... end)` shows a `w × h` window onto something `content_h` units tall. Draw the whole content with `sf`, from `(0, 0)` at its top; the kit scrolls it (wheel, or dragging the bar on the right), hides what's outside, and only lets clicks reach what's visible. Any widget works inside: buttons, fields, item slots, even drag and drop. Lists and scroll areas inside it get the wheel first when the mouse is over them; the area itself scrolls only when it has more than fits. To make a window's contents scroll when the player makes the window smaller, put them in a scroll area as tall as they need (the `ui_showcase` mod does this for every app).
```lua
f:scroll_area("recipes", 0, 1, 8, 5, #recipes * 1.2, function(sf)
    for i, rec in ipairs(recipes) do
        if sf:button("r" .. i, 0, (i - 1) * 1.2, 7, 1, rec.name) then show(rec) end
    end
end)
```

<a id="fn-uikit-icon"></a>`f:icon(id, x, y, size, picture, label [, { selected, item, draw, tooltip, movable }])` is a desktop-style icon: a picture with a label under it (`draw = function(px, py, size_px)` draws your own picture with shapes instead; with `item = true`, `picture` is a block or item name, drawn as in inventories). It returns what the player did, and second, `{ ctrl, shift }`:
- `"select"`: pressed on it;
- `"open"`: a double-click;
- `"menu"`: a right-click.

With `movable = true` it can also be dragged; its position is yours, so keep `x, y` in your state:
- `"drag", dx, dy, mods`: move it (and the other selected icons) by `dx, dy` units this frame;
- `"drop"`: let go after a drag (snap to a grid, save positions);
- `"click"`: let go *without* dragging (usually: now select only this one).

#### Selecting several things, and moving them together

<a id="fn-uikit-marquee"></a>`f:marquee(id, x, y, w, h [, { windows = true }])` is the box you drag on an empty desktop to select things. Call it **after** the things in that area (icons, buttons…), so a press on one of them goes to it rather than starting a box. While the player drags from empty space, a highlighted box follows the mouse, drawn over everything. It returns `box, done, mods` every frame of the drag:
- `box` is `{ x, y, w, h }` in units, or `nil` when nothing is being selected;
- `done` is `true` on the frame the mouse is let go;
- a press that isn't dragged gives a box of size 0 with `done = true`: "clicked on nothing", usually *clear the selection*.

<a id="fn-uikit-overlaps"></a>`kit.overlaps(box, x, y, w, h)` tells you whether something is inside the box. With `windows = true`, the windows the box touches are selected too.

<a id="fn-uikit-selection"></a>`kit.selection()` is a set to keep your selection in: `sel:has(id)`, `sel:set(id, on)`, `sel:toggle(id)`, `sel:only(id)`, `sel:clear()`, `sel:count()`, `sel:list()`.

<a id="fn-uikit-icon_grid"></a>**Icons that never overlap.** Like a real desktop, `kit.icon_grid` gives every icon a cell of its own. Icons are dragged freely, and when they're let go each one snaps to the nearest *free* cell. Drop an icon on another and it goes to the nearest empty cell instead.
```lua
local grid = state.grid or kit.icon_grid{ cell_w = 3, cell_h = 3 }; state.grid = grid
grid:set_area(0, 0, f.w_units, f.h_units - 2)          -- every frame: where icons may go
for _, id in ipairs(my_apps) do
    if not state.icons[id] then grid:add(state.icons, id) end   -- new: first free cell, down then across
end
grid:keep_in(state.icons, dragging and sel:list())    -- the screen got smaller: move them back in
if dragging then                                       -- outlines where the dragged ones will land
    for id, p in pairs(grid:preview(state.icons, sel:list())) do
        f:rect(p.x - 0.2, p.y - 0.1, 2, 2.5, { 1, 1, 1, 0.08 })
    end
end
-- ...f:icon as below; on "drag" set dragging = true, and on "drop":
grid:arrange(state.icons, sel:list())                  -- each to the nearest free cell
```
The showcase's desktop (`mods/ui_showcase`) works this way.

<a id="fn-uikit-selected_windows"></a>**Windows** can be selected too: with the box above, or by **Ctrl+click on a title bar** (again to unselect). Selected windows get a bright edge. Dragging the title bar of any selected window **moves them all together**; dragging an unselected window moves it alone and lets the selection go. From code: `f:selected_windows()`, `f:select_window(id [, false])`, `f:clear_window_selection()`.

A desktop where icons can be box-selected, Ctrl+clicked and dragged as a group:
```lua
local sel = state.sel or kit.selection(); state.sel = sel
for id, p in pairs(state.icons) do                        -- state.icons[id] = { x =, y = }
    local what, a, b = f:icon(id, p.x, p.y, 1.6, p.picture, p.label, { selected = sel:has(id), movable = true })
    if what == "select" then
        if a.ctrl then sel:toggle(id) elseif not sel:has(id) then sel:only(id) end
    elseif what == "click" and not a.ctrl then sel:only(id)
    elseif what == "drag" then
        if not sel:has(id) then sel:only(id) end
        for _, other in ipairs(sel:list()) do state.icons[other].x = state.icons[other].x + a; state.icons[other].y = state.icons[other].y + b end
    end
end
local box, done, mods = f:marquee("select", 0, 0, f.w_units, f.h_units, { windows = true })
if box then
    if box.w == 0 then if done and not mods.ctrl then sel:clear() end
    else
        if not mods.ctrl then sel:clear() end
        for id, p in pairs(state.icons) do if kit.overlaps(box, p.x, p.y, 1.6, 2.5) then sel:set(id, true) end end
    end
end
```
The `ui_showcase` mod does exactly this, with Ctrl+A to select every icon.

<a id="fn-uikit-popup"></a>**Menus:** `f:open_popup(id, x, y)` opens a menu at `x, y`; then, every frame, `f:popup(id, entries [, { width }])` draws it while it's open and returns the number of the chosen entry (once, and then it closes). `"-"` in `entries` is a separator line. A click anywhere else closes it. Menus are drawn above everything, windows included.
```lua
local what = f:icon("file1", 2, 2, 1.5, "doc.png", "letter.txt")
if what == "menu" then f:open_popup("file_menu", 3.5, 2.5) end            -- right-click: a context menu
local choice = f:popup("file_menu", { "Open", "Rename", "-", "Delete" })
if choice == 4 then delete_file("letter.txt") end
```

#### The kit in any screen: `kit.embed`

Forms use the in-game screen slot. <a id="fn-uikit-embed"></a>`kit.embed{ id, draw [, width, height, unit, state, input] }` puts the same widgets anywhere you draw: the HUD, the creative inventory, the pause menu, the title screen's menus.
```lua
local panel = kit.embed{ id = "mymod:quick_bar", width = 10, height = 1.2, draw = function(f, state)
    for i = 1, 8 do
        local click, dropped = f:item((i - 1) * 1.25, 0, 1.2, favourites[i], { drag = { i = i }, drop = true, id = "fav" .. i })
        if dropped then favourites[i], favourites[dropped.i] = favourites[dropped.i], favourites[i] end
        if click then hotbar.set_slot(hotbar.get_active_slot(), favourites[i]) end
    end
end }

local previous = render_creative_inventory
function render_creative_inventory(W, H, ...)
    if previous then previous(W, H, ...) end
    panel:draw(W / 2 - 300, H - 90, 600, 72)     -- pixels: where it goes in this screen
end
```
- `embed:draw(x, y, w, h)` draws it in that pixel rectangle. With `width` and `height` (units), the unit size is fitted to the rectangle; without them, `unit` is the pixels per unit (default 32).
- It reads the mouse and keyboard itself (the new [frame input](#everything-that-happened-this-frame)), so it needs no click hooks. The screen's own `handle_*_click` still gets the click too, so don't put your panel over its buttons.
- While a kit form is open, embeds ignore input (the form is on top).
- `input = false` makes it draw-only: use that on the HUD while the mouse is captured for looking around.
- Windows, drag and drop, scroll areas and menus all work inside embeds.
- `embed:is_typing()` says whether one of its text fields has focus; the screen around it should then leave keys alone (return `true` from its key handler). `embed:stop_typing()` leaves the field.

#### On older engines

The shapes, clipping, cursors and frame input in §14.2 and §14.3 need an engine built with this version's `Inventory.cpp`, `main.cpp` and `UiDraw.h`. On an older build, `ui_kit` fills in what's missing so mods still run: shapes are drawn from rectangles (blockier), there's no clipping, and kit pages in other screens get their clicks from `ui.get_mouse`. The console then says *"This engine is older than ui_kit"*. `kit.full_engine` is `true` when the engine has everything. If you see `attempt to call a nil value (field 'draw_gradient')` or similar from a mod that doesn't use `ui_kit`, rebuild the engine.

#### See it all working

The **`ui_showcase`** mod (`mods/ui_showcase/init.lua`) uses every part of the kit, and is written to be read and copied from. To open it in game:
- type `/uikit`, or press **F7** (anywhere, even with the creative inventory open);
- hold the **UI Kit Remote** item and right-click;
- right-click a **Showcase Laptop** block;
- or use the buttons on the **UI Kit** tab of the creative inventory, which also hands out the remote and the laptop.

It opens a desktop with a **Tour** window: a checklist of everything to try with the windows (move, resize, maximize, minimize and restore from the taskbar, bring to front, close), the desktop icons and menus, drag and drop, scrolling and typing. Each item ticks itself off as you do it, and **Open every app** lays all the apps out side by side. The apps:
- **Widgets**: buttons, checkboxes, sliders, text and password fields, lists, tooltips, text that wraps as you resize the window;
- **Inventory**: drag and drop between a chest and your real hotbar, with stacking, splitting, Shift+click and a trash can;
- **Shapes**: the engine's drawing, animated: circles, pies, lines, polygons, gradients, rotating and tinted pictures, clipping;
- **Scrolling**: a scroll area of widgets;
- **Notes**: copy and paste.

F7 or Escape closes it. It also adds two tabs to the creative inventory ([how](#creative-inventory-tabs-for-your-mod)).

### 14.7 Patterns and mistakes

- **One layout function for drawing and clicks.** Compute every rectangle in one function of `(W, H)`, and call it from `render_*` and `handle_*_click`. Duplicated numbers drift apart, and buttons stop working where they're drawn. (`ui_kit` avoids this: each widget draws and checks clicks in the same call.)
- **Text too high or too low?** `ui.draw_text`'s `y` is the baseline. Add `ui.text_height(scale)` to a top position.
- **Hover:** `local mx, my = ui.get_mouse()`, then `mx >= x and mx < x + w and my >= y and my < y + h`. Draw the hover look in the same frame.
- **Scrolling a list by hand:** keep `first` (the top row). `handle_inventory_scroll(dy)` does `first = first - dy`, clamped to `0 .. #items - visible`; draw rows `first + 1` to `first + visible`. A click on row `r` (counting from the top) is item `first + r`.
- <a id="fn-textedit"></a>**Typing by hand:** use `lualib/textedit.lua` for the caret, as the game's own menus do. `local te = require("textedit")`, give each box an id, and:
  - `te.focus(id, text, click_x, scale)`: on a click (the caret goes where you click; `click_x` is pixels from where the text starts), or `te.focus(id, text)` for the end.
  - `text = te.insert(id, text, utf8.char(cp), max_chars)` in `on_char_input`.
  - `text = te.key(id, text, key, action, mods)` in your key hook. It handles Left/Right, Home/End, Backspace/Delete, and with Ctrl whole words. Return `true` from the hook while the box has focus, so E and other letters don't close the screen.
  - `shown, caret_x = te.view(id, text, box_width, scale [, "*"])` when drawing: the part of the text that fits, scrolled to keep the caret in view (`"*"` for passwords). Draw `shown`, then `te.draw_caret(id, text_x + caret_x, top, height, r, g, b)`.
  - `te.reset(id)` when the box is opened for new text.
  `ui_kit` fields do all of this already.
- **Tabs:** keep `state.tab`, draw tab buttons with `ui.draw_button(..., state.tab == "x" and "selected" or "normal")`, and draw only the chosen tab's content.
- **Don't do slow work while drawing.** Draw functions run 60 times a second. Build lists (sorting, filtering, reading storage) when something changes, not every frame.
- **Close your screen when it no longer applies:** for example, when the player walks away from the block (check the distance in `on_update`), or dies.
- **Screens are your player's own.** Drawing happens in each player's game; on a server, a shop must ask the server to do the actual change (give items, take coins) through server-side code such as a chat command or block events, or players could cheat.

## 15. Hotbar

Slots are numbered from 1:
- <a id="fn-hotbar-get_slot"></a><a id="fn-hotbar-set_slot"></a>`hotbar.get_slot(i)` returns name, count. `hotbar.set_slot(i, name [, count])` sets a slot.
- <a id="fn-hotbar-get_active_slot"></a><a id="fn-hotbar-set_active_slot"></a>`hotbar.get_active_slot()` / `set_active_slot(i)`
- <a id="fn-hotbar-get_size"></a><a id="fn-hotbar-get_slot_count"></a><a id="fn-hotbar-set_position"></a><a id="fn-hotbar-set_size"></a><a id="fn-hotbar-set_slot_size"></a>`hotbar.get_slot_count()` (same as `get_size()`), `set_size(n)`, `set_slot_size(px)`, `set_position(x, y)`
- <a id="fn-hotbar-get_slot_block"></a>`hotbar.get_slot_block(i)` returns just the name in a slot ("air" if empty)
- <a id="fn-hotbar-set_selected_texture"></a><a id="fn-hotbar-set_texture"></a><a id="fn-hotbar-set_texture_scale"></a>`hotbar.set_texture(path)`, `set_selected_texture(path)`, `set_texture_scale(s)`
- <a id="fn-hotbar-is_creative_open"></a><a id="fn-hotbar-toggle_creative"></a><a id="fn-hotbar-set_creative_open"></a>`hotbar.is_creative_open()`, `toggle_creative()`, `set_creative_open(true|false)`: whether the creative inventory is open, and opening or closing it (the mouse is freed while it's open, as with the inventory key)

### Pick block

Press the **middle mouse button** on a block to put it in your hand, like Minecraft. Players can move it to another key or mouse button in Settings > Controls (**Pick Block**).

- **Creative:** if the block is in the hotbar, that slot is selected. If not, it goes into the selected slot when that's empty, else the next empty slot (which is then selected), else it replaces what's in the selected slot.
- **Survival** (the survival mod): if the block is in the hotbar, that slot is selected. If it's in your inventory, it moves into the hotbar the same way, and whatever it replaces goes back into the inventory. If you don't have one, nothing happens.
- **What you get:** the block itself, or the item its `pick` property names. Craft & Ruin's crops pick wheat seeds, and farmland picks dirt.

<a id="fn-hooks-on_pick_block"></a>Mods can change what happens with the `on_pick_block(x, y, z, item, meta, block)` hook. `item` is what would be picked, and `block` is the block that's actually there. Return `true` when your mod has handled it; return `false` to let the engine's creative behaviour run. Chain it like any hook:

```lua
-- Pick block on a painting gives the painting item, with its picture kept in the item's name
local previous = on_pick_block
function on_pick_block(x, y, z, item, meta, block, ...)
    if block == "painting" and engine.is_creative() then
        engine.give_item("painting_" .. (meta % 16))
        return true
    end
    if previous then return previous(x, y, z, item, meta, block, ...) end
    return false
end
```

For the common case, a block that picks as a different item, set `pick = "..."` in `register_block` instead. No hook is needed.

## 16. Hooks the engine calls

Define any of these as globals. Only the ones you need. If another mod may already define one, keep a reference to the old function and call it. [Hooks, and why you must chain them](#l3-hooks-and-why-you-must-chain-them) explains how.

| hook | when |
|---|---|
| `set_world_seed(seed)`, `generate_chunk(chunk, cx, cz)` | world generation (section 5) |
| `on_update(dt)` | every frame |
| `on_chat_command(text, player, is_operator, reply)` → bool | a `/command` (same arguments in the game and on servers) |
| <a id="fn-hooks-on_shared_changed"></a>`on_shared_changed(key, value)` | a shared value changed (section 17) |
| `on_right_click_block(x, y, z, block, item, sneaking, repeat)` → bool | right-click on a block |
| `on_block_broken(x, y, z, block, meta, player)` | a player broke a block |
| `on_block_placed(x, y, z, block, player)` | a player placed a block |
| `can_edit_block(player, x, y, z, old_block, new_block, old_meta, new_meta)` → `false[, reason]` | before a player's change (block or metadata) on a server or LAN game: refuse it (section 17) |
| `on_player_join(name, info)`, `on_player_leave(name, x, y, z)` | players joining and leaving (section 17) |
| `on_player_chat(name, text)` → `false` or new text | a chat line before everyone sees it (section 17) |
| `on_server_start()`, `on_server_stop()` | the server starts or stops (section 17) |
| `on_pick_block(x, y, z, item, meta, block)` → bool | pick block (middle mouse); see [Pick block](#pick-block) |
| `on_use_hoe(...)`, `on_use_scythe(...)` | tool use: return true if used ([farming](#farming)) |
| `update_player_animation(dt, moving, pitch)` | every frame in game |
| `get_water_color(x, z)` | underwater colour |
| <a id="fn-hooks-on_char_input"></a><a id="fn-hooks-on_scroll_input"></a>`on_char_input(codepoint)`, `on_scroll_input(dx, dy)` | text and mouse wheel |
| `toggle_main_inventory()`, `handle_inventory_click(...)`, `render_inventory(W, H)` | your own inventory |
| `render_*` / `handle_*` / `on_*_opened` | screens (section 14) |

`lua_api.txt` lists them all and marks the ones your loaded mods define.

## 17. Multiplayer and servers

This chapter has two halves:

- **Running a server** ([17a](#17a-running-a-server) to [17f](#17f-logs-diagnostics-and-updates)): for whoever owns a server. Setting it up, the settings, everyday moderation, reports, and keeping it healthy. No Lua needed.
- **Mods on a server** ([17g](#17g-mods-on-a-server)): for mod authors. What runs where, server events and functions, and the limits mods run under.

A world can be played three ways, and mods run the same in all of them: **singleplayer**; **a LAN game** (open a world, press Esc, click Host Game; people on your network see it under Multiplayer > Local Network); and **the dedicated server**, `BlockSmithServer`, a program of its own with no window, for a server that stays up.

### 17a. Running a server

**Quick start.**

1. Put `BlockSmithServer` (`BlockSmithServer.exe` on Windows) next to the game's `games/` and `mods/` folders. It's built with the game, or on its own with `cmake --build build --config Release --target BlockSmithServer`.
2. Run `BlockSmithServer --setup` and answer the questions (name, game, world, port, max players...). Or just start it: it writes a `server.conf` with the defaults, which you can edit.
3. Start it: `BlockSmithServer`. It says `'My Server' is listening on port 30000`, and anything you type in its window is a [server command](#17c-server-commands) (no `/` needed).
4. Join from the game: Multiplayer > + Add Server, with the address (`localhost` on the same PC; otherwise its IP, plus `:port` if it isn't 30000). Players outside your network need port 30000 (UDP) forwarded on the router.
5. Make yourself the boss: type `op <your name>` in the server window. From then on you can use every command in chat too, with `/` in front.

Command-line options: `--setup`, `--config <file>` (another settings file), `--game <folder>`, `--world <name>`, `--port <number>` (these three override `server.conf` for one run), `--list-games`, `--version`.

**The server's files**, all next to `server.conf` unless it says otherwise. They're plain text: you can read and edit them with any editor (edit the lists while the server is stopped, or type `reload` after).

| file | what's in it | changed by |
|---|---|---|
| `server.conf` | all the settings (below) | you; `motd`, `name`, `setworldspawn` |
| `ops.txt` | operators, `Name \| level` | `op`, `deop` |
| `whitelist.txt` | who may join while `whitelist = true` | `whitelist add/remove` |
| `banned.txt`, `banned-ips.txt` | banned names and addresses, with the reason | `ban`, `unban`, `ban-ip`, `pardon-ip` |
| `accounts.txt` | names protected by a player's password (only a hash) | players joining with a password; `password reset` |
| `gamemodes.txt` | players given another game mode | `gamemode` |
| `reports.txt` | player reports ([17d](#17d-player-reports)) | `/report`, `reports close/reopen/delete` |
| `server_icon.png` | the icon in the server list (64×64 PNG, up to 64 KB) | you |
| `logs/latest.log` | everything the server printed, with times; older logs and `crash-*.txt` beside it | the server |
| `saves/<world>/` | the world: chunks, `players/`, `gamerules.txt`, `spawnpoints.txt`, `mod_storage.txt` | the server |

**Settings** (`server.conf`, `key = value`, `#` starts a comment). Change them while it runs by editing the file and typing `reload`; `game`, `world`, `seed` and `port` need a restart.

| setting | default | |
|---|---|---|
| **In the server list** | | |
| `name` | BlockSmith Server | the server's name |
| `motd` | (a gold title) | the description under it: `{gold}`, `{red}`, `{#ff8800}`... colours, `{b}bold{/b}`, `{n}` second line. Also the `motd <text>` command |
| `icon` | server_icon.png | the icon file |
| **What runs** | | |
| `game` | craft_and_ruin | the game (a folder in `games/`; `games` lists them). Players need it too |
| `world` | world | the world folder in `saves/` (made if it doesn't exist) |
| `seed` | (random) | the seed for a new world |
| `port` | 30000 | the UDP port |
| `disabled_mods` | | mods not to load (comma separated); players need the same mods as the server |
| **Players** | | |
| `max_players` | 50 | the most players at once (1 to 4000; a game hosting from its pause menu: up to 200) |
| `view_distance` | 8 | how far players may see, in chunks (2 to 10); lower is less work |
| `spawn` | 8 8 | where new players appear (x z) |
| `welcome` | (a gold welcome) | said to each player who joins; `{player}` is their name |
| `gamemode` | survival | survival or creative, for everyone not given another |
| `whitelist` | false | only `whitelist.txt` may join |
| `require_password` | false | players must set a password (Settings > Player) to join. Recommended for public servers |
| `spawn_protection` | 16 | blocks around spawn only operators may change (0 = off; off while there are no operators) |
| `op_level` | 4 | the level `op <name>` gives when no level is said ([levels](#17c-server-commands)) |
| **Players' client mods** | | |
| `client_mods` | true | may players run their own client mods here ([§20](#20-client-mods)) |
| `client_mods_chat` | false | may those send chat and commands for the player |
| `client_mods_players` | true | may they see other players' names and positions |
| `client_mods_block_range` | 64 | how far from the player they may read blocks (0 = any distance) |
| **Performance and time** | | |
| `tick_rate` | 30 | updates a second (20 to 60) |
| `generator_threads` | 0 | threads making new terrain (0 = CPU cores - 1) |
| `autosave_seconds` | 60 | |
| `day_length` | 20 | minutes in a whole day |
| `lua_time_limit` | 5 | seconds one mod event may run before it's stopped (0 = no limit) |
| **Online content and updates** | | |
| `remote_content` | true | may mods download data (never code) ([§21](#21-online-content)) |
| `remote_domains` | | sites approved besides GitHub |
| `update_check` | true | look for a newer BlockSmith ([17f](#17f-logs-diagnostics-and-updates)) |
| `update_url` | (the BlockSmith repo) | where its version file is |

### 17b. Everyday moderation

What you'll do most, and the command for it. Type them in the server window, or in chat with `/` in front as an operator.

| to... | type |
|---|---|
| make someone an operator (or limit what they may do) | `op Alex`, `op Alex 3` (levels below); `deop Alex` |
| see who's online | `list` |
| warn someone privately | `msg Alex please stop` |
| send someone away / keep them away | `kick Alex spamming`; `ban Alex griefing`, `unban Alex`; `ban-ip Alex` |
| only let friends in | `whitelist on`, `whitelist add Alex`, `whitelist list` |
| read and handle player reports | `reports`, `reports show 3`, `reports close 3 warned them` ([17d](#17d-player-reports)) |
| change someone's game mode | `gamemode creative Alex` |
| bring someone to you, or go to them | `tp Alex @s`, `tp Alex` |
| fix someone's inventory or rescue them | `give Alex torch 16`, `clear Alex`, `kill Alex` |
| set the time or the rules | `time set day`, `gamerule keepInventory true` |
| move the spawn | `setworldspawn` (where you stand) |
| change the description in the list | `motd {gold}Season 2 is live!` |
| someone forgot their password | `password reset Alex` |
| save, reload the lists, stop | `save`, `reload`, `stop` |
| is everything all right? | `tps`, `diag` |

**Protecting the spawn.** `spawn_protection = 16` (the default): blocks within 16 of the spawn can only be changed by operators (level 1 or more). It only works once there's an operator, as in Minecraft.

**Protecting names.** A player who joins with a password (Settings > Player) owns that name on your server: nobody can use it without the password. `require_password = true` makes everyone do it. Passwords never leave the player's PC: the server keeps a per-server proof, not the password.

### 17c. Server commands

Commands work like Minecraft's: typed in the server console (no slash), or in chat by players with `/` in front.

**Who may use what: operator levels.** `op <name> [1-4]` makes someone an operator. Without a level they get `op_level` from `server.conf` (default 4). The levels are stored in `ops.txt` and listed by `ops`. The console can do everything.

| level | may | commands |
|---|---|---|
| everyone | talk and look | `help`, `list`, `msg`/`tell`, `me`, `report`, `version` |
| 1 | build inside spawn protection | |
| 2 | change the game | `gamemode`, `tp`/`teleport`, `give`, `clear`, `kill`, `time`, `gamerule`, `setworldspawn`, `spawnpoint`, `seed`, `say`, `tps` |
| 3 | manage players | `reports`, `kick`, `ban`, `unban`/`pardon`, `ban-ip`, `pardon-ip`, `banlist [players\|ips]`, `whitelist`, `op`, `deop`, `ops` |
| 4 | run the server | `stop`, `save`/`save-all`, `save-off`, `save-on`, `reload`, `games`, `mods`, `motd`, `name`, `password reset`, `diag` |

An operator can't give a higher level than their own. Mods' commands get `is_operator` (any level) and decide for themselves.

**Choosing players.** Wherever a command takes a player you can use a name or a target selector:
- `@a`: everyone;
- `@p`: the nearest player (to you; from the console, to spawn);
- `@r`: someone at random;
- `@s`: yourself.

**Coordinates** are numbers, or `~` for "where the player is" (`~5` is 5 more, `~-2` is 2 less). Whole numbers are block centres for x and z.

| command | does |
|---|---|
| `tp <x y z>`, `tp <player>`, `tp <players> <x y z>`, `tp <players> <player>` | teleports (`teleport` is the same) |
| `give <players> <item> [count]` | items into their hotbar (in stacks of 64; when it's full, a mod's inventory takes the rest) |
| `clear [players]` | empties their inventory |
| `kill [players]` | they die (in survival: the death screen; otherwise straight back to spawn) |
| `me <action>` | `* Name action` in chat |
| `setworldspawn [x y z]` | moves the world's spawn point (saved in `server.conf`) |
| `spawnpoint [players] [x y z]` | where those players come back after dying (saved with the world in `spawnpoints.txt`) |
| `gamerule [rule] [true\|false\|default]` | lists, shows or sets a game rule (below) |
| `save-off`, `save-on` | autosave off and on (`save-all` saves now) |
| `ban-ip <address\|player> [reason]`, `pardon-ip <address>` | bans an IP address, kicking anyone on it (`banned-ips.txt`) |

<a id="gamerules"></a>**Game rules** are saved with the world (`gamerules.txt`) and sent to every player:

| rule | default | |
|---|---|---|
| `doDaylightCycle` | true | the time of day moves on |
| `keepInventory` | the game's | players keep their items when they die |
| `naturalRegeneration` | true | health comes back when you're well fed |
| `fallDamage` | true | falling hurts |
| `drowningDamage` | true | running out of air underwater hurts |
| `fireDamage` | true | lava hurts |
| `doMobSpawning` | true | mobs appear by themselves ([§13a](#entity-spawning)) |
| `randomTickSpeed` | 3 | blocks picked per 16×16×16 section per tick ([random ticks](#abm)): how fast grass spreads and crops grow. 0 = never |

The survival mod follows them. Your mods can too: a rule's value is the shared value `"gamerule.<name>"`, so `engine.get_shared("gamerule.keepInventory")` is `"true"`, `"false"`, or `""` (not set: use your own default). In singleplayer, `/gamerule` in chat does the same for your world, and `/give`, `/clear`, `/kill` and `/tp` work there too. `/give` works the way Minecraft's does: `/give @s core_blocks:basalt 64`. Who to give to (`@s`, `@p` or your own name) can be left out, and the item is its name (`basalt`) or its full name (`core_blocks:basalt`, the grey name you see when pointing at it). Any `something:` in front works, so `minecraft:stone` gives stone.

<a id="fn-hooks-on_server_action"></a>**`give`, `clear` and `kill` happen in the player's game**, where inventories and health live: the server asks, and the game does it. A mod can do these its own way with `on_server_action(action, a, b)`, returning `true` when it has. `action` is `"give"` (`a` = item, `b` = count), `"clear"` or `"kill"` (`a` = the reason shown on the death screen). The survival mod uses it to kill with its death screen and to clear its inventory too. Chain it like other hooks.

### 17d. Player reports

Anyone can type `/report <player> <reason>` in chat (or `report` in the console). The report is saved in `reports.txt` next to `server.conf`, so the owner can read it later, whether or not they were online when it happened. Each report keeps:

- who reported whom, when, and why;
- where both players were (or that the reported player was offline);
- the chat around it: everything either of them said in the 15 minutes before (up to 30 lines).

The player who was reported has to be on the server or have played on it (any capitals). A player can send one report a minute and at most 3 open reports about the same person, and can't report themselves.

Reading and handling them, in the console or as an operator (level 3) in chat:

```
reports                       the open reports, newest first
reports all                   the closed ones too
reports show 2                one report in full: positions, reason, the chat
reports close 2 warned them   dealt with (who closed it, and the note, are kept)
reports reopen 2
reports delete 2              (level 4 / the console)
```

New reports show in the console straight away and go to the operators online (level 3+). Operators who join later are told how many are open, and the console says so when the server starts. Mods get `on_player_report(id, reporter, reported, reason)`.

### 17e. Game modes and permissions


On a server, **the server decides each player's game mode**, and only its admins can change it. A player who joins a survival server can't put themselves in creative.

- **Dedicated server:** `gamemode = survival` (or `creative`) in `server.conf` is the mode everyone starts in. Operators change a player's mode with `/gamemode survival|creative [name]`, and it's remembered in `gamemodes.txt`. Anyone can type `/gamemode` on its own to see their mode.
- **LAN game:** players who join get the host world's mode. Only the host can change it, with `/gamemode survival|creative <player>`.
- **Locked in the engine:** while you're on a server, `engine.set_creative` refuses anything but the mode the server gave you (it returns `false`), so no mod can switch you either.
- **Checked by the server:** outside creative, the server won't let a player break blocks that can't be dug (bedrock, `hardness = -1`), even with a modified game.

For mods:
- <a id="fn-engine-get_server_game_mode"></a>`engine.get_server_game_mode()` returns `"survival"` or `"creative"` (the mode the server gave you), or `nil` when you're not on a server.
- <a id="fn-engine-is_operator"></a>`engine.is_operator()` is true in singleplayer and when hosting. On a server, it says whether you're one of its operators, for example to show admin buttons.
- <a id="fn-hooks-on_game_mode"></a>`on_game_mode(mode, is_operator)` runs when the server sets your mode: when you join, and when an operator changes it. `mode` is `nil` after you leave the server. The survival mod uses this to follow the server.
- `on_chat_command`'s `is_operator` is your real operator status on a server, so mods can keep their own commands for admins.

### 17f. Logs, diagnostics and updates

**The log.** Everything the server prints also goes to `logs/latest.log` next to `server.conf`, with the time on every line. The previous log is kept as `logs/<date>-<n>.log`.

**Diagnostics (the game and the server).** Both write everything they print to `logs/latest.log` (next to `server.conf`, or next to the game's `settings.txt`), with the time on each line; the previous log is kept as `logs/<date>-<n>.log`. To find what went wrong:

- **System report** at the start: version, commit, protocol, build type, compiler, architecture, OS, CPU, threads, memory, Lua version, process and folder, and for the game its graphics card and OpenGL version.
- **A trace under every warning and error** (everything printed to the error stream): the build, the thread, how long it had been running, and the native call stack that printed it. A warning that repeats gets the full trace the 1st, 10th, 100th, 1000th... time, with the count.
- **Lua errors with their Lua stack**: every Lua error the engine catches, and every error a mod catches with `pcall` (`[Lua] Error caught by pcall`), is printed with its Lua traceback: which file and line, called from where, through which mods. `pcall` itself works exactly as in plain Lua.
- **Crash reports**: a crash (bad memory access, abort, illegal instruction...) or an uncaught C++ exception prints what happened and the call stack, to the console, the end of `latest.log`, and `logs/crash-<date>_<time>.txt`.
- **`diag`** (server console, or operators level 4): the system report, ticks per second, chunks and players, the update check, and the warnings so far, most frequent first.

```
[Lua] on_chat_command: mods/mymod/init.lua:20: command blew up
    | trace: server BlockSmith 0.1-alpha (commit 2d5ea37fe8) | thread 1406907518 | up 7.0 s
    |   #0 ??  (BlockSmithServer+0x8118f)
    |   #1 main+0x533b  (BlockSmithServer+0x7a6cb)
```

Stack frames name the function where the platform can, with its module and offset. On Linux and macOS only *exported* functions have names there, and a function that isn't exported shows as the nearest one that is, which can mislead. So each trace ends with a ready-made command that gives the exact function, file and line of every frame in the program: copy it and run it next to the program.

```
    |   exact names and lines: addr2line -f -C -i -e BlockSmithServer 0x1752c6 0x96ada 0x78aef
```

That's `addr2line` on Linux and `atos -o BlockSmithServer -l <load address> ...` on macOS. On Windows the traces read the `.pdb` next to the `.exe` themselves. Release builds keep the line tables this needs (`-DBLOCKSMITH_DEBUG_INFO=OFF` leaves them out). A crash from a signal lists `program(+offset)` for each frame; `addr2line -e <program> <offset>` resolves those the same way.

**Which build.** The server says which BlockSmith it is, with the git commit it was built from, when it starts (`BlockSmith dedicated server 0.1-alpha (commit 1a2b3c4d5e, protocol BlockSmith 0.1)`), as the first line of every log, in the "listening on port" line, and when anyone types `version`. `BlockSmithServer --version` prints it and exits. The commit is stamped in on every build from git (`-dirty` if there were uncommitted changes); without git it's `unknown`, and `-DBLOCKSMITH_COMMIT=<hash>` (or the `BLOCKSMITH_COMMIT` environment variable) sets it for builds from a source archive. The protocol is what games and servers compare to see if they can play together.

**Which versions may play together.** A game may join a server running the same BlockSmith release or an older one; an older game can't join a newer server: it's told to update ("Your game (BlockSmith 0.1-alpha) is older than this server (BlockSmith 0.2-alpha). An update is available..."). Builds of the same release play together (`0.2-alpha` counts as 0.2, so a `0.2-alpha` game and a `0.2-beta` server play together). Underneath, each game says which network protocols it can still speak (`net::kMinProtocolVersion` up to `net::kProtocolVersion`), so a newer game uses the older server's protocol; when support for an old protocol is dropped, raise `kMinProtocolVersion`. The server list marks servers you can't join and why, and older servers you can still join ("Runs an older BlockSmith (0.1-alpha): you can still join").

**New versions.** The game and the server check now and then (at start, then every 6 hours) whether a newer BlockSmith is out. They read one small JSON file, `version.json`, as data, like any remote content (HTTPS, whitelisted hosts, checked to really be JSON); nothing is ever downloaded or installed for you.

```json
{
  "latest": "0.2-alpha",
  "min_supported": "0.1-alpha",
  "commit": "1a2b3c4d5e",
  "download": "https://github.com/Gerold55/testvoxel/releases/latest",
  "notes": "New biomes and menus"
}
```

- `latest`: the newest version. Newer than this build: *update available*.
- `min_supported` (optional): older than this: *update required* (it can't play with up-to-date games and servers any more).
- `download` (optional): where to get it. Only an `https` page on `github.com`, `*.github.io` or the version file's own site is used; anything else is left out.
- `commit` and `notes` (optional): shown with it. Notes are one line, up to 300 characters.

Where it shows: the title screen gets a gold "Update available: 0.2-alpha - click to download" line above the version (red "Update required" when it must be installed); clicking it opens the download page in your browser. The server prints it in its console (and log) when it changes, tells operators when they join, and adds it to `version`. With a warning (an update available or required, or a check that failed) the details follow on the next lines: the file that was checked, the HTTP result, this build's version and commit, the latest and minimum versions, what it decided and why, and the error if there was one:

```
[Server] Couldn't check for updates: the site answered 404
[Server]   version file: https://raw.githubusercontent.com/Gerold55/testvoxel/main/version.json
[Server]   result: HTTP 404, download failed
[Server]   this build: 0.1-alpha (commit 2d5ea37fe8)
[Server]   error: the site answered 404
```

**Between games and servers.** Each server says which BlockSmith it runs when the server list asks it, and each game says which one it is when it joins, so:

- The Multiplayer screen shows a server's BlockSmith version in its details: gold with "Update available: this server runs 0.2-alpha" when it's newer than your game. A server you can't join because of the version says which way round: "This server runs a newer BlockSmith (0.3-alpha): update your game to join", or "...an older BlockSmith (5.1.0): it needs updating". When the game's own check found a newer version, the Multiplayer footer has an "Update available: 0.2-alpha - Download" link.
- A player whose game is older than the server (or than the newest release, from the server's own check) is told so in chat when they join, with the download link. The server's console logs which BlockSmith each player has, and notes when a player's game is newer than the server.
- `engine.get_server_status(address)` has `engine_version`, `engine_commit` and `engine_compare` (`"newer"`, `"older"`, `"same"`, or `""` for older servers that don't say), and `engine.open_update_page()` opens the download page from a title-screen page.

Older games and servers that don't send their version still work together as before; they just don't get these messages.

Settings: `update_check = false` turns it off, `update_url = <address>` reads another file (it must be allowed like any remote content: GitHub or a domain in `remote_domains`). Both go in `settings.txt` for the game and `server.conf` for a server. The default is `https://raw.githubusercontent.com/Gerold55/testvoxel/main/version.json`: publish a new version by changing that file. Mods can read the answer with `engine.get_update_info()` -> `{ enabled, checked, available, required, current, commit, latest, min_supported, download, notes, error }`.

### 17g. Mods on a server

Mods run the same way in singleplayer, when hosting a LAN game, and on the dedicated `BlockSmithServer`. Blocks are sent to players by **name**, so the server and its players need the same mods. The server runs world generation, fluids and block changes; players see the results.

- <a id="fn-engine-is_server"></a>**On the server:** drawing, hotbar and sky functions do nothing. `engine.is_server()` is true there. Functions about this game's own player or window return nothing there: `engine.get_player_position()` is `nil` on the server (it has no player of its own), so check what you get (`if type(x) == "number"`) or use the player's position from an event or `engine.get_player_info(name)`.

**Shared values** let a mod decide something and have every player see it, the way the weather mod does:
- <a id="fn-engine-set_shared"></a>`engine.set_shared(key, text)` sets a value. Only whoever runs the world can: singleplayer, the host, or the dedicated server.
- <a id="fn-engine-get_shared"></a>`engine.get_shared(key)` reads one.
- `on_shared_changed(key, value)` runs on every player's game when a value changes, and when they join.
- <a id="fn-engine-is_authority"></a>`engine.is_authority()` tells your mod whether it's the one deciding.

On the dedicated server, `on_update(dt)` runs every tick. `/commands` the server doesn't know go to `on_chat_command(text, player, is_operator, reply)`, and `reply(text)` answers only that player. Commands added with `chat.register_command` show up in the server's `help`. When you host a LAN game, your guests' `/commands` reach `on_chat_command` too, with `is_operator` false.

#### Server events

These run wherever the world is: on the dedicated server, and in the game for singleplayer and when you host a LAN game. Chain them like any hook (`local previous = on_player_join`).

| event | when | return |
|---|---|---|
| <a id="fn-hooks-on_player_join"></a>`on_player_join(name, info)` | someone joined. `info.operator`, `info.mode` (`"survival"`/`"creative"`), `info.new_account` (they just protected their name with a password) | |
| <a id="fn-hooks-on_player_leave"></a>`on_player_leave(name, x, y, z)` | someone left (or was kicked), and where they were | |
| <a id="fn-hooks-on_player_chat"></a>`on_player_chat(name, text)` | a chat line, **before** anyone sees it | `false` drops it; a string replaces it; nothing lets it through |
| <a id="fn-hooks-can_edit_block"></a>`can_edit_block(player, x, y, z, old_block, new_block, old_meta, new_meta)` | a player is about to change a block: break or place one, or change only its metadata (opening a door, filling a tank). It is also asked for changes a mod makes for them with `engine.player_set_block`. `old_meta` is what is stored now and `new_meta` what they ask for (0..255). | `false` refuses it, and `false, "reason"` also tells the player why. Their game gets the real block back. |
| `on_player_harvest(name, item, x, y, z)` | a joined player asked to use `item` on that block ([`engine.request_harvest`](#fn-engine-request_harvest); the scythe) | |
| <a id="fn-hooks-on_server_start"></a>`on_server_start()` | the dedicated server is ready for players (in the game: you started hosting) | |
| <a id="fn-hooks-on_server_stop"></a>`on_server_stop()` | the server is stopping, before the world is saved (in the game: you stopped hosting) | |

`on_block_broken` and `on_block_placed` also get the player's name, as their last argument.

`can_edit_block` checks players on a server and LAN guests. Your own edits in singleplayer, or as the host, aren't checked. Nothing the player's game does can skip it: the server asks it before changing anything.

```lua
-- Spawn protection: only operators may build within 24 blocks of 0, 0
local previous = can_edit_block
function can_edit_block(player, x, y, z, old, new)
    if previous and previous(player, x, y, z, old, new) == false then return false end
    if math.abs(x) < 24 and math.abs(z) < 24 and not engine.is_player_operator(player) then
        return false, "Spawn is protected"
    end
end

-- Welcome message and a simple word filter
local previous_join = on_player_join
function on_player_join(name, info)
    if previous_join then previous_join(name, info) end
    engine.message_player(name, "Welcome, " .. name .. "!", 1, 0.85, 0.3)
end

local previous_chat = on_player_chat
function on_player_chat(name, text)
    if previous_chat then
        local r = previous_chat(name, text)
        if r == false then return false end
        if type(r) == "string" then text = r end
    end
    return (text:gsub("[Bb]adword", "****"))
end
```

#### Server functions

| function | |
|---|---|
| <a id="fn-engine-get_online_players"></a>`engine.get_online_players()` | `{ { name, x, y, z, mode, operator }, ... }`: everyone in the world. In singleplayer that's you; as a LAN host, you and your guests. |
| <a id="fn-engine-get_player_info"></a>`engine.get_player_info(name)` | that table for one player, or `nil` if they're not online |
| <a id="fn-engine-message_player"></a>`engine.message_player(name, text [, r, g, b])` | a chat line only they see (colour 0..1 or 0..255). True if they're online. |
| <a id="fn-engine-kick_player"></a>`engine.kick_player(name [, reason])` | disconnects them (right after the current event, never in the middle of it). True if they're online. |
| <a id="fn-engine-teleport_player"></a>`engine.teleport_player(name, x, y, z)` | moves them |
| <a id="fn-engine-player_set_block"></a>`engine.player_set_block(name, x, y, z, block [, meta])` | changes a block for a joined player, with the same checks as their own edits: within reach (plus a block of slack, for a 3×3 patch), undiggable blocks only in creative, spawn protection, `can_edit_block`. Returns true if the change was made: everyone sees it, and `on_block_placed` / `on_block_broken` run. Otherwise it returns false and the refusal reason, if there is one to show (it doesn't message them itself). It also returns false, with no reason, when the block and metadata are already exactly what was asked for: nothing changed. Use it for anything a player earns from a change, and give the reward only on true. |
| <a id="fn-engine-set_player_mode"></a><a id="fn-engine-get_player_mode"></a>`engine.set_player_mode(name, "survival"\|"creative")`, `engine.get_player_mode(name)` | their game mode. On the dedicated server, a change is remembered in `gamemodes.txt`. |
| <a id="fn-engine-is_player_operator"></a>`engine.is_player_operator(name)` | is that player an operator? (LAN guests never are; you are, in your own world) |
| <a id="fn-engine-get_server_info"></a>`engine.get_server_info()` | `{ dedicated, running, online, game, max_players, name, world, ... }` |
| `engine.get_storage(key)`, `engine.set_storage(key, value)` | on the dedicated server too: saved in the world's `mod_storage.txt`, with the world |

#### Server safety

- **Mods can't freeze the server.** If one call into a mod (`on_update`, an event, a command) runs longer than `lua_time_limit` seconds (default 5, set in `server.conf`; 0 = no limit), it's stopped with an error and the server carries on. In the game the limit is 5 seconds for the events above.
- **Errors aren't spammed.** A mod error that repeats every tick is printed once, until the message changes.
- **Chat and command flooding is limited.** Each player gets a burst of 6 messages, then one every 0.75 s. More are dropped with a "slow down" warning, and 25 dropped in a row gets them kicked.
- **Clean chat.** Control characters are removed from chat and commands, so nobody can fake extra lines.
- **What mods can read** is limited to the mods, games and `lualib/` folders, and they can't load native code. See [Lua modules: require](#lua-modules-require).

#### Functions for multiplayer menus

- <a id="fn-engine-host_game"></a><a id="fn-engine-join_server"></a><a id="fn-engine-stop_hosting"></a>`engine.join_server`, `host_game`, `stop_hosting`
- <a id="fn-engine-get_connection_status"></a><a id="fn-engine-get_network_info"></a><a id="fn-engine-get_player_list"></a>`engine.get_connection_status`, `get_network_info`, `get_player_list`
- <a id="fn-engine-get_lan_games"></a><a id="fn-engine-get_server_status"></a><a id="fn-engine-ping_server"></a>`engine.ping_server`, `get_server_status`, `get_lan_games`
- <a id="fn-engine-list_servers"></a><a id="fn-engine-save_servers"></a>`engine.list_servers`, `save_servers`: the saved server list, `{ { name, address, pinned }, ... }` (`servers.txt`). `pinned = true` keeps a server at the top of the Multiplayer list.
- <a id="fn-engine-get_player_name"></a><a id="fn-engine-has_player_password"></a><a id="fn-engine-set_player_name"></a><a id="fn-engine-set_player_password"></a>`engine.get_player_name`, `set_player_name`, `set_player_password`, `has_player_password`
- <a id="fn-engine-cancel_join"></a><a id="fn-engine-is_scanning_lan"></a>`engine.cancel_join` stops connecting; `engine.is_scanning_lan` is true while looking for LAN games
- <a id="fn-engine-get_server_settings"></a><a id="fn-engine-has_server_icon"></a><a id="fn-engine-set_server_setting"></a>`engine.get_server_settings`, `set_server_setting` (Settings > Server: description, max players, port), `has_server_icon`
- <a id="fn-engine-allow_firewall"></a>`engine.allow_firewall()` asks Windows again to let other PCs connect (for a "friends can't join" button). Returns `ok, status`: `true, "allowed"` (always, off Windows), `false, "failed"`, or `false, "pending"` when Windows is still applying it; the game records the result itself when it finishes, and asking again meanwhile starts nothing new

## 18. Engine screens and settings

### Creative inventory: tabs for your mod

<a id="fn-creative-register_tab"></a>Like mods in Minecraft, a mod can give its blocks and items **a tab of their own** in the creative inventory's sidebar. Mods' tabs go under the built-in ones (All Items, Tools, Blocks...), below a line. One line in your mod's `init.lua` is enough:
```lua
creative.register_tab{ name = "Gems", icon = "ruby" }    -- every block and item THIS mod registers
```
The engine remembers which mod registered each block (`ui.get_block_info(name).mod`), so the tab knows what's yours. Other ways to choose what's in it:

| field | |
|---|---|
| `name` | **required**: the tab's label |
| `icon` | an item or block shown on the tab (default: its first item) |
| `mod = "farming"` or `{ "farming", "farming_plus" }` | everything those mods register (to group several mods, or for a mod that only adds things to another's tab) |
| `items = { "ruby", "ruby_block", "ruby_sword" }` | exactly these, in this order |
| `filter = function(name, info) ... end` | your own test (`info` is `ui.get_block_info(name)`) |
| `exclusive` | `true` (default): its items are only in this tab, not also in Blocks, Tools... `false`: in both. Either way they're in All Items and in search results. |
| `id` | a name for the tab; register again with the same `id` to replace it |
| `draw = function(f, state) ... end` | instead of an item grid, a **whole page of your own**, drawn with [`ui_kit`](#145-ui_kit-forms-the-easy-way) in the items area: buttons, sliders, lists, item slots, drag and drop. `width`, `height` (units) fit your layout to the space; leave them out and `f.w_units`, `f.h_units` tell you how much room there is. |

On a page of your own:
- <a id="fn-creative-hold"></a>`creative.hold(name [, count])` puts an item on the player's cursor, exactly as if they'd clicked it in the grid, so they can then click a hotbar slot to put it there. `creative.get_held()` returns `name, count` (or `nil`).
- Text fields work: while one has focus, E and Escape type or leave the field instead of closing the screen.
- The mouse wheel goes to your page; over the sidebar it scrolls the sidebar.

When there are more tabs than fit, the sidebar scrolls with the mouse wheel.

<a id="fn-creative-register_category"></a><a id="fn-creative-set_category"></a>Smaller changes: `creative.register_category{ id, name, icons, filter [, before] }` adds a built-in-style category (its items sorted in by the filter), and `creative.set_category(item, category_id)` moves one item to another category or tab.

The creative inventory only opens in creative mode, so tabs are a creative-mode thing; in survival, players get items the normal way. A mod whose `init.lua` may run before the inventory mod's should say so in its `mod.conf`: `optional_depends = inventory`.

**A complete example** is the `ui_showcase` mod (`mods/ui_showcase/`). It registers two tabs: "UI Showcase", with its own blocks and items, and "UI Kit", a page drawn with `ui_kit`: buttons that open the showcase or put its remote or laptop on your cursor, and items you can rearrange by dragging.

A mod's page isn't remembered when the creative inventory closes: it opens on All Items next time.

### Title screen look

A game styles the title screen with files in its `menu/` folder (`games/<game>/menu/`):

| File | What it is |
|---|---|
| `header.png` | The title picture, drawn at its own proportions (otherwise the game's title in big gold text) |
| `panorama_0.png` … `panorama_5.png` | Optional: six square pictures taken from one spot, turning slowly behind the title screen like Minecraft's panorama. In order: 0 front (north, −Z), 1 right, 2 back, 3 left, 4 up, 5 down. Each is a 90° view, as the game's camera sees it with `engine.set_fov(90)` in a square window and `engine.set_player_look(yaw, pitch)` at yaws −90, 0, 90, 180 (pitch 0), then −90 with pitch 89.9 (up) and −90 with pitch −89.9 (down). All six or none. Without them the engine draws its own landscape. |
| `splashes.txt` | Optional: short lines, one per line (`#` starts a comment, 80 characters at most). One is picked at random and pulses in yellow under the title picture, like Minecraft's splash text. |
| `icon.png` | The game's icon in the top bar |
| `button.png` | The picture for every button (otherwise plain grey boxes) |
| `button_hover.png` | Optional: the button under the mouse. Without it, `button.png` lights up. |

**Behind it all** is the game's panorama, or, without one, an animated scene the engine draws: a sky, a sun, drifting blocky clouds and three layers of hills with trees scrolling past at different speeds.

**No picture?** The engine looks for the game's `menu/button.png`, then `mods/base/menu/button.png` (shared by every game), and otherwise draws its own built-in button. So buttons always look like buttons, even with no texture files at all.

**Every menu uses it:** the pause menu, Settings, Singleplayer, Multiplayer, Mods and Texture Packs draw their buttons with the same picture, so they all match the title screen. Disabled buttons are darker, Delete buttons are tinted red, and the open tab uses the hover look.

<a id="fn-ui-draw_button"></a><a id="fn-ui-get_button_text_color"></a>Your own screens can do the same:
```lua
local state = (not enabled and "disabled") or (hovered and "hover") or "normal"   -- or "selected"
ui.draw_button(x, y, w, h, state, is_delete_button)   -- true if drawn
local r, g, b = ui.get_button_text_color(hovered)      -- the label colour from game.conf
ui.draw_text(label, ...)                                -- then draw the label on top
```

Button pictures are drawn **9-slice**. The corners stay as drawn, the edges stretch one way and the middle stretches both ways, so one picture fits wide and narrow buttons. The picture is scaled to each button's height, so pixel art stays crisp and square. A 100×20 picture with a 3-pixel border is a good start.

Optional settings in `game.conf`:
```
button_border = 3                 # corner size in the picture's pixels (default: a sixth of its height)
button_text_color = #e0e0e0       # label colour (or 224, 224, 224)
button_text_hover_color = #ffffa0
```

Mostly used by the base mod's menus. You'll only need these to replace those menus:
- <a id="fn-engine-create_world"></a><a id="fn-engine-delete_world"></a><a id="fn-engine-get_active_world"></a><a id="fn-engine-get_game_info"></a><a id="fn-engine-get_worlds"></a><a id="fn-engine-list_worlds"></a><a id="fn-engine-play_world"></a><a id="fn-engine-save_world"></a><a id="fn-engine-set_active_world"></a><a id="fn-engine-start_game"></a><a id="fn-engine-take_world_error"></a>**Worlds:** `engine.list_worlds`, `create_world`, `play_world`, `delete_world`, `save_world`, `get_game_info`, and the older `get_worlds`, `get_active_world`, `set_active_world`, `start_game`. `engine.play_world(name)` returns `true`, or `false` and why not: the name isn't a world folder in `saves/`, the selected game isn't installed, or the world was made with another game than the selected one. It doesn't have to be the game that's loaded: playing a world whose game or mods differ from the loaded ones reloads the mods for it first, behind the loading screen. `engine.take_world_error()` returns why the last world was closed or couldn't be opened (for example, its world generator failed), once, else `nil`; the world list shows it. Each `engine.list_worlds()` entry is `{ name, seed, game_mode, played, has_thumbnail }`, newest first: `played` reads like "2 hours ago", and `has_thumbnail` says the world has a picture. The engine takes that picture (`saves/<world>/thumbnail.png`, 320x180) from the game view whenever you pause, every five minutes while you play, and a few seconds after a world without one is opened; `ui.world_thumbnail(name)` returns it for `ui.draw_image` (or `nil`), and picks up a newer one by itself.
- <a id="fn-engine-get_keybinds"></a><a id="fn-engine-get_video_settings"></a><a id="fn-engine-key_name"></a><a id="fn-engine-reset_keybinds"></a><a id="fn-engine-set_keybind"></a><a id="fn-engine-set_video_setting"></a>**Settings:** `engine.get_keybinds`, `set_keybind`, `reset_keybinds`, `key_name`, `get_video_settings`, `set_video_setting`
  - <a id="fn-hooks-handle_settings_mouse"></a>**Mouse buttons as controls:** keybinds store a mouse button as `1000 + button` (1002 = middle, 1003 and up = side buttons). `engine.key_name(1002)` returns `"Middle Mouse"`. Movement, jump, sneak, sprint and pick block can use mouse buttons, but not left or right, which dig and place. The engine calls `handle_settings_mouse(code)` when a mouse button is pressed on the Settings screen. Return true to use the press (the base settings menu binds it to the control waiting for a key).
- <a id="fn-engine-back_to_title"></a><a id="fn-engine-close_settings"></a><a id="fn-engine-is_in_game"></a><a id="fn-engine-is_paused"></a><a id="fn-engine-open_settings"></a><a id="fn-engine-pause"></a><a id="fn-engine-quit"></a><a id="fn-engine-quit_to_menu"></a><a id="fn-engine-resume"></a>**Game flow:** `engine.open_settings`, `close_settings`, `pause`, `resume`, `is_paused`, `is_in_game`, `quit_to_menu`, `back_to_title`, `quit`
- <a id="fn-engine-get_texture_pack"></a><a id="fn-engine-list_mods"></a><a id="fn-engine-list_texture_packs"></a><a id="fn-engine-open_mods_folder"></a><a id="fn-engine-open_texture_packs_folder"></a><a id="fn-engine-set_mod_enabled"></a><a id="fn-engine-set_texture_pack"></a>**Mods and texture packs:** `engine.list_mods`, `set_mod_enabled`, `open_mods_folder`, `list_texture_packs`, `set_texture_pack`, `get_texture_pack`, `open_texture_packs_folder`. For a `mods/` folder mod, `enabled` and `set_mod_enabled(id, on)` are the selected game's default for its worlds ([which mods a world runs](#which-mods-a-world-runs)).
- <a id="fn-engine-get_world_mods"></a><a id="fn-engine-set_world_mod"></a>**A world's mods:** `engine.get_world_mods(world)` returns `{ game, mods = { { id, title, description, source, required, enabled, default, choice, loaded }, ... } }`. `source` is `"Global"` (the `mods/` folder) or `"Game"` (always on). `default` is the game's choice, `choice` the world's own (`true`/`false`, nil to follow the game), `enabled` what the world runs, and `loaded` whether it's running right now. `engine.set_world_mod(world, id, on)` returns `true`, or `false` and a reason. Pass `on = nil` to follow the game again. It takes effect the next time the world is opened, reloading mods then if needed.
- <a id="fn-engine-create_server_profile"></a><a id="fn-engine-delete_server_profile"></a><a id="fn-engine-get_server_profile"></a><a id="fn-engine-has_server_build"></a><a id="fn-engine-list_games"></a><a id="fn-engine-list_mods_for_game"></a><a id="fn-engine-list_server_profiles"></a><a id="fn-engine-open_server_profile_folder"></a><a id="fn-engine-save_server_profile"></a><a id="fn-engine-start_server_profile"></a><a id="fn-engine-stop_server_profile"></a>**Dedicated servers (My Servers):** `engine.list_games`, `list_server_profiles`, `get_server_profile`, `save_server_profile`, `create_server_profile`, `delete_server_profile`, `start_server_profile`, `stop_server_profile`, `list_mods_for_game`, `open_server_profile_folder`, `has_server_build`
- <a id="fn-menu-get_active_game"></a><a id="fn-menu-get_games"></a><a id="fn-menu-set_active_game"></a>**Choosing a game:** `menu.get_games`, `get_active_game`, `set_active_game`

## 19. Tips

- **Errors** go to the console with the file and line, and to the Mods screen. A broken mod doesn't stop the others.
- **F3** in-game shows the block and fluid you're looking at, with its id and metadata.
- **Many changes at once:** use `engine.set_block` freely, because changes are batched to other players each tick.
- <a id="fn-buckets-register"></a>**Mods working together:** a mod can offer functions to others in a global table, like `buckets.register(...)`. Check it exists before using it (`if buckets then ... end`), since the player may not have that mod. To be sure it has loaded first, add `depends = buckets` to your `mod.conf`. These tables appear in `lua_api.txt`.


## 20. Client mods

Scripts in `clientmods/<name>/init.lua` that run only in the player's own game. [L10](#l10-client-mods-scripts-for-your-own-game) explains the idea and the server rules. This is the complete API.

**Folders and loading:**
- `clientmods/<name>/init.lua` is a client mod. They load alphabetically after the game's mods; `depends = other` in its `mod.conf` loads it after another client mod.
- **Where the folder is:** `clientmods/` goes next to `mods/`. Inside `mods/` (`mods/clientmods/`) or next to the game's `.exe` also works. The console says which folder it uses (`[Client mods] Folder: ...`) and how many client mods it found. The Mods screen's **Client Mods Folder** button opens it. Client mods are never run as game mods, even inside `mods/`.
- Pictures in the mod's folder (any subfolder) can be drawn by name: `ui.draw_image("map_frame.png", ...)`. Sounds in the folder or its `sounds/` play by name.
- **Off until switched on.** The Mods screen lists client mods (marked *Client mod*) with an Enable button, like other mods, and the change applies after a restart. It's saved in `clientmods/mods.conf` as `load_mod_<name> = true`. For Lua menus, `engine.list_mods()` lists them with `client = true` and id `client:<name>`, and `engine.set_mod_enabled("client:<name>", on)` switches one.
- `settings.txt`: `client_mods = false` switches them all off.
- Saved data goes in `clientmods/_storage/`.

**Callbacks.** Each takes a function and can be used any number of times by any number of mods:

| Function | Your function gets | When |
|---|---|---|
| <a id="fn-client-register_on_update"></a>`client.register_on_update(fn)` | `dt` | every frame while you're in a world |
| <a id="fn-client-register_on_hud"></a>`client.register_on_hud(fn)` | `W, H` | every frame, to draw with `ui.*`, on top of the game's HUD |
| <a id="fn-client-register_on_key"></a>`client.register_on_key(fn)` | `key, action, mods` | a key pressed or released while playing. Compare with `ui.keys.M`, `ui.keys.F4`, `ui.keys["1"]`... and `ui.actions.PRESS`. You can't take the key away from the game. |
| <a id="fn-client-register_on_chat_message"></a>`client.register_on_chat_message(fn)` | `text` | every line added to chat (not those your client mods write) |
| <a id="fn-client-register_on_right_click_block"></a>`client.register_on_right_click_block(fn)` | `x, y, z, block` | you right-click a block |
| <a id="fn-client-register_on_dig_block"></a>`client.register_on_dig_block(fn)` | `x, y, z, block` | you break a block |
| <a id="fn-client-register_on_join"></a>`client.register_on_join(fn)` | `{ mode, address, world }` | you enter a world (`mode` = `"singleplayer"`, `"lan_host"` or `"server"`). Use it to load per-world data. |
| <a id="fn-client-register_on_leave"></a>`client.register_on_leave(fn)` | | you leave it |
| <a id="fn-client-register_chat_command"></a>`client.register_chat_command(name, { params, description, func })` | `param` (the text after the name) | you type `.name ...` in chat. Return `true, "message"` or `false, "error"` to answer. |

**Reading the world** (limited by the server's block range; nil when out of range or not loaded):
- <a id="fn-client-get_block"></a>`client.get_block(x, y, z)` returns name, meta.
- <a id="fn-client-get_surface"></a>`client.get_surface(x, z)` returns y, name: the highest block in that column, for maps.
- <a id="fn-client-get_light_level"></a>`client.get_light_level(x, y, z)` returns 0–15, like `engine.get_light_level`.
- <a id="fn-client-get_block_color"></a>`client.get_block_color(name)` returns r, g, b (0–1): the average colour of the block's top picture, for maps.
- <a id="fn-client-get_block_def"></a>`client.get_block_def(name)` and <a id="fn-client-list_blocks"></a>`client.list_blocks()` work like the `engine.` ones.

**You and the game:**
- <a id="fn-client-get_player_state"></a>`client.get_player_state()`, <a id="fn-client-get_player_position"></a>`client.get_player_position()`, <a id="fn-client-get_player_name"></a>`client.get_player_name()`, <a id="fn-client-is_creative"></a>`client.is_creative()`, <a id="fn-client-is_left_handed"></a>`client.is_left_handed()`, <a id="fn-client-get_dig_progress"></a>`client.get_dig_progress()`: the same as the `engine.` versions.
- <a id="fn-client-get_hotbar_slot"></a>`client.get_hotbar_slot(i)` returns name, count; <a id="fn-client-get_active_slot"></a>`client.get_active_slot()`; <a id="fn-client-get_hotbar_size"></a>`client.get_hotbar_size()`.
- <a id="fn-client-get_time"></a>`client.get_time()` returns the time of day (0–1, as `sky.get_time`). <a id="fn-client-key_name"></a>`client.key_name(code)` returns a key's name.
- <a id="fn-client-get_players"></a>`client.get_players()` returns other players `{ name, x, y, z, yaw }`. It's empty if the server doesn't allow it.
- <a id="fn-client-get_server_info"></a>`client.get_server_info()` returns `{ mode, address, world }`. <a id="fn-client-get_rules"></a>`client.get_rules()` returns `{ allowed, chat, players, block_range }`.

**Chat:**
- <a id="fn-client-display_chat_message"></a>`client.display_chat_message(text [, r, g, b])` shows a line in your own chat (nobody else sees it).
- <a id="fn-client-send_chat_message"></a>`client.send_chat_message(text)` sends chat, or a `/command`, as if you typed it. It returns false when the server doesn't allow it.

**Your mod:**
- <a id="fn-client-get_mod_storage"></a>`client.get_mod_storage()`, called while your mod loads, returns storage kept between sessions: `s:get(key)`, `s:set(key, value)` (nil removes), `s:get_string`, `s:get_int`, `s:get_float`, `s:get_keys()`. Values are text.
- <a id="fn-client-dofile"></a>`client.dofile("file.lua")` runs another file of your mod, while it loads, and returns what it returns.
- <a id="fn-client-get_current_modname"></a>`client.get_current_modname()` (while loading), <a id="fn-client-get_modpath"></a>`client.get_modpath([name])`, <a id="fn-client-get_mod_names"></a>`client.get_mod_names()`.

**Also there:** `ui.*` (all the drawing functions, `ui.keys` and `ui.actions`), `sound.play` / `stop` / `fade` / `set_gain` / `set_pitch` / `set_position` / `is_playing` / `exists` / `get_volume`, `print`, and Lua's `string`, `table`, `math`, `utf8`, and `os.time` / `clock` / `date`.

**Server settings** (`server.conf`, then type `reload` in the server window):
```
client_mods = true                 # false: players' client mods don't run here
client_mods_chat = false           # may they send chat and commands for the player?
client_mods_players = true         # may they see other players?
client_mods_block_range = 64       # how far they can read blocks (0 = any distance)
```
For a LAN game, the host's `settings.txt` can set the same keys with `lan_` in front (`lan_client_mods_chat = false`...). A game mod running on the server can also change the rules with `engine.set_shared("engine:client_mods", "allowed=1;chat=0;players=1;range=32")`.

## 21. Online content

Mods can show content that lives online, on **GitHub** (raw files or GitHub Pages) or on sites the player approved: news, changelogs, server lists, pictures, sounds. You change the file on GitHub and every player sees the new version, **without a new mod download**. It's the same idea as Minecraft's news feed, or the in-game websites of some Minecraft mods.

**It's data only.** The engine never runs anything it downloads. Mods get JSON as Lua tables, text as strings, pictures to draw with `ui.draw_image`, and sounds to play with `sound.play`. Your mod's own code decides what to do with them.

### Setting it up

1. Put your files in a GitHub repository, for example `news.json` and `banner.png` in `you/mymod-content`.
2. Say where your mod's content lives, in its `mod.conf`:
   ```
   remote_sources = https://raw.githubusercontent.com/you/mymod-content/main/
   ```
   Several sources are separated by commas. A mod can only download from below its own sources. On GitHub a source must name the owner and the repository, so no mod can claim all of GitHub.
3. Load it:

```lua
-- A news board that keeps itself up to date
local news, banner

-- Checked every 2 minutes; the callback runs straight away, then only when the file changed
remote.watch("https://raw.githubusercontent.com/you/mymod-content/main/news.json", 120, function(r)
    if r.ok then news = r.data else print("news: " .. r.error) end
end)

remote.fetch("https://raw.githubusercontent.com/you/mymod-content/main/banner.png", function(r)
    if r.ok then banner = r.image end
end)

local previous_hud = render_hud
function render_hud(W, H)
    if previous_hud then previous_hud(W, H) end
    if banner then ui.draw_image(banner, 20, 20, 256, 64) end
    if news then
        for i, item in ipairs(news.items or {}) do
            ui.draw_text(item.title, 20, 100 + i * 24, 0.6)
        end
    end
end

-- A "Refresh" button on your screen: ask GitHub now; the watch above hears about changes
local function on_refresh_clicked()
    remote.refresh("https://raw.githubusercontent.com/you/mymod-content/main/news.json")
end
```

### Functions

| function | |
|---|---|
| <a id="fn-remote-fetch"></a>`remote.fetch(url, callback [, options])` | Downloads (or takes from the cache) and calls `callback(result)` when it's ready. Returns an id, or `nil, error` right away (for example, the URL isn't in your `remote_sources`). `options.max_age = 30`: a copy checked less than this many seconds ago is used without asking the site; `options.refresh = true` always asks. |
| <a id="fn-remote-watch"></a>`remote.watch(url, seconds, callback)` | Calls `callback(result)` once straight away, then again **only when the content changes**. It checks every `seconds` (at least 10). Returns an id, or `nil, error`. |
| <a id="fn-remote-unwatch"></a>`remote.unwatch(id)` | stops a watch |
| <a id="fn-remote-refresh"></a>`remote.refresh(url)` | checks now (for a Refresh button). Watches of that URL are told if it changed. |
| <a id="fn-remote-get_cached"></a>`remote.get_cached(url)` | the cached copy as a result table, or `nil`, without any network use. Lets you show the last version instantly while a fresh check runs. |
| <a id="fn-remote-check_url"></a>`remote.check_url(url)` | `true`, or `false, why`: may your mod use this URL? |
| <a id="fn-remote-is_available"></a>`remote.is_available()` | `true`, or `false, why` (switched off in the settings, or a build without HTTPS) |
| <a id="fn-remote-null"></a>`remote.null` | stands for JSON `null` inside arrays (in objects, `null` fields are simply missing) |

**The result table:** `ok`, `url`, `kind` (`"json"`, `"text"`, `"image"` or `"audio"`), `changed`, `from_cache`, `stale` (the site couldn't be reached, so this is the last good copy), `status`, `error`, plus the content:

| kind | file names | you get |
|---|---|---|
| `json` | `.json` (or no extension and the text is JSON) | `result.data`: Lua tables |
| `text` | `.txt` `.md` `.csv` `.tsv` (or no extension) | `result.data`: a string |
| `image` | `.png` `.jpg` `.jpeg` (up to 4096 × 4096, 8 MB) | `result.image`: give it to `ui.draw_image` |
| `audio` | `.ogg` `.wav` (up to 16 MB) | `result.sound`: give it to `sound.play` |

JSON and text can be up to 4 MB each; JSON can nest at most 64 levels deep.

### How updates reach players

- Every file is kept in a cache, together with the version GitHub gave it. Checking again only asks "has it changed?". If not, nothing is downloaded, and it works offline with the last copy (`stale = true`).
- A new version gets a new cache file, so pictures and sounds update on screen too.
- **GitHub itself caches files for a few minutes.** After you push a change, raw files can take up to about 5 minutes to change (GitHub Pages up to about 10). Players' games see it on the first check after that. A watch every 1–5 minutes is plenty. Checking more often doesn't make GitHub any faster.
- Each mod may make at most 120 requests a minute.

### Which sites

- **Always allowed:** `raw.githubusercontent.com`, `gist.githubusercontent.com` and `<name>.github.io` (GitHub Pages).
- **Other sites:** only if the player lists them in `settings.txt`: `remote_domains = example.com, cdn.mysite.org`. On a dedicated server, the owner does this in `server.conf`. A domain also allows its subdomains.
- **Switch it all off:** `remote_content = false`, in `settings.txt` or `server.conf`.

### Safety: why players can trust it

The engine treats everything online as untrusted:

- **Nothing downloaded ever runs.** Lua, JavaScript, DLLs, EXEs, shell scripts, Java, WebAssembly and HTML can't be downloaded at all (`.lua`, `.js`, `.exe`, `.dll`, `.sh`, `.bat`, `.ps1`, `.jar`, `.html` and similar are refused before connecting).
- **Files are checked by what they contain, not their name.** A program renamed to `picture.png` or `data.json` is refused (Windows, Linux and macOS programs, scripts starting with `#!`, compiled Lua, ZIP/JAR archives, WebAssembly, Office files with macros). Pictures and sounds must really be PNG/JPEG/OGG/WAV, and text must be valid UTF-8.
- **Downloaded text can't be turned into code.** `load()` refuses any downloaded text or JSON string, whole or piece by piece. Downloads are kept in a cache folder outside every mod folder, so `require` and `dofile` can't reach them either.
- **Strict addresses.** HTTPS only, on the normal port, to approved domains only. No IP addresses, `localhost` or local network names, no `user:password@`, and no `..` or encoded slashes to escape your mod's folder. A redirect is only followed to the same site, and only after being checked again.
- **Limits:** file sizes, picture sizes, JSON depth, requests per minute, and a time limit on your callbacks (like other mod events).

`remote.*` isn't available to client mods or world generation.

## 22. Glossary

| Word | Meaning |
|---|---|
| **authority** | The game or server that runs the world: singleplayer, the LAN host or the dedicated server. `engine.is_authority()`. |
| **block** | Anything registered with `engine.register_block`: a solid cube, a plant, a fluid or an item. |
| **chain** | Keeping the previous hook (`local previous = on_update`) and calling it from yours, so every mod's hook runs. |
| **client mod** | A player's own script in `clientmods/`, running only in their game: a minimap, waypoints, a HUD. The server decides what client mods may do there. |
| **chunk** | A 16 × 256 × 16 column of the world, generated, saved and sent as one piece. |
| **cutout** | See-through pixels that are either fully clear or fully solid (leaves). Cheaper than `transparent`. |
| **hook** | A global function with a special name that the engine calls when something happens. |
| **item** | A block with `inventory_image`. It shows as a flat picture and is never placed. |
| **load time / run time** | Load time is when your file first runs at startup (register things then). Run time is later, inside hooks. |
| **metadata** | The one extra number (0–255) every block in the world has, for its state. |
| **operator** | A server admin. Operators can use commands such as `/gamemode` and `/op`. |
| **shared value** | A text value the authority sets with `engine.set_shared` and every player's game receives. |
