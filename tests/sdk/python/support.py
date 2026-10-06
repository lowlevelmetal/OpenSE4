"""Shared by the SDK package's tests (tests/sdk/python/test_sdk_*.py): the fixtures the
C++ side made from a game of the engine fixture, skipping, and the values the tests
publish for the C++ side to check (commands to decode, random numbers to compare
between the two runtimes).

The fixtures come from tests/sdk/test_sdk_python.cpp: in the game's runtime as a
value, under CPython as a JSON file (run_sdk_tests.py --fixtures, or the
OPENSE4_SDK_FIXTURES variable under pytest).
"""

FIXTURES = {}
OUTPUTS = {}


class Skip(Exception):
    """The test cannot run here (a fixture is missing)."""


def skip(reason):
    try:
        import pytest  # noqa: F401  (only when running under pytest)
    except ImportError:
        raise Skip(reason)
    pytest.skip(reason)


def fixture(name):
    if not FIXTURES:
        load_from_environment()
    if name not in FIXTURES:
        skip("no fixture " + repr(name) + " (tests/sdk/test_sdk_python.cpp makes them)")
    return FIXTURES[name]


def load_from_environment():
    """Under pytest: the fixtures from the file OPENSE4_SDK_FIXTURES names, if any."""
    try:
        import json
        import os
    except ImportError:
        return
    path = os.environ.get("OPENSE4_SDK_FIXTURES")
    if path and os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            FIXTURES.update(json.load(f))


def publish(key, value):
    """A value for the C++ side to check after the run."""
    OUTPUTS[key] = value


def raises(exception, fn, *args, **kwargs):
    """The exception `fn(*args, **kwargs)` raises, which must be of type `exception`."""
    try:
        fn(*args, **kwargs)
    except exception as e:
        return e
    raise AssertionError("expected " + exception.__name__ + " from " + getattr(fn, "__name__", "the call"))


def view_map():
    return fixture("view")
