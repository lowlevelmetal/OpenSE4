"""The classic computer player as a library (`ai.builtin`): the commands its ministers
would give for this empire now, and its answers to the game's questions.

    orders.extend(ai.builtin.economy(view))                     # the classic economy
    orders.extend(ai.builtin.orders(view, skip=["attack"]))     # all but the Attack minister
    research = ai.builtin.economy(view, ministers=["research"])  # one minister only

Each returns the commands as maps (docs/sdk/commands.md), nothing applied yet: keep
them, filter them (`[c for c in cs if c["kind"] != "set_research"]`), or change them
before adding them to `orders`. Minister names are those of the `minister`
enumeration (opense4.enums.MINISTER).
"""

from __future__ import annotations

from typing import Any, Dict, Iterable, List, Optional

from . import enums, services
from ._values import _enums


def _plan(call: str, ministers: Optional[Iterable[str]], skip: Iterable[str]) -> List[Dict[str, Any]]:
    m = _enums(None if ministers is None else list(ministers), enums.MINISTER, "ministers", True)
    s = _enums(list(skip), enums.MINISTER, "skip")
    return list(services.current().services.builtin(call, m, s))


def politics(view: Any = None, ministers: Optional[Iterable[str]] = None, skip: Iterable[str] = ()) -> List[Dict[str, Any]]:
    """The classic Politics minister's commands: messages, answers, treaties. `view` is
    accepted for symmetry: the classic AI always plans from the empire's state now."""
    return _plan("politics", ministers, skip)


def orders(view: Any = None, ministers: Optional[Iterable[str]] = None, skip: Iterable[str] = ()) -> List[Dict[str, Any]]:
    """The classic orders: movement, colonizing, attack, defence, exploration, fleets,
    supply, scrapping and refits, from the ministers named (None: all) less those skipped."""
    return _plan("orders", ministers, skip)


def economy(view: Any = None, ministers: Optional[Iterable[str]] = None, skip: Iterable[str] = ()) -> List[Dict[str, Any]]:
    """The classic economy: designs, research, intelligence and construction."""
    return _plan("economy", ministers, skip)


def answer(question: Any = None) -> Any:
    """The classic answer to the question being asked now (colony_type, enter_sector or
    decloak): what the engine does when the player answers None."""
    ctx = services.current()
    args = ctx.args if question is None else (question.raw if hasattr(question, "raw") else question)
    return ctx.services.builtin_answer(ctx.call, args)
