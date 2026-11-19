-- inventory.lua — Creative Inventory
-- ---------------------------------------------------------------------------
-- A category sidebar on the left; a search box, grid / list view and pages on top; the items
-- in the middle; your hotbar at the bottom. Everything (layout, colours, categories) lives in
-- this file - edit freely.
--
--   click an item        pick up a stack (click a hotbar slot to put it there; click outside
--                        the window to throw it away)
--   click a hotbar slot  put down / pick up / swap
--   shift-click          a hotbar item: deletes it (like Minecraft); an item in the grid: a
--                        stack into your hotbar (the first empty slot)
--   Inventory tab        (bottom of the sidebar) switches to your survival inventory screen
--                        (equipment, crafting, storage); its "Creative" button comes back
--   mouse wheel          next / previous page          type   search (click the box first)
--   Left / Right         previous / next page          Esc    stop typing
--
-- Other mods can add categories or move items between them:
--   creative.register_category{ id = "magic", name = "Magic", icons = { "wand" },
--                               filter = function(name, info) return name:find("wand") end }
--   creative.set_category("ruby_lamp", "decoration")     -- one item, any category id
--
-- ...or give their own blocks and items a tab of their own in the sidebar (below the
-- built-in ones), like mods in Minecraft. From the mod's init.lua:
--   creative.register_tab{ name = "Magic", icon = "wand" }     -- every item this mod registers
-- See creative.register_tab below for picking the items yourself, or drawing a whole page.
--
-- C++ provides ui.draw_rect / draw_text / text_width / text_height / draw_block_icon,
-- ui.get_all_blocks(), ui.get_block_info(name), ui.get_mouse(), ui.get_time(), ui.keys,
-- ui.actions and hotbar.* (1-based). It calls render_creative_inventory(W, H),
-- handle_creative_click / _scroll / _key / _char and on_creative_opened / _closed.
-- ---------------------------------------------------------------------------

print("[UI] inventory.lua loaded")

-- ===========================================================================
-- THEME
-- ===========================================================================

local te = (function()   -- the caret in text boxes (lualib/textedit.lua)
    local ok, m = pcall(require, "textedit")
    if ok then return m end
    print("[UI] lualib/textedit.lua is missing: text boxes can only type at the end")
    local function view(_, t, w, sc)
        while #t > 0 and ui.text_width(t, sc) > w do t = t:sub(2) end
        return t, ui.text_width(t, sc)
    end
    return { reset = function() end, focus = function() end, view = view,
             key = function(_, t, k) if k == ui.keys.BACKSPACE then return t:sub(1, -2), true end return t, false end,
             insert = function(_, t, s, max) if max and #t >= max then return t end return t .. s end,
             draw_caret = function(_, x, y, h, r, g, b, a) if ui.get_time() % 1 < 0.5 then ui.draw_rect(x, y, 1, h, r, g, b, a) end end }
end)()

local C = {
    dim         = { 0.00, 0.00, 0.00, 0.55 },   -- over the world
    frame       = { 0.55, 0.60, 0.68, 1.00 },   -- outer border
    frame_hi    = { 0.72, 0.77, 0.84, 1.00 },
    frame_lo    = { 0.30, 0.34, 0.40, 1.00 },
    window      = { 0.17, 0.20, 0.24, 1.00 },   -- inside the window
    panel       = { 0.12, 0.14, 0.17, 1.00 },   -- sidebar / grid / hotbar backgrounds
    panel_hi    = { 0.26, 0.30, 0.36, 1.00 },
    panel_lo    = { 0.07, 0.08, 0.10, 1.00 },
    slot        = { 0.20, 0.23, 0.28, 1.00 },
    slot_hi     = { 0.30, 0.34, 0.41, 1.00 },
    slot_lo     = { 0.10, 0.11, 0.14, 1.00 },
    hover       = { 1.00, 1.00, 1.00, 0.16 },
    row         = { 0.18, 0.21, 0.26, 1.00 },   -- sidebar category
    row_hover   = { 0.23, 0.27, 0.33, 1.00 },
    accent      = { 0.20, 0.52, 0.92, 1.00 },   -- selected category / view button
    accent_hi   = { 0.45, 0.72, 1.00, 1.00 },
    accent_bg   = { 0.13, 0.30, 0.55, 1.00 },
    text        = { 0.93, 0.95, 0.97, 1.00 },
    text_dim    = { 0.60, 0.65, 0.72, 1.00 },
    text_faint  = { 0.42, 0.47, 0.54, 1.00 },
    field       = { 0.08, 0.09, 0.11, 1.00 },
    field_focus = { 0.45, 0.72, 1.00, 1.00 },
    tip_bg      = { 0.06, 0.07, 0.09, 0.96 },
    tip_border  = { 0.30, 0.55, 0.95, 0.80 },
}

local T = {                 -- sizes in GUI pixels (times the GUI scale)
    min_w = 600, min_h = 340,   -- the window: at least this, growing with the screen up to max
    max_w = 780, max_h = 430,
    pad = 6,
    sidebar_w = 132, cat_h = 26, cat_gap = 2,
    top_h = 22,
    slot = 30, slot_gap = 2,
    row_h = 22,             -- list view
    hotbar_h = 58,
    font = 0.5,             -- text scale at GUI scale 1
    stack_size = 64,        -- what you get from the creative grid
    max_search = 32,
}

-- ===========================================================================
-- CATEGORIES
-- Each item goes in the first category whose filter accepts it (All shows everything).
-- `icons`: the first of these that exists is the category's icon.
-- ===========================================================================

local function has(s, ...)
    for _, sub in ipairs({ ... }) do
        if string.find(s, sub, 1, true) then return true end
    end
    return false
end

-- A whole word of the name: word(n, "bed") matches "bed" and "red_bed", not "bedrock"
local function word(n, ...)
    for _, w in ipairs({ ... }) do
        for part in n:gmatch("[^_]+") do if part == w then return true end end
    end
    return false
end

local function is_food(name, info)
    if survival and survival.is_food and survival.is_food(name) then return true end
    return has(name, "apple", "bread", "beef", "pork", "chicken", "mutton", "cod", "salmon", "steak",
               "carrot", "potato", "cookie", "pie", "stew", "soup", "melon_slice", "berries", "cake")
end

local Categories = {
    { id = "all", name = "All Items", all = true },
    { id = "spawn_eggs", name = "Spawn Eggs", icons = { "spawn_egg" },
      filter = function(n) return has(n, "spawn_egg") end },
    { id = "combat", name = "Combat", icons = { "iron_sword", "sword", "bow" },
      filter = function(n, i) return (i and i.tool_type == "sword")
          or has(n, "sword", "bow", "arrow", "helmet", "chestplate", "leggings", "boots", "shield", "armor") end },
    { id = "tools", name = "Tools", icons = { "iron_pickaxe", "pickaxe", "scythe", "hoe" },
      filter = function(n, i) return (i and i.tool_type ~= nil and i.tool_type ~= "")
          or has(n, "pickaxe", "shovel", "_axe", "hoe", "scythe", "shears", "fishing_rod", "flint_and_steel") end },
    { id = "food", name = "Food", icons = { "apple", "bread", "apple_pie" },
      filter = function(n, i) return is_food(n, i) end },
    { id = "redstone", name = "Redstone", icons = { "redstone", "redstone_block", "lever" },
      filter = function(n) return has(n, "redstone", "piston", "lever", "button", "repeater", "comparator",
                                      "hopper", "dispenser", "dropper", "observer", "tnt", "rail", "pressure_plate") end },
    { id = "utility", name = "Utility", icons = { "bucket", "workbench", "chest" },
      filter = function(n) return has(n, "bucket", "workbench", "crafting", "furnace", "chest", "barrel",
                                      "anvil", "ladder", "smoker", "enchant") or word(n, "bed") end },
    { id = "nature", name = "Nature", icons = { "sapling_oak", "sapling", "flower_red", "leaves_oak" },
      filter = function(n, i) return not word(n, "pot") and ((i and i.pattern == "plantlike") or has(n, "leaves", "sapling",
          "flower", "grass_tuft", "short_grass", "fern", "mushroom", "cactus", "pumpkin", "melon",
          "vine", "seeds", "wheat", "reeds", "sugar_cane", "lily")) end },
    { id = "decoration", name = "Decoration", icons = { "lantern", "torch", "glass" },
      filter = function(n, i) return (i and (i.light or 0) > 0) or has(n, "torch", "lantern", "lamp", "glass",
          "carpet", "painting", "banner", "candle", "glowstone", "sea_lantern") or word(n, "pot", "flowerpot") end },
    { id = "blocks", name = "Blocks", icons = { "grass", "grass_block", "dirt" },
      filter = function(n, i) return not (i and i.item) and (has(n, "stone", "dirt", "grass", "sand", "gravel",
          "clay", "bedrock", "obsidian", "netherrack", "farmland", "podzol") or word(n, "ore", "log", "snow", "ice", "mud")) end },
    { id = "building", name = "Building", icons = { "bricks", "planks_oak", "cobblestone" },
      filter = function(n, i) return not (i and i.item) or has(n, "door", "fence", "stairs", "slab") end },
    { id = "misc", name = "Miscellaneous", icons = { "stick", "coal", "diamond" },
      filter = function() return true end },
    -- The player's own inventory (survival mod): not a list of items, kept at the bottom
    { id = "inventory", name = "Inventory", special = true, icons = { "chest" } },
}

local overrides = {}       -- item name -> category id (creative.set_category)
local ModTabs = {}         -- tabs from creative.register_tab, in order (also in Categories)

-- Which mod registered each block. Newer engines say so (ui.get_block_info(name).mod); on older
-- ones, the blocks registered from here on are noted as they're registered.
local registered_by = {}
local function mod_folder() return (g_CurrentModPath or ""):match("([^/\\]+)[/\\]*$") or "" end
if engine and engine.register_block and not creative_wrapped_register then
    creative_wrapped_register = true
    local register_block = engine.register_block
    engine.register_block = function(name, ...)
        if type(name) == "string" then registered_by[name] = mod_folder() end
        return register_block(name, ...)
    end
end
local function mod_of(name, info)
    if info and info.mod and info.mod ~= "" then return info.mod end
    return registered_by[name] or ""
end

creative = creative or {}
-- creative.register_category{ id, name, icons, filter [, before = "id"] }: a new category
-- (placed before the `before` one, else before Miscellaneous)
function creative.register_category(def)
    assert(type(def) == "table" and def.id and def.name, "creative.register_category needs { id, name, ... }")
    def.filter = def.filter or function() return false end
    local at = #Categories
    for i, c in ipairs(Categories) do if c.id == (def.before or "misc") then at = i break end end
    table.insert(Categories, at, def)
end
-- creative.set_category(item, id): put one item in a category
function creative.set_category(item, id) overrides[item] = id end

local function block_info(name) end   -- (defined below)
local cat_cache, all_cache_reset

-- creative.register_tab{ name [, id, icon, mod, items, filter, exclusive, draw, width, height] }
--   A tab of the mod's own in the creative inventory's sidebar, under the built-in ones.
--   Which items it shows (pick one; the default is the first):
--     (nothing)      every block and item the calling mod registers
--     mod = "farming" or { "farming", "farming_plus" }: everything those mods register
--     items = { "ruby", "ruby_block", "ruby_sword" }: exactly these, in this order
--     filter = function(name, info) return ... end: your own test
--   icon      an item shown on the tab (default: its first item)
--   exclusive true (default): its items are only in this tab, not also in Blocks, Tools...
--             (they're always in All Items and in search results)
--   draw      function(f, state): instead of an item grid, a whole page of your own, drawn
--             with ui_kit (lualib/ui_kit.lua) in the items area; width, height: its size in
--             units (left out: f.w_units, f.h_units say how much room there is)
--   Returns the tab's id. Call it again with the same id to replace it.
function creative.register_tab(def)
    assert(type(def) == "table" and def.name, "creative.register_tab needs at least { name = ... }")
    local this_mod = mod_folder()
    local id = def.id or ("tab:" .. (def.mod and tostring(def.mod) or this_mod) .. ":" .. def.name)
    local tab = { id = id, name = def.name, icons = def.icon and { def.icon } or nil, tab = true,
                  exclusive = def.exclusive ~= false, before = def.before }
    if def.items then
        local order = {}
        for i, n in ipairs(def.items) do order[n] = i end
        tab.order = order
        tab.filter = function(name) return order[name] ~= nil end
    elseif def.filter then
        tab.filter = def.filter
    else
        local mods = {}
        local m = def.mod or this_mod
        for _, x in ipairs(type(m) == "table" and m or { m }) do mods[x] = true end
        tab.filter = function(name, info) return mods[mod_of(name, info)] == true end
    end
    if def.draw then
        local ok, kit = pcall(require, "ui_kit")
        assert(ok, "a creative tab with draw needs lualib/ui_kit.lua: " .. tostring(kit))
        tab.page = kit.embed{ id = "creative:" .. id, width = def.width, height = def.height,
                              state = def.state or {}, draw = def.draw }
        tab.page_units = def.width ~= nil and def.height ~= nil
        if not def.items and not def.filter and not def.mod then tab.filter = function() return false end end
    end
    for i, c in ipairs(Categories) do                     -- replacing one with the same id
        if c.id == id then table.remove(Categories, i) break end
    end
    for i, c in ipairs(ModTabs) do if c.id == id then table.remove(ModTabs, i) break end end
    ModTabs[#ModTabs + 1] = tab
    local at = #Categories + 1
    for i, c in ipairs(Categories) do if c.special then at = i break end end   -- above Inventory
    table.insert(Categories, at, tab)
    if all_cache_reset then all_cache_reset() end
    return id
end

-- creative.hold(name [, count]): put an item on the cursor, as if picked from the grid (a custom
-- page uses this, so the player can then put it in the hotbar). creative.get_held() -> name, count
function creative.hold(name, count) end       -- (defined below, next to the state)
function creative.get_held() end

-- ===========================================================================
-- STATE AND CACHES
-- ===========================================================================

local state = {
    cat = 1,                -- index into Categories
    search = "",
    search_focused = false,
    page = 1,
    list_view = false,
    held = nil,             -- { name, count } on the cursor
}

local info_cache, all_cache = {}, nil
cat_cache = {}
all_cache_reset = function() info_cache, cat_cache, all_cache = {}, {}, nil end

function creative.hold(name, count)
    state.held = name and { name = name, count = count or T.stack_size } or nil
end
function creative.get_held()
    if state.held then return state.held.name, state.held.count end
    return nil
end

function block_info(name)
    local v = info_cache[name]
    if v == nil then
        v = (ui.get_block_info and ui.get_block_info(name)) or false
        info_cache[name] = v
    end
    return v or nil
end

local function hidden(name)
    if name == "air" then return true end
    -- fluids themselves aren't items (their buckets are)
    return (has(name, "water", "lava") and not has(name, "bucket")) or has(name, "flowing")
end

-- The category an item belongs to (not "all")
local function category_of(name)
    local c = cat_cache[name]
    if c then return c end
    local info = block_info(name)
    c = overrides[name]
    if not c then                                         -- mods' own tabs first...
        for _, tab in ipairs(ModTabs) do
            if tab.exclusive and tab.filter(name, info) then c = tab.id break end
        end
    end
    if not c then                                         -- ...then the built-in categories
        for _, cat in ipairs(Categories) do
            if not cat.all and not cat.special and not cat.tab and cat.filter(name, info) then c = cat.id break end
        end
    end
    c = c or "misc"
    cat_cache[name] = c
    return c
end

local function cat_index(id)
    for i, c in ipairs(Categories) do if c.id == id then return i end end
    return #Categories
end

-- Is this item shown in that category / tab?
local function in_cat(name, cat)
    if cat.all then return true end
    if cat.tab and not cat.exclusive then return cat.filter(name, block_info(name)) end
    return category_of(name) == cat.id
end

-- Every creative item, in category order (registration order inside each)
local function all_items()
    if all_cache then return all_cache end
    local list = {}
    for i, name in ipairs(ui.get_all_blocks and ui.get_all_blocks() or {}) do
        if not hidden(name) then list[#list + 1] = { name = name, order = i } end
    end
    table.sort(list, function(a, b)
        local ca, cb = cat_index(category_of(a.name)), cat_index(category_of(b.name))
        if ca ~= cb then return ca < cb end
        local tab = Categories[ca]
        if tab and tab.order then                         -- a tab with a list: its order
            return (tab.order[a.name] or 0) < (tab.order[b.name] or 0)
        end
        return a.order < b.order
    end)
    all_cache = {}
    for i, e in ipairs(list) do all_cache[i] = e.name end
    return all_cache
end

local function pretty(name)
    return (name:gsub("_", " "):gsub("(%a)(%w*)", function(a, b) return a:upper() .. b end))
end

-- What's shown: the chosen category, or search results across everything
local function shown_items()
    local q = state.search:lower()
    local cat = Categories[state.cat]
    local out = {}
    for _, name in ipairs(all_items()) do
        local ok
        if q ~= "" then
            ok = has(name:lower(), q) or has(pretty(name):lower(), q)
        else
            ok = in_cat(name, cat)
        end
        if ok then out[#out + 1] = name end
    end
    return out
end

local function count_in(cat)
    if cat.all then return #all_items() end
    local n = 0
    for _, name in ipairs(all_items()) do if in_cat(name, cat) then n = n + 1 end end
    return n
end

-- ===========================================================================
-- THE PLAYER'S INVENTORY (Inventory tab) - slots: hotbar, storage or equipment
-- ===========================================================================

-- survival.inventory once it has loaded this world's inventory (nil before, or without the mod)
local function player_inv()
    local inv = survival and survival.inventory
    if not inv or not inv.get then return nil end
    if inv.is_ready and not inv.is_ready() then return nil end
    return inv
end

-- ref = { kind = "hb", i } | { kind = "st", i } | { kind = "eq", slot }
local function slot_get(r)
    local inv = player_inv()
    local name, count
    if r.kind == "hb" then name, count = hotbar.get_slot(r.i)
    elseif r.kind == "st" and inv then name, count = inv.get(r.i)
    elseif r.kind == "eq" and inv then name = inv.get_equipment(r.slot); count = name and 1 or 0 end
    if not name or name == "" or not count or count <= 0 then return nil, 0 end
    return name, count
end

local function slot_set(r, name, count)
    local inv = player_inv()
    if not name or not count or count <= 0 then name, count = "", 0 end
    if r.kind == "hb" then hotbar.set_slot(r.i, name, count)
    elseif r.kind == "st" and inv then inv.set(r.i, name, count)
    elseif r.kind == "eq" and inv and inv.set_equipment then inv.set_equipment(r.slot, name) end
end

local function stack_max(name)
    local inv = player_inv()
    return (inv and inv.stack_max) and inv.stack_max(name) or T.stack_size
end
local function slot_max(r, name) return r.kind == "eq" and 1 or stack_max(name) end
local function slot_accepts(r, name)
    if r.kind ~= "eq" then return true end
    local inv = player_inv()
    return inv ~= nil and inv.fits_equipment ~= nil and inv.fits_equipment(r.slot, name)
end

-- Into the hotbar: onto the same item first, then empty slots. Returns what didn't fit.
local function into_hotbar(name, count)
    local n = hotbar.get_slot_count()
    for pass = 1, 2 do
        for i = 1, n do
            if count <= 0 then return 0 end
            local hn, hc = hotbar.get_slot(i)
            local empty = not hn or hn == "" or (hc or 0) <= 0
            if (pass == 1 and not empty and hn == name) or (pass == 2 and empty) then
                local have = empty and 0 or hc
                local put = math.min(count, stack_max(name) - have)
                if put > 0 then hotbar.set_slot(i, name, have + put) count = count - put end
            end
        end
    end
    return count
end

-- A click on a slot: Minecraft's rules (pick up / put down / merge / swap; shift moves)
local function click_slot(r, shift, inventory_tab)
    local name, count = slot_get(r)
    local held = state.held
    if shift and not held then
        if not name then return end
        local inv = player_inv()
        if r.kind == "hb" then
            if inventory_tab and inv then                    -- to storage
                local left = inv.add(name, count)
                slot_set(r, name, left)
            else
                slot_set(r, nil)                             -- other tabs: deleted, like Minecraft
            end
        elseif r.kind == "st" then                           -- to the hotbar
            slot_set(r, name, into_hotbar(name, count))
        elseif r.kind == "eq" and inv then                   -- take it off, into storage
            if inv.add(name, 1) == 0 then slot_set(r, nil) end
        end
        return
    end
    if held then
        if not slot_accepts(r, held.name) then return end
        local max = slot_max(r, held.name)
        if not name then                                     -- put down (as many as fit)
            local put = math.min(held.count, max)
            slot_set(r, held.name, put)
            state.held = (held.count - put > 0) and { name = held.name, count = held.count - put } or nil
        elseif name == held.name then                        -- merge
            local put = math.min(held.count, max - count)
            if put > 0 then
                slot_set(r, name, count + put)
                state.held = (held.count - put > 0) and { name = name, count = held.count - put } or nil
            end
        elseif held.count <= max then                        -- swap
            slot_set(r, held.name, held.count)
            state.held = { name = name, count = count }
        end
    elseif name then                                         -- pick up
        state.held = { name = name, count = count }
        slot_set(r, nil)
    end
end

-- ===========================================================================
-- DRAWING HELPERS
-- ===========================================================================

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local function inside(mx, my, x, y, w, h) return mx >= x and mx < x + w and my >= y and my < y + h end

local function rect(x, y, w, h, c, a)
    if w <= 0 or h <= 0 then return end
    ui.draw_rect(x, y, w, h, c[1], c[2], c[3], a or c[4] or 1)
end

local function th(scale) return ui.text_height and ui.text_height(scale) or 14 * scale end
local function tw(s, scale) return ui.text_width and ui.text_width(s, scale) or #s * 10 * scale end

-- Text with `y` = the top of the capitals
local function text(s, x, y, scale, c, shadow, S)
    y = y + th(scale)
    if shadow then ui.draw_text(s, x + S, y + S, scale, 0, 0, 0, 0.6) end
    ui.draw_text(s, x, y, scale, c[1], c[2], c[3], c[4] or 1)
end

-- Shortens text to fit `w` pixels (adds "..")
local function fit(s, w, scale)
    if tw(s, scale) <= w then return s end
    while #s > 1 and tw(s .. "..", scale) > w do s = s:sub(1, -2) end
    return s .. ".."
end

-- A box with a 1-pixel light top-left edge and dark bottom-right edge
local function bevel(x, y, w, h, S, fill, hi, lo)
    rect(x, y, w, h, lo)
    rect(x, y, w - S, h - S, hi)
    rect(x + S, y + S, w - 2 * S, h - 2 * S, fill)
end
-- A sunken box (dark top-left, light bottom-right)
local function sunken(x, y, w, h, S, fill, hi, lo)
    rect(x, y, w, h, hi)
    rect(x, y, w - S, h - S, lo)
    rect(x + S, y + S, w - 2 * S, h - 2 * S, fill)
end
local function outline(x, y, w, h, t, c)
    rect(x, y, w, t, c); rect(x, y + h - t, w, t, c)
    rect(x, y + t, t, h - 2 * t, c); rect(x + w - t, y + t, t, h - 2 * t, c)
end

local function draw_slot(x, y, size, S) sunken(x, y, size, size, S, C.slot, C.slot_hi, C.slot_lo) end

local function draw_count(count, x, y, size, fs, S)
    if not count or count <= 1 then return end
    local s = tostring(count)
    text(s, x + size - tw(s, fs) - 2 * S, y + size - th(fs) - 3 * S, fs, C.text, true, S)
end

-- Little pictures drawn from rectangles (no image files needed)
local function icon_grid(x, y, size, c)                 -- 3 x 3 squares
    local cell = size / 3
    for i = 0, 2 do for j = 0, 2 do
        rect(x + i * cell + cell * 0.15, y + j * cell + cell * 0.15, cell * 0.7, cell * 0.7, c)
    end end
end
local function icon_bag(x, y, size, c)                  -- a backpack
    local u = size / 10
    rect(x + 3.5 * u, y + 0.5 * u, 3 * u, 1.2 * u, c)
    rect(x + 1.5 * u, y + 2 * u, 7 * u, 7.5 * u, c)
    rect(x + 3 * u, y + 4.5 * u, 4 * u, 1 * u, { 0.12, 0.14, 0.17, 1 })
end
local function icon_list(x, y, size, c)                 -- 3 bars
    for j = 0, 2 do rect(x, y + size * (0.12 + j * 0.33), size, size * 0.18, c) end
end
local function icon_search(x, y, size, c, S)           -- magnifying glass
    local r = size * 0.62
    outline(x, y, r, r, math.max(1, math.floor(1.5 * S)), c)
    for k = 0, 2 do
        local d = r - S + k * size * 0.13
        rect(x + d, y + d, size * 0.16, size * 0.16, c)
    end
end
local function arrow(x, y, size, left, c, S)           -- a solid triangle pointing < or >
    local rows = math.max(3, math.floor(size / S))
    local rh = size / rows
    local depth = size * 0.45
    for k = 0, rows - 1 do
        local f = 1 - math.abs((k + 0.5) / rows * 2 - 1)    -- 0 at the ends, 1 in the middle
        local w = depth * f
        local ax = x + (size - depth) / 2
        rect(left and (ax + depth - w) or ax, y + k * rh, w, rh + 0.5, c)
    end
end

local function category_icon(cat)
    if cat.icon_cache ~= nil then return cat.icon_cache or nil end
    local found = false
    for _, n in ipairs(cat.icons or {}) do
        local info = block_info(n)
        if info and info.has_texture then found = n break end
    end
    if not found and not cat.all and not cat.special then   -- else: the category's first item
        for _, n in ipairs(all_items()) do
            if in_cat(n, cat) then found = n break end
        end
    end
    cat.icon_cache = found
    return found or nil
end

local function draw_tooltip(lines, mx, my, W, H, S, fs)
    local pad = 4 * S
    local w = 0
    for _, l in ipairs(lines) do w = math.max(w, tw(l[1], l[3] or fs)) end
    w = w + pad * 2
    local h = pad * 2 + (#lines - 1) * 4 * S
    for _, l in ipairs(lines) do h = h + th(l[3] or fs) end
    local x, y = mx + 12 * S, my - h - 4 * S
    if x + w > W then x = mx - 12 * S - w end
    y = clamp(y, 0, H - h)
    rect(x, y, w, h, C.tip_bg)
    outline(x, y, w, h, S, C.tip_border)
    local ty = y + pad
    for _, l in ipairs(lines) do
        text(l[1], x + pad, ty, l[3] or fs, l[2], true, S)
        ty = ty + th(l[3] or fs) + 4 * S
    end
end

-- ===========================================================================
-- LAYOUT (shared by drawing and clicking)
-- ===========================================================================

local function layout(W, H)
    local S = clamp(math.floor(math.min(W / (T.min_w + 40), H / (T.min_h + 40)) * 2) / 2, 1, 4)
    local L = { S = S, fs = T.font * S, fs_small = T.font * 0.8 * S }
    L.w = math.floor(clamp(W / S - 40, T.min_w, T.max_w)) * S      -- bigger screens: more slots
    L.h = math.floor(clamp(H / S - 40, T.min_h, T.max_h)) * S
    L.x, L.y = math.floor((W - L.w) / 2), math.floor((H - L.h) / 2)
    local p = T.pad * S
    L.p = p

    -- Sidebar
    L.sideX, L.sideY = L.x + p, L.y + p
    L.sideW, L.sideH = T.sidebar_w * S, L.h - 2 * p
    L.catH, L.catGap = T.cat_h * S, T.cat_gap * S

    -- Right side
    L.rx = L.sideX + L.sideW + p
    L.rw = L.x + L.w - p - L.rx
    L.topY, L.topH = L.y + p, T.top_h * S

    -- Top bar: [ search ............ ] [grid] [list]   [<] 1 / 3 [>]
    local b = L.topH
    L.pagerW = b * 2 + 46 * S
    L.pagerX = L.rx + L.rw - L.pagerW
    L.listBtnX = L.pagerX - 8 * S - b
    L.gridBtnX = L.listBtnX - 3 * S - b
    L.searchX = L.rx
    L.searchW = L.gridBtnX - 6 * S - L.searchX
    L.prevX, L.nextX = L.pagerX, L.pagerX + L.pagerW - b

    -- Hotbar section at the bottom, items in between
    L.hotH = T.hotbar_h * S
    L.hotY = L.y + L.h - p - L.hotH
    L.areaX, L.areaY = L.rx, L.topY + L.topH + p
    L.areaW, L.areaH = L.rw, L.hotY - p - L.areaY

    L.slot, L.pitch = T.slot * S, (T.slot + T.slot_gap) * S
    local inner = 4 * S
    L.cols = math.max(1, math.floor((L.areaW - 2 * inner + T.slot_gap * S) / L.pitch))
    L.rows = math.max(1, math.floor((L.areaH - 2 * inner + T.slot_gap * S) / L.pitch))
    local gw, gh = L.cols * L.pitch - T.slot_gap * S, L.rows * L.pitch - T.slot_gap * S
    L.gridX = L.areaX + math.floor((L.areaW - gw) / 2)
    L.gridY = L.areaY + math.floor((L.areaH - gh) / 2)

    L.rowH = T.row_h * S                    -- list view
    L.listRows = math.max(1, math.floor((L.areaH - 2 * inner) / L.rowH))
    L.perPage = state.list_view and L.listRows or (L.cols * L.rows)
    return L
end

-- The sidebar's entries and where they are: categories from the top (empty ones left out),
-- then mods' tabs under a line, and the Inventory tab pinned to the bottom. When they don't
-- all fit, the list scrolls (mouse wheel over the sidebar): L.list is the visible part.
local function sidebar(L)
    local S = L.S
    local x, w, h = L.sideX + 3 * S, L.sideW - 6 * S, L.catH
    local inv_cat
    for _, cat in ipairs(Categories) do if cat.special then inv_cat = cat end end
    local has_inv = inv_cat and survival and survival.inventory
    local top = L.sideY + 3 * S
    local bottom = L.sideY + L.sideH - 3 * S - (has_inv and (h + L.catGap + 3 * S) or 0)
    L.list = { x = L.sideX + S, y = top, w = L.sideW - 2 * S, h = bottom - top }

    local out, y, divider = {}, 0, false
    for _, cat in ipairs(Categories) do
        if not cat.special and (cat.all or cat.page or count_in(cat) > 0) then
            if cat.tab and not divider then                 -- a line between built-in and mods' tabs
                divider = true
                if #out > 0 then
                    out[#out + 1] = { divider = true, y = y + 3 * S }
                    y = y + 8 * S
                end
            end
            out[#out + 1] = { cat = cat, x = x, y = y, w = w, h = h }
            y = y + h + L.catGap
        end
    end
    local total = y - L.catGap
    L.side_max = math.max(0, total - L.list.h)
    state.side_scroll = clamp(state.side_scroll or 0, 0, L.side_max)
    for _, e in ipairs(out) do e.y = e.y + top - state.side_scroll end
    if has_inv then
        out[#out + 1] = { cat = inv_cat, x = x, y = L.sideY + L.sideH - 3 * S - h, w = w, h = h, pinned = true }
    end
    return out
end


local function hotbar_slots(L)
    local n = (hotbar and hotbar.get_slot_count and hotbar.get_slot_count()) or 9
    local S = L.S
    local pitch = L.pitch
    local x0 = L.rx + math.floor((L.rw - (n * pitch - T.slot_gap * S)) / 2)    -- centred
    local y0 = L.hotY + 4 * S + th(L.fs_small) + 5 * S
    local out = {}
    for i = 1, n do out[i] = { x = x0 + (i - 1) * pitch, y = y0 } end
    return out
end

local function pages(L, count) return math.max(1, math.ceil(count / L.perPage)) end

-- Which item is under the mouse (grid or list), or nil
local function item_at(L, items, mx, my)
    local first = (state.page - 1) * L.perPage
    if state.list_view then
        local x, y = L.areaX + 4 * L.S, L.areaY + 4 * L.S
        if not inside(mx, my, x, y, L.areaW - 8 * L.S, L.listRows * L.rowH) then return nil end
        local r = math.floor((my - y) / L.rowH)
        return items[first + r + 1]
    end
    if not inside(mx, my, L.gridX, L.gridY, L.cols * L.pitch, L.rows * L.pitch) then return nil end
    local c, r = math.floor((mx - L.gridX) / L.pitch), math.floor((my - L.gridY) / L.pitch)
    if mx - (L.gridX + c * L.pitch) >= L.slot or my - (L.gridY + r * L.pitch) >= L.slot then return nil end
    return items[first + r * L.cols + c + 1]
end

-- ===========================================================================
-- RENDER
-- ===========================================================================

local last_layout = nil     -- (for the mouse wheel, which comes without the window size)

function render_creative_inventory(W, H)
    if not ui or not ui.draw_rect then return end
    local L = layout(W, H)
    last_layout = L
    local S, fs, small = L.S, L.fs, L.fs_small
    local mx, my = ui.get_mouse()
    local items = shown_items()
    local npages = pages(L, #items)
    state.page = clamp(state.page, 1, npages)
    local tip = nil

    -- Window
    rect(0, 0, W, H, C.dim)
    bevel(L.x - 2 * S, L.y - 2 * S, L.w + 4 * S, L.h + 4 * S, S, C.frame, C.frame_hi, C.frame_lo)
    rect(L.x, L.y, L.w, L.h, C.window)

    -- Sidebar: categories (those with no items are left out, except All)
    sunken(L.sideX, L.sideY, L.sideW, L.sideH, S, C.panel, C.panel_hi, C.panel_lo)
    local entries = sidebar(L)
    local clipped = false
    for _, e in ipairs(entries) do
      if e.divider then
        rect(L.sideX + 8 * S, e.y, L.sideW - 16 * S, S, C.panel_hi)
      elseif e.pinned or (e.y + e.h > L.list.y and e.y < L.list.y + L.list.h) then
        if not e.pinned and not clipped and ui.push_clip then ui.push_clip(L.list.x, L.list.y, L.list.w, L.list.h) clipped = true end
        if e.pinned and clipped then ui.pop_clip() clipped = false end
        local cat, x, y, w, h = e.cat, e.x, e.y, e.w, e.h
        local selected = (Categories[state.cat] == cat) and state.search == ""
        local hov = inside(mx, my, x, y, w, h) and (e.pinned or inside(mx, my, L.list.x, L.list.y, L.list.w, L.list.h))
        if selected then
            rect(x, y, w, h, C.accent_bg)
            outline(x, y, w, h, S, C.accent_hi)
        else
            bevel(x, y, w, h, S, hov and C.row_hover or C.row, C.panel_hi, C.panel_lo)
        end
        local isz = h - 8 * S
        if cat.all then
            icon_grid(x + 5 * S, y + 4 * S, isz, C.text)
        elseif cat.special and not category_icon(cat) then
            icon_bag(x + 5 * S, y + 4 * S, isz, C.text)
        else
            local icon = category_icon(cat)
            if icon then ui.draw_block_icon(icon, x + 5 * S, y + 4 * S, isz) end
        end
        local tx = x + 10 * S + isz
        local n = (cat.special or cat.page) and "" or tostring(count_in(cat))
        local nw = tw(n, small)
        text(fit(cat.name, w - (tx - x) - nw - 8 * S, fs), tx, y + (h - th(fs)) / 2, fs,
             selected and C.text or C.text, true, S)
        text(n, x + w - nw - 5 * S, y + (h - th(small)) / 2, small, selected and C.accent_hi or C.text_faint, false, S)
      end
    end
    if clipped then ui.pop_clip() end
    if L.side_max > 0 then                                  -- where in the list we are
        local bh = math.max(12 * S, L.list.h * L.list.h / (L.list.h + L.side_max))
        local by = L.list.y + (L.list.h - bh) * state.side_scroll / L.side_max
        rect(L.sideX + L.sideW - 3 * S, by, 2 * S, bh, C.text_faint)
    end

    -- Search box
    local sx, sy, sw, sh = L.searchX, L.topY, L.searchW, L.topH
    sunken(sx, sy, sw, sh, S, C.field, C.panel_hi, C.panel_lo)
    if state.search_focused then outline(sx, sy, sw, sh, S, C.field_focus) end
    local isz = sh - 10 * S
    icon_search(sx + 6 * S, sy + 5 * S, isz, state.search_focused and C.text or C.text_dim, S)
    local tx, ty = sx + 12 * S + isz, sy + (sh - th(fs)) / 2
    local clear_w = (state.search ~= "") and sh or 0
    if state.search == "" and not state.search_focused then
        text("Search items...", tx, ty, fs, C.text_faint, false, S)
    else
        local avail = sw - (tx - sx) - clear_w - 6 * S
        local shown, caret_x = te.view("creative:search", state.search, avail, fs)
        text(shown, tx, ty, fs, C.text, false, S)
        if state.search_focused then
            te.draw_caret("creative:search", tx + caret_x, ty - S, th(fs) + 2 * S, C.text[1], C.text[2], C.text[3], 1)
        end
    end
    if state.search ~= "" then
        local hov = inside(mx, my, sx + sw - sh, sy, sh, sh)
        text("x", sx + sw - sh + (sh - tw("x", fs)) / 2, ty, fs, hov and C.text or C.text_dim, false, S)
    end

    local page = state.search == "" and Categories[state.cat].page
    if page then
        -- A mod's own page (creative.register_tab with draw), drawn with ui_kit
        sunken(L.areaX, L.areaY, L.areaW, L.areaH, S, C.panel, C.panel_hi, C.panel_lo)
        if not Categories[state.cat].page_units then page.unit = 16 * S end
        page:draw(L.areaX + 4 * S, L.areaY + 4 * S, L.areaW - 8 * S, L.areaH - 8 * S)
    else
    -- Grid / list buttons
    for _, b in ipairs({ { L.gridBtnX, false }, { L.listBtnX, true } }) do
        local bx, on = b[1], (state.list_view == b[2])
        local hov = inside(mx, my, bx, sy, sh, sh)
        if on then
            rect(bx, sy, sh, sh, C.accent_bg); outline(bx, sy, sh, sh, S, C.accent_hi)
        else
            bevel(bx, sy, sh, sh, S, hov and C.row_hover or C.row, C.panel_hi, C.panel_lo)
        end
        local g = sh - 10 * S
        if b[2] then icon_list(bx + 5 * S, sy + 5 * S, g, C.text) else icon_grid(bx + 5 * S, sy + 5 * S, g, C.text) end
        if hov then tip = { { b[2] and "List view" or "Grid view", C.text } } end
    end

    -- Pager
    sunken(L.pagerX, sy, L.pagerW, sh, S, C.field, C.panel_hi, C.panel_lo)
    for _, b in ipairs({ { L.prevX, true, state.page > 1 }, { L.nextX, false, state.page < npages } }) do
        local hov = b[3] and inside(mx, my, b[1], sy, sh, sh)
        bevel(b[1], sy, sh, sh, S, hov and C.row_hover or C.row, C.panel_hi, C.panel_lo)
        arrow(b[1] + 5 * S, sy + 5 * S, sh - 10 * S, b[2], b[3] and C.text or C.text_faint, S)
    end
    local pg = state.page .. " / " .. npages
    text(pg, L.pagerX + (L.pagerW - tw(pg, fs)) / 2, ty, fs, C.text, false, S)

    -- Items
    sunken(L.areaX, L.areaY, L.areaW, L.areaH, S, C.panel, C.panel_hi, C.panel_lo)
    local first = (state.page - 1) * L.perPage
    local hovered = item_at(L, items, mx, my)
    if state.list_view then
        local x, w = L.areaX + 4 * S, L.areaW - 8 * S
        for r = 0, L.listRows - 1 do
            local name = items[first + r + 1]
            if not name then break end
            local y = L.areaY + 4 * S + r * L.rowH
            rect(x, y, w, L.rowH - S, (r % 2 == 0) and C.slot or C.row)
            if name == hovered then rect(x, y, w, L.rowH - S, C.hover) end
            local isz2 = L.rowH - 6 * S
            ui.draw_block_icon(name, x + 4 * S, y + 2 * S, isz2)
            text(fit(pretty(name), w * 0.6, fs), x + 10 * S + isz2, y + (L.rowH - th(fs)) / 2, fs, C.text, true, S)
            local cn = Categories[cat_index(category_of(name))].name
            text(cn, x + w - tw(cn, small) - 6 * S, y + (L.rowH - th(small)) / 2, small, C.text_dim, false, S)
        end
    else
        for r = 0, L.rows - 1 do
            for c = 0, L.cols - 1 do
                local x, y = L.gridX + c * L.pitch, L.gridY + r * L.pitch
                draw_slot(x, y, L.slot, S)
                local name = items[first + r * L.cols + c + 1]
                if name then
                    ui.draw_block_icon(name, x + 4 * S, y + 4 * S, L.slot - 8 * S)
                    if name == hovered then rect(x + S, y + S, L.slot - 2 * S, L.slot - 2 * S, C.hover) end
                end
            end
        end
    end
    if #items == 0 then
        local msg = state.search ~= "" and ("Nothing matches \"" .. state.search .. "\"") or "Nothing here yet"
        text(msg, L.areaX + (L.areaW - tw(msg, fs)) / 2, L.areaY + L.areaH / 2 - th(fs) / 2, fs, C.text_dim, false, S)
    end
    if hovered then
        tip = { { pretty(hovered), C.text },
                { Categories[cat_index(category_of(hovered))].name, C.accent_hi, small } }
    end
    end   -- (not a page)

    -- Hotbar
    sunken(L.rx, L.hotY, L.rw, L.hotH, S, C.panel, C.panel_hi, C.panel_lo)
    local hs = hotbar_slots(L)
    text("HOTBAR", L.rx + (L.rw - tw("HOTBAR", small)) / 2, L.hotY + 4 * S, small, C.text_dim, false, S)
    for i, s in ipairs(hs) do
        draw_slot(s.x, s.y, L.slot, S)
        local name, count = hotbar.get_slot(i)
        if name and name ~= "" then
            ui.draw_block_icon(name, s.x + 4 * S, s.y + 4 * S, L.slot - 8 * S)
            draw_count(count, s.x, s.y, L.slot, small, S)
        end
        if inside(mx, my, s.x, s.y, L.slot, L.slot) then
            rect(s.x + S, s.y + S, L.slot - 2 * S, L.slot - 2 * S, C.hover)
            if name and name ~= "" then tip = { { pretty(name), C.text } } end
        end
        local num = tostring(i % 10)
        text(num, s.x + (L.slot - tw(num, small)) / 2, s.y + L.slot + 2 * S, small, C.text_faint, false, S)
    end

    -- Tooltip (not while carrying something, like Minecraft), then the carried item
    if tip and not state.held and mx >= 0 then draw_tooltip(tip, mx, my, W, H, S, fs) end
    if state.held and mx >= 0 then
        local size = L.slot - 8 * S
        ui.draw_block_icon(state.held.name, mx - size / 2, my - size / 2, size)
        draw_count(state.held.count, mx - size / 2 - 4 * S, my - size / 2 - 4 * S, L.slot, small, S)
    end
end

-- ===========================================================================
-- INPUT
-- ===========================================================================

local function set_page(p, L, count)
    state.page = clamp(p, 1, pages(L, count))
end

function handle_creative_click(mx, my, W, H, button, shift)
    local L = layout(W, H)
    local items = shown_items()
    local sy, sh = L.topY, L.topH

    local page = state.search == "" and Categories[state.cat].page
    -- Categories (only the visible part of the scrolling list)
    for _, e in ipairs(sidebar(L)) do
        if not e.divider and inside(mx, my, e.x, e.y, e.w, e.h)
           and (e.pinned or inside(mx, my, L.list.x, L.list.y, L.list.w, L.list.h)) then
            if e.cat.special then                           -- Inventory: the survival inventory screen
                local inv = survival and survival.inventory
                if inv and inv.open_from_creative then
                    state.held = nil
                    inv.open_from_creative()
                end
                return
            end
            if page then page:stop_typing() end
            state.cat = cat_index(e.cat.id)
            state.search, state.search_focused, state.page = "", false, 1
            return
        end
    end

    -- Search box (and its clear button)
    if inside(mx, my, L.searchX, sy, L.searchW, sh) then
        if state.search ~= "" and mx >= L.searchX + L.searchW - sh then
            state.search, state.page = "", 1
            te.reset("creative:search")
        else                                                  -- the caret goes where you click
            local tx = L.searchX + 12 * L.S + (L.topH - 10 * L.S)
            te.focus("creative:search", state.search, mx - tx, L.fs)
        end
        state.search_focused = true
        if page then page:stop_typing() end
        return
    end
    state.search_focused = false

    -- A mod's page: its ui_kit widgets read the mouse themselves
    if page and inside(mx, my, L.areaX, L.areaY, L.areaW, L.areaH) then return end

    -- View buttons and pager
    if not page then
        if inside(mx, my, L.gridBtnX, sy, sh, sh) then state.list_view = false state.page = 1 return end
        if inside(mx, my, L.listBtnX, sy, sh, sh) then state.list_view = true state.page = 1 return end
        if inside(mx, my, L.prevX, sy, sh, sh) then set_page(state.page - 1, L, #items) return end
        if inside(mx, my, L.nextX, sy, sh, sh) then set_page(state.page + 1, L, #items) return end
    end

    -- An item: pick up a stack (click the same item again to put it back)
    if not page and inside(mx, my, L.areaX, L.areaY, L.areaW, L.areaH) then
        local name = item_at(L, items, mx, my)
        if shift and name and not state.held and hotbar and hotbar.get_slot then
            -- shift-click: a stack straight into the hotbar (the first empty slot)
            for i = 1, hotbar.get_slot_count() do
                local n = hotbar.get_slot(i)
                if not n or n == "" then hotbar.set_slot(i, name, T.stack_size) return end
            end
            return
        end
        if not name or (state.held and state.held.name == name) then
            state.held = nil
        else
            state.held = { name = name, count = T.stack_size }
        end
        return
    end

    -- Hotbar: put down / pick up / swap / merge
    if hotbar and hotbar.get_slot then
        local hs = hotbar_slots(L)
        for i, s in ipairs(hs) do
            if inside(mx, my, s.x, s.y, L.slot, L.slot) then
                -- shift-click: deleted (like Minecraft's creative tabs)
                click_slot({ kind = "hb", i = i }, shift, false)
                if not shift then hotbar.set_active_slot(i) end
                return
            end
        end
    end

    -- Outside the window while carrying something: drop it into the world, like Minecraft (left
    -- click: the whole stack, right click: one). An older engine without drop_item: thrown away.
    if state.held and not inside(mx, my, L.x, L.y, L.w, L.h) then
        local n = button == "right" and 1 or state.held.count
        if engine.drop_item then engine.drop_item(state.held.name, n) end
        state.held.count = state.held.count - n
        if state.held.count <= 0 then state.held = nil end
    end
end

-- Mouse wheel: up = previous page, down = next (the last page is clamped while drawing)
function handle_creative_scroll(dy)
    local mx, my = ui.get_mouse()
    local L = last_layout
    if L and L.list and inside(mx, my, L.sideX, L.sideY, L.sideW, L.sideH) then    -- over the sidebar: scroll it
        state.side_scroll = clamp((state.side_scroll or 0) - dy * L.catH, 0, L.side_max or 0)
        return
    end
    if state.search == "" and Categories[state.cat].page then return end   -- (a page reads the wheel itself)
    if dy > 0 then state.page = state.page - 1 elseif dy < 0 then state.page = state.page + 1 end
    if state.page < 1 then state.page = 1 end
end

function handle_creative_key(key, action, mods)
    local page = state.search == "" and Categories[state.cat].page
    if page and page:is_typing() then                   -- typing in a mod page's text field
        if key == ui.keys.ESCAPE and action == ui.actions.PRESS then page:stop_typing() end
        return true                                      -- E / WASD / Escape don't reach the game
    end
    if action ~= ui.actions.PRESS and action ~= ui.actions.REPEAT then return state.search_focused end
    if state.search_focused then
        if key == ui.keys.ESCAPE or key == ui.keys.ENTER or key == ui.keys.KP_ENTER then
            state.search_focused = false
        else                                                 -- Left / Right / Home / End / Backspace / Delete
            local before = state.search
            state.search = te.key("creative:search", state.search, key, action, mods)
            if state.search ~= before then state.page = 1 end
        end
        return true                                          -- typing: E / WASD don't reach the game
    end
    if ui.keys.LEFT and key == ui.keys.LEFT then state.page = math.max(1, state.page - 1) return true end
    if ui.keys.RIGHT and key == ui.keys.RIGHT then state.page = state.page + 1 return true end
    return false
end

function handle_creative_char(codepoint, ch)
    local page = state.search == "" and Categories[state.cat].page
    if page and page:is_typing() then return true end    -- (the page reads typing itself)
    if not state.search_focused then return false end
    if codepoint >= 32 and codepoint < 127 then
        state.search, state.page = te.insert("creative:search", state.search, ch, T.max_search), 1
    end
    return true
end

function on_creative_opened()
    all_cache_reset()                                    -- pick up blocks registered since last time
    for _, c in ipairs(Categories) do c.icon_cache = nil end
end

function on_creative_closed()
    local page = Categories[state.cat] and Categories[state.cat].page
    if page then
        page:stop_typing()
        state.cat = 1                                    -- a mod's page: next time, start on All Items
    end
    state.search_focused = false
    state.held = nil                                     -- closing drops what's on the cursor
end

-- Kept for older C++ calls
function set_creative_open(isOpen) end
function toggle_creative_inventory() end
