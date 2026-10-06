"""OpenSE4's modding SDK in Python: computer players, the view of the game, commands and,
later, rules hooks. The same package runs inside the game (on its MicroPython) and in
external bots (on CPython 3.10 or newer). docs/sdk/python-api.md is its guide.

    from opense4 import ai, cmd, order     # players, commands, orders
    from opense4 import view, rules        # the view and the rules view, typed
    from opense4 import rng, enums         # random numbers, the enumerations

Submodules load when they are imported, so a script pays only for what it uses.
"""

API = 1
"""The SDK interface this package speaks (docs/MODDING_SDK.md, section 14.5)."""
