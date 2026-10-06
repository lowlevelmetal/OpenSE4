"""opense4.rules: the hook registry and the effects interface of the rules tier (which
arrives in a later step: nothing calls the hooks yet)."""

import sys

import support
from opense4 import rules


def test_hooks_register_in_order():
    rules.clear()
    calls = []

    @rules.on("colony_end_of_turn")
    def first(game, colony, fx):
        calls.append(("first", colony))

    @rules.on("colony_end_of_turn")
    def second(game, colony, fx):
        calls.append(("second", colony))

    assert rules.handlers("colony_end_of_turn") == [first, second]
    assert rules.handlers("turn_start") == []
    for h in rules.handlers("colony_end_of_turn"):
        h(None, 7, None)
    assert calls == [("first", 7), ("second", 7)]
    support.raises(ValueError, rules.on, "colony_end_of_the_world")
    support.raises(ValueError, rules.handlers, "nope")
    rules.clear()
    assert rules.handlers("colony_end_of_turn") == []
    assert "check_victory" in rules.HOOKS and len(rules.HOOKS) == len(set(rules.HOOKS))


def test_effects_is_an_interface():
    names = [n for n in dir(rules.Effects) if not n.startswith("_")]
    for n in ("add_resources", "change_happiness", "create_vehicle", "set_treaty", "log", "fire_event"):
        assert n in names
    # A method an implementation leaves out says so.
    e = support.raises(NotImplementedError, rules.Effects.change_happiness, None, 7, -1)
    assert "rules tier" in str(e)

    class Partial(rules.Effects):
        def log(self, empire, text, title="", category="misc"):
            return "logged"

    if sys.implementation.name == "cpython":
        support.raises(TypeError, Partial)   # CPython's abc refuses an incomplete one

    methods = {}
    for n in names:
        methods[n] = lambda self, *args, **kwargs: n
    Complete = type("Complete", (rules.Effects,), methods)
    fx = Complete()
    assert fx.change_happiness(7, -1) is not None
