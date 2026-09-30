# Map files

A map file holds a quadrant: its systems, every object in them, the warp links, and
optional starting points for the empires. Game Setup can start a game on a map
instead of generating a quadrant (spec 01 §12). The original game's map format is not
documented, so OpenSE4 uses a text format of its own, described here.

## Where maps live

Maps are kept in the `maps` folder of OpenSE4's user data directory, next to `saves`
and `empires` (on Linux usually `~/.local/share/OpenSE4/maps`). They are never written
into the game install. The file name is the map name followed by `.toml`.

## Making and using maps

- **Save Map on Game Setup's Quadrant page** writes the quadrant shown there. That is
  either the generated preview (which has no starting points) or the map loaded before.
- **Save Map in the Game Menu** (F2) writes the current quadrant of a game in progress:
  its systems and stellar objects, without ships, units or what the empires know. It is
  available only when the game was set up with *Players can save the map during the
  game* (Game Settings page, off by default). The file gets the starting points the game
  still holds, as in the original (spec 01 §12): for a game started from a map, all of
  that map's specific points and the common points no empire took; a generated game has
  none. The empires' capitals are not written as starting points.
- **Load Map on the Quadrant page** picks a map from the maps folder. The game then
  starts on it. The quadrant options on that page (type, size, warp point options) no
  longer apply. Loading a map replaces the previous map together with its starting
  points. **Generate Map Now** returns to a generated quadrant.

## Placement with starting points

This follows spec 01 §3.6 and §12. Map starting points are used before random
placement:

1. For each empire in player order: it takes the starting point reserved for its
   player slot (the last one listed, when several are). An empire without one takes a
   random common point from those left, which is then used up. A point on a sector that
   an earlier empire already took is skipped (an OpenSE4 choice: the original does not
   check, so two empires could share a homeworld).
2. The planet in that sector becomes the empire's homeworld. If the planet's
   atmosphere is not the empire's, it is converted: it gets a random natural planet
   record with the empire's atmosphere and planet type at the same size, and keeps its
   name. If the sector holds no planet, one is created there, as random placement
   would create it: named after the system with the numeral one above the number of
   sectors that hold a planet.
3. Empires still without a homeworld are placed at random in player order, as for a
   generated quadrant. Homes placed from starting points count as homes placed before.

## Format

The file is TOML (https://toml.io). All keys are lower case. Positions count from 0:
galaxy squares run from x 0 to width − 1 and y 0 to height − 1, and sectors of a
system run from 0 to 12 in both directions. Values not listed as optional are
required.

### Top level

| Key | Meaning |
|---|---|
| `format` | Always `"opense4-map"`. |
| `version` | The format version, currently `1`. Newer versions are refused. |
| `name` | The map's name. Optional; if missing, the file name is used. |
| `quadrant_type` | Optional. The QuadrantTypes.txt record the quadrant was made from, for information. |
| `width`, `height` | Optional. The galaxy grid in squares (default 67 × 46). |
| `[[systems]]` | One table per system, in order. At least one is required. |
| `[[starts]]` | Optional. One table per starting point. |

### `[[systems]]`

| Key | Meaning |
|---|---|
| `name` | The system's name. |
| `x`, `y` | The galaxy square. |
| `type` | The SystemTypes.txt record, by name. If the data set has no record of that name, the first record is used and a warning is shown. |
| `physical_type` | Optional. `"Normal"`, `"Nebulae"` or `"Black Hole"`; the default comes from the system type. |
| `abilities` | Optional. The system-wide abilities (see Abilities). |
| `[[systems.objects]]` | The system's objects, in order. |

### `[[systems.objects]]`

| Key | Meaning |
|---|---|
| `kind` | `"Star"`, `"Planet"`, `"Asteroids"`, `"Storm"`, `"Warp Point"`, `"Destroyed Star"` or `"Comet"`. |
| `sect_type` | The SectType.txt record, by its position in the file, counting from 0. If that record is not of the object's kind, the first record of the kind with the same attributes is used, else the first of the kind, with a warning. |
| `name` | The object's name. |
| `x`, `y` | The sector. |
| `size` | Optional. The planet, star or storm size. The default comes from the SectType record. |
| `surface`, `atmosphere` | Optional, for planets and asteroid fields: the physical type (`"Rock"`, `"Ice"`, `"Gas Giant"`) and the atmosphere. |
| `conditions` | Optional, for planets and asteroid fields: hundredths of the 0 to 1.5 scale (100 means 1.0), at most 150. Default 100. A saved map rounds conditions to the nearest hundredth. |
| `values` | Optional, for planets and asteroid fields: `[minerals, organics, radioactives]`, as percentages (or remaining stock in finite-resource games). Default `[100, 100, 100]`. |
| `star_age`, `star_color`, `star_luminosity` | Optional, for stars and destroyed stars. |
| `abilities` | Optional. The object's own abilities (see Abilities). |
| `warp_to` | For warp points: `[system, object]`, the linked warp point, given as the system's position in the file and the object's position in that system's list, both counting from 0. Both ends of a link point at each other; a link written at only one end is completed. A warp point without `warp_to` leads nowhere. |

### Abilities

An ability is an inline table:
`{ type = "Sector - Damage", value1 = "50", value2 = "" }`. The optional
`description` key holds its text. The type and values are written as the data files
spell them.

### `[[starts]]`

| Key | Meaning |
|---|---|
| `system` | The system's position in the file, counting from 0. |
| `x`, `y` | The sector. |
| `player` | Optional. The player slot the point is reserved for, counting from 1. Without it (or with 0), the point is common and any player may take it. |

### Example

```toml
format = "opense4-map"
version = 1
name = "Two Suns"
width = 67
height = 46

[[systems]]
name = "Arden"
x = 10
y = 12
type = "Test Single Star"
physical_type = "Normal"

  [[systems.objects]]
  kind = "Planet"
  sect_type = 4
  name = "Arden I"
  x = 3
  y = 3
  size = "Large"
  surface = "Rock"
  atmosphere = "Oxygen"
  conditions = 90
  values = [110, 95, 100]

  [[systems.objects]]
  kind = "Warp Point"
  sect_type = 8
  name = "Warp Point"
  x = 12
  y = 6
  warp_to = [1, 0]

[[systems]]
name = "Bryn"
x = 20
y = 12
type = "Test Single Star"

  [[systems.objects]]
  kind = "Warp Point"
  sect_type = 8
  name = "Warp Point"
  x = 0
  y = 6
  warp_to = [0, 1]

[[starts]]
system = 0
x = 3
y = 3
player = 1

[[starts]]
system = 1
x = 6
y = 6
```

The system type names and SectType positions in the example are those of our test data
(`tests/fixtures/minimal_dataset`). A map made with one data set can be loaded with
another; records that are missing there are replaced as described above.
