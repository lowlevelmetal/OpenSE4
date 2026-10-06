"""Hegemon's own tests. `opense4-sdk test mods/hegemon` runs them in the game's runtime
(with a new game of the installed data set for game_view() and game_rules()) and plays a
short game; under pytest (PYTHONPATH with OpenSE4's python/ folder, this mod's ai/ and
tests/) the ones that need the game's data skip."""

from opense4 import testing

import fake_engine
from hegemon import Hegemon
from hegemon.knowledge import Knowledge
from hegemon.world import World
from hegemon.economy import Economy
from hegemon.expansion import Expansion
from hegemon.research import Research
from hegemon.construction import Construction
from hegemon.tactics import Tactics, piece_dist
from hegemon.invasion import troops_needed
from hegemon.intel import Force, design_strength


def _game():
    view = testing.game_view()
    rules = testing.game_rules()
    return view, rules


def _services(rules, levels):
    return testing.FakeServices(rules=rules, queries={"design_figures": fake_engine.design_figures(rules, levels),
                                                      "movement": fake_engine.movement})


def _plain(value):
    """Only what the game keeps: None, bools, whole numbers, text, lists and maps with text keys."""
    if value is None or isinstance(value, (bool, int, str)):
        return not isinstance(value, float)
    if isinstance(value, list):
        return all(_plain(v) for v in value)
    if isinstance(value, dict):
        return all(isinstance(k, str) and _plain(v) for k, v in value.items())
    return False


# ---- the rules, classified ----

def test_the_rules_are_read_by_their_abilities():
    view, rules = _game()
    kn = Knowledge(rules, view["my"]["research"]["levels"], view["game"]["options"]["tech_cost"])
    assert any(f.gen[0] > 0 for f in kn.facilities), "a mineral generator"
    assert any(f.research > 0 for f in kn.facilities), "a research facility"
    assert any(f.spaceport for f in kn.facilities), "a spaceport"
    assert any(c.engine > 0 for c in kn.components), "an engine"
    assert any(c.bridge for c in kn.components), "a bridge"
    assert any(c.weapon is not None for c in kn.components), "a weapon"
    assert kn.level_cost(0, 2) >= kn.level_cost(0, 1) > 0


# ---- the planners on a new game ----

def test_the_economy_prices_every_kind_of_output():
    view, rules = _game()
    w = World(view)
    kn = Knowledge(rules, w.research_levels())
    econ = Economy(w, kn)
    assert len(econ.prices) == 5 and all(p > 0 for p in econ.prices)
    assert econ.delivered(w.home_system()) > 0
    for c in w.my_colonies:
        assert econ.colony_info[c["planet"]]["system"] == w.system_of(c["planet"])


def test_expansion_ranks_planets_it_may_settle():
    view, rules = _game()
    w = World(view)
    kn = Knowledge(rules, w.research_levels())
    ex = Expansion(w, kn, Economy(w, kn), {}, {})
    surfaces = []
    for c in kn.components:
        if c.colonize and kn.meets(c.reqs):
            surfaces.extend(c.colonize)
    targets = ex.targets(surfaces)
    for score, o, s in targets:
        assert o["colony"] is None and o["surface"] in surfaces and score > 0
    assert [t[0] for t in targets] == sorted((t[0] for t in targets), reverse=True)


def test_research_queues_areas_we_may_research_in_order():
    view, rules = _game()
    w = World(view)
    kn = Knowledge(rules, w.research_levels())
    r = Research(w, kn, Economy(w, kn), {"economy": 1.0, "expansion": 1.0, "military": 1.0, "military_share": 0.3})
    mem = {}
    cmd = r.plan(mem)
    assert cmd is not None and cmd["kind"] == "set_research" and cmd["evenly"] is False
    areas = [q["area"] for q in cmd["queue"]]
    assert 0 < len(areas) <= 12 and len(set(areas)) == len(areas)
    assert set(areas) <= set(view["my"]["research"]["researchable"])
    assert mem["rp_head"] in ("civ", "mil")


def test_construction_fills_idle_queues_within_the_treasury():
    view, rules = _game()
    w = World(view)
    kn = Knowledge(rules, w.research_levels())
    econ = Economy(w, kn)
    cons = Construction(w, kn, econ, {"ships": [], "yard_sites": [], "depots": [], "defense": {}})
    cmds = cons.plan()
    for c in cmds:
        assert c["kind"] == "queue_add" and c["item"]["kind"] in ("facility", "upgrade", "vehicle")
    for r in ("minerals", "organics", "radioactives"):
        assert cons.budget[r] >= -econ.report["colonies"][r] - 1   # never far beyond what comes in


# ---- a whole turn, as the game asks it ----

def test_a_turn_and_the_next_session_through_the_harness():
    view, rules = _game()
    services = _services(rules, view["my"]["research"]["levels"])
    h = testing.Harness(Hegemon, services)
    for call in ("politics", "orders", "economy"):
        r = h.call(call, view=view, turn=1)
        assert "error" not in r, r.get("error")
        for c in r["commands"]:
            assert isinstance(c, dict) and "kind" in c
    economy = r["commands"]
    assert any(c["kind"] == "set_research" for c in economy)
    assert any(c["kind"] == "create_design" for c in economy)
    h.call("end_session")
    assert _plain(h.memory)
    assert h.memory["roles"], "it remembers its designs"
    # A new session starts from that memory and goes on.
    r = h.call("orders", view=view, turn=2)
    assert "error" not in r, r.get("error")


def test_designs_fit_their_hulls_and_cover_the_roles():
    view, rules = _game()
    services = _services(rules, view["my"]["research"]["levels"])
    h = testing.Harness(Hegemon, services)
    r = h.call("economy", view=view, turn=1)
    assert "error" not in r, r.get("error")
    made = [c["design"] for c in r["commands"] if c["kind"] == "create_design"]
    names = [d["name"] for d in made]
    assert len(set(names)) == len(names)
    assert any(n.startswith("Warship ") for n in names)
    assert any(n.startswith("Colonist ") for n in names)
    hulls = rules["hulls"]
    comps = rules["components"]
    for d in made:
        used = sum(comps[e["component"]]["tonnage"] for e in d["entries"])
        assert used <= hulls[d["hull"]]["tonnage"], d["name"]
        assert d["design_type"] in view["my"]["design_types"]


# ---- battles (no game data needed) ----

def _piece(i, owner, x, y, kind="vehicle", hp=100, shields=0, weapons=None, movement=3, troops=False, size=1):
    return {"id": i, "kind": kind, "owner": owner, "start_owner": owner, "vehicle": None, "planet": None, "design": None,
            "name": "p%d" % i, "type": "ship" if kind == "vehicle" else None, "position": {"x": x, "y": y}, "size": size,
            "facing": 0, "alive": True, "mothballed": False, "cloaked": False, "captured": False, "damage": 0,
            "hit_points": hp, "full_hit_points": hp, "shields": shields, "shields_max": shields, "movement": movement,
            "speed": movement * 2, "supply": 100, "has_supply": True, "count": 1, "acted": False, "leader": -1,
            "is_leader": False, "group": -1, "formation": -1, "budget": 1, "engaged": 0,
            "launch_left": {"fighters": 0, "satellites": 0, "drones": 0}, "seek_target": -1, "launcher": -1, "carrier": -1,
            "drone_target": -1, "weapons": weapons or [], "cargo": [], "troops": troops, "boarding_attack": 0}


def _gun(range_, ready=1):
    return {"index": 0, "component": None, "kind": "direct_fire", "range": range_, "reload": [0], "reload_rate": 1,
            "ready": ready, "instances": 1, "together": 1, "enabled": True}


def test_tactics_focus_on_the_most_dangerous_enemy_and_end_the_phase():
    battle = {"round": 1, "rounds_max": 29, "pieces": [
        _piece(0, 0, 10, 10, weapons=[_gun(4)]),
        _piece(1, 0, 11, 10, weapons=[_gun(4)]),
        _piece(2, 1, 14, 10, hp=50, weapons=[_gun(4), _gun(4)]),      # dangerous and fragile
        _piece(3, 1, 14, 12, hp=400),                                 # harmless and tough
    ]}
    orders = Tactics(battle, 0, lambda e: e == 1).plan()
    fires = [o for o in orders if o["kind"] == "fire"]
    assert fires and fires[0]["target"] == 2
    assert orders[-1]["kind"] == "end_phase"
    for o in orders:
        if o["kind"] == "move":
            assert 0 <= o["x"] < 72 and 0 <= o["y"] < 63


def test_tactics_land_troops_beside_a_colony():
    battle = {"round": 8, "rounds_max": 29, "pieces": [
        _piece(0, 0, 20, 20, troops=True, weapons=[]),
        _piece(1, 1, 21, 21, kind="planet", hp=300, size=4, movement=0),
    ]}
    assert piece_dist(battle["pieces"][0], battle["pieces"][1]) <= 1
    orders = Tactics(battle, 0, lambda e: e == 1).plan()
    assert {"kind": "drop_troops", "piece": 0} in orders


def test_troops_needed_grow_with_the_militia():
    small = troops_needed({"total_population": 30}, {"weapon_damage": 30, "structure": 20, "shields": 0})
    big = troops_needed({"total_population": 2000}, {"weapon_damage": 30, "structure": 20, "shields": 0})
    assert 1 <= small < big


def test_lanchester_strength():
    a = Force()
    a.add(*design_strength({"weapon_damage": 100, "structure": 200, "shields": 0, "phased_shields": 0}), 2)
    b = Force()
    b.add(*design_strength({"weapon_damage": 100, "structure": 200, "shields": 0, "phased_shields": 0}), 1)
    assert a.beats(b, 3.9) and not a.beats(b, 4.1)
