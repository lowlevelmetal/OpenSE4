"""Runs the external bots' tests (test_sdk_external.py) under CPython 3.10 or newer, with
the opense4 package on PYTHONPATH:

    PYTHONPATH=python python3 tests/sdk/python/run_external_tests.py [-k WORD]

tests/sdk/test_sdk_bots.cpp and test_sdk_arena.cpp run it; the training environment's tests
need OPENSE4_SDK and OPENSE4_ENV_DATA (else they are skipped).
"""

import os
import sys
import traceback


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    sys.path.insert(1, os.path.join(here, "..", "..", "..", "python"))
    import support
    import test_sdk_external as module
    only = argv[argv.index("-k") + 1] if "-k" in argv else ""
    passed, failed, skipped = [], [], []
    for name in sorted(n for n in dir(module) if n.startswith("test_") and only in n):
        try:
            getattr(module, name)()
            passed.append(name)
        except support.Skip as e:
            skipped.append(name + ": " + str(e))
        except Exception as e:
            failed.append(name + ": " + type(e).__name__ + ": " + str(e) + "\n" + "".join(traceback.format_exception(e)))
    for line in failed:
        print("FAILED " + line)
    for line in skipped:
        print("skipped " + line)
    print("{} passed, {} failed, {} skipped".format(len(passed), len(failed), len(skipped)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
