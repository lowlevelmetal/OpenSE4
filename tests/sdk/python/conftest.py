"""pytest: the SDK package's tests find the package (python/) and their fixtures
(OPENSE4_SDK_FIXTURES, a file tests/sdk/test_sdk_python.cpp writes)."""

import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "python"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import support  # noqa: E402

support.load_from_environment()
