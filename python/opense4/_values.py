"""Argument conversions for the command constructors (opense4.cmd, opense4.order,
opense4.tactical) and the services: ids or the view's objects in, the protocol's plain
values out. A wrong argument is a TypeError or ValueError naming the field, before
anything reaches the engine.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

from . import enums
from ._record import Record

# An id, or a record of the view that stands for it (a Vehicle for a vehicle id).
Ref = Any
# A location: a map {system, x, y}, a (system, x, y) tuple, a system id or System (its
# centre), a SpaceObject (its sector), a Vehicle or Fleet (where it is).
LocationLike = Any
# Resources: a map {minerals, organics, radioactives} or a (minerals, organics, radioactives) tuple.
ResourcesLike = Any
# A construction queue: a Colony or planet (its queue), a Vehicle, or a map {planet, vehicle}.
TargetLike = Any

# The centre sector of a system.
CENTRE = 6

# The record kinds that may stand for each kind of id.
_ID_KINDS: Dict[str, Tuple[str, ...]] = {
    "system": ("system",),
    "object": ("space_object", "colony"),
    "planet": ("space_object", "colony"),
    "empire": ("empire", "my_empire"),
    "vehicle": ("vehicle",),
    "fleet": ("fleet",),
    "design": ("view_design",),
    "message": ("message",),
}


def _fail(where: str, expected: str, value: Any) -> None:
    raise TypeError(where + ": expected " + expected + ", got " + _describe(value))


def _describe(value: Any) -> str:
    if isinstance(value, Record):
        return type(value).__name__
    if isinstance(value, str):
        return "the text " + repr(value)
    return type(value).__name__ + " " + repr(value)


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def id_of(value: Any, kind: str = "", where: str = "id") -> Optional[int]:
    """The id `value` names: an int, None, or a record of the view of the right kind."""
    if value is None or _is_int(value):
        return value
    if isinstance(value, Record):
        kinds = _ID_KINDS.get(kind)
        if kinds is not None and value._kind not in kinds:
            _fail(where, "a " + kind + " id or record", value)
        if "id" in value._d or value._kind == "colony":
            return value.id
    _fail(where, ("a " + kind + " id" if kind else "an id") + " or None", value)
    return None


def _id(value: Any, kind: str, where: str) -> Optional[int]:
    return id_of(value, kind, where)


def _ids(values: Any, kind: str, where: str) -> List[Optional[int]]:
    if isinstance(values, (str, dict, int, Record)) or values is None:
        _fail(where, "a list of " + kind + " ids", values)
    return [id_of(v, kind, where + "[" + str(i) + "]") for i, v in enumerate(values)]


def _ref(value: Any, kind: str, where: str) -> Optional[int]:
    # A ref may name something the view does not list, so any int is fine.
    return id_of(value, kind, where)


def _index(value: Any, where: str, nullable: bool = False) -> Optional[int]:
    if value is None:
        if nullable:
            return None
        _fail(where, "an index", value)
    if _is_int(value):
        return value
    if isinstance(value, Record) and "id" in value._d:
        return value._d["id"]
    if isinstance(value, Record) and "index" in value._d:
        return value._d["index"]
    _fail(where, "an index or a record of the rules", value)
    return None


def _indices(values: Any, where: str) -> List[int]:
    if values is None:
        return []
    return [_index(v, where + "[" + str(i) + "]") for i, v in enumerate(values)]


def _int(value: Any, where: str, nullable: bool = False) -> Optional[int]:
    if value is None and nullable:
        return None
    if not _is_int(value):
        _fail(where, "a whole number", value)
    return value


def _bool(value: Any, where: str, nullable: bool = False) -> Optional[bool]:
    if value is None and nullable:
        return None
    if value is not True and value is not False:
        _fail(where, "True or False", value)
    return value


def _text(value: Any, where: str, nullable: bool = False) -> Optional[str]:
    if value is None and nullable:
        return None
    if not isinstance(value, str):
        _fail(where, "text", value)
    return value


def _int_or_text(value: Any, where: str) -> Union[int, str]:
    if not _is_int(value) and not isinstance(value, str):
        _fail(where, "a whole number or text", value)
    return value


def _map(value: Any, where: str, nullable: bool = False) -> Optional[Dict[str, Any]]:
    # A map of plain values (a mod order's arguments): a copy, its keys text, objects of
    # the view standing for their ids.
    if value is None:
        return None if nullable else {}
    if not isinstance(value, dict):
        _fail(where, "a dict", value)
    out: Dict[str, Any] = {}
    for k, v in value.items():
        if not isinstance(k, str):
            _fail(where, "a dict with text keys", value)
        out[k] = plain(v)
    return out


def _list_of_int(values: Any, where: str) -> List[int]:
    return [_int(v, where + "[" + str(i) + "]") for i, v in enumerate(values)]


def _list_of_text(values: Any, where: str) -> List[str]:
    if isinstance(values, str):
        _fail(where, "a list of texts", values)
    return [_text(v, where + "[" + str(i) + "]") for i, v in enumerate(values)]


def _enum(value: Any, names: Tuple[str, ...], where: str, nullable: bool = False) -> Optional[str]:
    if value is None and nullable:
        return None
    if not isinstance(value, str) or value not in names:
        raise ValueError(where + ": " + repr(value) + " is not one of " + ", ".join(names))
    return value


def _enums(values: Any, names: Tuple[str, ...], where: str, nullable: bool = False) -> Optional[List[str]]:
    if values is None:
        if nullable:
            return None
        return []
    if isinstance(values, str):
        _fail(where, "a list of names", values)
    return [_enum(v, names, where + "[" + str(i) + "]") for i, v in enumerate(values)]


def _stellar(value: Any, where: str) -> int:
    # A stellar manipulation by its number or its name (stellar_action).
    if isinstance(value, str):
        if value not in enums.STELLAR_ACTION:
            raise ValueError(where + ": " + repr(value) + " is not one of " + ", ".join(enums.STELLAR_ACTION))
        return enums.STELLAR_ACTION.index(value)
    return _int(value, where)


def _piece(value: Any, where: str) -> int:
    # A battle piece by its position, or the piece itself.
    if _is_int(value):
        return value
    index = getattr(value, "index", None)
    if _is_int(index):
        return index
    _fail(where, "a piece's position in the battle (or the piece)", value)
    return -1


# ---- Structs --------------------------------------------------------------------------------------


def location_of(value: Any, where: str = "location") -> Dict[str, Any]:
    """A location map from what stands for one (see LocationLike)."""
    if isinstance(value, dict):
        return value
    if isinstance(value, Record):
        kind = value._kind
        d = value._d
        if kind == "location":
            return d
        if kind == "system":
            return {"system": d["id"], "x": CENTRE, "y": CENTRE}
        if kind == "space_object":
            return {"system": d["system"], "x": d["sector"]["x"], "y": d["sector"]["y"]}
        if kind == "colony":
            planet = value.planet
            if planet is not None:
                return location_of(planet, where)
        if kind in ("vehicle", "fleet"):
            return d["location"]
        _fail(where, "a location", value)
    if isinstance(value, (tuple, list)) and len(value) == 3:
        return {"system": id_of(value[0], "system", where + ".system"), "x": _int(value[1], where + ".x"),
                "y": _int(value[2], where + ".y")}
    if _is_int(value):
        return {"system": value, "x": CENTRE, "y": CENTRE}
    _fail(where, "a location", value)
    return {}


def _resources(value: Any, where: str) -> Dict[str, Any]:
    if isinstance(value, dict):
        return value
    if isinstance(value, Record):
        return value._d
    if isinstance(value, (tuple, list)) and len(value) == 3:
        return {"minerals": _int(value[0], where + ".minerals"), "organics": _int(value[1], where + ".organics"),
                "radioactives": _int(value[2], where + ".radioactives")}
    _fail(where, "resources", value)
    return {}


def _queue_target(value: Any, where: str) -> Dict[str, Any]:
    if isinstance(value, dict):
        return value
    if isinstance(value, Record):
        if value._kind in ("colony", "space_object"):
            return {"planet": value.id, "vehicle": None}
        if value._kind == "vehicle":
            return {"planet": None, "vehicle": value.id}
    _fail(where, "a queue target (a Colony, a planet, a Vehicle or {planet, vehicle})", value)
    return {}


def _research_project(value: Any, where: str) -> Dict[str, Any]:
    if value is None or _is_int(value):
        return {"area": value, "progress": 0}
    if isinstance(value, Record) and value._kind == "tech":
        return {"area": value.id, "progress": 0}
    return _plain_struct(value, where)


def _plain_struct(value: Any, where: str) -> Dict[str, Any]:
    if isinstance(value, dict):
        return value
    if isinstance(value, Record):
        return value._d
    _fail(where, "a map", value)
    return {}


_STRUCTS = {
    "location": location_of,
    "resources": _resources,
    "queue_target": _queue_target,
    "research_project": _research_project,
}


def _struct(value: Any, name: str, where: str, nullable: bool = False) -> Optional[Dict[str, Any]]:
    if value is None:
        if nullable:
            return None
        _fail(where, "a " + name, value)
    convert = _STRUCTS.get(name, _plain_struct)
    return convert(value, where)


def _structs(values: Any, name: str, where: str) -> List[Dict[str, Any]]:
    if isinstance(values, (str, dict)):
        _fail(where, "a list of " + name, values)
    return [_struct(v, name, where + "[" + str(i) + "]") for i, v in enumerate(values)]


def _orders(values: Any, where: str) -> List[Dict[str, Any]]:
    if isinstance(values, (str, dict)):
        _fail(where, "a list of orders", values)
    out = []
    for i, v in enumerate(values):
        if isinstance(v, Record):
            v = v._d
        if not isinstance(v, dict) or "kind" not in v:
            _fail(where + "[" + str(i) + "]", "an order (opense4.order)", v)
        out.append(v)
    return out


# ---- Plain values for the engine -----------------------------------------------------------------


def plain(value: Any) -> Any:
    """`value` with the view's records replaced by what the engine takes: an entity by its
    id, any other record by its map. Lists, tuples and dicts are converted throughout."""
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, Record):
        if value._kind == "colony" or "id" in value._d:
            if value._kind not in ("location", "resources"):
                return value.id
        return value._d
    if isinstance(value, dict):
        return {k: plain(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(v) for v in value]
    return value
