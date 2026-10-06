"""opense4.view and opense4.rules over views the engine built (sdk::buildView) of a game of
the engine fixture: every documented field reads, ids resolve to what the view lists,
lookups and the convenience lists agree with the raw maps."""

import support
from opense4 import _records
from opense4._record import Entity, Record
from opense4.rules import Rules
from opense4.view import Colony, Design, Empire, Fleet, Location, SpaceObject, System, Vehicle, View


def _views():
    return [View(support.fixture("view")), View(support.fixture("whole_view")), View(support.fixture("view_1"))]


LISTS = (("empires", "empire"), ("systems", "system"), ("objects", "object"), ("colonies", "colony"),
         ("vehicles", "vehicle"), ("fleets", "fleet"), ("designs", "design"), ("messages", "message"))


def test_lists_and_lookups():
    for view in _views():
        for field, kind in LISTS:
            items = getattr(view, field)
            assert len(items) == len(view.raw[field]), field
            assert getattr(view, field) is items   # wrapped once
            lookup = getattr(view, kind)
            for item, raw in zip(items, view.raw[field]):
                assert item.raw is raw
                key = raw["planet"] if kind == "colony" else raw["id"]
                assert lookup(key) is item
                assert lookup(item) is item   # an object stands for its id
        assert view.vehicle(10 ** 9) is None and view.system(None) is None
        assert len(view.log) == len(view.raw["log"]) and len(view.battles) == len(view.raw["battles"])


def _walk(record, seen, path, problems):
    """Reads every field of `record` and of the records it holds, checking that ids resolve.
    The objects ids resolve to (entities) are walked from the view's lists, not here."""
    kind = record._kind
    fields = _records.FIELDS.get(kind)
    if fields is None:
        return
    cls = type(record)
    for name in fields:
        raw = record.raw[name]
        attr = getattr(cls, name, None)
        if attr is not None:
            value = getattr(record, name)
        else:
            # an id resolved under another name (a planet's colony_owner, the view's me)
            value = raw
        if isinstance(value, Record) and not isinstance(value, Entity):
            _walk(value, seen, path + "." + name, problems)
        elif isinstance(value, list):
            for i, v in enumerate(value):
                if isinstance(v, Record) and not isinstance(v, Entity):
                    _walk(v, seen, path + "." + name + "[" + str(i) + "]", problems)
        id_name = name + "_id"
        if hasattr(cls, id_name) and name != "id" and getattr(record, id_name) is not None and getattr(record, name) is None:
            problems.append(path + "." + name + ": id " + repr(raw) + " names nothing in the view")


def test_every_documented_field_reads_and_every_id_resolves():
    for view in _views():
        problems = []
        _walk(view, {}, "view", problems)
        for field, _ in LISTS:
            for i, item in enumerate(getattr(view, field)):
                _walk(item, {}, field + "[" + str(i) + "]", problems)
        assert not problems, "\n".join(problems[:10])


def test_references():
    view = View(support.fixture("view"))
    for v in view.vehicles:
        assert v.owner.id == v.owner_id
        if v.design_id is not None:
            assert isinstance(v.design, Design) and v.design.id == v.design_id
        if v.fleet_id is not None:
            assert isinstance(v.fleet, Fleet) and v in v.fleet.members
        assert v.system is v.location.system
        assert isinstance(v.location, Location) and v.location.system_id == v.raw["location"]["system"]
    for f in view.fleets:
        assert [m.id for m in f.members] == f.member_ids == f.raw["members"]
        assert f.vehicles == [m for m in f.members if m is not None]
    for c in view.colonies:
        assert isinstance(c.planet, SpaceObject) and c.planet.id == c.planet_id == c.id
        assert c.planet.colony is c and c.system is c.planet.system
        assert c.owner.id == c.owner_id
    for s in view.systems:
        if s.objects is not None:
            assert [o.id for o in s.objects] == s.object_ids
            assert all(o.system is s for o in s.objects)
    assert view.me.is_me and view.me.id == view.empire_id == view.my.id
    assert view.my.empire is view.me


def test_entities_are_equal_by_id_and_hashable():
    a = View(support.fixture("view"))
    b = View(support.fixture("view"))
    v, w = a.vehicles[0], b.vehicles[0]
    assert v is not w and v == w and hash(v) == hash(w)
    assert {v: 1}[w] == 1
    assert v != a.vehicles[1] and v != a.fleets[0] if a.fleets else v != a.vehicles[1]
    assert a.colonies[0] == b.colonies[0] and len({c for c in a.colonies + b.colonies}) == len(a.colonies)
    loc = a.vehicles[0].location
    assert loc == b.vehicles[0].location and {loc: 1}[b.vehicles[0].location] == 1
    assert isinstance(a.empires[0], Entity) and isinstance(a.empires[0], Empire)


def test_records_read_raw_values():
    view = View(support.fixture("view"))
    v = view.vehicles[0]
    assert v["name"] == v.name == v.raw["name"] and v.get("nothing", 5) == 5 and "owner" in v
    assert "Vehicle(" in repr(v)
    assert isinstance(view.game.options.starting_resources.minerals, int)
    assert view.game.simultaneous == (view.raw["game"]["turn_style"] == "simultaneous")


def test_our_affairs():
    view = View(support.fixture("view"))
    my = view.my
    me = view.empire_id
    assert my.vehicles and all(v.owner_id == me for v in my.vehicles)
    assert len(my.vehicles) == len([v for v in view.raw["vehicles"] if v["owner"] == me])
    assert all(v.type == "ship" for v in my.ships) and all(v.type == "base" for v in my.bases)
    assert all(v.is_unit for v in my.units)
    assert len(my.ships) + len(my.bases) + len(my.units) == len([v for v in my.vehicles if v.type is not None])
    for v in my.idle_vehicles:
        assert v.orders == [] and v.fleet is None and v.mine
    assert my.colonies and all(c.mine for c in my.colonies)
    t = my.colonies[0].colony_type
    assert my.colonies[0] in my.colonies_of_type(t)
    d = my.designs[0]
    assert my.design_named(d.name) is d and my.design_named("no such design") is None
    if not d.obsolete:
        assert d in my.designs_of_type(d.design_type)
    assert my.stored.total == sum(my.raw["stored"].values())
    assert my.stored.covers((0, 0, 0)) and not my.stored.covers((10 ** 12, 0, 0))


def test_theirs():
    view = View(support.fixture("view"))
    facts = support.fixture("facts")
    seen = view.vehicle(facts["seen_foreign"])
    assert seen is not None and seen in view.foreign_vehicles and not seen.mine
    assert view.vehicle(facts["hidden_foreign"]) is None   # in a system we have not explored
    assert view.system(facts["unexplored"]) in view.unexplored_systems
    assert view.system(facts["unexplored"]).objects is None
    for e in view.enemies:
        assert e.at_war and view.is_enemy(e)
    for c in view.enemy_colonies:
        assert view.is_enemy(c.owner_id)
    assert seen in seen.owner.vehicles
    assert view.treaty_with(view.me) is None
    whole = View(support.fixture("whole_view"))
    assert whole.vehicle(facts["hidden_foreign"]) is not None


def test_places():
    view = View(support.fixture("view"))
    home = view.system(view.my.home_system_id)
    assert home.planets and all(p.is_planet for p in home.planets)
    assert all(p in view.planets for p in home.planets)
    assert set(w.id for w in view.warp_points) == set(o["id"] for o in view.raw["objects"] if o["kind"] == "warp_point")
    assert home.colonies and all(c.system is home for c in home.colonies)
    ship = view.my.ships[0]
    assert ship in view.vehicles_in(ship.system) and ship in view.vehicles_at(ship.location)
    assert ship in view.vehicles_at(ship)   # anything that stands for a place
    assert view.objects_in(home) == home.objects
    assert ship.location.distance_to(ship.location) == 0
    assert ship.location.distance_to({"system": ship.location.system_id, "x": 0, "y": 0}) == max(ship.location.x, ship.location.y)


def test_the_rules_view():
    rules = Rules(support.fixture("rules"))
    raw = support.fixture("rules")
    assert len(rules.components) == len(raw["components"]) and rules.components[0].id == 0
    for c in rules.components:
        assert rules.component(c.id) is c and rules.component(c) is c
        assert rules.component_named(c.name) is not None
        for a in c.abilities:
            assert c.has_ability(a.name)
            assert rules.aggregation(a.name) is not None
    assert rules.component(10 ** 6) is None and rules.component_named("no such component") is None
    assert all(rules.hull(h.id) is h for h in rules.hulls)
    assert all(rules.tech_named(t.name) is not None for t in rules.techs)
    assert rules.facilities and rules.facility_named(rules.facilities[0].name) is not None
    assert all(isinstance(m.name, str) for m in rules.mounts)
    weapons = [c for c in rules.components if c.is_weapon]
    assert weapons and all(c.weapon.damage_at_range for c in weapons)
    problems = []
    _walk(rules, {}, "rules", problems)
    for table in ("components", "facilities", "hulls", "techs", "races", "formations", "intel_projects"):
        for i, r in enumerate(getattr(rules, table)):
            _walk(r, {}, table + "[" + str(i) + "]", problems)
    assert not problems
    # A view with rules gives them, and its designs' hulls are indices into them.
    view = View(support.fixture("view"), rules=rules)
    assert view.rules is rules
    for d in view.my.designs:
        assert rules.hull(d.hull) is not None
