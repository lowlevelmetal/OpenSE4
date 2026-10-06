"""Runs a computer player as an external bot (docs/sdk/bots-and-arena.md):

    python -m opense4.bot admiral:Admiral --port 6722 --token 3f2a... [--slot 0]
    python -m opense4.bot fleet.admiral:Admiral --path mymod/ai --reconnect

The player is `module:Class`, an opense4.ai.Player subclass, imported with the folders of
--path (default: the current folder) in front of the module search path, the way the game
puts a mod's ai/ folder at the root. Where to connect comes from the options, else from the
environment the game's tools give the bots they start (OPENSE4_BOT_HOST, OPENSE4_BOT_PORT,
OPENSE4_BOT_TOKEN, OPENSE4_BOT_SLOT). The token is better given in the environment than on
the command line, where other users of the computer may see it.

Exit status: 0 once the game ended, 1 when the game refused the bot or could not be
reached, 2 for a mistake in the arguments or the player.
"""

from __future__ import annotations

import argparse
import importlib
import os
import sys
from typing import List, Optional

from . import external
from .ai import Player


def load_player(spec: str, paths: List[str]) -> type:
    """The class `module:Class` names, imported with `paths` first on the search path."""
    module_name, sep, class_name = spec.partition(":")
    if not sep or not module_name or not class_name:
        raise ValueError("the player is module:Class, such as admiral:Admiral, not " + repr(spec))
    for p in reversed(paths):
        full = os.path.abspath(p)
        if full not in sys.path:
            sys.path.insert(0, full)
    module = importlib.import_module(module_name)
    cls = module
    for part in class_name.split("."):
        cls = getattr(cls, part, None)
        if cls is None:
            raise ValueError("the module " + module_name + " has no " + class_name)
    if not isinstance(cls, type) or not issubclass(cls, Player):
        raise ValueError(spec + " is not a subclass of opense4.ai.Player")
    return cls


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(prog="python -m opense4.bot",
                                 description="Plays an opense4.ai.Player as an external bot of an OpenSE4 game.")
    ap.add_argument("player", help="module:Class, an opense4.ai.Player subclass")
    ap.add_argument("--path", action="append", default=[], help="a folder to import the player from (repeatable; default: .)")
    ap.add_argument("--host", help="the game's address (default 127.0.0.1, or OPENSE4_BOT_HOST)")
    ap.add_argument("--port", type=int, help="the game's bot port (default %d, or OPENSE4_BOT_PORT)" % external.DEFAULT_PORT)
    ap.add_argument("--token", help="the game's token (default: OPENSE4_BOT_TOKEN)")
    ap.add_argument("--slot", type=int, help="the external slot to play (default: the first free one, or OPENSE4_BOT_SLOT)")
    ap.add_argument("--name", help="the bot's name in the game's log (default: the class's)")
    ap.add_argument("--reconnect", action="store_true", help="connect again whenever the game ends a connection (stop with Ctrl+C)")
    ap.add_argument("--wait", type=float, default=30.0, help="seconds to keep trying while the game does not listen yet (default 30)")
    args = ap.parse_args(argv)
    try:
        cls = load_player(args.player, args.path or ["."])
    except Exception as e:   # the player's own import errors too
        print("opense4 bot: " + type(e).__name__ + ": " + str(e), file=sys.stderr)
        return 2
    try:
        external.run(cls, host=args.host, port=args.port, token=args.token, slot=args.slot, name=args.name or cls.__name__,
                     reconnect=args.reconnect, wait=args.wait)
    except external.Refused:
        return 1
    except OSError as e:
        print("opense4 bot: cannot reach the game: " + str(e), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
