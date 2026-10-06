# A first picture mod

In this tutorial you make a mod of pictures only: a new ship picture, **Heron**, that any
design can wear, as PNGs with soft transparent edges, and a click sound of your own. It
changes nothing in the rules, so players of a network game need not have it. The chapter
behind it is [Pictures, sounds and music](../assets.md).

A finished mod of the same kind is OpenSE4's test fixture
[tests/fixtures/mods/picture-pack](../../../../tests/fixtures/mods/picture-pack/): a ship
picture at twice the classic size and an OGG click.

## 1. Make the mod

```sh
opense4-sdk new assets heron-pictures --id=me.heron-pictures --name="Heron pictures"
```

```text
heron-pictures/
  mod.toml
  README.md
  assets/README.md
```

## 2. Draw the pictures

A ship picture is a pair: a **mini** for the maps and lists, and a **portrait** for the
designer and the reports. Their classic sizes are 36x36 and 128x128 pixels.

- Draw the ship **facing up**: the game turns minis to face the way a ship moves.
- Save them as **PNG with transparency** (an alpha channel). Everything around the ship is
  transparent; soft, half-transparent edges look right over any background. (In a BMP,
  black is the transparent colour instead.)
- For sharp pictures on large windows, draw them at a **whole multiple** of the classic size:
  72x72 and 256x256 are drawn in the classic places at the classic sizes, from more pixels.
- Any paint program will do. The SDK's example pictures are drawn by a short Python script
  from a few shapes, [tools/make_example_assets.py](../../../../tools/make_example_assets.py),
  if you would rather write your pictures.

Put them where the game looks for a ship picture of base name `Heron` for every race:

```text
heron-pictures/assets/Pictures/RaceGeneric/Generic_Mini_Heron.png       72x72
heron-pictures/assets/Pictures/RaceGeneric/Generic_Portrait_Heron.png   256x256
```

The game asks for `Generic_Mini_Heron.bmp`; the PNG of the same base name answers.

## 3. A sound

Replace the interface's button click with your own short sound, as OGG Vorbis (or WAV):

```text
heron-pictures/assets/Sounds/button.ogg
```

## 4. Check it

```sh
opense4-sdk check heron-pictures
```

`check` reads every picture and sound: a file that does not read is an error; a picture
smaller than the classic size, or larger but not a whole multiple of it, is a warning. It
also says that no hull names the bitmap `Heron`: that is expected here, since a design
chooses it (below).

```text
warning: assets/Pictures/RaceGeneric/Generic_Mini_Heron.png: no hull's Primary or Alternate Bitmap Name is 'heron': only designs that choose it as their own picture show it
```

## 5. Use it

Put the folder (or a `.zip` of it) in your mods folder and switch it on in the title
screen's **Mods** window, or start the game with it:

```sh
opense4-sdk run heron-pictures -- --quick-start=Terran
```

Open the ship designer and make a design. A small arrow in the corner of the design's
picture offers the hull's pictures and **Heron**. Choose it: the design's ships wear it on
the map and in every report. Click a button to hear your sound.

## 6. Going further

- **Replace a hull's picture for one race.** Find the hull's `Primary Bitmap Name` with
  `opense4-sdk dump --out=dump` (in `Data/VehicleSize.txt`), and put
  `Pictures/Races/<Style>/<Style>_Mini_<bitmap>.png` and `..._Portrait_<bitmap>.png` under
  `assets/`. That race's ships of that hull now look like yours.
- **Replace it for every race**: `Pictures/RaceGeneric/Generic_Mini_<bitmap>.png`, which
  races without a picture of their own use.
- **A whole race style** is a folder of pictures: [Pictures, sounds and music](../assets.md#race-style-folders).
- **Share it**: `opense4-sdk pack heron-pictures` makes `me.heron-pictures-0.1.0.zip`
  ([Getting started](../getting-started.md#sharing-a-mod)). Draw everything yourself: never
  share pictures or sounds from the original game.
