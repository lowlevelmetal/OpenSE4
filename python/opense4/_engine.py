"""The player's side of the computer-player protocol (docs/sdk/ai-protocol.md): the
engine calls `dispatch(request)` and gets the response.

- A session starts with a request that carries `player` (and `memory`): the
  dispatcher imports the player's module, makes its Player and gives it the memory.
  Sessions are per empire, so one interpreter can serve several empires at once.
- Each request sets the player's per-request attributes, calls the callback the
  request names, and answers with the commands or the answer, the memory, the notes
  and the log lines.
- An exception from the player's code is answered with `error` (its type, message
  and traceback); the session goes on. `end_session` ends it.

In the game the services are the engine's native module `_opense4`; an external bot
makes a `Dispatcher` with its connection's services (opense4.external).
"""

from __future__ import annotations

import sys
import traceback
from typing import Any, Callable, Dict, Optional

from . import services as _services
from .ai import (BattleState, ColonyTypeQuestion, DecloakQuestion, EntryQuestion, Orders, Player, TacticalOrders)
from .rng import Random

API = 1
PLANNING = ("politics", "orders", "economy")
CALLS = ("politics", "orders", "economy", "colony_type", "enter_sector", "decloak", "battle_round", "end_session")


class ProtocolError(Exception):
    """A request the protocol does not allow."""


class Session:
    """One empire's player for one engine call (docs/sdk/ai-protocol.md, section 2)."""

    def __init__(self, empire: int) -> None:
        self.empire = empire
        self.player: Optional[Player] = None
        self.view: Any = None           # the latest view (opense4.view.View)
        self.failure: Optional[Dict[str, Any]] = None   # why the player could not be made
        self._rules: Any = None

    def rules(self, backend: Any) -> Any:
        """The rules view, asked once per session."""
        if self._rules is None:
            from .rules import Rules
            self._rules = Rules(backend.rules())
        return self._rules


def error_of(e: BaseException) -> Dict[str, Any]:
    """The protocol's `error` for an exception: {type, message, traceback}."""
    try:
        text = "".join(traceback.format_exception(e))
    except Exception:   # the traceback could not be written
        text = ""
    try:
        message = str(e)
    except Exception:
        message = "(the message could not be written)"
    return {"type": type(e).__name__, "message": message, "traceback": text}


def _import_player(info: Dict[str, Any]) -> Player:
    module = info.get("module")
    name = info.get("class")
    if not isinstance(module, str) or not isinstance(name, str):
        raise ProtocolError("`player` names a module and a class: " + repr(info))
    __import__(module)
    mod = sys.modules[module]
    cls = getattr(mod, name, None)
    if cls is None:
        raise ProtocolError("the module " + module + " has no class " + name)
    if not isinstance(cls, type) or not issubclass(cls, Player):
        raise ProtocolError(module + "." + name + " is not a subclass of opense4.ai.Player")
    return cls()


class Dispatcher:
    """Answers the engine's requests for every session it is given."""

    def __init__(self, backend: Optional[Any] = None, factory: Optional[Callable[[], Player]] = None) -> None:
        """`backend`: the services (default: the engine's native module). `factory`: makes
        the player when a session's first request names none (external bots)."""
        self.backend = backend if backend is not None else _services.NativeServices()
        self.factory = factory
        self.sessions: Dict[int, Session] = {}

    # ---- sessions ----

    def _session(self, request: Dict[str, Any]) -> Session:
        empire = request["empire"]
        if "player" in request:
            session = Session(empire)
            self.sessions[empire] = session
            info = request["player"]
            try:
                if self.factory is not None:   # an external bot plays its own player
                    player = self.factory()
                elif info is None:
                    raise ProtocolError("the first request of the session names no player")
                else:
                    player = _import_player(info)
                if not isinstance(player, Player):
                    raise ProtocolError("the player is not an opense4.ai.Player: " + repr(player))
            except Exception as e:
                # Every request of the session answers with this error.
                session.failure = error_of(e)
                return session
            memory = request.get("memory")
            if memory is not None:
                player.memory = memory
            elif player.memory is None:   # the first session: the player's own start, or a map
                player.memory = {}
            session.player = player
            return session
        session = self.sessions.get(empire)
        if session is None:
            raise ProtocolError("no session for empire " + str(empire) + ": a session's first request carries `player`")
        return session

    # ---- requests ----

    def handle(self, request: Dict[str, Any]) -> Dict[str, Any]:
        """The response to one request (docs/sdk/ai-protocol.md, sections 3 and 4)."""
        response: Dict[str, Any] = {"commands": [], "answer": None, "memory": None, "notes": [], "log": []}
        session: Optional[Session] = None
        try:
            if not isinstance(request, dict):
                raise ProtocolError("a request is a map")
            if request.get("api") != API:
                raise ProtocolError("this player speaks api " + str(API) + ", not " + repr(request.get("api")))
            call = request.get("call")
            if call not in CALLS:
                raise ProtocolError("no call " + repr(call))
            session = self._session(request)
            if session.failure is not None:
                response["error"] = session.failure
            else:
                self._run(session, request, response)
        except Exception as e:
            response["error"] = error_of(e)
        except SystemExit as e:
            response["error"] = error_of(e)
        if session is not None and session.player is not None:
            player = session.player
            response["memory"] = player.memory
            notes = getattr(player, "_notes", None)
            response["notes"] = list(notes.values()) if notes else []
            response["log"] = getattr(player, "_log", None) or []
        if isinstance(request, dict) and request.get("call") == "end_session":
            self.sessions.pop(request.get("empire"), None)
        return response

    def _run(self, session: Session, request: Dict[str, Any], response: Dict[str, Any]) -> None:
        player = session.player
        call = request["call"]
        args = request.get("args")
        if args is None:
            args = {}
        seed = request.get("seed")
        # What every request sets on the player.
        player.empire_id = request["empire"]
        player.turn = request.get("turn", 0)
        player.call = call
        player.seed = 0 if seed is None else seed
        player.random = Random(player.seed)
        player.refused = args.get("refused") or []
        player._notes = {}
        player._log = []
        view = request.get("view")
        if view is not None:
            from .view import View
            backend = self.backend
            session.view = View(view, rules=lambda: session.rules(backend))
        player.view = session.view

        _services._current = _services.Context(self.backend, request, session)
        try:
            if call in PLANNING:
                orders = Orders()
                given = getattr(player, call)(session.view, orders)
                if given is not None:
                    orders.extend(given)
                response["commands"] = orders.commands
            elif call == "colony_type":
                question = ColonyTypeQuestion(session.view, args)
                answer = player.colony_type(session.view, question)
                if answer is not None:
                    if not isinstance(answer, str):
                        raise TypeError("colony_type answers a colony type's name or None, not " + repr(answer))
                    if question.choices and answer not in question.choices:
                        raise ValueError("colony_type: " + repr(answer) + " is not one of " + ", ".join(question.choices))
                response["answer"] = answer
            elif call == "enter_sector":
                response["answer"] = _yes_no(call, player.enter_sector(session.view, EntryQuestion(session.view, args)))
            elif call == "decloak":
                response["answer"] = _yes_no(call, player.decloak(session.view, DecloakQuestion(session.view, args)))
            elif call == "battle_round":
                battle = args.get("battle")
                orders = TacticalOrders(request["empire"])
                given = player.battle_round(BattleState(battle if battle is not None else {}, request["empire"]), orders)
                if given is not None and given is not True and given is not False:
                    orders.extend(given)
                response["answer"] = {"orders": orders.commands} if orders.commands else None
            elif call == "end_session":
                player.end_session()
        finally:
            _services._current = None


def _yes_no(call: str, answer: Any) -> Optional[bool]:
    if answer is not None and answer is not True and answer is not False:
        raise TypeError(call + " answers True, False or None, not " + repr(answer))
    return answer


_dispatcher: Optional[Dispatcher] = None


def dispatch(request: Dict[str, Any]) -> Dict[str, Any]:
    """The engine's entry point in the game: one request in, its response out."""
    global _dispatcher
    if _dispatcher is None:
        _dispatcher = Dispatcher()
    return _dispatcher.handle(request)
