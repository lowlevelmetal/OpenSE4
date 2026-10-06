"""A step-by-step environment for training computer players, machine-learning ones above all
(docs/sdk/bots-and-arena.md). CPython 3.10 or newer, no other package needed.

    from opense4 import cmd, env, order

    game = env.Game(seed=7, opponents=["builtin"], turns=200)
    view = game.reset()
    done = False
    while not done:
        commands = my_policy(view)                  # a list of commands (opense4.cmd)
        view, reward, done, info = game.step(commands)
    game.close()

One empire, empire 0, is played step by step: each step gives the commands of one game
turn, which are applied where a player gives its orders (the `orders` call), and returns
the view at the start of the next turn, the change in the empire's score as the reward,
whether the game ended, and what else there is to know (`info`). The other empires are
played by the opponents: the classic AI ("builtin") or players of mods ("mod.id:Player",
with the mod in `mods`). The same seed and the same commands give the same game.

The engine is `opense4-sdk env-host`, run as a child process and reached over the external
bots' connection; `sdk` names it, else OPENSE4_SDK, else opense4-sdk on PATH.
"""

from __future__ import annotations

from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

from . import external
from .view import View


class EnvError(RuntimeError):
    """The engine could not start, or stopped."""


class Game:
    """A game for one empire played step by step. See the module's documentation."""

    def __init__(self, seed: int = 1, setup: Optional[str] = None, opponents: Sequence[str] = ("builtin",), turns: int = 100,
                 races: Sequence[str] = (), systems: int = 0, quadrant_size: int = 0, turn_based: bool = False,
                 builtin: Sequence[str] = (), data: Optional[str] = None, mods: Sequence[str] = (), mods_dir: Optional[str] = None,
                 sdk: Optional[str] = None, log: Optional[str] = None, timeout: float = 86400.0) -> None:
        """
        seed:       the first reset's seed; reset(seed=...) chooses another
        setup:      a server setup file (options, empires and their races), else:
        opponents:  who plays the other empires, one entry each: "builtin" or "mod.id:Player"
        turns:      game turns at most
        races, systems, quadrant_size, turn_based: the game, as for opense4-sdk arena
        builtin:    the calls the classic AI answers for our empire: any of "politics" and
                    "economy" (the rest of our turn is ours: the commands of each step)
        data, mods, mods_dir: the game folder and mods (default: the installed game)
        sdk:        the opense4-sdk program
        log:        a file for the engine's log (default: discarded)
        timeout:    how long the engine waits for a step, in seconds
        """
        self.seed = seed
        self.setup = setup
        self.opponents = list(opponents)
        self.turns = turns
        self.races = list(races)
        self.systems = systems
        self.quadrant_size = quadrant_size
        self.turn_based = turn_based
        self.builtin = tuple(builtin)
        for call in self.builtin:
            if call not in ("politics", "economy"):
                raise ValueError("builtin: the classic AI may answer politics and economy, not " + repr(call))
        self.data = data
        self.mods = list(mods)
        self.mods_dir = mods_dir
        self.sdk = sdk
        self.log = log
        self.timeout = timeout
        self.view: Optional[View] = None       # the view of the step under way
        self.empire_id = 0
        self.result: Optional[Dict[str, Any]] = None   # how the game ended, once it did
        self._resets = 0
        self._process: Any = None
        self._conn: Optional[external.SocketConnection] = None
        self._request: Optional[Dict[str, Any]] = None
        self._rules: Any = None
        self._score = 0

    # ---- the engine ----

    def _program(self) -> str:
        import os
        import shutil
        for candidate in (self.sdk, os.environ.get("OPENSE4_SDK"), shutil.which("opense4-sdk")):
            if candidate:
                return candidate
        raise EnvError("opense4-sdk was not found: give Game(sdk=...) or set OPENSE4_SDK")

    def _start(self, seed: int) -> None:
        import json
        import os
        import secrets
        import subprocess
        token = secrets.token_hex(16)
        args = [self._program(), "env-host", "--seed=" + str(seed), "--turns=" + str(self.turns), "--port=0",
                "--timeout=" + str(int(self.timeout)), "--quadrant-size=" + str(self.quadrant_size)]
        for o in self.opponents:
            args.append("--opponent=" + o)
        for r in self.races:
            args.append("--race=" + r)
        if self.setup:
            args.append("--setup=" + str(self.setup))
        if self.systems:
            args.append("--systems=" + str(self.systems))
        if self.turn_based:
            args.append("--turn-based")
        if self.data:
            args.append("--data=" + str(self.data))
        for m in self.mods:
            args.append("--mod=" + str(m))
        if self.mods_dir:
            args.append("--mods-dir=" + str(self.mods_dir))
        environment = dict(os.environ, OPENSE4_BOT_TOKEN=token)
        self._log_file = open(self.log, "ab") if self.log else subprocess.DEVNULL
        self._process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=self._log_file, stdin=subprocess.DEVNULL, env=environment)
        line = self._process.stdout.readline()
        try:
            hello = json.loads(line.decode("utf-8")) if line else None
        except ValueError:
            hello = None
        if not isinstance(hello, dict) or "port" not in hello:
            code = self._process.wait()
            raise EnvError("opense4-sdk env-host did not start (exit {}){}".format(
                code, "; see " + self.log if self.log else "; give Game(log=...) to see why"))
        self.empire_id = hello.get("empire", 0)
        self._conn = external.connect("127.0.0.1", hello["port"], token, 0, "opense4.env", wait=30.0)

    def close(self) -> None:
        """Ends the game and the engine."""
        if self._conn is not None:
            self._conn.say_goodbye("the environment closed")
            self._conn = None
        if self._process is not None:
            try:
                self._process.wait(timeout=10)
            except Exception:
                self._process.kill()
                self._process.wait()
            if self._process.stdout:
                self._process.stdout.close()
            self._process = None
        log = getattr(self, "_log_file", None)
        if log is not None and hasattr(log, "close"):
            log.close()
        self._log_file = None
        self._request = None

    def __enter__(self) -> "Game":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    # ---- the protocol: everything but our orders is answered here ----

    def _advance(self) -> Optional[Dict[str, Any]]:
        """Answers the engine's requests until it asks for our orders (returned), or the
        game ends (None)."""
        conn = self._conn
        assert conn is not None
        while True:
            request = conn.receive()
            if request is None:
                bye = conn.goodbye or {}
                self.result = bye
                return None
            call = request.get("call")
            if call == "orders":
                return request
            response: Dict[str, Any] = {"commands": [], "answer": None, "notes": [], "log": []}
            if call in self.builtin:
                response["commands"] = conn.service("builtin", {"call": call, "ministers": None, "skip": []})
            conn.send(response)

    def _wrap(self, request: Dict[str, Any]) -> View:
        return View(request["view"], rules=self._rules_view)

    def _rules_view(self) -> Any:
        if self._rules is None:
            from .rules import Rules
            if self._conn is None or self._request is None:
                raise EnvError("the rules view is asked while a step is under way (after reset, before step)")
            self._rules = Rules(self._conn.service("rules", {}))
        return self._rules

    @staticmethod
    def _score_of(view: View) -> int:
        my = view.raw.get("my") or {}
        score = my.get("score")
        return score if isinstance(score, int) else 0

    # ---- the environment ----

    def reset(self, seed: Optional[int] = None) -> View:
        """Starts a new game (seed: the constructor's for the first reset, one more for each
        later one, unless given) and returns the view at the start of our first turn."""
        self.close()
        if seed is None:
            seed = self.seed + self._resets
        self._resets += 1
        self.current_seed = seed
        self.result = None
        self._rules = None
        self._start(seed)
        self._request = self._advance()
        if self._request is None:
            raise EnvError("the game ended before our first turn: " + repr(self.result))
        self.view = self._wrap(self._request)
        self._score = self._score_of(self.view)
        return self.view

    def step(self, commands: Iterable[Dict[str, Any]] = ()) -> Tuple[Optional[View], int, bool, Dict[str, Any]]:
        """Gives this turn's commands, plays the turn, and returns (view, reward, done, info):
        the view at the start of the next turn (the last one once done), the change in our
        score, whether the game ended (turns played, a victory, our empire destroyed), and
        info: the turn, the commands the game refused, how it ended."""
        if self._request is None or self._conn is None:
            raise EnvError("no step is under way: call reset() first" if self.result is None else "the game is over: call reset()")
        listed = [c for c in commands]
        problem = external.value_problem(listed, "the commands")
        if problem:
            raise ValueError(problem)
        self._conn.send({"commands": listed, "answer": None, "notes": [], "log": []})
        request = self._advance()
        self._request = request
        if request is None:
            result = self.result or {}
            final = self._score
            for entry in result.get("scores", []):
                if entry.get("empire") == self.empire_id:
                    final = entry.get("score", final)
            reward = final - self._score
            self._score = final
            info = {"turn": result.get("turn"), "refused": [], "game_over": result.get("game_over", False),
                    "winner": result.get("winner"), "won_by": result.get("won_by"), "scores": result.get("scores", []),
                    "reason": result.get("reason")}
            return self.view, reward, True, info
        self.view = self._wrap(request)
        score = self._score_of(self.view)
        reward = score - self._score
        self._score = score
        args = request.get("args") or {}
        info = {"turn": request.get("turn"), "refused": args.get("refused", [])}
        return self.view, reward, False, info

    def query(self, name: str, **args: Any) -> Any:
        """A query of docs/sdk/view.md ("Queries") on the state of the step under way, as a
        record of its result type: game.query("path", vehicle=ship, destination=home)."""
        if self._request is None or self._conn is None:
            raise EnvError("queries are asked while a step is under way (after reset, before step)")
        from . import _records
        from ._values import plain
        from .view import record
        spec = _records.QUERIES.get(name)
        if spec is None:
            raise ValueError("no query " + repr(name) + "; the queries are " + ", ".join(_records.QUERIES))
        names, results = spec
        for key in args:
            if key not in names:
                raise TypeError(name + ": no argument " + repr(key) + "; it takes " + ", ".join(names))
        result = self._conn.service("query", {"name": name, "args": {k: plain(v) for k, v in args.items()}})
        kind = results[0] if results else None
        for r in results if len(results) > 1 else ():
            for key in args:
                if r.startswith(key + "_"):
                    kind = r
        return record(kind, result, self.view) if kind is not None and isinstance(result, dict) else result

    def builtin_commands(self, call: str = "orders", ministers: Optional[List[str]] = None, skip: Sequence[str] = ()) -> List[Dict[str, Any]]:
        """The commands the classic AI would give our empire now (opense4.builtin): a baseline
        to imitate, or to mix with one's own."""
        if self._request is None or self._conn is None:
            raise EnvError("the classic AI is asked while a step is under way (after reset, before step)")
        return self._conn.service("builtin", {"call": call, "ministers": ministers, "skip": list(skip)})
