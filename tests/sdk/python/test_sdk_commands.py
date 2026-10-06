"""opense4.cmd, opense4.order and opense4.tactical against the docs (the schema) and the
engine's codec: every command, order kind, tactical order kind and type has its
constructor, with every documented field and the engine's defaults; given the
engine's samples, each builds exactly what the engine encodes. The C++ side decodes
what is published here."""

import support
from opense4 import cmd, enums, order, tactical
from opense4.view import View


def _schema(name):
    return support.fixture("schema")[name]


def _codec():
    return support.fixture("codec")


def test_every_command_kind_has_a_constructor():
    kinds = _codec()["kinds"]
    assert sorted(cmd.COMMAND_KINDS) == sorted(kinds), "the package's command kinds differ from the engine's"
    for kind in kinds:
        build = getattr(cmd, kind, None)
        assert build is not None, "opense4.cmd has no " + kind
        made = build()
        assert list(made) == ["kind"] + _schema(kind)["fields"], kind + ": the fields differ from docs/sdk/commands.md"
        assert made == _codec()["commands"][kind], kind + ": the defaults differ from the engine's: " + repr(made)


def test_every_order_kind_has_a_constructor():
    names = _schema("order_kind")["values"]
    assert list(order.KINDS) == names and list(enums.ORDER_KIND) == names
    fields = _schema("order")["fields"]
    built = []
    for kind in names:
        build = getattr(order, kind)
        made = build()
        assert list(made) == fields, kind
        assert made == _codec()["orders"][kind], kind + ": the defaults differ from the engine's: " + repr(made)
        built.append(made)
    # With the fields each kind uses.
    built.append(order.move_to((4, 3, 9)))
    built.append(order.warp(88))
    built.append(order.attack(vehicle=77))
    built.append(order.attack(object=19, location=(4, 2, 2)))
    built.append(order.colonize(19))
    built.append(order.load_cargo(amount=-1))
    built.append(order.drop_cargo(21, 4))
    built.append(order.recover_units(21, 95, 10))
    built.append(order.use_component(3))
    built.append(order.stellar_manipulation("create_storm", location=(4, 2, 2)))
    built.append(order.move_to_waypoint(2))
    built.append(order.convert_resources(5000, "organics", "minerals"))
    built.append(order.retrofit(18))
    built.append(order.seek(vehicle=77))
    built.append(order.join_fleet(3))
    assert built[-6]["amount"] == 6   # a manipulation by its name
    support.publish("orders_built", built)


def test_every_tactical_order_kind_has_a_constructor():
    names = _schema("tactical_order_kind")["values"]
    assert list(tactical.KINDS) == names
    fields = _schema("tactical_order")["fields"]
    built = []
    for kind in names:
        made = getattr(tactical, kind)()
        assert list(made) == fields, kind
        assert made == _codec()["tactical"][kind], kind + ": the defaults differ from the engine's: " + repr(made)
        built.append(made)
    built.append(tactical.move(2, 10, 4))
    built.append(tactical.move(2, path=[{"x": 3, "y": 4}, {"x": 4, "y": 4}], alone=True))
    built.append(tactical.fire(2, 7, -1))
    built.append(tactical.toggle_weapon(2, 0, False))
    built.append(tactical.launch_fighters(2, 22, 20, 10, empire=1))
    built.append(tactical.set_leader(2, 1, 0))
    support.publish("tactical_built", built)


def test_every_type_has_a_constructor():
    for name in cmd.TYPES:
        made = getattr(cmd, name)()
        assert list(made) == _schema(name)["fields"], name
        assert made == _codec()["types"][name], name + ": the defaults differ from the engine's: " + repr(made)


def test_the_engines_samples_rebuilt():
    # Each sample the engine encoded, given back field by field, comes out the same.
    built = []
    for sample in _codec()["samples"]:
        fields = dict(sample)
        kind = fields.pop("kind")
        made = getattr(cmd, kind)(**fields)
        assert made == sample, kind + ": " + repr(made) + " is not " + repr(sample)
        built.append(made)
    kinds = set(c["kind"] for c in built)
    assert kinds == set(cmd.COMMAND_KINDS), "the samples miss " + repr(set(cmd.COMMAND_KINDS) - kinds)
    support.publish("commands_built", built)


def test_objects_of_the_view_stand_for_their_ids():
    view = View(support.view_map())
    my = view.my
    ship = my.ships[0]
    colony = my.colonies[0]
    design = my.designs[0]
    home = view.system(my.home_system_id)
    made = []

    c = cmd.set_orders(vehicle=ship, orders=[order.move_to(home), order.warp(view.warp_points[0])])
    assert c["vehicle"] == ship.id and c["orders"][0]["location"] == {"system": home.id, "x": 6, "y": 6}
    assert c["orders"][1]["object"] == view.warp_points[0].id
    made.append(c)
    made.append(cmd.give(ship, [order.sentry()], repeat=True))
    made.append(cmd.give(colony, [order.use_facility(0)]))
    assert made[-1]["planet"] == colony.planet_id
    if my.fleets:
        made.append(cmd.give(my.fleets[0], [order.move_to(ship)]))
        assert made[-1]["orders"][0]["location"] == ship.location.raw
    made.append(cmd.build(colony, design, count=2))
    assert made[-1]["target"] == {"planet": colony.id, "vehicle": None}
    assert made[-1]["item"]["design"] == design.id and made[-1]["item"]["count"] == 2
    made.append(cmd.queue_add(target=ship, item=cmd.queue_item(design=design)))
    assert made[-1]["target"] == {"planet": None, "vehicle": ship.id}
    made.append(cmd.set_research([4, 9], evenly=False))
    assert made[-1]["queue"] == [{"area": 4, "progress": 0}, {"area": 9, "progress": 0}]
    made.append(cmd.create_fleet("Pickets", my.ships[:1]))
    made.append(cmd.rename(planet=colony, name="New Hope"))
    made.append(cmd.set_ministers(areas=["research", "politics"], new_vehicles=False))
    made.append(cmd.send_message(cmd.message(to_empire=view.empires[1], type="propose_treaty", treaty="non_aggression")))
    assert made[-1]["message"]["to_empire"] == view.empires[1].id
    made.append(cmd.tag_minefield(cmd.centre(home)))
    made.append(cmd.transfer_cargo(from_planet=colony, to_vehicle=ship, population_race=view.me, amount=5))
    assert made[-1]["population_race"] == view.empire_id
    support.publish("commands_from_view", made)

    # The wrong kind of object, or a value of the wrong type, is refused before the engine.
    support.raises(TypeError, cmd.set_orders, vehicle=colony)
    support.raises(TypeError, cmd.leave_fleet, vehicle="12")
    support.raises(TypeError, cmd.leave_fleet, vehicle=True)
    support.raises(TypeError, cmd.mothball, vehicle=ship, mothball=1)
    support.raises(TypeError, order.attack, vehicle=view.planets[0])
    support.raises(ValueError, cmd.set_ministers, areas=["dancing"])
    support.raises(ValueError, order.stellar_manipulation, "make_tea")
    support.raises(TypeError, cmd.set_orders, vehicle=ship, orders=[{"location": None}])
    support.raises(TypeError, cmd.give, ship.id, [])
    e = support.raises(TypeError, cmd.rename, fleet=ship)
    assert "rename.fleet" in str(e)


def test_filters():
    a = cmd.set_research([1])
    b = cmd.leave_fleet(vehicle=3)
    c = cmd.set_intel([])
    assert cmd.only([a, b, c], "set_research", "set_intel") == [a, c]
    assert cmd.without([a, b, c], "set_research") == [b, c]
