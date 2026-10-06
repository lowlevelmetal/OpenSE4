"""What wrapping a large view costs (docs/sdk/python-api.md, "Costs").

A view of the engine fixture is copied into a large one (a galaxy of `copies` copies
of the fixture's systems, twelve times as many vehicles per copy), then each phase
does what a computer player's turn commonly does, building on the phases before it.
tests/sdk/test_sdk_python.cpp times the phases in the game's runtime (with their
bytecodes); under CPython:

    python3 tests/sdk/python/sdk_bench.py --view view.json [--copies 40]
"""

import sys

_STATE = {}

PHASES = (
    "wrap",
    "lookup_1000_by_id",
    "my_vehicles",
    "idle_and_by_type",
    "vehicles_list",
    "read_5_fields_each_vehicle",
    "read_5_fields_each_raw_map",
    "colonies_planets_systems",
    "jumps_from_home",
    "jumps_50_pairs",
)

VEHICLES_PER_COPY = 12


def _shift_location(loc, off):
    loc = dict(loc)
    if loc["system"] is not None:
        loc["system"] = loc["system"] + off
    return loc


def make_big(view, copies):
    """A copy of `view` with `copies` galaxies of its systems and more vehicles."""
    so = 1 + max([s["id"] for s in view["systems"]] + [0])
    oo = 1 + max([o["id"] for o in view["objects"]] + [0])
    vo = 1 + max([v["id"] for v in view["vehicles"]] + [0])
    fo = 1 + max([f["id"] for f in view["fleets"]] + [0])
    big = dict(view)
    systems, objects, colonies, vehicles, fleets = [], [], [], [], []
    for k in range(copies):
        for s in view["systems"]:
            s = dict(s)
            s["id"] += k * so
            if s["objects"] is not None:
                s["objects"] = [i + k * oo for i in s["objects"]]
            systems.append(s)
        for o in view["objects"]:
            o = dict(o)
            o["id"] += k * oo
            o["system"] += k * so
            if o["destination"] is not None:
                o["destination"] += k * oo
            if o["destination_system"] is not None:
                o["destination_system"] += k * so
            objects.append(o)
        for c in view["colonies"]:
            c = dict(c)
            c["planet"] += k * oo
            colonies.append(c)
        for m in range(VEHICLES_PER_COPY):
            n = k * VEHICLES_PER_COPY + m
            for v in view["vehicles"]:
                v = dict(v)
                v["id"] += n * vo
                v["location"] = _shift_location(v["location"], k * so)
                if v["fleet"] is not None:
                    v["fleet"] += n * fo
                vehicles.append(v)
            for f in view["fleets"]:
                f = dict(f)
                f["id"] += n * fo
                f["members"] = [i + n * vo for i in f["members"]]
                for key in ("leader", "chosen_leader"):
                    if f[key] is not None:
                        f[key] += n * vo
                f["location"] = _shift_location(f["location"], k * so)
                fleets.append(f)
    big["systems"] = systems
    big["objects"] = objects
    big["colonies"] = colonies
    big["vehicles"] = vehicles
    big["fleets"] = fleets
    return big


def setup(view, copies=40):
    """Makes the large view; returns how large it is. The modules are imported here, so
    that the phases measure only the work."""
    import opense4.galaxy  # noqa: F401
    import opense4.view  # noqa: F401
    big = make_big(view, copies)
    _STATE.clear()
    _STATE["raw"] = big
    return {key: len(big[key]) for key in ("systems", "objects", "colonies", "vehicles", "fleets", "designs")}


def phase_names():
    return list(PHASES)


def phase(name):
    """Runs one phase on the large view; returns a number so nothing is optimised away."""
    from opense4.view import View
    if name == "wrap":
        _STATE["view"] = View(_STATE["raw"])
        return 1
    view = _STATE["view"]
    if name == "vehicles_list":
        return len(view.vehicles)
    if name == "lookup_1000_by_id":
        ids = [v["id"] for v in _STATE["raw"]["vehicles"]]
        step = max(1, len(ids) // 1000)
        found = 0
        for i in range(0, len(ids), step):
            if view.vehicle(ids[i]) is not None:
                found += 1
        return found
    if name == "my_vehicles":
        return len(view.my.vehicles)
    if name == "idle_and_by_type":
        return len(view.my.idle_vehicles) + len(view.my.ships) + len(view.my.units)
    if name == "read_5_fields_each_vehicle":
        total = 0
        for v in view.vehicles:
            d = v.design
            total += len(v.name) + v.location.x + (0 if d is None else len(d.name)) + v.count + v.damage
        return total
    if name == "read_5_fields_each_raw_map":
        # The same reads on the maps themselves (record.raw): what the wrappers add.
        designs = {d["id"]: d for d in _STATE["raw"]["designs"]}
        total = 0
        for v in _STATE["raw"]["vehicles"]:
            d = designs.get(v["design"])
            total += len(v["name"]) + v["location"]["x"] + (0 if d is None else len(d["name"])) + v["count"] + v["damage"]
        return total
    if name == "colonies_planets_systems":
        total = 0
        for c in view.colonies:
            p = c.planet
            if p is not None:
                total += len(p.name) + (0 if c.system is None else 1)
        return total
    if name == "jumps_from_home":
        home = view.my.home_system_id
        return len(view.galaxy.distances(home))
    if name == "jumps_50_pairs":
        systems = [s.id for s in view.explored_systems]
        g = view.galaxy
        total = 0
        for i in range(50):
            a = systems[(i * 7) % len(systems)]
            b = systems[(i * 13 + 5) % len(systems)]
            j = g.jumps(a, b)
            total += -1 if j is None else j
        return total
    raise ValueError("no phase " + repr(name))


def main(argv):
    import argparse
    import json
    import os
    import time
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "python"))
    ap = argparse.ArgumentParser(description="what wrapping a large view costs, under CPython")
    ap.add_argument("--view", required=True, help="a view as JSON")
    ap.add_argument("--copies", type=int, default=40)
    args = ap.parse_args(argv)
    with open(args.view, encoding="utf-8") as f:
        view = json.load(f)
    print("a large view: " + json.dumps(setup(view, args.copies)))
    for name in PHASES:
        t0 = time.perf_counter()
        phase(name)
        print("  {:<28} {:>10.0f} us".format(name, (time.perf_counter() - t0) * 1e6))
    return 0


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.exit(main(sys.argv[1:]))
