# Clean-room policy

This project aims to be a **mechanically faithful reimplementation** of a
classic 4X game that the player already owns. It follows the model used by
engine re-creations such as OpenXcom, OpenMW and fheroes2:

- we write every line of code, every document and our own content ourselves;
- the player's **own installed copy** supplies the original data, art, sound
  and music at runtime, on their own machine;
- nothing from the original game is ever redistributed.

Anyone contributing, human or AI, follows these rules.

> This is engineering policy, not legal advice. The original game's license
> agreement forbids copying, reverse engineering and "derivative works". Get a
> lawyer's review before any public release, especially a commercial one.

## Allowed sources of knowledge

1. **The manual** (HTML/PDF shipped with the game). Read it to learn the rules;
   restate them in our own words.
2. **The data files' documented format.** The `Data/*.txt` files are plain text,
   designed for players to edit, and each one documents its own fields. We
   implement a compatible reader (interoperability) and learn the semantics of
   each field.
3. **Black-box observation of the running game.** Play it; drive it with
   `tools/observe` (synthetic input, window capture); compare its behavior
   with ours.
4. Public community knowledge (FAQs, strategy guides, wikis), restated in our own words.

## Forbidden

- Disassembling, decompiling, hex-dumping, running `strings` on, or otherwise
  inspecting the original **executables** or their memory. Observation stays
  black-box: input in, pixels and behavior out.
- Copying any original file into this repository: data files, images, sounds,
  music, fonts, maps, save files, or the manual. That includes partial copies,
  such as data tables pasted into docs or tests.
- Copying text verbatim into docs, code comments or UI strings, beyond short
  functional identifiers. Examples of identifiers: data-file field names such
  as `Tonnage Space Taken`, ability identifiers such as `Supply Storage`, enum
  values, and UI labels.
- Using the "Space Empires" trademark as, or in, our product name, logo or artwork.
  Our name is **OpenSE4**. Refer to the original only to say which game the engine is
  for, as in "an engine for Space Empires IV Deluxe". Keep the non-affiliation notice
  in the README.

## Where things live

| What | Where | In git? |
|---|---|---|
| Rules specs, in our own words | `docs/spec/` | yes |
| Classic-format data reader (interop) | `src/datafile/`, `src/ruleset/` | yes |
| Our own test fixtures (invented content) | `tests/fixtures/` | yes |
| Screenshots and notes from the player's copy | `reference/` | **no**, gitignored |
| The player's installed game | Wherever Steam put it. Auto-detected, never copied. | no |

## Tools

- `opense4-datacheck [DIR]` loads a data set (by default the player's installed
  copy) and reports every problem and any field we don't read yet.
- `opense4-observe` lists windows, clicks and types into the running original game
  under X11/XWayland. On GNOME Wayland, XTest input is blocked without a portal
  permission, so use `sclick` (events sent straight to the window).
- To launch the original: `steam steam://rungameid/1610`. Capture a window with
  `import -window <id> file.png` (ImageMagick). Save captures under
  `reference/`, never anywhere tracked.

## Testing against the player's data

Tests use only our own fixture content. The opt-in test

```sh
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests -tc="*installed*"
```

loads the player's installed data set, if present, and checks that it parses without errors.
