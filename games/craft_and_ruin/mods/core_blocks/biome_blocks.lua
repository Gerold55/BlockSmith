print("[Craft & Ruin] Registering biome block types...")

engine.register_block("lily_pad", 1.0, 1.0, 1.0, "textures/cw_lily_pad.png", nil, {
    description = "Lily Pad",
    transparent = true,
    cutout = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    inventory_image = "textures/cw_lily_pad.png",
    tint = false
})

engine.register_block("mushroom_red", 1.0, 1.0, 1.0, "textures/cw_mushroom_red.png", nil, {
    description = "Red Mushroom",
    transparent = true,
    cutout = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false
})

engine.register_block("mushroom_brown", 1.0, 1.0, 1.0, "textures/cw_mushroom_brown.png", nil, {
    description = "Brown Mushroom",
    transparent = true,
    cutout = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false
})

-- Blocks for Craft & Ruin's biomes (terrain_gen/mapgen.lua places them): taiga and jungle trees,
-- savanna acacias, snow and ice, badlands terracotta and red sand, and the volcanoes' basalt.
-- `description` is the name players see: pointing at the block, in the inventory, and in
-- /give's answer. /give takes the block's name ("basalt") or its full name ("core_blocks:basalt").

-- --- Spruce (taiga, snowy taiga, mountains) ---

-- Spruce Log
engine.register_block("log_spruce", 1.0, 1.0, 1.0, {
    top    = "textures/cw_spruce_log_top.png",
    bottom = "textures/cw_spruce_log_top.png",
    sides  = "textures/cw_spruce_log.png"
}, nil, {
    rotation = "axis",           -- lies along the face it is put against
    description = "Spruce Log",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Spruce Leaves (dark, coloured by the biome)
engine.register_block("leaves_spruce", 60, 120, 70, "textures/spruce_leaves.png", nil, {
    description = "Spruce Leaves",
    transparent = true,
    cutout      = true,
    walkable    = true,
    behavior    = "solid",
    tint        = true
})

-- --- Jungle ---

-- Jungle Log
engine.register_block("log_jungle", 1.0, 1.0, 1.0, {
    top    = "textures/log_jungle_top.png",
    bottom = "textures/log_jungle_top.png",
    sides  = "textures/log_jungle.png"
}, nil, {
    rotation = "axis",           -- lies along the face it is put against
    description = "Jungle Log",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Jungle Leaves (vivid green, coloured by the biome)
engine.register_block("leaves_jungle", 60, 190, 40, "textures/leaves_jungle.png", nil, {
    description = "Jungle Leaves",
    transparent = true,
    cutout      = true,
    walkable    = true,
    behavior    = "solid",
    tint        = true
})

-- --- Acacia (savanna) ---

-- Acacia Log (grey bark, orange wood)
engine.register_block("log_acacia", 1.0, 1.0, 1.0, {
    top    = "textures/acacia_log_top.png",
    bottom = "textures/acacia_log_top.png",
    sides  = "textures/acacia_log.png"
}, nil, {
    rotation = "axis",           -- lies along the face it is put against
    description = "Acacia Log",
    transparent = false,
    walkable = true,
    behavior = "solid"
})

-- Acacia Leaves (dry olive, coloured by the biome)
engine.register_block("leaves_acacia", 140, 160, 60, "textures/acacia_leaves.png", nil, {
    description = "Acacia Leaves",
    transparent = true,
    cutout      = true,
    walkable    = true,
    behavior    = "solid",
    tint        = true
})

-- --- Cold places (snowy plains, snowy taiga, mountain peaks) ---

-- Snow Block (dug with a shovel; sand sounds come from the name "snow")
engine.register_block("snow_block", 1.0, 1.0, 1.0, "textures/snow.png", nil, {
    description = "Snow Block",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 0.2,
    tool = "shovel"
})

-- Snowy Grass block: keep its snowy texture colors unchanged across biomes.
engine.register_block("snowy_grass", 1.0, 1.0, 1.0, {
    top    = "textures/snow.png",
    bottom = "textures/cw_dirt.png",
    sides  = "textures/cw_dirt.png"
}, {
    sides  = "textures/snow_grass_side_overlay.png"
}, {
    description = "Snowy Grass Block",
    transparent = false,
    walkable = true,
    behavior = "solid",
	hardness = 0.6,
    tint = false,
	tool = "shovel",
    drops = "dirt",

    -- Sounds the engine plays by itself
    sounds = {
        footstep = { name = "default_dirt_footstep", gain = 0.45, pitch = 1.1 },  -- walking / landing on it
        dig      = { name = "default_dirt_dig",      gain = 0.6 },               -- each hit while digging (survival)
        dug      = { name = "default_dirt_dug",      gain = 0.9 },               -- when it breaks
        place    = { name = "default_place",         gain = 0.8 },               -- when it's placed
    },
})

-- Ice (frozen lakes and rivers: see-through, tints the light passing through it)
engine.register_block("ice", 1.0, 1.0, 1.0, "textures/ice.png", nil, {
    description = "Ice",
    transparent = true,
    walkable = true,
    behavior = "solid",
    hardness = 0.5,
    tool = "pickaxe",
    light_filter = { 0.8, 0.9, 1.0 }
})

-- --- Dry places (desert, savanna, badlands) ---

-- Red Sand (badlands; falls like sand)
engine.register_block("red_sand", 0.8, 0.45, 0.2, "textures/red_sand.png", nil, {
    description = "Red Sand",
    walkable = true,
    tint = false,
    gravity = true
})

-- Coarse Dirt (savanna patches, mesa tops)
engine.register_block("coarse_dirt", 1.0, 1.0, 1.0, "textures/coarse_dirt.png", nil, {
    description = "Coarse Dirt",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 0.5,
    tool = "shovel"
})

-- Dead Bush (cross-plane plantlike pattern without biome tinting)
engine.register_block("dead_bush", 1.0, 1.0, 1.0, "textures/dry_shrub.png", nil, {
    description = "Dead Bush",
    transparent = true,
    walkable = false,
    selectable = true,
    pattern = "plantlike",
    tint = false
})

-- --- Terracotta (the badlands' banded cliffs) ---
-- Stone sounds, given here (the name "terracotta" doesn't pick any by itself)

engine.register_block("terracotta", 1.0, 1.0, 1.0, "textures/terracotta.png", nil, {
    description = "Terracotta",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe",
    sounds = {
        footstep = { name = "default_stone_footstep", gain = 0.5 },
        dig      = { name = "default_stone_dig", gain = 0.6 },
        dug      = { name = "default_stone_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

engine.register_block("terracotta_orange", 1.0, 1.0, 1.0, "textures/terracotta_orange.png", nil, {
    description = "Orange Terracotta",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe",
    sounds = {
        footstep = { name = "default_stone_footstep", gain = 0.5 },
        dig      = { name = "default_stone_dig", gain = 0.6 },
        dug      = { name = "default_stone_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

engine.register_block("terracotta_yellow", 1.0, 1.0, 1.0, "textures/terracotta_yellow.png", nil, {
    description = "Yellow Terracotta",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe",
    sounds = {
        footstep = { name = "default_stone_footstep", gain = 0.5 },
        dig      = { name = "default_stone_dig", gain = 0.6 },
        dug      = { name = "default_stone_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

engine.register_block("terracotta_white", 1.0, 1.0, 1.0, "textures/terracotta_white.png", nil, {
    description = "White Terracotta",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe",
    sounds = {
        footstep = { name = "default_stone_footstep", gain = 0.5 },
        dig      = { name = "default_stone_dig", gain = 0.6 },
        dug      = { name = "default_stone_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

engine.register_block("terracotta_red", 1.0, 1.0, 1.0, "textures/terracotta_red.png", nil, {
    description = "Red Terracotta",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe",
    sounds = {
        footstep = { name = "default_stone_footstep", gain = 0.5 },
        dig      = { name = "default_stone_dig", gain = 0.6 },
        dug      = { name = "default_stone_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

engine.register_block("terracotta_brown", 1.0, 1.0, 1.0, "textures/terracotta_brown.png", nil, {
    description = "Brown Terracotta",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe",
    sounds = {
        footstep = { name = "default_stone_footstep", gain = 0.5 },
        dig      = { name = "default_stone_dig", gain = 0.6 },
        dug      = { name = "default_stone_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

engine.register_block("terracotta_light_gray", 1.0, 1.0, 1.0, "textures/terracotta_light_gray.png", nil, {
    description = "Light Gray Terracotta",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe",
    sounds = {
        footstep = { name = "default_stone_footstep", gain = 0.5 },
        dig      = { name = "default_stone_dig", gain = 0.6 },
        dug      = { name = "default_stone_dug", gain = 0.9 },
        place    = { name = "default_place", gain = 0.8 },
    },
})

-- --- Stone and soil ---

-- Andesite (rocky mountain slopes)
engine.register_block("andesite", 1.0, 1.0, 1.0, "textures/andesite.png", nil, {
    description = "Andesite",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.5,
    tool = "pickaxe"
})

-- Clay (river and swamp beds; dirt sounds come from the name "clay")
engine.register_block("clay", 1.0, 1.0, 1.0, "textures/cw_clay.png", nil, {
    description = "Clay",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 0.6,
    tool = "shovel"
})

-- --- Volcanoes (the basalt hills) ---

-- Basalt (columns on the sides, the column ends on top)
engine.register_block("basalt", 1.0, 1.0, 1.0, {
    top    = "textures/basalt_top.png",
    bottom = "textures/basalt_top.png",
    sides  = "textures/basalt_side.png"
}, nil, {
    description = "Basalt",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 1.25,
    tool = "pickaxe"
})

-- Obsidian (lava's edges, crusted over; very slow to dig)
engine.register_block("obsidian", 1.0, 1.0, 1.0, "textures/obsidian.png", nil, {
    description = "Obsidian",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 50,
    tool = "pickaxe"
})

-- Magma Block (the glowing bed of the lava rivers: gives off a dim orange light)
engine.register_block("magma_block", 1.0, 1.0, 1.0, "textures/magma.png", nil, {
    description = "Magma Block",
    transparent = false,
    walkable = true,
    behavior = "solid",
    hardness = 0.5,
    tool = "pickaxe",
    light = 3,
    light_color = { 1.0, 0.45, 0.15 }
})

-- Volcanic Ash (falls like sand)
engine.register_block("volcanic_ash", 0.4, 0.4, 0.4, "textures/volcanic_ash.png", nil, {
    description = "Volcanic Ash",
    walkable = true,
    tint = false,
    gravity = true,
    hardness = 0.5,
    tool = "shovel"
})

-- --- Lava (the volcanoes' crater pools and rivers) ---
-- If another mod has already registered lava, keep it; otherwise Craft & Ruin provides its
-- own so the volcanoes always have lava. It glows by itself (the name).
if (engine.get_block_id("lava") or 0) == 0 then
    -- Lava Source
    engine.register_block("lava", 1.0, 1.0, 1.0, "textures/cr_lava.png", nil, {
        description = "Lava",
        transparent = false,
        walkable = false,
        selectable = false
    })

    -- Flowing Lava (only appears by flowing)
    engine.register_block("flowing_lava", 1.0, 1.0, 1.0, "textures/cr_lava.png", nil, {
        description = "Flowing Lava",
        transparent = false,
        walkable = false,
        selectable = false,
        in_creative = false
    })

    -- How it flows: Minecraft's lava (slow, short, not infinite)
    if engine.register_fluid then
        engine.register_fluid{
            name = "lava",
            source = "lava",
            flowing = "flowing_lava",
            spread = 3,
            tick = 1.5,
            slope_distance = 2,
            infinite = false,
            swimmable = true,
            swim_speed = 1.0
        }
    end

    -- Lava that touches water hardens: a source into obsidian, flowing lava into stone
    if engine.register_fluid_reaction then
        engine.register_fluid_reaction{
            fluid = "lava",
            touching = "water",
            source_becomes = "obsidian",
            flowing_becomes = "stone"
        }
    end
end

print("[Craft & Ruin] Biome blocks registered")
