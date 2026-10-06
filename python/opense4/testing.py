"""Testing computer players without the game: prepared service answers, requests, and a
harness that plays a Player through the calls of a turn.

    from opense4 import testing
    from ai.player import Prospector

    h = testing.Harness(Prospector, testing.FakeServices(builtin={"orders": []}))
    r = h.call("orders", view=view_map)
    assert "error" not in r and r["commands"]

Runs the same on CPython and in the game's runtime. Under `opense4-sdk test`, which runs a
mod's tests/ in the game's runtime, `game_view()` and `game_rules()` also give a view and the
rules view of a new game of the mod's data set.
"""

from __future__ import annotations

from typing import Any, Callable, Dict, List, Optional, Tuple

from ._engine import API, Dispatcher
from .services import ServiceError, Services


class FakeServices(Services):
    """Services with prepared answers. Each may be a value or a function of the service's
    arguments; `calls` records every service asked, as (service, args).

    rules:    the rules view map
    queries:  {query name: result, or function(args) -> result}
    builtin:  {call: commands, or function(ministers, skip) -> commands}
    answers:  {call: answer, or function(args) -> answer}  (builtin_answer)
    apply:    function(command) -> result (default: {ok: True, reason: ""})
    """

    def __init__(self, rules: Any = None, queries: Optional[Dict[str, Any]] = None, builtin: Optional[Dict[str, Any]] = None,
                 answers: Optional[Dict[str, Any]] = None, apply: Optional[Callable[[Dict[str, Any]], Any]] = None) -> None:
        self._rules = rules
        self._queries = queries if queries is not None else {}
        self._builtin = builtin if builtin is not None else {}
        self._answers = answers if answers is not None else {}
        self._apply = apply
        self.calls: List[Tuple[str, Dict[str, Any]]] = []
        self.applied: List[Dict[str, Any]] = []

    def call(self, service: str, args: Dict[str, Any]) -> Any:
        self.calls.append((service, args))
        if service == "rules":
            if self._rules is None:
                raise ServiceError("no rules view prepared")
            return self._rules
        if service == "query":
            name = args["name"]
            if name not in self._queries:
                raise ServiceError("no answer prepared for the query " + repr(name), "ValueError")
            answer = self._queries[name]
            return answer(args["args"]) if callable(answer) else answer
        if service == "builtin":
            answer = self._builtin.get(args["call"], [])
            return answer(args["ministers"], args["skip"]) if callable(answer) else list(answer)
        if service == "builtin_answer":
            answer = self._answers.get(args["call"])
            return answer(args["args"]) if callable(answer) else answer
        if service == "apply":
            self.applied.append(args["command"])
            if self._apply is not None:
                return self._apply(args["command"])
            return {"ok": True, "reason": ""}
        raise ServiceError("no service " + repr(service), "ValueError")


def request(call: str, view: Optional[Dict[str, Any]] = None, empire: int = 0, turn: int = 1, seed: int = 0,
            args: Optional[Dict[str, Any]] = None, start: bool = False, memory: Any = None,
            player: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """A request of the protocol. `start`: the first of a session (with `player` and `memory`)."""
    r: Dict[str, Any] = {"api": API, "call": call, "empire": empire, "turn": turn, "seed": seed, "view": view,
                         "args": {} if args is None else args}
    if start:
        r["player"] = player
        r["memory"] = memory
    return r


class Harness:
    """Plays one Player through requests, as the engine would, with fake services."""

    def __init__(self, player: Callable[[], Any], services: Optional[Services] = None, empire: int = 0,
                 memory: Any = None) -> None:
        self.services = services if services is not None else FakeServices()
        self.dispatcher = Dispatcher(self.services, factory=player)
        self.empire = empire
        self.memory = memory
        self._started = False

    @property
    def player(self) -> Any:
        """The session's player (after the first call)."""
        s = self.dispatcher.sessions.get(self.empire)
        return None if s is None else s.player

    def call(self, call: str, view: Optional[Dict[str, Any]] = None, args: Optional[Dict[str, Any]] = None, turn: int = 1,
             seed: int = 0) -> Dict[str, Any]:
        """One request; the first starts the session with the harness's memory."""
        r = request(call, view, self.empire, turn, seed, args, start=not self._started, memory=self.memory)
        self._started = True
        response = self.dispatcher.handle(r)
        self.memory = response["memory"]
        if call == "end_session":
            self._started = False
        return response


class Skip(Exception):
    """A test that cannot run here: opense4-sdk test counts it as skipped."""


def _engine_fixtures() -> Any:
    try:
        import _opense4_testing   # opense4-sdk test's, in the game's runtime only
    except ImportError:
        _skip("game_view() and game_rules() need opense4-sdk test, which makes a game of the mod's data set")
    return _opense4_testing


def _skip(reason: str) -> None:
    try:
        import pytest   # under pytest, its own skip
    except ImportError:
        raise Skip(reason)
    pytest.skip(reason)


def game_view(empire: int = 0) -> Dict[str, Any]:
    """The view of `empire` (0 or 1) of a new game of the mod's data set, two computer empires
    from the seed of opense4-sdk test --seed: a view map, as the engine sends it. Skips the
    test where there is no such game (CPython, or no data set)."""
    native = _engine_fixtures()
    try:
        return native.view({"empire": empire})
    except RuntimeError as e:
        _skip(str(e))
    return {}


def game_rules() -> Dict[str, Any]:
    """The rules view of the mod's data set, as the engine sends it (opense4.rules.Rules wraps it)."""
    native = _engine_fixtures()
    try:
        return native.rules({})
    except RuntimeError as e:
        _skip(str(e))
    return {}
