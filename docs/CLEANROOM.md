# Clean-room and reverse-engineering policy

This project aims to be a **mechanically faithful reimplementation** of a
classic 4X game that the player already owns. It follows the model used by
engine re-creations such as OpenXcom, OpenMW and fheroes2:

- we write every line of code, every document and our own content ourselves;
- the player's **own installed copy** supplies the original data, art, sound
  and music at runtime, on their own machine;
- nothing from the original game is ever redistributed.

Anyone contributing, human or AI, follows these rules.

> This is engineering policy, not legal advice.

> **Change on 2026-09-29.** The project owner decided to also study the
> original executable (disassembly and decompilation) to confirm the rules, so
> OpenSE4 is not clean-room in the strict sense. It still never copies
> or redistributes anything from the original, and its code is written from
> our specs, not from the binary. The rules below keep it that way.

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
5. **Analysis of the executable** on the contributor's own machine (Ghidra,
   rizin, gdb under Wine), under the rules in the next section.

## Reverse engineering: study, specify, then implement

1. **Raw material stays local.** Analysis projects, disassembly and decompiler
   output, addresses, symbol and function names taken from the binary, memory
   dumps and notes that quote any of these live only under `reference/re/`
   (gitignored). They never go into tracked files, commits, issues or pull
   requests.
2. **Findings become plain-language rules.** What the game does goes into
   `docs/spec/` in our own words: behaviour, formulas written as ordinary maths
   in our own notation, and the constants that are rules (a 14 % rate, a limit
   of 50). Never code, never pseudo-code transcribed from the decompiler, never
   addresses or binary symbol names.
3. **Code follows the spec, not the listing.** Write or update the spec note
   first, then implement from that text, in our own structure and names. Do
   not write engine code while reading decompiler output.
4. **Say where a rule comes from.** Mark rules in the specs and the code as
   "(confirmed: binary)" when checked in the executable, "(observed)" for
   black-box observation, and "(inferred)" for guesses.
5. **No patching.** Never modify, crack or redistribute the executable, and
   never work around copy protection.
6. **The lint checks it.** `tools/cleanroom_check.py` also rejects tracked
   text containing address-like numbers (eight hex digits starting with 004
   or 005) or decompiler-generated names (`FUN_`, `DAT_` or `LAB_` followed by
   an address).

Our own helper scripts that read the player's executable at run time (for
example to list its UI forms) may be tracked under `tools/re/`. Their output
may not.

## Forbidden
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
| The README's screenshots of OpenSE4 running on the player's copy (an exception the owner made on 2026-10-03; they show the original's art) | `docs/screenshots/` | yes |
| Analysis projects, listings, raw reverse-engineering notes | `reference/re/` | **no**, gitignored |
| Our reverse-engineering helper scripts (no output) | `tools/re/` | yes |
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

- Reverse engineering: `ghidra` (headless: `/opt/ghidra/support/analyzeHeadless`),
  `rizin`/`cutter`, `gdb` with `wine`. Keep projects and output in
  `reference/re/`.

### Rebuilding the analysis workspace

The scripts in `tools/re/` turn the executable into searchable text under
`reference/re/`. They need a Python virtual environment with `pyghidra`
(`reference/re/venv`), `pefile` for the system Python, and
`GHIDRA_INSTALL_DIR=/opt/ghidra`.

```sh
EXE=".../Space Empires IV Deluxe/se4/Se4.exe"; RE=reference/re
/opt/ghidra/support/analyzeHeadless $RE/ghidra se4 -import "$EXE"   # import and analyse
tools/re/delphi_classes.py "$EXE" $RE/out/classes.json              # Delphi class records
$RE/venv/bin/python tools/re/ghidra_delphi.py $RE/ghidra se4 $RE/out/classes.json [$RE/out/names.tsv]
$RE/venv/bin/python tools/re/ghidra_types.py $RE/ghidra se4 $RE/out/types.tsv
$RE/venv/bin/python tools/re/ghidra_export.py $RE/ghidra se4 $RE/decomp
```

`ghidra_delphi.py` finds the methods that only class records point to, applies
Delphi's register calling convention, names methods after their classes and
marks string literals. `ghidra_types.py` applies structure layouts that we
write by hand from the data-file loaders. `ghidra_export.py` writes the
decompiled functions, an index of callers and strings, and the class tables.
All of this output stays in `reference/re/`.

## Testing against the player's data

Tests use only our own fixture content. The opt-in test

```sh
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests -tc="*installed*"
```

loads the player's installed data set, if present, and checks that it parses without errors.
