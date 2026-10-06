# Pictures, sounds and music

A mod's `assets/` folder holds pictures, sounds, music, fonts and pointers, laid out as in
the game folder: `assets/Pictures/RaceGeneric/Generic_Mini_Lancer.png` stands for the
game's `Pictures/RaceGeneric/Generic_Mini_Lancer.bmp`. A file there is used in place of
the installed game's file of the same path, and a new path adds a file. Assets change only
how the game looks and sounds: they are not part of a mod's identity, and the players of a
network game may each have their own.

The formats and sizes below are specified in
[Mod packages and data patches](../packages-and-data.md#pictures-sounds-music-and-fonts);
this chapter is the modder's view of them. The tutorial
[A first picture mod](tutorials/picture-mod.md) makes one.

## How a file is found

Every lookup of a picture, sound, music track, font or pointer tries the enabled mods'
`assets/` folders first, the last mod in the load order first, then the installed game.
Names match in any letter case, on every platform. So:

- a mod **replaces** a file by putting one at the same path;
- a later mod's file wins over an earlier mod's;
- a picture the game asks for as `Name.bmp` may be given as `Name.png` (below).

The game asks for files by fixed names, or by names the data gives: a hull's
`Primary Bitmap Name`, a component's `Pic Num`, a weapon's `Weapon Sound`, a race's style
folder. A file under a name nothing asks for is never shown; `opense4-sdk check` warns
about picture files no hull names, and about files outside `Pictures/`, `Sounds/`,
`Music/` and `Fonts/`.

## Pictures

### Formats

- **PNG**, with its own transparency: its alpha channel, so soft edges and shadows look
  right. The best choice for new pictures.
- **BMP**, as the original's: black (0, 0, 0) is transparent where the game draws the
  picture over something.
- The game reads the format from the file itself, so a `.bmp` holding PNG data is read as
  a PNG. JPEG data is read too, but only under a `.bmp` name: the game never asks for
  `.jpg` files, and `check` says so.
- In a folder that has both `Name.png` and `Name.bmp`, the PNG is used.

### Kinds and classic sizes

| Pictures | Where | Classic size |
|---|---|---|
| Ship and unit minis (map, lists) | `Pictures/Races/<Style>/<Style>_Mini_<name>`, `Pictures/RaceGeneric/Generic_Mini_<name>` | 36x36 |
| Ship and unit portraits (designer, reports) | `..._Portrait_<name>` | 128x128 |
| A race's portrait | `<Style>_Race_Portrait` | 128x128 |
| A race's population pictures | `<Style>_Pop_Mini`, `<Style>_Pop_Portrait` | 20x20, 36x36 |
| A race's emblem (its flags, the empire's colour) | `<Style>_Main` | 100x20 |
| Component and facility portraits | `Pictures/Components/Comp_NNN`, `Pictures/Facilities/Facil_NNN` | 128x128 |
| Component and facility icons | cells of the sheets `Components.bmp`, `Facility.bmp` | 36x36 cells |
| Planets | `Pictures/Planets/pNNNN` | 128x128 |
| Events | `Pictures/Events/<name>` | 128x128 |
| A system's report picture, combat tiles | `Pictures/Systems/<name>`, `<name>TileN` | 128x128, 72x72 |
| The system panel's backgrounds | `Pictures/Systems/1024X768/*`, `800X600/*` | 660x660, 490x490 |
| Shields, the large explosion | `Shields`, `BigExplosion` | 288x36, 576x72 |
| The intro screen | `Pictures/Game/Screens/1024X768/Intro`, `800X600/Intro` | 1024x768, 800x600 |
| Everything else: buttons, window pieces, `General.bmp` | as the installed game's own copy | |

**Ship pictures** have a base name: a hull's `Primary Bitmap Name` (and `Alternate Bitmap
Name`, shown when the primary has no picture). For a race whose style is `Terran` and a
base name `Lancer` the game looks for `Pictures/Races/Terran/Terran_Mini_Lancer` (also
under `Pictures/RaceNeutral/`), then `Pictures/RaceGeneric/Generic_Mini_Lancer`, and the
same with `Portrait_`. A generic picture serves every race; a race's own wins for that race.
Minis are turned to face the way a ship moves, so draw them facing up.

**Component and facility pictures** come from a number, `Pic Num`: the cell of the sheet
(`Pic Num` 1 is the first 36x36 cell) and the portrait `Comp_NNN` (`Comp_001`). The sheets
have a fixed number of cells, those of the installed game's sheet, so a new component
borrows a number from the sheet; giving it a portrait of its own under that number would
change the picture of every component with that number. [Components](data/components.md#pictures)
says more.

### Larger pictures

A picture larger than its kind's classic size is drawn in the classic picture's place and
at its size, so it is sharper on a large window: a 256x256 portrait shows where a 128x128
one does. OpenSE4 shrinks it to the window's resolution with a filter that averages the
pixels it covers, so it does not shimmer; the Sharp pixels setting still decides how it is
drawn on screen.

- Make a larger picture a **whole multiple** of the classic size (twice, three times) so
  that its pixels line up. `opense4-sdk check` warns about other sizes, and about pictures
  *smaller* than the classic size, which are drawn stretched.
- A larger **sheet** keeps its cells at the installed sheet's places, scaled: in a
  `Components.bmp` twice the size, each 36x36 cell becomes 72x72.

### A design's own picture

A design may show a picture of its own in place of its hull's. In the ship designer a small
arrow in the corner of the design's picture offers the hull's pictures and every ship
picture the enabled mods add (a mini and a portrait with the same base name). A picture
pack, a mod with nothing but such pairs, gives players new looks for any hull. The choice
is saved with the design (`picture` in the design commands); a computer without the
picture shows the hull's.

## Sounds and music

- **Formats**: WAV at any rate and in any of its formats, MP3 for music, and OGG Vorbis
  under the same base name as the classic file. In a folder that has both, the OGG is used.
- **Interface sounds** have classic names, looked for in the remastered set
  (`Sounds/New/<name>.wav`) when it is chosen, then in `Sounds/`: the button click is
  `button`, and there are `close`, `cmdbtn`, `ordbtn` and `endturn` among others. A mod's
  file under either folder is used before the installed game's: `assets/Sounds/button.ogg`
  replaces the button click.
- **Weapon sounds** are named by the weapon's `Weapon Sound` field, with its extension
  (`zap.wav`), looked for as the interface sounds are. A new weapon can bring its own: the
  [weapon-line](../../../mods/examples/weapon-line/) example's lances name `ember.wav`,
  which the mod holds as `assets/Sounds/ember.wav`.
- **Music**: tracks under `Music/`, named by the playlists in `Settings.txt`
  ([Settings and lists](data/settings.md)). A mod adds tracks as files and names them in
  the playlists with a settings patch; `Music/Track 01.ogg` plays for a playlist's
  `Track 01.mp3`.

## Fonts and pointers

Windows raster fonts (`.fon`, `.fnt`) or TrueType (`.ttf`, `.otf`) under `Fonts/`, and
Windows pointer files (`.cur`, `.ani`) under `Pictures/`, replace the game's by name, as
other files do. The mod folder that the game's own `Path.txt` names is still read for fonts
and pointers, after the mods and before the game's own files.

## Race style folders

A race's pictures are in its style folder: `Pictures/Races/<Style>/` (or
`Pictures/RaceNeutral/<Style>/` for neutral races), every file named `<Style>_...`. A new
race style is a folder of pictures under the mod's `assets/` plus the race's data files
under `data/` in the same layout ([Races](data/races.md)):

```text
zorg/
  assets/Pictures/Races/Zorg/Zorg_Race_Portrait.png
  assets/Pictures/Races/Zorg/Zorg_Main.png
  assets/Pictures/Races/Zorg/Zorg_Mini_<each hull's bitmap name>.png
  assets/Pictures/Races/Zorg/Zorg_Portrait_<each hull's bitmap name>.png
  data/Pictures/Races/Zorg/Zorg_AI_General.txt       the race's preset (a game file)
```

Hull pictures a style lacks fall back to `RaceGeneric`, so a new style can start with a
portrait and an emblem and add ships over time.

## Checking

`opense4-sdk check` reads every picture and sound of the mod: a file that does not read as
its format is an error; a picture smaller than its kind's classic size, or larger but not
a whole multiple, a hull whose pictures are missing, a ship picture no hull names, a
`Pic Num` beyond the sheet and a file where the game does not look are warnings.

## Your own work only

Draw and record your own: never put pictures, sounds or music from the original (or from
anyone who has not allowed it) in a mod you share. The example mods' pictures and sound
are made by a script from shapes and formulas
([tools/make_example_assets.py](../../../tools/make_example_assets.py)); that is one way to
make small assets that are certainly yours.
