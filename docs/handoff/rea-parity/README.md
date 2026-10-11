# Handoff: the 2026-10-11 parity audit of the original (REA)

This folder carries an unfinished piece of work from a local session to whoever continues it.
Delete `docs/handoff/` when every package below is merged (it is not documentation of
OpenSE4; the rules end up in docs/spec/).

## What was asked

The owner asked for feature parity with the original Space Empires IV Deluxe, checked
against its executable with the REA reverse-engineering tools, **without touching the
features OpenSE4 added on purpose** (the Learn window, mods and the SDK, encrypted
networking, the dedicated server, input scripts, the settings window, and everything the
docs call OpenSE4's own, an OpenSE4 extension or "Our client differs").

## What was done

1. **Analysis (done, local).** Nine analysts compared every dialog of the original (98
   dialog classes) and the engine actions behind them with OpenSE4's code. They worked from
   the executable through REA and the local decompiled export, which stay on the owner's
   machine. Their findings, in plain language and without anything from the listing, are in
   [`findings/`](findings/): about 130 differences, each with what the original does, what
   OpenSE4 does now (with file:line references from commit 900a434), impact (H/M/L),
   evidence and the spec section it belongs in.
2. **Work packages.** The findings are grouped into 12 packages with mostly separate files,
   each in [`tasks/`](tasks/). Read [`brief.md`](brief.md) first; it holds the rules every
   implementer follows (clean room, saved-state format 10, commands, docs, git).
3. **Implementation (started).** Branch `rea/integration` holds the base: commit 85c670b
   bumps the save format to 10 and the network protocol to 8, so every package adds its
   saved fields from format 10. Seven packages were started in parallel and stopped for
   this handoff; their state is in the table below.

## Status of the packages

| Package | Task | Branch | State at handoff |
|---|---|---|---|
| scenarios | [tasks/scenarios.md](tasks/scenarios.md) | `rea/scenarios` | WIP snapshot fa5beb1 (stopped mid-work, not built): classic save version gating from 1.40, scenario fields in the state, tests started. Next: finish the gating tests, then the scenario files, window and Learn tab. |
| economy | [tasks/economy.md](tasks/economy.md) | create from `rea/integration` | Not started; plan left. |
| diplomacy | [tasks/diplomacy.md](tasks/diplomacy.md) | `rea/diplomacy` | WIP snapshot 0e2fdc9 (stopped mid-work, not built): engine side under way (treaty and message rules, packages, AI, score); client partly. |
| mainwin | [tasks/mainwin.md](tasks/mainwin.md) | `rea/mainwin` | WIP snapshot bf571da (stopped mid-work, not built): command changes (Rename, SetMinister's fleet field) and SDK docs under way; no client changes yet. |
| gameflow | [tasks/gameflow.md](tasks/gameflow.md) | `rea/gameflow` | WIP snapshot cb502a2 (stopped mid-work, not built): turn-based engine changes under way (victory's last turn, a human's death at its next turn start, failure reporting); no client changes yet. |
| designs | [tasks/designs.md](tasks/designs.md) | `rea/designs` | WIP snapshot d25b885 (stopped mid-work, not built): intelligence targets (state, commands, SDK, import/export) and client pickers under way. |
| combat | [tasks/combat.md](tasks/combat.md) | create from `rea/integration` | Not started; plan left. |
| units | [tasks/units.md](tasks/units.md) | create from `rea/integration` | Not started. |
| scrap | [tasks/scrap.md](tasks/scrap.md) | create from `rea/integration` | Not started. |
| setup | [tasks/setup.md](tasks/setup.md) | create from `rea/integration` | Not started. |
| stellar | [tasks/stellar.md](tasks/stellar.md) | create from `rea/integration` | Not started. |
| files | [tasks/files.md](tasks/files.md) | create from `rea/integration` after `rea/scenarios` is merged | Not started. |

A WIP snapshot is exactly what the stopped implementer had on disk: read its diff (`git show <commit>`) and finish it, or redo the package if the diff is in the way. "Plan left" means the first implementer read the code and wrote a step-by-step plan at the
end of the task file; follow it unless the code says otherwise.

## Order of work

1. `scenarios` before `files`: `files` reuses the general version gating of the original's
   saved games that `scenarios` adds (saves from version 1.40 up; the stock empire files are
   1.51 and the Tutorial's game is 1.58).
2. The rest are independent. Most edit different files; shared ones (commands.cpp,
   main_window.cpp, empire_status.cpp, serialize_io.hpp, classic_save_*) are split by
   function in each task's "Other packages own" line.
3. Merge each finished branch into `rea/integration`. Expect conflicts in serialize_io.hpp,
   serialize.hpp's "Version 10" comment, `InterfaceOptions` (several packages add remembered
   tabs), the command variant (new commands are appended at its end), docs/sdk/commands.md
   and the serialize test's golden values.

## Finishing (after all packages are merged)

1. Regenerate the determinism goldens once: `OPENSE4_PRINT_GOLDEN=1
   ./build/debug/tests/opense4_tests -tc="determinism*" -s`, paste the values into
   tests/test_determinism.cpp, and say in the commit which rule changes moved them. The
   pre-SDK check (hashing at format 8 must reproduce the old goldens, CLAUDE.md) no longer
   holds once rules change on purpose; update that note too.
2. Fix the serialize test's golden checksum and size; run the whole test suite,
   `python3 tools/gen_sdk_python.py --check`, `python3 tools/gen_sdk_reference.py --check`,
   the SDK's Python tests, `python3 tools/run_input_tests.py --fixture-data --small` and
   `python3 tools/cleanroom_check.py`.
3. Add a "Version 10" summary to serialize.hpp and the protocol 8 paragraph to
   docs/MULTIPLAYER.md "Protocol", and a line to docs/PARITY_PLAN.md.
4. Remove `docs/handoff/`.
5. Ask the owner before merging into `engine-reimplementation` or `main`, pushing those, or
   opening a PR.

## What only the owner's machine can do

The cloud has no installed game, no `reference/` folder and no REA. List these in the final
report so the owner runs them locally:

- tests with `OPENSE4_CLASSIC_DATA=auto` (the Tutorial's game import, the 20 stock empire
  files and the mods' empire files, round trips with the original's saves);
- running the client (it needs the install) and the input scripts that aren't fixture-only;
- `opense4-datacheck` and the text half of `tools/cleanroom_check.py`;
- new questions for the original: the open questions the findings list (for example, what a
  recovery in battle does to launch capacity, a map file from the original's own map
  editor) need a session with the executable. Mark such rules "(inferred)" and add the
  question to the spec instead of guessing silently.

## Owner's standing rules (from the local session's memory)

- Commits are authored as `Matthew Geiger <matthewgeigersoftware@gmail.com>`; never add a
  Co-Authored-By trailer or any AI attribution.
- Never write that the original's license forbids reverse engineering, in docs, commits or
  release notes.
- Push `rea/*` branches freely; ask before pushing `main` or `engine-reimplementation`, before
  force-pushing, and before opening PRs or releases.
