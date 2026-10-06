"""The rules scripts' side of the engine (docs/sdk/rules.md): the engine calls
`dispatch(request)` and gets the response.

- `load` imports each rules mod's modules (its scripts/ folder), so that they register
  their functions with opense4.rules, and answers what each mod registered.
- Every other call runs one mod's functions: a hook (`hook`), an order's check or effect
  (`order_check`, `order`), an event (`event`), an intelligence project
  (`intel_project`), a victory condition (`victory`) or an objective's action
  (`objective`). Each gets a fresh `Game` and `fx`; the response carries the function's
  result, the mod data it changed, its log lines, or the error it raised (with its
  traceback).
"""

from __future__ import annotations

import sys
from typing import Any, Dict, List, Optional

from . import rules as _rules

API = 1


def error_of(e: BaseException) -> Dict[str, Any]:
    """The protocol's `error` for an exception: {type, message, traceback}."""
    import traceback
    try:
        text = "".join(traceback.format_exception(e))
    except Exception:   # the traceback could not be written
        text = ""
    try:
        message = str(e)
    except Exception:
        message = "(the message could not be written)"
    return {"type": type(e).__name__, "message": message, "traceback": text}


class ProtocolError(Exception):
    """A request the protocol does not allow."""


# What a rules call may import as it runs, imported once when the scripts load: a call's
# cost in bytecodes then does not depend on what ran before it in the interpreter.
_PRELOAD = ("traceback", "opense4._record", "opense4._records", "opense4._values", "opense4.view", "opense4.rules")


def _load(request: Dict[str, Any]) -> Dict[str, Any]:
    for name in _PRELOAD:
        __import__(name)
    out: List[Dict[str, Any]] = []
    for mod in request.get("mods") or []:
        mod_id = mod["id"]
        entry: Dict[str, Any] = {"id": mod_id, "registered": [], "error": None}
        _rules._loading = mod_id
        try:
            for name in mod.get("modules") or []:
                if name in sys.modules:
                    continue   # another mod's module of the same name was left out (logged by the engine)
                __import__(name)
        except BaseException as e:
            entry["error"] = error_of(e)
        finally:
            _rules._loading = None
        entry["registered"] = [{"kind": r.kind, "name": r.name, "step": r.step or "", "when": r.when or ""}
                               for r in _rules.registrations(mod_id)]
        out.append(entry)
    return {"mods": out}


def _location(game: Any, value: Any) -> Any:
    from ._record import CLASSES
    return None if value is None else CLASSES["location"](game, value)


def _hook_args(game: Any, name: str, a: Dict[str, Any]) -> List[Any]:
    """The arguments a hook's function gets between `game` and `fx` (docs/sdk/rules.md, "Hooks")."""
    if name in ("generate_galaxy", "after_galaxy", "turn_start", "check_victory", "turn_end"):
        return []
    if name == "new_game":
        return [_rules.Setup(game, a["setup"])]
    if name == "orders_applied":
        return [game.empire(a["empire"])]
    if name == "movement_day":
        return [a["day"]]
    if name == "vehicle_entered_sector":
        return [game.vehicle(a["vehicle"]), _location(game, a["location"])]
    if name == "before_battle":
        return [_rules.BattleSite(game, a)]
    if name == "after_battle":
        from ._record import CLASSES
        return [None if a["battle"] is None else CLASSES["battle"](game, a["battle"])]
    if name == "vehicle_destroyed":
        from ._record import CLASSES
        return [None if a["vehicle"] is None else CLASSES["vehicle"](game, a["vehicle"]), a["cause"]]
    if name == "empire_end_of_turn":
        return [game.empire(a["empire"]), a["step"], a["when"]]
    if name in ("colony_end_of_turn", "colony_founded"):
        return [game.colony(a["colony"])]
    if name == "vehicle_built":
        return [_rules.Built(game, a)]
    if name == "tech_researched":
        return [game.empire(a["empire"]), game.rules.tech(a["area"]), a["level"]]
    if name == "treaty_changed":
        return [game.empire(a["empire"]), game.empire(a["other"]), a["treaty"], a["old_treaty"]]
    if name == "message_sent":
        from ._record import CLASSES
        return [None if a["message"] is None else CLASSES["message"](game, a["message"])]
    if name == "event_fired":
        return [_rules.EventInfo(game, a)]
    raise ProtocolError("no hook " + repr(name))


def _result(value: Any) -> Any:
    """A function's answer as plain values: an object of the game stands for its id."""
    from ._record import Record
    if isinstance(value, Record):
        return value.id
    return value


def _run(request: Dict[str, Any], game: Any, fx: Any) -> Any:
    call = request.get("call")
    mod = request.get("mod")
    name = request.get("name")
    args = request.get("args") or {}
    if call == "hook":
        step = args.get("step") if name == "empire_end_of_turn" else None
        when = args.get("when") if name == "empire_end_of_turn" else None
        found = [r for r in _rules.registrations(mod) if r.matches(mod, "hook", name, step, when)]
        extra = _hook_args(game, name, args)
        for r in found:
            r.fn(game, *(extra + [fx]))
        return None
    found = [r for r in _rules.registrations(mod) if r.matches(mod, call, name)]
    if not found:
        raise ProtocolError("the mod " + str(mod) + " registered no " + str(call) + " " + repr(name))
    fn = found[0].fn
    if call == "order_check":
        answer = fn(game, _rules.OrderInfo(game, args["order"]))
        if answer is None or answer is True:
            return None
        if answer is False or isinstance(answer, str):
            return answer
        raise TypeError("an order's check answers None (it may be given) or the reason it is refused, not " + repr(answer))
    if call == "order":
        fn(game, _rules.OrderInfo(game, args["order"]), fx)
        return None
    if call == "event":
        fn(game, _rules.EventInfo(game, args["event"]), fx)
        return None
    if call == "intel_project":
        return bool(fn(game, _rules.ProjectInfo(game, args["project"]), fx))
    if call == "victory":
        answer = fn(game)
        if answer is None or answer is False or answer is True:
            return answer
        return _result(answer)
    if call == "objective":
        fn(game, _rules.ObjectiveInfo(game, args["objective"]), fx)
        return None
    raise ProtocolError("no call " + repr(call))


def dispatch(request: Dict[str, Any], backend: Any = None) -> Dict[str, Any]:
    """The engine's entry point: one request in, its response out."""
    if not isinstance(request, dict) or request.get("api") != API:
        return {"error": {"type": "ProtocolError", "message": "a request is a map of api " + str(API), "traceback": ""}}
    if request.get("call") == "load":
        return _load(request)
    response: Dict[str, Any] = {"result": None, "mod_data": [], "log": []}
    game: Optional[_rules.Game] = None
    del _rules._log[:]
    try:
        game = _rules.Game(backend)
        fx = _rules.NativeEffects(game)
        response["result"] = _run(request, game, fx)
    except BaseException as e:
        response["error"] = error_of(e)
    if game is not None:
        response["mod_data"] = game._changed_mod_data()
    response["log"] = list(_rules._log)
    del _rules._log[:]
    return response
