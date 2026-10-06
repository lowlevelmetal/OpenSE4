"""opense4._engine: the player's side of the computer-player protocol
(docs/sdk/ai-protocol.md), driven with requests made from a view the engine built and
services prepared in advance (opense4.testing). tests/sdk/test_sdk_python.cpp also
drives it with the engine's own services."""

import support
from opense4 import ai, cmd, external, order, tactical, testing
from opense4._engine import Dispatcher
from opense4.services import ServiceError


def _commands():
    return support.fixture("codec")["samples"][:3]


class Recorder(ai.Player):
    """Records what it was given; answers from its memory's plan."""

    def politics(self, view, orders):
        self.memory["seen"] = self.memory.get("seen", 0) + 1
        self.memory["draws"] = [self.random.randint(1, 100) for _ in range(3)]
        self.memory["refused"] = self.refused
        self.memory["turn"] = [self.turn, self.call, self.empire_id]
        self.note(view.my.ships[0], "flagship")
        self.note(view.my.ships[0], "flagship, again")   # replaces the first
        self.note(view.my.colonies[0], "home")
        self.note(view.my.home_system_id, "start", kind="system")
        self.log("politics done")

    def orders(self, view, orders):
        ship = view.my.ships[0]
        orders.add(cmd.give(ship, [order.move_to(view.system(view.my.home_system_id))]))
        classic = ai.builtin.orders(view, skip=["attack"])
        orders.extend(cmd.without(classic, "set_research"))
        return [cmd.leave_fleet(vehicle=ship)]

    def colony_type(self, view, question):
        if question.planet is not None:
            self.memory["planet"] = question.planet.name
        return question.choices[-1]

    def enter_sector(self, view, question):
        return len(question.enemies) < 2

    def decloak(self, view, question):
        return ai.builtin.answer()

    def battle_round(self, battle, orders):
        for piece in battle.my_pieces:
            target = battle.weakest_enemy_in_range(piece)
            if target is not None:
                orders.target(piece, target)
        orders.add(tactical.end_phase())


def _harness(player=Recorder, **services):
    services.setdefault("builtin", {"politics": [], "orders": _commands(), "economy": []})
    return testing.Harness(player, testing.FakeServices(**services))


def test_a_session():
    view = support.view_map()
    h = _harness(answers={"decloak": True})
    r = h.call("politics", view, seed=11, turn=5, args={"refused": [{"index": 0, "command": {"kind": "x"}, "reason": "no"}]})
    assert "error" not in r, r.get("error")
    assert r["commands"] == [] and r["answer"] is None
    m = r["memory"]
    assert m["seen"] == 1 and m["turn"] == [5, "politics", 0] and m["refused"][0]["reason"] == "no"
    assert r["log"] == ["politics done"]
    ship = h.player.view.my.ships[0]
    assert r["notes"] == [
        {"object": ship.id, "kind": "vehicle", "text": "flagship, again"},
        {"object": h.player.view.my.colonies[0].planet_id, "kind": "object", "text": "home"},
        {"object": view["my"]["home_system"], "kind": "system", "text": "start"},
    ]

    r = h.call("orders", view)
    assert "error" not in r, r.get("error")
    kinds = [c["kind"] for c in r["commands"]]
    assert kinds[0] == "set_orders" and kinds[-1] == "leave_fleet"
    assert r["commands"][1:-1] == [c for c in _commands() if c["kind"] != "set_research"]
    asked = [a for s, a in h.services.calls if s == "builtin"][-1]
    assert asked == {"call": "orders", "ministers": None, "skip": ["attack"]}
    assert r["notes"] == [] and r["log"] == []   # per request

    r = h.call("colony_type", None, args={"colony": 7, "planet": view["colonies"][0]["planet"], "vehicle": None,
                                         "choices": ["Mining", "Research"]})
    assert r["answer"] == "Research" and r["commands"] == []
    assert r["memory"]["planet"] == h.player.view.colonies[0].planet.name   # the session's view resolves it

    r = h.call("enter_sector", None, args={"vehicles": [ship.id], "sector": {"system": 1, "x": 2, "y": 3}, "enemies": [1]})
    assert r["answer"] is True
    r = h.call("decloak", None, args={"object": ship.id, "reason": "attack"})
    assert r["answer"] is True
    assert h.services.calls[-1] == ("builtin_answer", {"call": "decloak", "args": {"object": ship.id, "reason": "attack"}})

    battle = {"round": 1, "rounds_max": 30, "objects": [], "pieces": [
        {"id": 0, "owner": 0, "position": {"x": 1, "y": 1}, "damage": 0, "weapons": [{"range": 3, "reload": 0, "ready": True}]},
        {"id": 1, "owner": 1, "position": {"x": 3, "y": 2}, "damage": 10, "weapons": []},
        {"id": 2, "owner": 1, "position": {"x": 2, "y": 2}, "damage": 40, "weapons": []},
        {"id": 3, "owner": 1, "position": {"x": 9, "y": 9}, "damage": 90, "weapons": []},
    ]}
    r = h.call("battle_round", None, args={"battle": battle})
    orders = r["answer"]["orders"]
    assert [(o["kind"], o["piece"], o["target"], o["empire"]) for o in orders] == [("fire", 0, 2, 0), ("end_phase", -1, -1, 0)]

    r = h.call("end_session")
    assert r["memory"]["seen"] == 1 and h.player is None


def test_memory_survives_sessions_and_nothing_else_does():
    view = support.view_map()
    h = _harness()
    h.call("politics", view)
    h.player.scratch = "kept within the session"
    assert h.call("orders", view)["memory"]["seen"] == 1 and h.player.scratch
    first = h.call("end_session")["memory"]
    assert first["seen"] == 1
    r = h.call("politics", view)
    assert r["memory"]["seen"] == 2
    assert not hasattr(h.player, "scratch")


def test_the_same_seed_gives_the_same_numbers():
    view = support.view_map()
    a = _harness().call("politics", view, seed=99)["memory"]["draws"]
    b = _harness().call("politics", view, seed=99)["memory"]["draws"]
    c = _harness().call("politics", view, seed=100)["memory"]["draws"]
    assert a == b and a != c


def test_errors_answer_with_type_message_and_traceback():
    class Faulty(ai.Player):
        def politics(self, view, orders):
            self.memory["before"] = True
            self.plan()

        def plan(self):
            raise KeyError("no such plan")

        def colony_type(self, view, question):
            return 42

        def enter_sector(self, view, question):
            return "yes"

    view = support.view_map()
    h = _harness(Faulty)
    r = h.call("politics", view)
    e = r["error"]
    assert e["type"] == "KeyError" and "no such plan" in e["message"]
    assert e["traceback"].startswith("Traceback (most recent call last):")
    assert "in plan" in e["traceback"] and "in politics" in e["traceback"]
    assert r["commands"] == [] and r["memory"]["before"] is True
    # The session goes on.
    r = h.call("economy", view)
    assert "error" not in r
    assert h.call("colony_type", None, args={"choices": ["Mining"]})["error"]["type"] == "TypeError"
    assert h.call("enter_sector", None, args={})["error"]["type"] == "TypeError"

    class Picky(ai.Player):
        def colony_type(self, view, question):
            return "Farming"

    assert _harness(Picky).call("colony_type", None, args={"choices": ["Mining"]})["error"]["type"] == "ValueError"


def test_requests_the_protocol_does_not_allow():
    d = Dispatcher(testing.FakeServices())
    r = d.handle(testing.request("orders"))
    assert r["error"]["type"] == "ProtocolError" and "no session" in r["error"]["message"]
    bad = testing.request("politics", start=True, player={"module": "sdk_player_fixture", "class": "Counter"})
    bad["api"] = 2
    assert d.handle(bad)["error"]["type"] == "ProtocolError"
    assert d.handle(testing.request("dance", start=True))["error"]["type"] == "ProtocolError"
    assert d.handle(["not", "a", "map"])["error"]["type"] == "ProtocolError"
    assert d.handle(testing.request("politics", start=True))["error"]["type"] == "ProtocolError"   # no player named


def test_players_imported_by_name():
    view = support.view_map()
    services = testing.FakeServices(builtin={"politics": _commands()})
    d = Dispatcher(services)
    start = testing.request("politics", view, empire=0, start=True,
                            player={"mod": "test.fixture", "name": "Counter", "module": "sdk_player_fixture", "class": "Counter"})
    r = d.handle(start)
    assert "error" not in r, r.get("error")
    assert r["memory"] == {"made": True, "politics": 1}   # the player's own memory the first time
    assert r["commands"] == _commands()                    # the classic politics, by default
    # Two empires at once: a session each.
    other = dict(start, empire=1, memory={"politics": 5})
    assert d.handle(other)["memory"] == {"politics": 6}
    assert d.handle(testing.request("politics", view, empire=0))["memory"]["politics"] == 2
    # A class that is not a player, a missing class, a missing module: each request of the session says why.
    for player, why in (({"module": "sdk_player_fixture", "class": "NotAPlayer"}, "not a subclass"),
                        ({"module": "sdk_player_fixture", "class": "Ghost"}, "has no class"),
                        ({"module": "no_such_module", "class": "X"}, "")):
        r = d.handle(testing.request("politics", view, empire=2, start=True, player=player))
        assert why in r["error"]["message"], r["error"]
        assert d.handle(testing.request("orders", view, empire=2))["error"] == r["error"]


def test_services():
    view = support.view_map()
    paths = support.fixture("whole_paths")
    route = [p for p in paths if p["found"] and p["jumps"]][0]

    class Asker(ai.Player):
        def orders(self, view, orders):
            r = self.query("path", origin=view.system(route["origin"]), destination=route["destination"])
            self.memory["path"] = [r.found, r.length, r.steps[-1].system.id]
            self.memory["components"] = len(self.rules.components)
            self.memory["rules_again"] = self.rules is view.rules
            done = self.apply(cmd.leave_fleet(vehicle=view.my.ships[0]))
            self.memory["applied"] = [done.ok, done.reason]
            try:
                self.query("teleport")
            except ValueError as e:
                self.memory["unknown"] = str(e)
            try:
                self.query("path", vehicel=1)
            except TypeError as e:
                self.memory["typo"] = str(e)
            try:
                self.query("movement", vehicle=1)
            except ServiceError as e:
                self.memory["refused"] = e.type
            try:
                ai.builtin.economy(view, ministers=["chef"])
            except ValueError as e:
                self.memory["minister"] = str(e)

    def path(args):
        assert args == {"origin": route["origin"], "destination": route["destination"]}
        return {"found": True, "steps": [{"system": route["destination"], "x": 6, "y": 6}], "length": route["length"],
                "jumps": route["jumps"], "turns": None}

    services = dict(rules=support.fixture("rules"), queries={"path": path})
    h = testing.Harness(Asker, testing.FakeServices(**services))
    r = h.call("orders", view)
    assert "error" not in r, r.get("error")
    m = r["memory"]
    assert m["path"] == [True, route["length"], route["destination"]]
    assert m["components"] == len(support.fixture("rules")["components"]) and m["rules_again"] is True
    assert m["applied"] == [True, ""]
    assert "teleport" in m["unknown"] and "vehicel" in m["typo"] and m["refused"] == "ValueError" and "chef" in m["minister"]
    assert h.services.applied == [cmd.leave_fleet(vehicle=h.player.view.my.ships[0].id)]
    assert [s for s, _ in h.services.calls].count("rules") == 1   # once per session


def test_services_only_during_a_request():
    e = support.raises(RuntimeError, ai.builtin.orders)
    assert "no request" in str(e)


def test_an_external_bot():
    view = support.view_map()

    class Connection(external.Connection):
        def __init__(self, requests):
            self.requests = list(requests)
            self.responses = []
            self.services = []

        def receive(self):
            return self.requests.pop(0) if self.requests else None

        def send(self, response):
            self.responses.append(response)

        def service(self, name, args):
            self.services.append(name)
            if name == "builtin":
                return []
            raise ServiceError("not here")

    conn = Connection([testing.request("politics", view, start=True), testing.request("orders", view),
                       testing.request("end_session")])
    assert external.run(conn, Recorder) == 3
    assert [("error" in r) for r in conn.responses] == [False, False, False]
    assert conn.services == ["builtin"]   # the orders call asked the classic orders
    assert conn.responses[-1]["memory"]["seen"] == 1
