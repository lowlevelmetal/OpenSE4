# The Scholar's tests, on a new game of your data set (opense4-sdk test gives it; under
# CPython they skip).

from opense4 import testing

from scholar import Scholar


def classic_economy(ministers, skip):
    """What the classic economy might give: a research queue and some construction."""
    return [{"kind": "set_research", "queue": [{"area": 0, "progress": 0}], "evenly": True, "repeat": False},
            {"kind": "rename", "vehicle": None, "fleet": None, "design": None, "planet": None, "name": "kept"}]


def play_economy():
    rules = testing.game_rules()
    view = testing.game_view()
    h = testing.Harness(Scholar, testing.FakeServices(rules=rules, builtin={"economy": classic_economy}))
    return view, h.call("economy", view=view)


def test_it_keeps_the_classic_economy_but_its_research():
    view, r = play_economy()
    assert "error" not in r, r.get("error")
    kinds = [c["kind"] for c in r["commands"]]
    assert "rename" in kinds, "the classic commands other than research are kept"
    research = [c for c in r["commands"] if c["kind"] == "set_research"]
    assert len(research) <= 1
    if research:
        queue = [p["area"] for p in research[0]["queue"]]
        assert queue != [0] or len(view["my"]["research"]["researchable"]) == 1
        assert set(queue) <= set(view["my"]["research"]["researchable"])
        assert research[0]["evenly"] is False


def test_it_counts_its_plans_in_its_memory():
    _, r = play_economy()
    assert r["memory"]["plans"] == 1
