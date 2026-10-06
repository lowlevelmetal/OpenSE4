# Pioneer's tests. opense4-sdk test runs them in the game's own Python on a new game of
# your data set (testing.game_view() and game_rules()); then it plays a short game of
# Pioneer against the classic AI, which fails on any error the player raises. Under
# CPython (pytest) the tests that need a game skip.

from opense4 import rules, testing

import designs
import parts as p
from pioneer import Pioneer


def our_parts():
    view = testing.game_view()
    return view, p.Parts(rules.Rules(testing.game_rules()), view["my"]["research"]["levels"])


def test_designs_fit_their_hulls():
    """Whatever the data set, a design Pioneer composes fits its hull and keeps each
    family within its limit; on a data set without the parts it makes none."""
    _, parts = our_parts()
    made = 0
    for role, surface in [("scout", None), ("warship", None)] + [("colony", s) for s in p.COLONIZE]:
        composed = designs.compose(role, parts, surface)
        if composed is None:
            continue
        hull, components = composed
        made += 1
        assert designs.fits(components, hull), role
        assert any(c.has_ability(p.ENGINE) for c in components), role + " has an engine"
        if hull.must_have_bridge:
            assert sum(1 for c in components if c.has_ability(p.BRIDGE)) == 1, role + " has one bridge"
        if role == "colony":
            assert any(c.has_ability(p.COLONIZE[surface]) for c in components)
    if made == 0 and parts.best_with(p.BRIDGE) is not None:
        raise AssertionError("a data set with a bridge should give at least one design")


def answers(designs_made):
    """Services for an economy call: the designer accepts everything, and each design
    applied comes back as made (as the engine's apply service answers)."""
    def apply(command):
        if command["kind"] == "create_design":
            design = dict(command["design"], id=1000 + len(designs_made))
            designs_made.append(design)
            return {"ok": True, "reason": "", "changed": {"designs": [design]}, "removed": {}}
        return {"ok": True, "reason": "", "changed": {}, "removed": {}}
    return testing.FakeServices(rules=testing.game_rules(),
                                queries={"design_figures": {"valid": True, "problems": []},
                                         "queue_item_problem": {"problem": ""},
                                         "colonize_problem": {"problem": ""}},
                                apply=apply)


def test_economy_makes_designs_and_remembers_their_roles():
    made = []
    h = testing.Harness(Pioneer, answers(made))
    r = h.call("economy", view=testing.game_view())
    assert "error" not in r, r.get("error")
    roles = r["memory"].get("designs", {})
    assert len(roles) == len(made)
    for role, design_id in roles.items():
        assert role in ("scout", "warship") or role.startswith("colony:")
        assert design_id in [d["id"] for d in made]
    # Every command is a well-formed one (the harness decodes nothing, so check kinds).
    for command in r["commands"]:
        assert command["kind"] in ("set_research", "queue_add"), command["kind"]


def test_orders_on_the_first_turn_are_well_formed():
    h = testing.Harness(Pioneer, answers([]))
    r = h.call("orders", view=testing.game_view())
    assert "error" not in r, r.get("error")
    for command in r["commands"]:
        assert command["kind"] in ("set_orders", "create_fleet"), command["kind"]


def test_politics_accepts_peace_and_refuses_war():
    h = testing.Harness(Pioneer, answers([]))
    view = testing.game_view()
    me = view["my"]["id"]
    other = [e["id"] for e in view["empires"] if e["id"] != me][0]

    def message(mid, treaty):
        return {"id": mid, "from_empire": other, "to_empire": me, "sent_turn": 1, "type": "propose_treaty", "tone": 1,
                "text": "", "treaty": treaty, "offer": [], "request": [], "third_empire": None, "system": None,
                "planet": None, "in_reply_to": None, "delivered": True, "answered": False, "dated": 1}
    view = dict(view, messages=[message(7, "non_aggression"), message(8, "subjugation")])
    r = h.call("politics", view=view)
    assert "error" not in r, r.get("error")
    answered = {c["message"]: c["accept"] for c in r["commands"]}
    assert answered == {7: True, 8: False}
