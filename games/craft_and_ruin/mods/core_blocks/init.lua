print("[Craft & Ruin] Registering core block types...")

-- Non-rendered Air Block
engine.register_block("air", 0.0, 0.0, 0.0, nil, nil, {
    transparent = true,
    walkable = false,
    selectable = false
})

-- Standard Opaque Blocks
engine.register_block("stone", 1.0, 1.0, 1.0, "textures/cw_stone.png", nil, {
    description = "Stone",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("cobble", 1.0, 1.0, 1.0, "textures/cw_cobblestone.png", nil, {
    description = "Cobble",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("stonebricks", 1.0, 1.0, 1.0, "textures/stone_brick.png", nil, {
    description = "Stone Bricks",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

--Ores

engine.register_block("ore_coal", 1.0, 1.0, 1.0, "textures/coal_ore.png", nil, {
    description = "Coal Ore",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("ore_iron", 1.0, 1.0, 1.0, "textures/iron_ore.png", nil, {
    description = "Iron Ore",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("ore_gold", 1.0, 1.0, 1.0, "textures/gold_ore.png", nil, {
    description = "Gold Ore",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("ore_diamond", 1.0, 1.0, 1.0, "textures/diamond_ore.png", nil, {
    description = "Diamond Ore",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("ore_copper", 1.0, 1.0, 1.0, "textures/copper_ore.png", nil, {
    description = "Copper Ore",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("ore_emerald", 1.0, 1.0, 1.0, "textures/emerald_ore.png", nil, {
    description = "Emerald Ore",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("dirt", 1.0, 1.0, 1.0, "textures/cw_dirt.png", nil, {
    description = "Dirt",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Grass block with a truer, less yellow green tint
engine.register_block("grass", 120, 200, 85, {
    top    = "textures/cw_grass_top.png",
    bottom = "textures/cw_dirt.png",
    sides  = "textures/cw_dirt.png"
}, {
    sides  = "textures/cw_grass_side_overlay.png"
}, {
    description = "Grass Block",
    transparent = false,
    walkable = true,
    behavior = "solid",
    tint = true,

    -- Sounds the engine plays by itself
    sounds = {
        footstep = { name = "default_dirt_footstep", gain = 0.45, pitch = 1.1 },  -- walking / landing on it
        dig      = { name = "default_dirt_dig",      gain = 0.6 },               -- each hit while digging (survival)
        dug      = { name = "default_dirt_dug",      gain = 0.9 },               -- when it breaks
        place    = { name = "default_place",         gain = 0.8 },               -- when it's placed
    },
})

-- Oak Log
engine.register_block("log_oak", 1.0, 1.0, 1.0, {
    top    = "textures/cw_oak_log_top.png",
    bottom = "textures/cw_oak_log_top.png",
    sides  = "textures/cw_oak_log.png"
}, nil, {
    rotation = "axis",           -- lies along the face it is put against
    description = "Oak Log",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Oak Leaves
engine.register_block("leaves_oak", 75, 180, 30, "textures/cw_oak_leaves.png", nil, {
    description = "Oak Leaves",
    transparent = true,
    cutout      = true,
    walkable    = true,
    behavior    = "solid",
    tint        = true
})

-- Birch Log
engine.register_block("log_birch", 1.0, 1.0, 1.0, {
    top    = "textures/cw_birch_log_top.png",
    bottom = "textures/cw_birch_log_top.png",
    sides  = "textures/cw_birch_log.png"
}, nil, {
    rotation = "axis",           -- lies along the face it is put against
    description = "Birch Log",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Birch Leaves (Golden Autumn Tint)
engine.register_block("leaves_birch", 1.0, 0.85, 0.1, "textures/cw_birch_leaves.png", nil, {
    description = "Birch Leaves",
    transparent = true,
    cutout      = true,
    walkable    = true,
    behavior    = "solid",
    tint        = true
})

-- --- Planks (crafted from logs: 1 log = 4 planks in the survival inventory) ---
-- Wood sounds, axe digging and hardness 2 come automatically from the name ("planks").
engine.register_block("planks_oak", 1.0, 1.0, 1.0, "textures/planks_oak.png", nil, {
    description = "Oak Planks",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("planks_birch", 1.0, 1.0, 1.0, "textures/planks_birch.png", nil, {
    description = "Birch Planks",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("planks_spruce", 1.0, 1.0, 1.0, "textures/cw_spruce_planks.png", nil, {
    description = "Spruce Planks",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

engine.register_block("planks_jungle", 1.0, 1.0, 1.0, "textures/planks_jungle.png", nil, {
    description = "Jungle Planks",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- --- Stick (an item: shown as a picture, never placed) ---
-- Crafted from 2 planks; leaves sometimes drop them (leafdecay mod).
engine.register_block("stick", 1.0, 1.0, 1.0, "textures/stick.png", nil, {
    description = "Stick",
    transparent = true,
    walkable = false,
    inventory_image = "stick.png",
})

-- --- Saplings (leaves sometimes drop them; plant them like flowers) ---
engine.register_block("sapling_oak", 1.0, 1.0, 1.0, "textures/sapling_oak.png", nil, {
    description = "Oak Sapling",
    transparent = true, 
    walkable = false, 
    selectable = true, 
    pattern = "plantlike", 
    tint = false
})
engine.register_block("sapling_birch", 1.0, 1.0, 1.0, "textures/sapling_birch.png", nil, {
    description = "Birch Sapling",
    transparent = true, 
    walkable = false, 
    selectable = true, 
    pattern = "plantlike", 
    tint = false
})

-- --- Workbench: right-click it to craft on a 3x3 grid (survival mod) ---
-- Crafted from 4 planks in the inventory's 2x2 grid.
engine.register_block("workbench", 1.0, 1.0, 1.0, {
    top    = "textures/workbench_top.png",
    bottom = "textures/planks_oak.png",
    sides  = "textures/workbench_side.png"
}, nil, {
    description = "Workbench",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 2.5,
    tool = "axe",
    sounds = {
        footstep = { name = "default_wood_footstep", gain = 0.5 },
        dig      = { name = "default_wood_dig", gain = 0.6 },
        dug      = { name = "default_wood_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

-- Static Water Source Block
engine.register_block("water", 0.2, 0.4, 0.9, {
    top = "textures/ws_water_source_animated.png",
    bottom = "textures/ws_water_source_animated.png",
    sides = "textures/ws_water_source_animated.png"
}, nil, {
    description = "Water",
    transparent = true,
    walkable = false,
    selectable = false,
    behavior = "liquid",
    tint = false
})

-- Flowing Water Stream Block (Uses flowing textures)
engine.register_block("flowing_water", 0.2, 0.4, 0.9, {
    top = "textures/ws_water_flowing_animated.png",
    bottom = "textures/ws_water_flowing_animated.png",
    sides = "textures/ws_water_flowing_animated.png"
}, nil, {
    description = "Flowing Water",
    transparent = true,
    walkable = false,
    selectable = false,
    behavior = "liquid",
    tint = false,
    in_creative = false
})

-- Register Cattail Stem (Bottom/Middle part in water)
engine.register_block("cattail", 0.2, 0.6, 0.2, "cw_cattail_bottom.png", nil, {
    description = "Cattail",
    transparent = true,
    walkable = false,
    pattern = "plantlike",
    waterlogged = true
})

-- Register Cattail Top (Brown fluffy head)
engine.register_block("cattail_top", 0.4, 0.2, 0.1, "cw_cattail_top.png", nil, {
    description = "Cattail",
    transparent = true,
    walkable = false,
    pattern = "plantlike"
})

-- Register Bedrock (Unbreakable bottom layer)
engine.register_block("bedrock", 0.2, 0.2, 0.2, "cw_bedrock.png", nil, {
    description = "Bedrock",
    walkable = true,
    selectable = false
})

-- Register Sand (Desert / Beach surface layer)
engine.register_block("sand", 0.85, 0.82, 0.65, "cw_sand.png", nil, {
    description = "Sand",
    walkable = true,
    tint = false,
    gravity = true
})

-- Register Gravel (Rough underground / cave patch blocks)
engine.register_block("gravel", 0.5, 0.5, 0.5, "cw_gravel.png", nil, {
    description = "Gravel",
    walkable = true,
    tint = false,
    gravity = true
})

-- --- NEW: Vegetation Blocks ---

-- Short Grass (Cross-plane plantlike pattern with biome tinting)
engine.register_block("short_grass", 120, 200, 85, "textures/cw_grass.png", nil, {
    description = "Short Grass",
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = true
})

-- Reeds Lower/Middle Stalk
engine.register_block("reed", 0.8, 0.9, 0.8, {
    top    = "textures/cw_reeds.png",
    bottom = "textures/cw_reeds.png",
    sides  = "textures/cw_reeds.png"
}, nil, {
    description = "Reeds",
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false
})

-- Reeds Top Tip Stalk
engine.register_block("reed_top", 0.8, 0.9, 0.8, {
    top    = "textures/cw_reeds_top.png",
    bottom = "textures/cw_reeds.png",
    sides  = "textures/cw_reeds_top.png"
}, nil, {
    description = "Reeds",
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false,
    in_creative = false
})

-- Register Cactus (Desert column block)
engine.register_block("cactus", 0.3, 0.7, 0.3, {
    top    = "textures/cw_cactus_top.png",
    bottom = "textures/cw_cactus_bottom.png",
    sides  = "textures/cw_cactus_side.png"
}, nil, {
    description = "Cactus",
    transparent = false,
    walkable = true,
    selectable = true,
    behavior = "solid",
    in_creative = true
})

-- Red Flower (Cross-plane plantlike pattern without biome tinting)
engine.register_block("poppy", 1.0, 1.0, 1.0, "textures/cw_flower_poppy.png", nil, {
    description = "Poppy",
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false
})

engine.register_block("flower_daisy", 1.0, 1.0, 1.0, "textures/cw_flower_daisy.png", nil, {
    description = "Daisy",
    transparent = true, 
    walkable = false, 
    selectable = true, 
    gravity = false, 
    pattern = "plantlike", 
    tint = false
})

engine.register_block("flower_bluebell", 1.0, 1.0, 1.0, "textures/cw_flower_bluebell.png", nil, {
    description = "Bluebell",
    transparent = true, 
	walkable = false, 
	selectable = true, 
	gravity = false, 
	pattern = "plantlike", 
	tint = false
})

engine.register_block("flower_azure_bluet", 1.0, 1.0, 1.0, "textures/cw_flower_azure_bluet.png", nil, {
    description = "Azure Bluet",
    transparent = true, 
	walkable = false, 
	selectable = true, 
	gravity = false, 
	pattern = "plantlike", 
	tint = false
})

engine.register_block("flower_allium", 1.0, 1.0, 1.0, "textures/cw_flower_allium.png", nil, {
    description = "Allium",
    transparent = true, 
	walkable = false, 
	selectable = true, 
	gravity = false, 
	pattern = "plantlike", 
	tint = false
})

engine.register_block("flower_tulip_white", 1.0, 1.0, 1.0, "textures/cw_flower_tulip_white.png", nil, {
    description = "White Tulip",
    transparent = true, 
	walkable = false, 
	selectable = true, 
	gravity = false, 
	pattern = "plantlike", 
	tint = false
})

engine.register_block("flower_tulip_red", 1.0, 1.0, 1.0, "textures/cw_flower_tulip_red.png", nil, {
    description = "Red Tulip",
    transparent = true, 
	walkable = false, 
	selectable = true, 
	gravity = false, 
	pattern = "plantlike", 
	tint = false
})

engine.register_block("flower_tulip_pink", 1.0, 1.0, 1.0, "textures/cw_flower_tulip_pink.png", nil, {
    description = "Pink Tulip",
    transparent = true, 
	walkable = false, 
	selectable = true, 
	gravity = false, 
	pattern = "plantlike", 
	tint = false
})

-- Load separate UI script
dofile(g_CurrentModPath .. "/ui.lua")
dofile(g_CurrentModPath .. "/pause_menu.lua")
dofile(g_CurrentModPath .. "/leafdecay.lua")
-- Particles: torches smoking, lava popping, leaves falling, and the pictures the engine uses
dofile(g_CurrentModPath .. "/particles.lua")
-- Biome blocks: spruce, jungle and acacia trees, snow and ice, terracotta and red sand
dofile(g_CurrentModPath .. "/biome_blocks.lua")
-- Slabs, stairs and signs
dofile(g_CurrentModPath .. "/building_blocks.lua")
-- Random ticks: grass spreading and dying. World-generator Lua states have no random-tick API.
if engine.register_random_tick then dofile(g_CurrentModPath .. "/abm.lua") end
