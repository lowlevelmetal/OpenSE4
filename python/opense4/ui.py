"""Interface extensions (docs/sdk/interface.md): the values a mod's ui/*.toml files show
that its own Python works out.

A mod's ui/*.py files register functions with `ui.value`:

    from opense4 import ui

    @ui.value("charge")
    def charge(view, ship):
        return ship.supply * 100 // max(1, ship.design.figures.supply)

Each gets `view`, the player's own view of the game (docs/sdk/view.md, read from the
engine a part at a time: what the player knows, never more), and the thing the value is
shown for: a Vehicle, Fleet, SpaceObject (a planet), Colony, System, Empire or Design. It
answers a whole number, text, True or False, None (no value: shown as "-"), or a list of
these (shown joined with ", ").

Interface code runs on the player's own computer, in the sandbox, with a small budget per
value. It reads the game and never changes it: there are no effects and no commands here
(a panel's button gives a mod order as a command, docs/sdk/rules.md "Orders"). Values are
worked out again only when the game changes (a command given, a new turn), never every
frame; nothing lasts in module globals from one batch of values to the next.
"""

from __future__ import annotations

import sys
from typing import Any, Callable, Dict, List, Optional

from .view import View as _View, _LISTS

API = 1

# The kinds of things a value may be shown for, as the engine names them.
THINGS = ("vehicle", "fleet", "object", "colony", "system", "empire", "design")


class Registration:
    """One registered function: its mod, kind ("value") and name."""

    def __init__(self, mod: str, kind: str, name: str, fn: Callable[..., Any]) -> None:
        self.mod = mod
        self.kind = kind
        self.name = name
        self.fn = fn


_registry: List[Registration] = []
_loading: Optional[str] = None   # the mod whose ui/ files the engine is importing


def value(name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the decorated function as the computed value `name` (`value = "name"` in
    a row of a [[panel]], a [[column]] or an [[empire_page.column]]): `f(view, thing)`."""
    if not isinstance(name, str) or not name:
        raise TypeError("ui.value needs the value's name, as the mod's ui/*.toml files name it")

    def register(fn: Callable[..., Any]) -> Callable[..., Any]:
        _registry.append(Registration(_loading if _loading is not None else "", "value", name, fn))
        return fn
    return register


def registrations(mod: Optional[str] = None) -> List[Registration]:
    """Everything registered (by one mod, or all), in order."""
    return [r for r in _registry if mod is None or r.mod == mod]


def find(mod: str, name: str) -> Optional[Callable[..., Any]]:
    """The function a mod registered as the value `name`, or None."""
    for r in _registry:
        if r.mod == mod and r.kind == "value" and r.name == name:
            return r.fn
    return None


def clear() -> None:
    """Forgets every registration (for tests)."""
    del _registry[:]


# ---- The player's view, read from the engine a part at a time --------------------------------------------

class _Backend:
    """The engine's native functions for interface values (module _opense4_ui)."""

    def __init__(self, native: Any = None) -> None:
        self._native = native

    def call(self, name: str, arg: Dict[str, Any]) -> Any:
        if self._native is None:
            import _opense4_ui  # the engine's, in the game only
            self._native = _opense4_ui
        return getattr(self._native, name)(arg)


class _Data:
    """The player's view map (docs/sdk/view.md), each part read from the engine when first used."""

    def __init__(self, backend: _Backend, empire: Optional[int]) -> None:
        self._backend = backend
        self._parts: Dict[str, Any] = {"api": 1, "empire": empire, "whole": False}

    def __getitem__(self, key: str) -> Any:
        parts = self._parts
        if key not in parts:
            parts[key] = self._backend.call("read", {"what": key})
        return parts[key]

    def __contains__(self, key: str) -> bool:
        return key in ("api", "empire", "whole", "game", "my", "empires", "systems", "objects", "colonies", "vehicles",
                       "fleets", "designs", "messages", "log", "battles")

    def get(self, key: str, default: Any = None) -> Any:
        return self[key] if key in self else default

    def loaded(self, key: str) -> bool:
        return key in self._parts


class PlayerView(_View):
    """The player's own view of the game (opense4.view.View), read from the engine a part
    at a time; a lookup by id (`view.vehicle(id)`) reads that thing only."""

    def __init__(self, backend: Optional[_Backend] = None, empire: Optional[int] = None) -> None:
        b = backend if backend is not None else _Backend()
        self._backend = b
        from .rules import Rules
        _View.__init__(self, _Data(b, empire), rules=lambda: Rules(b.call("rules", {})))

    def resolve(self, kind: str, id: Optional[int]) -> Any:
        if id is None:
            return None
        found = self._found.get(kind)
        if found is None:
            found = {}
            self._found[kind] = found
        r = found.get(id)
        if r is None:
            field, record, key = _LISTS[kind]
            if self._d.loaded(field):
                r = _View.resolve(self, kind, id)
            else:
                from ._record import CLASSES
                raw = self._backend.call("read", {"what": kind, "id": id})
                r = None if raw is None else CLASSES[record](self, raw)
            if r is not None:
                found[id] = r
        return r

    def ability(self, thing: Any, name: str, kind: Optional[str] = None) -> Optional[int]:
        """The value of an ability (a declared one or the game's own) on one of the
        player's vehicles, designs or colonies, or on a system; None when it does not
        carry it."""
        from .rules import target_of
        t = target_of(thing, kind or "")
        if t is None:
            raise TypeError("ability: no thing")
        if kind is not None:
            t["kind"] = kind
        return self._backend.call("ability", {"kind": t["kind"], "id": t["id"], "name": name})


# ---- The engine's side ---------------------------------------------------------------------------------

def _error_of(e: BaseException) -> Dict[str, Any]:
    import traceback
    try:
        text = "".join(traceback.format_exception(e))
    except Exception:
        text = ""
    try:
        message = str(e)
    except Exception:
        message = "(the message could not be written)"
    return {"type": type(e).__name__, "message": message, "traceback": text}


# What a value may import as it runs, imported when the modules load.
_PRELOAD = ("traceback", "opense4._record", "opense4._records", "opense4._values", "opense4.view", "opense4.rules")


def _load(request: Dict[str, Any]) -> Dict[str, Any]:
    """Imports each mod's ui/ modules so that they register their values; answers what
    each registered."""
    global _loading
    for name in _PRELOAD:
        __import__(name)
    out: List[Dict[str, Any]] = []
    for mod in request.get("mods") or []:
        mod_id = mod["id"]
        entry: Dict[str, Any] = {"id": mod_id, "registered": [], "error": None}
        _loading = mod_id
        try:
            for name in mod.get("modules") or []:
                if name in sys.modules:
                    continue   # another mod's module of the same name was left out (the engine says so)
                __import__(name)
        except BaseException as e:
            entry["error"] = _error_of(e)
        finally:
            _loading = None
        entry["registered"] = [r.name for r in registrations(mod_id)]
        out.append(entry)
    return {"mods": out}


_view: Optional[PlayerView] = None


def _thing(view: PlayerView, kind: str, id: Any) -> Any:
    if kind == "vehicle":
        return view.vehicle(id)
    if kind == "fleet":
        return view.fleet(id)
    if kind == "object":
        return view.object(id)
    if kind == "colony":
        return view.colony(id)
    if kind == "system":
        return view.system(id)
    if kind == "empire":
        return view.empire(id)
    if kind == "design":
        return view.design(id)
    raise ValueError("no kind of thing " + repr(kind))


def _plain(v: Any) -> Any:
    """A value's answer as the engine takes it: whole numbers, text, booleans, None, lists."""
    from ._record import Record
    if v is None or isinstance(v, (bool, int, str)):
        return v
    if isinstance(v, Record):
        name = getattr(v, "name", None)
        return name if isinstance(name, str) else v.id
    if isinstance(v, (list, tuple)):
        return [_plain(x) for x in v]
    if isinstance(v, float):
        raise TypeError("a value is a whole number, text, True, False, None or a list of these, not a float (round it)")
    raise TypeError("a value is a whole number, text, True, False, None or a list of these, not " + type(v).__name__)


def _value(request: Dict[str, Any], backend: Any = None) -> Dict[str, Any]:
    """One value: {mod, name, kind, id} in, {value} or {error} out. The view is shared by
    the values of one batch."""
    global _view
    try:
        if _view is None:
            _view = PlayerView(backend, request.get("empire"))
        fn = find(request["mod"], request["name"])
        if fn is None:
            raise LookupError("the mod " + str(request["mod"]) + " registers no value " + repr(request["name"]) +
                              " (@ui.value(\"" + str(request["name"]) + "\") in its ui/*.py)")
        thing = _thing(_view, request["kind"], request["id"])
        if thing is None:
            return {"value": None}
        return {"value": _plain(fn(_view, thing))}
    except BaseException as e:
        return {"error": _error_of(e)}


def dispatch(request: Dict[str, Any], backend: Any = None) -> Dict[str, Any]:
    """The engine's entry point: `load` imports the mods' ui/ modules, `value` works out one value."""
    if not isinstance(request, dict) or request.get("api") != API:
        return {"error": {"type": "ProtocolError", "message": "a request is a map of api " + str(API), "traceback": ""}}
    call = request.get("call")
    if call == "load":
        return _load(request)
    if call == "value":
        return _value(request, backend)
    return {"error": {"type": "ProtocolError", "message": "no call " + repr(call), "traceback": ""}}
