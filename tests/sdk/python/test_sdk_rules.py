"""opense4.rules: the registry of the rules tier, the effects interface, the game as rules
functions read it (with a stand-in for the engine's native functions) and the dispatcher
the engine calls (opense4._rules_engine)."""

import sys

import support
from opense4 import _rules_engine, rules


class FakeNative:
    """The engine's _opense4_rules functions, answered from prepared values; records calls."""

    def __init__(self, parts=None, records=None, mod_data=None):
        self.parts = parts or {}
        self.records = records or {}
        self.mod_data = mod_data or {}
        self.calls = []

    def read(self, arg):
        self.calls.append(("read", arg))
        what = arg["what"]
        if what == "mod_data":
            return self.mod_data.get((arg["kind"], arg["id"]), {})
        if "id" in arg:
            return self.records.get((what, arg["id"]))
        return self.parts.get(what)

    def effect(self, arg):
        self.calls.append(("effect", arg))
        if arg["name"] == "create_vehicle":
            return 99
        if arg["name"] == "add_resources":
            return {"minerals": arg["args"]["minerals"], "organics": 0, "radioactives": 0}
        return None

    def random(self, arg):
        self.calls.append(("random", arg))
        return 3 if arg["op"] != "chance" else True

    def option(self, arg):
        return {"bounty": 25}.get(arg["name"], 0)

    def ability(self, arg):
        self.calls.append(("ability", arg))
        return 5

    def query(self, arg):
        return {"length": 4}

    def rules(self, arg):
        return {"techs": [], "components": []}


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
    support.raises(ValueError, rules.on, "turn_start", step="income")
    support.raises(ValueError, rules.on, "empire_end_of_turn", step="dancing")
    support.raises(ValueError, rules.on, "empire_end_of_turn", when="during")
    rules.clear()
    assert rules.handlers("colony_end_of_turn") == []
    assert "check_victory" in rules.HOOKS and len(rules.HOOKS) == len(set(rules.HOOKS))
    assert len(rules.STEPS) == 10


def test_registrations_name_their_mod_and_kind():
    rules.clear()
    rules._loading = "test.a"

    @rules.order("boost")
    def boost(game, order, fx):
        pass

    @rules.on("empire_end_of_turn", step="income", when="after")
    def income(game, empire, step, when, fx):
        pass

    rules._loading = "test.b"

    @rules.victory("crown")
    def crown(game):
        return None

    rules._loading = None
    a = rules.registrations("test.a")
    assert [(r.kind, r.name, r.step, r.when) for r in a] == [("order", "boost", None, None),
                                                            ("hook", "empire_end_of_turn", "income", "after")]
    assert a[1].matches("test.a", "hook", "empire_end_of_turn", "income", "after")
    assert not a[1].matches("test.a", "hook", "empire_end_of_turn", "research", "after")
    assert not a[1].matches("test.a", "hook", "empire_end_of_turn", "income", "before")
    assert [r.name for r in rules.registrations("test.b")] == ["crown"]
    support.raises(TypeError, rules.order, "")
    rules.clear()


def test_effects_is_an_interface():
    names = [n for n in dir(rules.Effects) if not n.startswith("_")]
    for n in ("add_resources", "change_happiness", "create_vehicle", "set_treaty", "log", "fire_event", "damage", "repair",
              "change_supply", "remove_vehicle", "add_facility", "remove_facility", "add_research", "grant_tech", "set_planet",
              "link_systems", "replace_galaxy", "set_option", "victory", "set_mod_data"):
        assert n in names
    # A method an implementation leaves out says so.
    e = support.raises(NotImplementedError, rules.Effects.change_happiness, None, 7, -1)
    assert "change_happiness" in str(e)

    class Partial(rules.Effects):
        def log(self, empire, text, title="", category="misc", location=None):
            return "logged"

    if sys.implementation.name == "cpython":
        support.raises(TypeError, Partial)   # CPython's abc refuses an incomplete one
    # The engine's implementation has every method.
    for n in names:
        assert getattr(rules.NativeEffects, n) is not getattr(rules.Effects, n), n


def test_the_game_reads_a_part_at_a_time_and_effects_read_again():
    native = FakeNative(parts={"game": {"turn": 4}, "empires": [{"id": 0, "name": "A"}, {"id": 1, "name": "B"}]},
                        records={("colony", 7): {"planet": 7, "owner": 1, "population": []}, ("empire", 0): {"id": 0, "name": "A"}},
                        mod_data={("colony", 7): {"seen": 1}})
    game = rules.Game(rules._Backend(native))
    colony = game.colony(7)
    assert colony is not None and colony.owner_id == 1
    assert ("read", {"what": "colony", "id": 7}) in native.calls
    assert not any(c == ("read", {"what": "empires"}) for c in native.calls)   # a lookup reads that thing only
    assert game.colony(8) is None
    assert [e.name for e in game.empires] == ["A", "B"]
    # Mod data: the mod's own, changed in place, reported when it changed.
    assert colony.mod_data == {"seen": 1}
    colony.mod_data["seen"] = 2
    assert game._changed_mod_data() == [{"kind": "colony", "id": 7, "value": {"seen": 2}}]
    # An effect goes to the engine with ids, and what was read is read again.
    fx = rules.NativeEffects(game)
    assert fx.add_resources(colony.owner_id, minerals=5)["minerals"] == 5
    assert native.calls[-1] == ("effect", {"name": "add_resources", "args": {"empire": 1, "minerals": 5, "organics": 0, "radioactives": 0}})
    before = len(native.calls)
    game.colony(7)
    assert len(native.calls) == before + 1   # read again after the effect
    fx.fire_event("windfall", colony)
    assert native.calls[-1][1]["args"] == {"name": "windfall", "target": {"kind": "colony", "id": 7}}
    fx.rename(game.empire(0), "C")
    assert native.calls[-1][1] == {"name": "rename", "args": {"kind": "empire", "id": 0, "name": "C"}}
    # The game's random numbers come from the engine.
    assert game.rng.below(10) == 3 and game.rng.chance(50) is True
    assert game.option("bounty") == 25
    assert game.ability(colony, "Test Field Battery") == 5
    assert native.calls[-1][1] == {"kind": "colony", "id": 7, "name": "Test Field Battery"}


def test_the_dispatcher_runs_one_mods_functions():
    rules.clear()
    rules._loading = "test.mine"
    seen = []

    @rules.on("turn_start")
    def mine(game, fx):
        seen.append(("mine", game.turn))
        game.mod_data["turns"] = game.mod_data.get("turns", 0) + 1
        rules.log("hello")

    @rules.order_check("boost")
    def check(game, order):
        return None if order.args["power"] < 3 else "too much"

    rules._loading = "test.theirs"

    @rules.on("turn_start")
    def theirs(game, fx):
        seen.append(("theirs", game.turn))

    rules._loading = None
    native = FakeNative(parts={"game": {"turn": 9}})
    r = _rules_engine.dispatch({"api": 1, "call": "hook", "mod": "test.mine", "name": "turn_start", "turn": 9, "args": {}},
                               rules._Backend(native))
    assert "error" not in r, r
    assert seen == [("mine", 9)]
    assert r["mod_data"] == [{"kind": "game", "id": -1, "value": {"turns": 1}}]
    assert r["log"] == ["hello"]
    order = {"mod": "test.mine", "name": "boost", "empire": 0, "vehicle": None, "fleet": None, "colony": None,
             "target_empire": None, "args": {"power": 5}}
    r = _rules_engine.dispatch({"api": 1, "call": "order_check", "mod": "test.mine", "name": "boost", "turn": 9,
                                "args": {"order": order}}, rules._Backend(native))
    assert r["result"] == "too much"
    # A function that raises: the error with its traceback.
    rules._loading = "test.mine"

    @rules.on("turn_end")
    def broken(game, fx):
        raise ValueError("no luck")

    rules._loading = None
    r = _rules_engine.dispatch({"api": 1, "call": "hook", "mod": "test.mine", "name": "turn_end", "turn": 9, "args": {}},
                               rules._Backend(native))
    assert r["error"]["type"] == "ValueError" and r["error"]["message"] == "no luck"
    rules.clear()
