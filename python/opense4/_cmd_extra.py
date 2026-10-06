"""Helpers of opense4.cmd written by hand: shorter ways to say common commands."""

from __future__ import annotations

from typing import Any, Dict, Iterable, List, Sequence

from ._record import Record
from ._values import CENTRE, _fail, id_of


def centre(system: Any) -> Dict[str, Any]:
    """The location of a system's centre sector (a system id or System)."""
    return {"system": id_of(system, "system", "centre.system"), "x": CENTRE, "y": CENTRE}


def give(target: Any, orders: Sequence[Dict[str, Any]], repeat: bool = False) -> Dict[str, Any]:
    """set_orders for a Vehicle, Fleet or Colony (or its planet): its new order list."""
    from .cmd import set_orders
    if isinstance(target, Record):
        if target._kind == "vehicle":
            return set_orders(vehicle=target, orders=orders, repeat=repeat)
        if target._kind == "fleet":
            return set_orders(fleet=target, orders=orders, repeat=repeat)
        if target._kind in ("colony", "space_object"):
            return set_orders(planet=target, orders=orders, repeat=repeat)
    _fail("give.target", "a Vehicle, Fleet or Colony (with ids, use set_orders(vehicle=...))", target)
    return {}


def build(target: Any, what: Any, count: int = 1, position: int = -1) -> Dict[str, Any]:
    """queue_add: build `count` of a design (a Design) or a facility (a rules Facility) in
    the queue of a Colony (or its planet) or of a Vehicle with a space yard."""
    from .cmd import queue_add, queue_item
    if isinstance(what, Record) and what._kind == "view_design":
        item = queue_item(kind="vehicle", design=what, count=count)
    elif isinstance(what, Record) and what._kind == "facility":
        item = queue_item(kind="facility", facility=what, count=count)
    else:
        _fail("build.what", "a Design or a Facility (with ids, use queue_add and queue_item)", what)
        item = {}
    return queue_add(target=target, item=item, position=position)


def only(commands: Iterable[Dict[str, Any]], *kinds: str) -> List[Dict[str, Any]]:
    """The commands of these kinds, in order."""
    return [c for c in commands if c["kind"] in kinds]


def without(commands: Iterable[Dict[str, Any]], *kinds: str) -> List[Dict[str, Any]]:
    """The commands of other kinds, in order."""
    return [c for c in commands if c["kind"] not in kinds]
