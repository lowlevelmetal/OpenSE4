"""The base of every typed record (opense4._records, opense4.view, opense4.rules).

A record wraps one map of the engine's values without copying it: each property reads
its field when asked. Ids are resolved through the view the record belongs to.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

# The final class of each documented type, by its name in the docs ("vehicle",
# "location"): filled by opense4._records, opense4.view and opense4.rules.
CLASSES: Dict[str, Any] = {}


class Record:
    """A typed record over one map of the engine's values.

    Every documented field is a property. `record.raw` is the map itself, and
    `record["field"]` reads a field by its name, exactly as the engine sent it.
    """

    _kind = ""

    def __init__(self, view: Any, data: Dict[str, Any]) -> None:
        self._v = view
        self._d = data

    @property
    def raw(self) -> Dict[str, Any]:
        """The map this record reads, as the engine sent it."""
        return self._d

    def __getitem__(self, key: str) -> Any:
        return self._d[key]

    def __contains__(self, key: str) -> bool:
        return key in self._d

    def get(self, key: str, default: Any = None) -> Any:
        """The field `key` as the engine sent it, or `default`."""
        return self._d.get(key, default)

    def __eq__(self, other: Any) -> bool:
        return type(other) is type(self) and other._d == self._d

    def __ne__(self, other: Any) -> bool:
        return not self.__eq__(other)

    def __hash__(self) -> int:
        raise TypeError("unhashable record: " + type(self).__name__)

    def __repr__(self) -> str:
        d = self._d
        parts = []
        for key in ("id", "name"):
            if key in d:
                parts.append(key + "=" + repr(d[key]))
        if not parts:
            n = 0
            for key in d:
                if n == 4:
                    parts.append("...")
                    break
                parts.append(key + "=" + repr(d[key]))
                n += 1
        return type(self).__name__ + "(" + ", ".join(parts) + ")"


class Entity(Record):
    """A record with an id: equal to another of the same kind with the same id, and
    usable as a dict key or in a set."""

    @property
    def id(self) -> int:
        return self._d["id"]

    def __eq__(self, other: Any) -> bool:
        return isinstance(other, Entity) and other._kind == self._kind and other.id == self.id

    def __hash__(self) -> int:
        return hash((self._kind, self.id))


class NoView:
    """What records outside a view resolve their ids with: nothing is found."""

    def resolve(self, kind: str, id: Optional[int]) -> Any:
        return None

    def resolve_all(self, kind: str, ids: List[int]) -> List[Any]:
        return [None for _ in ids]


NO_VIEW = NoView()


def wrap(kind: str, view: Any, data: Optional[Dict[str, Any]]) -> Any:
    """`data` as a record of the type `kind`, or None."""
    if data is None:
        return None
    return CLASSES[kind](view, data)


def wrap_list(kind: str, view: Any, items: Optional[List[Dict[str, Any]]]) -> Any:
    """Each map of `items` as a record of the type `kind` (None stays None)."""
    if items is None:
        return None
    cls = CLASSES[kind]
    return [cls(view, x) for x in items]
