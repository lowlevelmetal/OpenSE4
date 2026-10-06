"""Runs the SDK package's tests (test_sdk_*.py) on whichever Python runs it: the game's
MicroPython (tests/sdk/test_sdk_python.cpp calls run()) or CPython 3.10 or newer:

    python3 tests/sdk/python/run_sdk_tests.py --fixtures fixtures.json [--out results.json]

The fixtures are made by the C++ side; without them the tests that need them are
skipped. The tests also run under pytest (OPENSE4_SDK_FIXTURES names the fixtures).
"""

import sys

import support

MODULES = (
    "test_sdk_random",
    "test_sdk_commands",
    "test_sdk_view",
    "test_sdk_galaxy",
    "test_sdk_dispatch",
    "test_sdk_rules",
)


def _traceback(e):
    try:
        import traceback
        return "".join(traceback.format_exception(e))
    except Exception:
        return ""


def run(fixtures=None, modules=MODULES):
    """Runs every test; returns {passed, failed, skipped, outputs}."""
    support.FIXTURES.clear()
    if fixtures:
        support.FIXTURES.update(fixtures)
    support.OUTPUTS.clear()
    passed, failed, skipped = [], [], []
    for name in modules:
        __import__(name)
        module = sys.modules[name]
        for test in sorted(n for n in dir(module) if n.startswith("test_")):
            fn = getattr(module, test)
            if not callable(fn):
                continue
            label = name + "." + test
            try:
                fn()
                passed.append(label)
            except support.Skip as e:
                skipped.append(label + ": " + str(e))
            except Exception as e:
                failed.append(label + ": " + type(e).__name__ + ": " + str(e) + "\n" + _traceback(e))
    return {"passed": passed, "failed": failed, "skipped": skipped, "outputs": dict(support.OUTPUTS)}


def main(argv):
    import argparse
    import json
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "..", "..", "..", "python"))
    ap = argparse.ArgumentParser(description="the SDK package's tests, under CPython")
    ap.add_argument("--fixtures", help="the fixtures tests/sdk/test_sdk_python.cpp wrote")
    ap.add_argument("--out", help="write the results here, as JSON")
    args = ap.parse_args(argv)
    fixtures = None
    if args.fixtures:
        with open(args.fixtures, encoding="utf-8") as f:
            fixtures = json.load(f)
    results = run(fixtures)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(results, f)
    for line in results["failed"]:
        print("FAILED " + line)
    print("{} passed, {} failed, {} skipped".format(len(results["passed"]), len(results["failed"]), len(results["skipped"])))
    return 1 if results["failed"] else 0


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.exit(main(sys.argv[1:]))
