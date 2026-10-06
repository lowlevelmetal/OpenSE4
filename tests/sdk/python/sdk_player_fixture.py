"""A computer player module for test_sdk_dispatch.py: the dispatcher imports it by name, as
it imports a mod's player."""

from opense4 import ai


class Counter(ai.Player):
    """Counts its requests in its memory; keeps every classic decision."""

    def __init__(self):
        self.memory = {"made": True}

    def politics(self, view, orders):
        self.memory["politics"] = self.memory.get("politics", 0) + 1
        ai.Player.politics(self, view, orders)


class NotAPlayer:
    pass
