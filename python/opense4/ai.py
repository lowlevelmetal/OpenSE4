"""Computer players (docs/MODDING_SDK.md, section 6; docs/sdk/ai-protocol.md).

A computer player is a subclass of `Player` that overrides the decisions it wants to
make; every one it leaves alone is the classic AI's:

    from opense4 import ai, cmd, order

    class Admiral(ai.Player):
        def orders(self, view, orders):
            for fleet in view.my.idle_fleets:
                target = view.galaxy.nearest(fleet, view.enemy_colonies)
                if target is not None:
                    orders.add(cmd.give(fleet, [order.attack(object=target.planet)]))
            orders.extend(ai.builtin.orders(view, skip=["attack"]))

`ai.builtin` (opense4.builtin) is the classic AI as a library.
"""

from __future__ import annotations

from typing import Any, Dict, Iterable, List, Optional

from . import builtin, services
from ._record import NO_VIEW, Record
from ._values import _int, plain
from .rng import Random

__all__ = ["Player", "Orders", "TacticalOrders", "ColonyTypeQuestion", "EntryQuestion", "DecloakQuestion",
           "BattleState", "Piece", "builtin"]


class Orders:
    """The commands a planning call gives, in order (docs/sdk/commands.md)."""

    def __init__(self) -> None:
        self._commands: List[Dict[str, Any]] = []

    def add(self, command: Dict[str, Any]) -> None:
        """Adds a command (a map from opense4.cmd, or one the classic AI gave)."""
        if isinstance(command, Record):
            command = command.raw
        if not isinstance(command, dict) or "kind" not in command:
            raise TypeError("a command is a map with a kind (opense4.cmd), not " + repr(command))
        self._commands.append(command)

    def extend(self, commands: Iterable[Dict[str, Any]]) -> None:
        """Adds commands, in order."""
        if isinstance(commands, dict):
            raise TypeError("extend() takes a list of commands; add() takes one")
        for c in commands:
            self.add(c)

    def remove(self, *kinds: str) -> None:
        """Drops the commands of these kinds."""
        self._commands = [c for c in self._commands if c["kind"] not in kinds]

    def clear(self) -> None:
        self._commands = []

    @property
    def commands(self) -> List[Dict[str, Any]]:
        """The commands so far (the list itself)."""
        return self._commands

    def __iter__(self) -> Any:
        return iter(self._commands)

    def __len__(self) -> int:
        return len(self._commands)

    def __getitem__(self, i: int) -> Dict[str, Any]:
        return self._commands[i]

    def __bool__(self) -> bool:
        return bool(self._commands)


class TacticalOrders(Orders):
    """The tactical orders a battle round gives (docs/sdk/commands.md, "Tactical battles"),
    each for the player's side: opense4.tactical builds them, and `empire` is filled in."""

    def __init__(self, empire: int) -> None:
        Orders.__init__(self)
        self.empire = empire

    def add(self, command: Dict[str, Any]) -> None:
        if isinstance(command, dict) and command.get("empire") is None and "piece" in command:
            command["empire"] = self.empire
        Orders.add(self, command)

    def fire(self, piece: Any, target: Any, weapon: int = -1) -> None:
        """Fires a piece's weapon (every enabled one: -1) at a target piece."""
        from . import tactical
        self.add(tactical.fire(piece, target, weapon))

    target = fire

    def move(self, piece: Any, x: int, y: int) -> None:
        """Moves a piece toward a square."""
        from . import tactical
        self.add(tactical.move(piece, x, y))


class _Args(Record):
    """A question's arguments (or a service's answer), with the ids resolved in the
    session's view when there is one; other fields read as attributes."""

    def __init__(self, view: Any, data: Dict[str, Any]) -> None:
        Record.__init__(self, view if view is not None else NO_VIEW, data)

    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            raise AttributeError(name)
        d = self._d
        if name in d:
            return d[name]
        raise AttributeError(name)


class ColonyTypeQuestion(_Args):
    """`colony_type`: a colony is founded; which colony type should it have?"""

    @property
    def colony_id(self) -> Any:
        """The new colony, as the engine names it."""
        return self._d.get("colony")

    @property
    def planet(self) -> Any:
        """Its planet (from the session's view; None when that view does not show it)."""
        return self._v.resolve("object", self._d.get("planet"))

    @property
    def planet_id(self) -> Optional[int]:
        return self._d.get("planet")

    @property
    def vehicle(self) -> Any:
        """The vehicle that founded it (from the session's view), or None."""
        return self._v.resolve("vehicle", self._d.get("vehicle"))

    @property
    def vehicle_id(self) -> Optional[int]:
        return self._d.get("vehicle")

    @property
    def choices(self) -> List[str]:
        """The colony types it may have."""
        return self._d.get("choices") or []


class EntryQuestion(_Args):
    """`enter_sector`: a group's move would enter a sector with enemies; should it?"""

    @property
    def vehicles(self) -> List[Any]:
        """The group's vehicles (from the session's view; None for one it does not list)."""
        return self._v.resolve_all("vehicle", self._d.get("vehicles") or [])

    @property
    def vehicle_ids(self) -> List[int]:
        return self._d.get("vehicles") or []

    @property
    def sector(self) -> Dict[str, Any]:
        """The sector: {system, x, y}."""
        return self._d.get("sector")

    @property
    def enemies(self) -> List[Any]:
        """The empires whose presence asks the question (from the session's view)."""
        return self._v.resolve_all("empire", self._d.get("enemies") or [])

    @property
    def enemy_ids(self) -> List[int]:
        return self._d.get("enemies") or []


class DecloakQuestion(_Args):
    """`decloak`: a cloaked vehicle or colony needs to decloak for an order or an attack."""

    @property
    def object_id(self) -> Optional[int]:
        """The vehicle or colony, as the engine names it."""
        return self._d.get("object")

    @property
    def vehicle(self) -> Any:
        """The vehicle, when the session's view lists one with that id."""
        return self._v.resolve("vehicle", self._d.get("object"))

    @property
    def reason(self) -> str:
        """"order" or "attack"."""
        return self._d.get("reason")


class Piece(Record):
    """A piece of a battle (docs/sdk/ai-protocol.md, section 5): its fields read as
    attributes (`piece.damage`), and `index` is its position in the battle's list, which
    tactical orders name."""

    def __init__(self, battle: "BattleState", data: Dict[str, Any], index: int) -> None:
        Record.__init__(self, NO_VIEW, data)
        self.index = index
        self._battle = battle

    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            raise AttributeError(name)
        d = self._d
        if name in d:
            return d[name]
        raise AttributeError(name)

    @property
    def mine(self) -> bool:
        return self._d.get("owner") == self._battle.empire

    @property
    def square(self) -> Any:
        p = self._d.get("position") or {}
        return (p.get("x", 0), p.get("y", 0))


class BattleState(Record):
    """A space battle as the empire sees it on its tactical screen (`battle_round`)."""

    def __init__(self, data: Dict[str, Any], empire: int) -> None:
        Record.__init__(self, NO_VIEW, data)
        self.empire = empire
        self._pieces = [Piece(self, p, i) for i, p in enumerate(data.get("pieces") or [])]

    @property
    def round(self) -> int:
        return self._d.get("round", 0)

    @property
    def rounds_max(self) -> int:
        return self._d.get("rounds_max", 0)

    @property
    def pieces(self) -> List[Piece]:
        return self._pieces

    @property
    def objects(self) -> List[Any]:
        return self._d.get("objects") or []

    def piece(self, index: int) -> Optional[Piece]:
        return self._pieces[index] if 0 <= index < len(self._pieces) else None

    @property
    def my_pieces(self) -> List[Piece]:
        return [p for p in self._pieces if p._d.get("owner") == self.empire]

    @property
    def enemy_pieces(self) -> List[Piece]:
        return [p for p in self._pieces if p._d.get("owner") not in (None, self.empire)]

    @staticmethod
    def distance(a: Piece, b: Piece) -> int:
        ax, ay = a.square
        bx, by = b.square
        return max(abs(ax - bx), abs(ay - by))

    def in_range(self, piece: Piece, target: Piece) -> bool:
        """A ready weapon of `piece` reaches `target`."""
        d = self.distance(piece, target)
        for w in piece._d.get("weapons") or []:
            if w.get("ready", True) and w.get("range", 0) >= d:
                return True
        return False

    def weakest_enemy_in_range(self, piece: Piece) -> Optional[Piece]:
        """The enemy piece in range of `piece` with the most damage (the first on a tie)."""
        best = None
        for p in self.enemy_pieces:
            if self.in_range(piece, p) and (best is None or p._d.get("damage", 0) > best._d.get("damage", 0)):
                best = p
        return best


class Player:
    """A computer player. Override the callbacks you want; the others are the classic AI's.

    Set for every request: `empire_id`, `turn`, `call`, `seed`, `random` (an
    opense4.rng.Random seeded from the request, the same every time the request is
    replayed), `view` (the session's latest view), `refused` (the commands of the
    previous planning call the game refused: [{index, command, reason}]).
    `memory` is what the player keeps between sessions (a map at first): only plain
    values (None, bool, int, str, lists, dicts with str keys).
    """

    memory: Any = None
    empire_id: int = -1
    turn: int = 0
    call: str = ""
    seed: int = 0
    random: Random
    view: Any = None
    refused: List[Dict[str, Any]] = []

    # ---- The decisions (docs/sdk/ai-protocol.md, section 3) ----

    def politics(self, view: Any, orders: Orders) -> Any:
        """Start of the turn, before messages are delivered: diplomacy. Default: the classic Politics minister."""
        orders.extend(builtin.politics(view))

    def orders(self, view: Any, orders: Orders) -> Any:
        """Start of the turn, after politics and delivery: movement, colonizing, attack,
        defence, exploration, fleets, supply, scrapping, refits. Default: the classic ministers."""
        orders.extend(builtin.orders(view))

    def economy(self, view: Any, orders: Orders) -> Any:
        """End of the turn: designs, research, intelligence, construction. Default: the classic ministers."""
        orders.extend(builtin.economy(view))

    def colony_type(self, view: Any, question: ColonyTypeQuestion) -> Optional[str]:
        """A colony is founded: return one of `question.choices`, or None for the classic choice."""
        return None

    def enter_sector(self, view: Any, question: EntryQuestion) -> Optional[bool]:
        """A move would enter a sector with enemies: True to enter, False to stay out, None to enter."""
        return None

    def decloak(self, view: Any, question: DecloakQuestion) -> Optional[bool]:
        """A cloaked vehicle or colony needs to decloak: True, False, or None for the classic minister."""
        return None

    def battle_round(self, battle: BattleState, orders: TacticalOrders) -> Any:
        """A round of a space battle: add tactical orders for our pieces, or none to let the strategies decide."""
        return None

    def end_session(self) -> None:
        """The session ends: the last chance to update `memory`."""

    # ---- Services (while a request is being handled) ----

    def note(self, obj: Any, text: str, kind: Optional[str] = None) -> None:
        """Attaches a note to something on the map for the client's AI view: a Vehicle,
        Fleet, Colony, SpaceObject, System, Empire, Design or Message, or an id with its
        `kind` ("vehicle", "fleet", "object", "system", "empire", "design", "message").
        A later note on the same thing this turn replaces it."""
        if isinstance(obj, Record):
            kind = _NOTE_KINDS.get(obj._kind)
            if kind is None:
                raise TypeError("note(): cannot attach a note to a " + type(obj).__name__)
            oid = obj.id
        else:
            if kind not in _NOTE_KINDS.values():
                raise TypeError("note(): give a record of the view, or an id with its kind=")
            oid = _int(obj, "note.object")
        notes = getattr(self, "_notes", None)
        if notes is None:
            notes = {}
            self._notes = notes
        key = (kind, oid)
        if key in notes:
            del notes[key]
        notes[key] = {"object": oid, "kind": kind, "text": str(text)}

    def log(self, text: Any) -> None:
        """Writes a line to the game's log file (not the empire's in-game log)."""
        lines = getattr(self, "_log", None)
        if lines is None:
            lines = []
            self._log = lines
        lines.append(str(text))

    def query(self, name: str, **args: Any) -> Any:
        """Asks a query of docs/sdk/view.md ("Queries") now (see opense4.services.query)."""
        return services.query(name, **args)

    @property
    def rules(self) -> Any:
        """The rules view (opense4.rules.Rules), asked once per session."""
        return services.rules()

    def apply(self, command: Dict[str, Any]) -> Any:
        """Applies a command now, in a planning call, and returns the engine's answer:
        `result.ok`, `result.reason` (docs/sdk/ai-protocol.md, section 6). A command
        applied this way is not also given in the call's answer."""
        result = services.current().services.apply(plain(command))
        return _Args(services.current().session.view, result) if isinstance(result, dict) else result


_NOTE_KINDS = {
    "vehicle": "vehicle", "fleet": "fleet", "space_object": "object", "colony": "object", "system": "system",
    "empire": "empire", "my_empire": "empire", "view_design": "design", "message": "message",
}
