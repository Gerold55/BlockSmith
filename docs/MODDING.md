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
3. [Part 3: Reference](#part-3-reference)
   - [Function index](#function-index): every function and hook, A to Z per table
   - [1. Folders](#1-folders)
   - [2. Your first mod](#2-your-first-mod)
   - [3. Blocks and items](#3-blocks-and-items)
   - [4. Changing the world](#4-changing-the-world)
   - [5. World generation](#5-world-generation)
   - [6. Items that do things](#6-items-that-do-things)
   - [7. Fluids](#7-fluids)
   - [8. Chat commands](#8-chat-commands)
   - [9. Sky, weather, fog, clouds, underwater](#9-sky-weather-fog-clouds-underwater)
   - [10. Light](#10-light)
   - [11. Survival: health, hunger, digging](#11-survival-health-hunger-digging)
   - [12. Sound](#12-sound)
   - [13. Models](#13-models)
   - [14. UI: menus and screens](#14-ui-menus-and-screens)
   - [15. Hotbar](#15-hotbar)
   - [16. Hooks the engine calls](#16-hooks-the-engine-calls)
   - [17. Multiplayer and servers](#17-multiplayer-and-servers)
   - [18. Engine screens and settings](#18-engine-screens-and-settings)
   - [19. Tips](#19-tips)
   - [20. Client mods](#20-client-mods)
   - [21. Glossary](#21-glossary)

---

# Part 1: Learn

This part explains how modding works, one idea at a time. Read it once from top to bottom. Each chapter builds on the one before.

## L1. How BlockSmith runs your mod

BlockSmith is split in two:

- **The engine** (C++) handles drawing, physics, light, sound, networking and saving. It provides *mechanics*: "a block can give off light", "a player can take damage", "a picture can be drawn here".
- **Lua mods** decide what the game *is*: which blocks exist, how much a fall hurts, what the HUD looks like, how terrain is shaped. Craft & Ruin, the default game, is just a set of Lua mods.

When you start a world, this happens in order:

1. The engine starts with no blocks and no rules.
2. It runs the **global mods** in `mods/`, in alphabetical order by folder name.
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

Mods that come with BlockSmith offer `survival`, `survival.inventory`, `weather`, `doors`, `buckets`, `leafdecay` and `default_sounds`. `lua_api.txt` lists every table your loaded mods offer.

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

The complete client API is in [Reference §20](#20-client-mods). `clientmods/minimap` comes with BlockSmith and is a complete example. Switch it on on the Mods screen, then press **M** to show or hide it, and use `.wp add home` to set a waypoint.

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

A notice board: right-click it to open a screen with a message and a Close button. The mouse is freed while it's open, and Escape or E closes it.

The engine has one in-game screen slot for mods: `render_inventory`, `handle_inventory_click` and `toggle_main_inventory`. The survival inventory uses it too, so a mod that adds a screen **chains all three** and only draws while its own screen is open.

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
    ui.draw_text("Town notice", L.x + 16, L.y + 14, 0.5, 1, 0.9, 0.6)
    ui.draw_text("No digging under the church.", L.x + 16, L.y + 60, 0.35, 1, 1, 1)

    local mx, my = ui.get_mouse()
    local hover = mx >= L.bx and mx < L.bx + L.bw and my >= L.by and my < L.by + L.bh
    ui.draw_button(L.bx, L.by, L.bw, L.bh, hover and "hover" or "normal")
    local r, g, b = ui.get_button_text_color(hover)
    local label, s = "Close", 0.35
    ui.draw_text(label, L.bx + (L.bw - ui.text_width(label, s)) / 2,
                 L.by + (L.bh - ui.text_height(s)) / 2, s, r, g, b)
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

Put this mod in the game's `mods/` folder, or give it `depends = survival`, so it loads after the survival mod and gets E and Escape first.

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

A cow that wanders around and plays its walk animation. Make the model and animation in Blockbench: export the model as *Bedrock Geometry* (`cow.geo.json`) and the animation with *Export Bedrock Animations* (`cow.animation.json`). Put both in your mod's `models/` folder and the skin in `textures/`.

```lua
local model = engine.load_model("cow.geo.json", "cow.png")
local anims = engine.load_animations("cow.animation.json")
local walk = anims and (anims:get("walk") or anims:get())
if walk then walk:set_loop(true) end

local cow = nil         -- { id, x, y, z, yaw, target_yaw, timer }
local t = 0

local function ground_at(x, z, from_y)
    for y = from_y + 3, from_y - 6, -1 do
        if engine.get_block_name(math.floor(x), y, math.floor(z)) ~= "air" then return y + 1 end
    end
    return nil
end

local previous_update = on_update
function on_update(dt, ...)
    if previous_update then previous_update(dt, ...) end
    if not model then return end

    -- Spawn one cow near the player the first time
    if not cow then
        local px, py, pz = engine.get_player_position()
        local y = ground_at(px + 4, pz, math.floor(py))
        if not y then return end
        cow = { x = px + 4, y = y, z = pz, yaw = 0, target_yaw = 0, timer = 0 }
        cow.id = engine.add_model(model, cow.x, cow.y, cow.z, cow.yaw)
    end

    -- Every few seconds, pick a new direction
    cow.timer = cow.timer - dt
    if cow.timer <= 0 then
        cow.target_yaw = math.random(0, 359)
        cow.timer = math.random(3, 7)
    end
    cow.yaw = cow.yaw + (cow.target_yaw - cow.yaw) * math.min(1, dt * 3)   -- turn smoothly

    -- Walk forward, and stay on the ground
    local speed = 1.2
    local nx = cow.x + math.cos(math.rad(cow.yaw)) * speed * dt
    local nz = cow.z + math.sin(math.rad(cow.yaw)) * speed * dt
    local ny = ground_at(nx, nz, cow.y)
    if ny and math.abs(ny - cow.y) <= 1 then
        cow.x, cow.y, cow.z = nx, ny, nz
    else
        cow.timer = 0                          -- a wall or a drop: turn around next frame
    end
    engine.move_model(cow.id, cow.x, cow.y, cow.z, cow.yaw)

    -- Animate
    t = t + dt
    if walk then
        model:reset_pose()
        walk:apply(model, t)
    end
end
```

**Things to know:**
- **Yaw** is in degrees. Yaw 0 faces +x (east), 90 faces +z.
- **Poses are shared.** Every copy you place with `engine.add_model` uses the same model, so they all share one pose. For cows that move out of step, load the model once per cow.
- **Moving bones yourself:** `local head = model:get_bone("head")`, then `head:set_rotation(x, y, z)` (degrees) or `head:set_position(x, y, z)`. Do it after `walk:apply` to add to the animation.
- **Multiplayer:** models are drawn only in your own game. This simple cow lives only on your screen; for a cow everyone sees, the authority decides where it is and shares that with `engine.set_shared`.

## C15. Your own terrain

A complete world generator: rolling grass hills, sand beaches and a sea. It replaces the game's terrain, so put it in a new game, or in a mod that loads after the game's terrain mod.

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

---

# Part 3: Reference

Every function, grouped by what it's for. Each section is complete on its own, so you can jump straight in from the index.

## Function index

Type in the search box (in `MODDING.html`) or use your browser's find (Ctrl+F) to jump to any function.

Every function and hook, by table, each linking to where it's explained. `lua_api.txt` (written by the engine at startup) lists what your build has.

**engine** (131)  
[`engine.add_model`](#fn-engine-add_model) · [`engine.add_player_velocity`](#fn-engine-add_player_velocity) · [`engine.allow_firewall`](#fn-engine-allow_firewall) · [`engine.back_to_title`](#fn-engine-back_to_title) · [`engine.cancel_join`](#fn-engine-cancel_join) · [`engine.close_settings`](#fn-engine-close_settings) · [`engine.create_server_profile`](#fn-engine-create_server_profile) · [`engine.create_world`](#fn-engine-create_world) · [`engine.delete_server_profile`](#fn-engine-delete_server_profile) · [`engine.delete_world`](#fn-engine-delete_world) · [`engine.draw_text`](#fn-engine-draw_text) · [`engine.find_texture`](#fn-engine-find_texture) · [`engine.get_active_world`](#fn-engine-get_active_world) · [`engine.get_block_def`](#fn-engine-get_block_def) · [`engine.get_block_id`](#fn-engine-get_block_id) · [`engine.get_block_name`](#fn-engine-get_block_name) · [`engine.get_connection_status`](#fn-engine-get_connection_status) · [`engine.get_dig_progress`](#fn-engine-get_dig_progress) · [`engine.get_fluid`](#fn-engine-get_fluid) · [`engine.get_game_info`](#fn-engine-get_game_info) · [`engine.get_keybinds`](#fn-engine-get_keybinds) · [`engine.get_lan_games`](#fn-engine-get_lan_games) · [`engine.get_light`](#fn-engine-get_light) · [`engine.get_light_level`](#fn-engine-get_light_level) · [`engine.get_metadata`](#fn-engine-get_metadata) · [`engine.get_network_info`](#fn-engine-get_network_info) · [`engine.get_player_list`](#fn-engine-get_player_list) · [`engine.get_player_name`](#fn-engine-get_player_name) · [`engine.get_player_position`](#fn-engine-get_player_position) · [`engine.get_player_state`](#fn-engine-get_player_state) · [`engine.get_server_game_mode`](#fn-engine-get_server_game_mode) · [`engine.get_server_profile`](#fn-engine-get_server_profile) · [`engine.get_server_settings`](#fn-engine-get_server_settings) · [`engine.get_server_status`](#fn-engine-get_server_status) · [`engine.get_shared`](#fn-engine-get_shared) · [`engine.get_storage`](#fn-engine-get_storage) · [`engine.get_swing`](#fn-engine-get_swing) · [`engine.get_texture_pack`](#fn-engine-get_texture_pack) · [`engine.get_video_settings`](#fn-engine-get_video_settings) · [`engine.get_worlds`](#fn-engine-get_worlds) · [`engine.give_item`](#fn-engine-give_item) · [`engine.has_player_password`](#fn-engine-has_player_password) · [`engine.has_server_build`](#fn-engine-has_server_build) · [`engine.has_server_icon`](#fn-engine-has_server_icon) · [`engine.host_game`](#fn-engine-host_game) · [`engine.is_authority`](#fn-engine-is_authority) · [`engine.is_creative`](#fn-engine-is_creative) · [`engine.is_fluid_source`](#fn-engine-is_fluid_source) · [`engine.is_flying`](#fn-engine-is_flying) · [`engine.is_in_game`](#fn-engine-is_in_game) · [`engine.is_inventory_open`](#fn-engine-is_inventory_open) · [`engine.is_left_handed`](#fn-engine-is_left_handed) · [`engine.is_operator`](#fn-engine-is_operator) · [`engine.is_outdoors`](#fn-engine-is_outdoors) · [`engine.is_paused`](#fn-engine-is_paused) · [`engine.is_replaceable`](#fn-engine-is_replaceable) · [`engine.is_scanning_lan`](#fn-engine-is_scanning_lan) · [`engine.is_server`](#fn-engine-is_server) · [`engine.is_third_person`](#fn-engine-is_third_person) · [`engine.is_world_generator`](#fn-engine-is_world_generator) · [`engine.join_server`](#fn-engine-join_server) · [`engine.key_name`](#fn-engine-key_name) · [`engine.list_blocks`](#fn-engine-list_blocks) · [`engine.list_fluids`](#fn-engine-list_fluids) · [`engine.list_games`](#fn-engine-list_games) · [`engine.list_mods`](#fn-engine-list_mods) · [`engine.list_mods_for_game`](#fn-engine-list_mods_for_game) · [`engine.list_server_profiles`](#fn-engine-list_server_profiles) · [`engine.list_servers`](#fn-engine-list_servers) · [`engine.list_texture_packs`](#fn-engine-list_texture_packs) · [`engine.list_worlds`](#fn-engine-list_worlds) · [`engine.load_animations`](#fn-engine-load_animations) · [`engine.load_model`](#fn-engine-load_model) · [`engine.move_model`](#fn-engine-move_model) · [`engine.open_mods_folder`](#fn-engine-open_mods_folder) · [`engine.open_server_profile_folder`](#fn-engine-open_server_profile_folder) · [`engine.open_settings`](#fn-engine-open_settings) · [`engine.open_texture_packs_folder`](#fn-engine-open_texture_packs_folder) · [`engine.override_block`](#fn-engine-override_block) · [`engine.pause`](#fn-engine-pause) · [`engine.ping_server`](#fn-engine-ping_server) · [`engine.play_world`](#fn-engine-play_world) · [`engine.quit`](#fn-engine-quit) · [`engine.quit_to_menu`](#fn-engine-quit_to_menu) · [`engine.register_block`](#fn-engine-register_block) · [`engine.register_fluid`](#fn-engine-register_fluid) · [`engine.register_fluid_reaction`](#fn-engine-register_fluid_reaction) · [`engine.register_item_use`](#fn-engine-register_item_use) · [`engine.remove_model`](#fn-engine-remove_model) · [`engine.reset_keybinds`](#fn-engine-reset_keybinds) · [`engine.respawn_player`](#fn-engine-respawn_player) · [`engine.resume`](#fn-engine-resume) · [`engine.save_server_profile`](#fn-engine-save_server_profile) · [`engine.save_servers`](#fn-engine-save_servers) · [`engine.save_world`](#fn-engine-save_world) · [`engine.send_chat`](#fn-engine-send_chat) · [`engine.set_active_world`](#fn-engine-set_active_world) · [`engine.set_ambient_occlusion`](#fn-engine-set_ambient_occlusion) · [`engine.set_block`](#fn-engine-set_block) · [`engine.set_block_meta`](#fn-engine-set_block_meta) · [`engine.set_creative`](#fn-engine-set_creative) · [`engine.set_dig_crack_texture`](#fn-engine-set_dig_crack_texture) · [`engine.set_first_person_arm`](#fn-engine-set_first_person_arm) · [`engine.set_first_person_hand`](#fn-engine-set_first_person_hand) · [`engine.set_fly_allowed`](#fn-engine-set_fly_allowed) · [`engine.set_flying`](#fn-engine-set_flying) · [`engine.set_footstep_distance`](#fn-engine-set_footstep_distance) · [`engine.set_fov`](#fn-engine-set_fov) · [`engine.set_inventory_open`](#fn-engine-set_inventory_open) · [`engine.set_keybind`](#fn-engine-set_keybind) · [`engine.set_mod_enabled`](#fn-engine-set_mod_enabled) · [`engine.set_model_visible`](#fn-engine-set_model_visible) · [`engine.set_moon_texture`](#fn-engine-set_moon_texture) · [`engine.set_place_repeat`](#fn-engine-set_place_repeat) · [`engine.set_player_controls`](#fn-engine-set_player_controls) · [`engine.set_player_name`](#fn-engine-set_player_name) · [`engine.set_player_password`](#fn-engine-set_player_password) · [`engine.set_player_position`](#fn-engine-set_player_position) · [`engine.set_server_setting`](#fn-engine-set_server_setting) · [`engine.set_shared`](#fn-engine-set_shared) · [`engine.set_smooth_lighting`](#fn-engine-set_smooth_lighting) · [`engine.set_storage`](#fn-engine-set_storage) · [`engine.set_sun_texture`](#fn-engine-set_sun_texture) · [`engine.set_texture_pack`](#fn-engine-set_texture_pack) · [`engine.set_video_setting`](#fn-engine-set_video_setting) · [`engine.start_game`](#fn-engine-start_game) · [`engine.start_server_profile`](#fn-engine-start_server_profile) · [`engine.stop_hosting`](#fn-engine-stop_hosting) · [`engine.stop_server_profile`](#fn-engine-stop_server_profile) · [`engine.swing_hand`](#fn-engine-swing_hand) · [`engine.write_api_reference`](#fn-engine-write_api_reference)

**world** (10)  
[`world.get_cave_settings`](#fn-world-get_cave_settings) · [`world.get_caves_enabled`](#fn-world-get_caves_enabled) · [`world.get_loaded_chunks`](#fn-world-get_loaded_chunks) · [`world.get_pending_chunks`](#fn-world-get_pending_chunks) · [`world.get_render_distance`](#fn-world-get_render_distance) · [`world.set_cave_settings`](#fn-world-set_cave_settings) · [`world.set_caves_enabled`](#fn-world-set_caves_enabled) · [`world.set_load_budget`](#fn-world-set_load_budget) · [`world.set_mesh_budget`](#fn-world-set_mesh_budget) · [`world.set_render_distance`](#fn-world-set_render_distance)

**ui** (14)  
[`ui.draw_block_icon`](#fn-ui-draw_block_icon) · [`ui.draw_button`](#fn-ui-draw_button) · [`ui.draw_image`](#fn-ui-draw_image) · [`ui.draw_rect`](#fn-ui-draw_rect) · [`ui.draw_server_icon`](#fn-ui-draw_server_icon) · [`ui.draw_text`](#fn-ui-draw_text) · [`ui.get_all_blocks`](#fn-ui-get_all_blocks) · [`ui.get_block_info`](#fn-ui-get_block_info) · [`ui.get_button_text_color`](#fn-ui-get_button_text_color) · [`ui.get_mouse`](#fn-ui-get_mouse) · [`ui.get_mouse_pos`](#fn-ui-get_mouse_pos) · [`ui.get_time`](#fn-ui-get_time) · [`ui.text_height`](#fn-ui-text_height) · [`ui.text_width`](#fn-ui-text_width)

**hotbar** (16)  
[`hotbar.get_active_slot`](#fn-hotbar-get_active_slot) · [`hotbar.get_rect`](#fn-hotbar-get_rect) · [`hotbar.get_size`](#fn-hotbar-get_size) · [`hotbar.get_slot`](#fn-hotbar-get_slot) · [`hotbar.get_slot_block`](#fn-hotbar-get_slot_block) · [`hotbar.get_slot_count`](#fn-hotbar-get_slot_count) · [`hotbar.is_creative_open`](#fn-hotbar-is_creative_open) · [`hotbar.set_active_slot`](#fn-hotbar-set_active_slot) · [`hotbar.set_position`](#fn-hotbar-set_position) · [`hotbar.set_selected_texture`](#fn-hotbar-set_selected_texture) · [`hotbar.set_size`](#fn-hotbar-set_size) · [`hotbar.set_slot`](#fn-hotbar-set_slot) · [`hotbar.set_slot_size`](#fn-hotbar-set_slot_size) · [`hotbar.set_texture`](#fn-hotbar-set_texture) · [`hotbar.set_texture_scale`](#fn-hotbar-set_texture_scale) · [`hotbar.toggle_creative`](#fn-hotbar-toggle_creative)

**chat** (2)  
[`chat.register_command`](#fn-chat-register_command) · [`chat.send`](#fn-chat-send)

**sky** (28)  
[`sky.add_light`](#fn-sky-add_light) · [`sky.flash`](#fn-sky-flash) · [`sky.get_light`](#fn-sky-get_light) · [`sky.get_moon_phase`](#fn-sky-get_moon_phase) · [`sky.get_precipitation`](#fn-sky-get_precipitation) · [`sky.get_rain_intensity`](#fn-sky-get_rain_intensity) · [`sky.get_stars`](#fn-sky-get_stars) · [`sky.get_time`](#fn-sky-get_time) · [`sky.move_light`](#fn-sky-move_light) · [`sky.remove_light`](#fn-sky-remove_light) · [`sky.reset`](#fn-sky-reset) · [`sky.set_ambient_color`](#fn-sky-set_ambient_color) · [`sky.set_ambient_intensity`](#fn-sky-set_ambient_intensity) · [`sky.set_block_light_strength`](#fn-sky-set_block_light_strength) · [`sky.set_brightness`](#fn-sky-set_brightness) · [`sky.set_fog_color`](#fn-sky-set_fog_color) · [`sky.set_held_light`](#fn-sky-set_held_light) · [`sky.set_light`](#fn-sky-set_light) · [`sky.set_min_light`](#fn-sky-set_min_light) · [`sky.set_moon_phase`](#fn-sky-set_moon_phase) · [`sky.set_moon_textures`](#fn-sky-set_moon_textures) · [`sky.set_precipitation`](#fn-sky-set_precipitation) · [`sky.set_rain_intensity`](#fn-sky-set_rain_intensity) · [`sky.set_sky_tint`](#fn-sky-set_sky_tint) · [`sky.set_stars`](#fn-sky-set_stars) · [`sky.set_sun_color`](#fn-sky-set_sun_color) · [`sky.set_sun_intensity`](#fn-sky-set_sun_intensity) · [`sky.set_time`](#fn-sky-set_time)

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

**Block light properties** (4)  
[`light`, `light_color`, `light_filter`, `light_absorb`](#fn-block-light)

**chunk (in generate_chunk)** (6)  
[`chunk:get_block`](#fn-chunk-get_block) · [`chunk:get_metadata`](#fn-chunk-get_metadata) · [`chunk:set_block`](#fn-chunk-set_block) · [`chunk:set_block_with_meta`](#fn-chunk-set_block_with_meta) · [`chunk:set_metadata`](#fn-chunk-set_metadata) · [`chunk:set_tint`](#fn-chunk-set_tint)

**model, bone and animation objects** (12)  
[`model:get_bone`](#fn-model-get_bone) · [`model:reset_pose`](#fn-model-reset_pose) · [`bone:get_rotation`](#fn-bone-get_rotation) · [`bone:set_position`](#fn-bone-set_position) · [`bone:set_rotation`](#fn-bone-set_rotation) · [`anims:get`](#fn-animationset-get) · [`anims:names`](#fn-animationset-names) · [`anim:apply`](#fn-animation-apply) · [`anim:get_length`](#fn-animation-get_length) · [`anim:get_name`](#fn-animation-get_name) · [`anim:is_looping`](#fn-animation-is_looping) · [`anim:set_loop`](#fn-animation-set_loop)

**Hooks (functions you define)** (36)  
[`generate_chunk`](#fn-hooks-generate_chunk) · [`get_dig_time`](#fn-hooks-get_dig_time) · [`get_foliage_color`](#fn-hooks-get_foliage_color) · [`get_grass_color`](#fn-hooks-get_grass_color) · [`get_water_color`](#fn-hooks-get_water_color) · [`handle_inventory_click`](#fn-hooks-handle_inventory_click) · [`handle_pause_click`](#fn-hooks-handle_pause_click) · [`handle_settings_mouse`](#fn-hooks-handle_settings_mouse) · [`on_block_broken`](#fn-hooks-on_block_broken) · [`on_block_placed`](#fn-hooks-on_block_placed) · [`on_char_input`](#fn-hooks-on_char_input) · [`on_chat_command`](#fn-hooks-on_chat_command) · [`on_dig_block`](#fn-hooks-on_dig_block) · [`on_game_mode`](#fn-hooks-on_game_mode) · [`on_hotbar_full`](#fn-hooks-on_hotbar_full) · [`on_pick_block`](#fn-hooks-on_pick_block) · [`on_player_click`](#fn-hooks-on_player_click) · [`on_right_click_block`](#fn-hooks-on_right_click_block) · [`on_scroll_input`](#fn-hooks-on_scroll_input) · [`on_shared_changed`](#fn-hooks-on_shared_changed) · [`on_update`](#fn-hooks-on_update) · [`on_use_hoe`](#fn-hooks-on_use_hoe) · [`on_use_scythe`](#fn-hooks-on_use_scythe) · [`render_creative_inventory`](#fn-hooks-render_creative_inventory) · [`render_hud`](#fn-hooks-render_hud) · [`render_inventory`](#fn-hooks-render_inventory) · [`render_mods`](#fn-hooks-render_mods) · [`render_multiplayer`](#fn-hooks-render_multiplayer) · [`render_pause_menu`](#fn-hooks-render_pause_menu) · [`render_player_list`](#fn-hooks-render_player_list) · [`render_settings`](#fn-hooks-render_settings) · [`render_texture_packs`](#fn-hooks-render_texture_packs) · [`render_world_select`](#fn-hooks-render_world_select) · [`set_world_seed`](#fn-hooks-set_world_seed) · [`toggle_main_inventory`](#fn-hooks-toggle_main_inventory) · [`update_player_animation`](#fn-hooks-update_player_animation)

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
      ...
mods/                           global mods: loaded with any game, can be switched off
  buckets/
    mod.conf                    title, author, description, version, depends, optional_depends, required = true|false
    init.lua                    if present, the ONLY file the engine runs (it loads the rest)
    textures/  models/
texturepacks/
  MyPack/pack.conf              any image with the same file name replaces a game texture
saves/<world>/                  worlds
servers/<profile>/              dedicated server setups (made with My Servers)
```

**How a mod's files run:**
- Mods run in a fixed order: global mods first, then the game's mods, each alphabetically by folder name. Mods with an `init.lua` run before loose `.lua` files. A mod whose `mod.conf` has `depends = a, b` (or `optional_depends`) runs after those mods.
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
| `gravity` | false | falls like sand |
| `tint` | false | tint all faces with r, g, b |
| `in_creative` | true | listed in the creative inventory |
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
- <a id="fn-hooks-on_block_broken"></a>`on_block_broken(x, y, z, block, meta)`: a player broke a block, in creative or survival. It runs after the block is gone; `meta` is what its metadata was (on a server: 0). The doors mod uses it to remove the other half.
- <a id="fn-hooks-on_block_placed"></a>`on_block_placed(x, y, z, block)`: a player placed a block.

Both run where the world is decided too: in singleplayer, for every player's edits when you host a LAN game, and on the dedicated server. A mod that changes the world from them, like leaf decay, should check `engine.is_authority()` so it only acts once.

<a id="fn-leafdecay-on_decay"></a>**Leaf decay** (the `leafdecay` mod): when a tree is cut down, its leaves fade away over the next few seconds. A leaf stays while a log is within 6 steps of it through other leaves, and leaves a player placed never decay. You can change `leafdecay.settings.distance` and `leafdecay.settings.delay`, or which blocks count with `leafdecay.is_leaves` and `leafdecay.is_log`. `leafdecay.on_decay(function(x, y, z, leaves) end)` runs when a leaf goes.

Leaves break instantly by hand. Like Minecraft, broken or decaying leaves can drop a sapling (5%), 1–2 sticks (2%), and from oak leaves an apple (0.5%), but only items the game has. The leaves themselves drop only with shears (a tool with `tool_type = "shears"`). Change the drops with `leafdecay.register_drops("leaves_oak", { { item = "sapling_oak", chance = 0.05 }, { item = "stick", chance = 0.02, min = 1, max = 2 } })`. There are no dropped items lying in the world yet, so decaying leaves give their drops to the player if they're within 32 blocks.
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

In multiplayer, commands the client doesn't handle go to the server. Server operators have their own commands there (see section 17).

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

- <a id="fn-sky-set_brightness"></a>`sky.set_brightness(v)`: 0 = moody (dark nights and caves), 1 = bright, like Minecraft's brightness slider. Default 0.35.
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
- <a id="fn-bone-set_rotation"></a><a id="fn-bone-get_rotation"></a>`bone:set_rotation(x, y, z)` / `bone:get_rotation()`: degrees, relative to the model file's pose.
- <a id="fn-bone-set_position"></a>`bone:set_position(x, y, z)`: an offset from where the model file puts the bone, in model pixels (16 per block). `model:reset_pose()` sets it back to 0; the file's own positions are never lost.
- <a id="fn-model-reset_pose"></a>`model:reset_pose()` puts every bone back to the model file's pose.

<a id="fn-hooks-on_update"></a><a id="fn-hooks-update_player_animation"></a>Use `on_update(dt)` to move them every frame. The player model is the global `g_PlayerModel`, and the engine calls `update_player_animation(dt, is_moving, pitch)`.

### First-person hand

In first person the engine shows what you're holding at the bottom of the screen. A block is a little cube; an item with an `inventory_image` (and plants) is a flat picture. With an empty hand it shows your arm, taken from the player model (`g_PlayerModel`) or a plain skin-coloured arm if the model has no arm bone. The hand sways as you walk, swings when you dig, place or use something, and dips and comes back up when you switch hotbar slots. The **Main Hand** setting (Settings > Video & Sound) moves it to the left for left-handed players.

- <a id="fn-engine-set_first_person_arm"></a>`engine.set_first_person_arm({ right = "upper_arm_right", left = "upper_arm_left", color = { 224, 176, 140 } })`: which bone of the player model is the arm (it's drawn with everything attached to it, in the model file's pose), and the plain arm's colour. Without this, common names such as `right_arm` and `arm_right` are tried.
- <a id="fn-engine-set_first_person_hand"></a>`engine.set_first_person_hand(false)` hides it, for example for a cutscene.
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

The Craft & Ruin player mod (`mods/player/animations.lua`) plays its walk cycle this way. It fades in when you start moving, plays faster when you sprint, and fades out when you stop.

## 14. UI: menus and screens

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

## 15. Hotbar

Slots are numbered from 1:
- <a id="fn-hotbar-get_slot"></a><a id="fn-hotbar-set_slot"></a>`hotbar.get_slot(i)` returns name, count. `hotbar.set_slot(i, name [, count])` sets a slot.
- <a id="fn-hotbar-get_active_slot"></a><a id="fn-hotbar-set_active_slot"></a>`hotbar.get_active_slot()` / `set_active_slot(i)`
- <a id="fn-hotbar-get_size"></a><a id="fn-hotbar-get_slot_count"></a><a id="fn-hotbar-set_position"></a><a id="fn-hotbar-set_size"></a><a id="fn-hotbar-set_slot_size"></a>`hotbar.get_slot_count()` (same as `get_size()`), `set_size(n)`, `set_slot_size(px)`, `set_position(x, y)`
- <a id="fn-hotbar-get_slot_block"></a>`hotbar.get_slot_block(i)` returns just the name in a slot ("air" if empty)
- <a id="fn-hotbar-set_selected_texture"></a><a id="fn-hotbar-set_texture"></a><a id="fn-hotbar-set_texture_scale"></a>`hotbar.set_texture(path)`, `set_selected_texture(path)`, `set_texture_scale(s)`
- <a id="fn-hotbar-is_creative_open"></a><a id="fn-hotbar-toggle_creative"></a>`hotbar.is_creative_open()`, `toggle_creative()`

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
| `on_block_broken(x, y, z, block, meta)` | a player broke a block |
| `on_block_placed(x, y, z, block)` | a player placed a block |
| `on_pick_block(x, y, z, item, meta, block)` → bool | pick block (middle mouse); see [Pick block](#pick-block) |
| `on_use_hoe(...)`, `on_use_scythe(...)` | tool use |
| `update_player_animation(dt, moving, pitch)` | every frame in game |
| `get_water_color(x, z)` | underwater colour |
| <a id="fn-hooks-on_char_input"></a><a id="fn-hooks-on_scroll_input"></a>`on_char_input(codepoint)`, `on_scroll_input(dx, dy)` | text and mouse wheel |
| `toggle_main_inventory()`, `handle_inventory_click(...)`, `render_inventory(W, H)` | your own inventory |
| `render_*` / `handle_*` / `on_*_opened` | screens (section 14) |

`lua_api.txt` lists them all and marks the ones your loaded mods define.

## 17. Multiplayer and servers

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
- **Operator commands:** `/op`, `/gamemode`, `/kick`, `/ban`, `/whitelist`, `/tps`, `/save`, `/stop` and more. Type `help` in the server console.

### Game modes and permissions

On a server, **the server decides each player's game mode**, and only its admins can change it. A player who joins a survival server can't put themselves in creative.

- **Dedicated server:** `gamemode = survival` (or `creative`) in `server.conf`, or Game mode on the My Servers settings page, is the mode everyone starts in. Operators change a player's mode with `/gamemode survival|creative [name]`, and it's remembered in `gamemodes.txt`. Anyone can type `/gamemode` on its own to see their mode.
- **LAN game:** players who join get the host world's mode. Only the host can change it, with `/gamemode survival|creative <player>`.
- **Locked in the engine:** while you're on a server, `engine.set_creative` refuses anything but the mode the server gave you (it returns `false`), so no mod can switch you either.
- **Checked by the server:** outside creative, the server won't let a player break blocks that can't be dug (bedrock, `hardness = -1`), even with a modified game.

For mods:
- <a id="fn-engine-get_server_game_mode"></a>`engine.get_server_game_mode()` returns `"survival"` or `"creative"` (the mode the server gave you), or `nil` when you're not on a server.
- <a id="fn-engine-is_operator"></a>`engine.is_operator()` is true in singleplayer and when hosting. On a server, it says whether you're one of its operators, for example to show admin buttons.
- <a id="fn-hooks-on_game_mode"></a>`on_game_mode(mode, is_operator)` runs when the server sets your mode: when you join, and when an operator changes it. `mode` is `nil` after you leave the server. The survival mod uses this to follow the server.
- `on_chat_command`'s `is_operator` is your real operator status on a server, so mods can keep their own commands for admins.

Lua functions for multiplayer menus:
- <a id="fn-engine-host_game"></a><a id="fn-engine-join_server"></a><a id="fn-engine-stop_hosting"></a>`engine.join_server`, `host_game`, `stop_hosting`
- <a id="fn-engine-get_connection_status"></a><a id="fn-engine-get_network_info"></a><a id="fn-engine-get_player_list"></a>`engine.get_connection_status`, `get_network_info`, `get_player_list`
- <a id="fn-engine-get_lan_games"></a><a id="fn-engine-get_server_status"></a><a id="fn-engine-ping_server"></a>`engine.ping_server`, `get_server_status`, `get_lan_games`
- <a id="fn-engine-list_servers"></a><a id="fn-engine-save_servers"></a>`engine.list_servers`, `save_servers`
- <a id="fn-engine-get_player_name"></a><a id="fn-engine-has_player_password"></a><a id="fn-engine-set_player_name"></a><a id="fn-engine-set_player_password"></a>`engine.get_player_name`, `set_player_name`, `set_player_password`, `has_player_password`
- <a id="fn-engine-cancel_join"></a><a id="fn-engine-is_scanning_lan"></a>`engine.cancel_join` stops connecting; `engine.is_scanning_lan` is true while looking for LAN games
- <a id="fn-engine-get_server_settings"></a><a id="fn-engine-has_server_icon"></a><a id="fn-engine-set_server_setting"></a>`engine.get_server_settings`, `set_server_setting` (Settings > Server: description, max players, port), `has_server_icon`
- <a id="fn-engine-allow_firewall"></a>`engine.allow_firewall()` asks Windows again to let other PCs connect (for a "friends can't join" button)

## 18. Engine screens and settings

### Title screen look

A game styles the title screen with files in its `menu/` folder (`games/<game>/menu/`):

| File | What it is |
|---|---|
| `header.png` | The title picture (otherwise the game's title in text) |
| `icon.png` | The game's icon in the top bar |
| `button.png` | The picture for every button (otherwise plain grey boxes) |
| `button_hover.png` | Optional: the button under the mouse. Without it, `button.png` lights up. |

**No picture?** The engine looks for the game's `menu/button.png`, then `mods/base/menu/button.png` (shared by every game), and otherwise draws its own built-in button. So buttons always look like buttons, even with no texture files at all.

**Every menu uses it:** the pause menu, Settings, Singleplayer, Multiplayer, My Servers, Mods and Texture Packs draw their buttons with the same picture, so they all match the title screen. Disabled buttons are darker, Delete buttons are tinted red, and the open tab uses the hover look.

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
- <a id="fn-engine-create_world"></a><a id="fn-engine-delete_world"></a><a id="fn-engine-get_active_world"></a><a id="fn-engine-get_game_info"></a><a id="fn-engine-get_worlds"></a><a id="fn-engine-list_worlds"></a><a id="fn-engine-play_world"></a><a id="fn-engine-save_world"></a><a id="fn-engine-set_active_world"></a><a id="fn-engine-start_game"></a>**Worlds:** `engine.list_worlds`, `create_world`, `play_world`, `delete_world`, `save_world`, `get_game_info`, and the older `get_worlds`, `get_active_world`, `set_active_world`, `start_game`
- <a id="fn-engine-get_keybinds"></a><a id="fn-engine-get_video_settings"></a><a id="fn-engine-key_name"></a><a id="fn-engine-reset_keybinds"></a><a id="fn-engine-set_keybind"></a><a id="fn-engine-set_video_setting"></a>**Settings:** `engine.get_keybinds`, `set_keybind`, `reset_keybinds`, `key_name`, `get_video_settings`, `set_video_setting`
  - <a id="fn-hooks-handle_settings_mouse"></a>**Mouse buttons as controls:** keybinds store a mouse button as `1000 + button` (1002 = middle, 1003 and up = side buttons). `engine.key_name(1002)` returns `"Middle Mouse"`. Movement, jump, sneak, sprint and pick block can use mouse buttons, but not left or right, which dig and place. The engine calls `handle_settings_mouse(code)` when a mouse button is pressed on the Settings screen. Return true to use the press (the base settings menu binds it to the control waiting for a key).
- <a id="fn-engine-back_to_title"></a><a id="fn-engine-close_settings"></a><a id="fn-engine-is_in_game"></a><a id="fn-engine-is_paused"></a><a id="fn-engine-open_settings"></a><a id="fn-engine-pause"></a><a id="fn-engine-quit"></a><a id="fn-engine-quit_to_menu"></a><a id="fn-engine-resume"></a>**Game flow:** `engine.open_settings`, `close_settings`, `pause`, `resume`, `is_paused`, `is_in_game`, `quit_to_menu`, `back_to_title`, `quit`
- <a id="fn-engine-get_texture_pack"></a><a id="fn-engine-list_mods"></a><a id="fn-engine-list_texture_packs"></a><a id="fn-engine-open_mods_folder"></a><a id="fn-engine-open_texture_packs_folder"></a><a id="fn-engine-set_mod_enabled"></a><a id="fn-engine-set_texture_pack"></a>**Mods and texture packs:** `engine.list_mods`, `set_mod_enabled`, `open_mods_folder`, `list_texture_packs`, `set_texture_pack`, `get_texture_pack`, `open_texture_packs_folder`
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

## 21. Glossary

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
