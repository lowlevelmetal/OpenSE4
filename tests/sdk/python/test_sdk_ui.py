"""opense4.ui: the interface tier's registry of computed values, the player's view as
values read it (with a stand-in for the engine's native functions) and the dispatcher
the engine calls (docs/sdk/interface.md)."""

from opense4 import ui


class FakeNative:
    """The engine's _opense4_ui functions, answered from prepared values; records calls."""

    def __init__(self, parts=None, records=None):
        self.parts = parts or {}
        self.records = records or {}
        self.calls = []

    def read(self, arg):
        self.calls.append(("read", arg))
        if "id" in arg:
            return self.records.get((arg["what"], arg["id"]))
        return self.parts.get(arg["what"])

    def ability(self, arg):
        self.calls.append(("ability", arg))
        return 7

    def rules(self, arg):
        return {"techs": [], "components": []}


def _backend(native):
    return ui._Backend(native)


def _request(name, kind, id, mod=""):
    return {"api": 1, "call": "value", "mod": mod, "name": name, "kind": kind, "id": id, "empire": 0}


def test_values_register_by_mod_and_name():
    ui.clear()
    ui._loading = "example.shields"

    @ui.value("charge")
    def charge(view, ship):
        return 3

    ui._loading = None
    assert [r.name for r in ui.registrations("example.shields")] == ["charge"]
    assert ui.find("example.shields", "charge") is charge
    assert ui.find("example.other", "charge") is None
    try:
        ui.value("")
        raise AssertionError("an empty name was taken")
    except TypeError:
        pass
    ui.clear()


def test_a_value_reads_the_thing_from_the_players_view():
    ui.clear()
    ui._view = None

    @ui.value("supply_text")
    def supply_text(view, ship):
        return str(ship.supply) + " units"

    native = FakeNative(records={("vehicle", 4): {"id": 4, "owner": 0, "name": "Lancer", "supply": 120, "design": None}})
    r = ui.dispatch(_request("supply_text", "vehicle", 4), _backend(native))
    assert r == {"value": "120 units"}, r
    assert native.calls[0] == ("read", {"what": "vehicle", "id": 4})
    # A thing the view does not have: no value, and the function is not called.
    ui._view = None
    r = ui.dispatch(_request("supply_text", "vehicle", 5), _backend(FakeNative()))
    assert r == {"value": None}, r
    ui._view = None
    ui.clear()


def test_answers_are_plain_values():
    ui.clear()
    ui._view = None

    @ui.value("list")
    def as_list(view, empire):
        return [1, "two", True, None]

    @ui.value("float")
    def as_float(view, empire):
        return 1.5

    @ui.value("record")
    def as_record(view, empire):
        return empire

    native = FakeNative(records={("empire", 0): {"id": 0, "name": "Terrans"}})
    assert ui.dispatch(_request("list", "empire", 0), _backend(native)) == {"value": [1, "two", True, None]}
    r = ui.dispatch(_request("float", "empire", 0), _backend(native))
    assert r["error"]["type"] == "TypeError" and "float" in r["error"]["message"], r
    assert ui.dispatch(_request("record", "empire", 0), _backend(native)) == {"value": "Terrans"}
    ui._view = None
    ui.clear()


def test_an_error_comes_back_with_its_traceback():
    ui.clear()
    ui._view = None

    @ui.value("broken")
    def broken(view, empire):
        raise ValueError("no charge")

    native = FakeNative(records={("empire", 0): {"id": 0, "name": "Terrans"}})
    r = ui.dispatch(_request("broken", "empire", 0), _backend(native))
    assert r["error"]["type"] == "ValueError" and r["error"]["message"] == "no charge", r
    r = ui.dispatch(_request("missing", "empire", 0), _backend(native))
    assert r["error"]["type"] == "LookupError" and "registers no value" in r["error"]["message"], r
    assert ui.dispatch({"api": 2})["error"]["type"] == "ProtocolError"
    ui._view = None
    ui.clear()


def test_the_view_reads_parts_and_abilities_from_the_engine():
    ui.clear()
    ui._view = None
    native = FakeNative(parts={"game": {"turn": 12}})
    view = ui.PlayerView(_backend(native), empire=0)
    assert view._d["game"]["turn"] == 12
    assert view.ability(("vehicle", 3), "Supply Storage") == 7
    assert ("ability", {"kind": "vehicle", "id": 3, "name": "Supply Storage"}) in native.calls
