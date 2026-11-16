# Fluids in BlockSmith

Water, lava, molten metals, oil, honey: any fluid flows the same way Minecraft water does.
The engine side lives in `Fluids.h` / `Fluids.cpp`. Mods add fluids from Lua.
`mods/lava` is a complete example to copy.

## Adding a fluid

```lua
-- 1. Two blocks: the source and the flowing block. The flowing block can have no texture
--    of its own, because fluids are drawn with their source block's textures.
engine.register_block("molten_iron", 1, 1, 1, "molten_iron.png", nil,
    { walkable = false, selectable = false, transparent = false })
engine.register_block("flowing_molten_iron", 1, 1, 1, "molten_iron.png", nil,
    { walkable = false, selectable = false, transparent = false, in_creative = false })

-- 2. How it flows
engine.register_fluid{
    name = "molten_iron",
    source = "molten_iron",           -- source block (default: name)
    flowing = "flowing_molten_iron",  -- flowing block (default: source block + metadata)
    spread = 2,                       -- blocks across flat ground, 1..7   (water 7, lava 3)
    tick = 2.0,                       -- seconds per flow step             (water 0.25, lava 1.5)
    slope_distance = 1,               -- how far it looks for a drop       (water 4, lava 2)
    infinite = false,                 -- 2 sources + a gap = a new source  (water true)
    swimmable = true,                 -- players swim in it
    swim_speed = 0.8,                 -- blocks per second while in it     (water 2.2)
    bucket = "molten_iron_bucket",    -- extra fields are free-form: other mods read them
    bucket_image = "molten_iron_bucket.png",
}

-- 3. Optional: what it turns into when it touches another fluid
engine.register_fluid_reaction{
    fluid = "molten_iron", touching = "water",
    source_becomes = "iron_block", flowing_becomes = "stone",
}
```

Registering a fluid name again replaces its settings, so a game can retune `water`.

## Reading fluids

| Function | Returns |
|---|---|
| `engine.get_fluid(x, y, z)` | `nil`, or `{ name, block, level, source, falling }` |
| `engine.is_fluid_source(x, y, z)` | `true` for a full source of any fluid |
| `engine.is_replaceable(x, y, z)` | `true` for air, any fluid, or a plant |
| `engine.list_fluids()` | every fluid's settings and extra fields, plus `mod_path` (the registering mod's folder) |
| `engine.set_block_meta(x, y, z, block, 0)` | places a fresh source |

Items that act on fluids use `engine.register_item_use(item, fn)`. The `pointed.fluid`
it receives is `{ x, y, z, name, block, source, falling, level }` for the first fluid the
crosshair passes through. See `mods/buckets` for an example.

## Buckets

The buckets mod makes a bucket for any fluid:

- Put `bucket` and `bucket_image` in the fluid definition, or
- call `if buckets then buckets.register("molten_iron", "molten_iron_bucket", "molten_iron_bucket.png") end`.

The picture is looked for in the registering mod's folder or its `textures/` folder.

## How fluid cells are stored

- **Source:** the source block, with metadata 0.
- **Flowing:** the flowing block. Metadata bits 0–2 hold the level, from 1 (next to a source) up to `spread`. Bit 3 (`0x8`) means falling.

## Flow rules

These are the same as Minecraft's `FlowingFluid`:

1. A fluid falls first: into air or plants, or onto thinner flowing fluid of its own kind.
2. Fluid that can fall only spreads sideways when 3 or more sources surround it.
3. Sideways, it loses one level per block and heads only for the nearest drop within `slope_distance` blocks.
4. Flowing fluid with nothing feeding it dries up.
5. Each fluid steps at its own `tick` speed. Different fluids never mix; they show a face where they meet, and react if a reaction is registered.

The server runs fluids. Clients only draw what it sends.
