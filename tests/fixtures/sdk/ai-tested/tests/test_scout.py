# The Scout player's tests: opense4-sdk test runs them in the game's own Python, with the
# mod's ai/ folder at the root (so `import scout` finds ai/scout.py).

from opense4 import rules, testing
from scout import Scout


def test_it_keeps_the_classic_orders_and_counts_its_turns():
    h = testing.Harness(Scout, testing.FakeServices(builtin={"orders": [{"kind": "rename", "vehicle": 4, "name": "Kept"}]}))
    view = testing.game_view()
    first = h.call("orders", view=view, turn=1)
    assert "error" not in first, first.get("error")
    assert first["commands"] == [{"kind": "rename", "vehicle": 4, "name": "Kept"}]
    assert first["memory"] == {"turns": 1}
    assert first["notes"] and first["notes"][0]["text"] == "home, turn 1"
    h.call("end_session")
    assert h.call("orders", view=view, turn=2)["memory"] == {"turns": 2}


def test_it_prefers_research_colonies():
    h = testing.Harness(Scout)
    r = h.call("colony_type", args={"colony": 3, "planet": 3, "vehicle": 9, "choices": ["Mining", "Research"]})
    assert r["answer"] == "Research"
    r = h.call("colony_type", args={"colony": 3, "planet": 3, "vehicle": 9, "choices": ["Mining"]})
    assert r["answer"] is None


def test_the_data_set_has_components():
    assert len(rules.Rules(testing.game_rules()).components) >= 1
