# A computer player written with OpenSE4's own `opense4` package
# (docs/sdk/python-api.md), for the engine's tests of the package in the game.

from opense4 import ai, tactical


class Captain(ai.Player):
    """The classic ministers, but its own colony types, its own fire in each
    battle round, and a note on its first ship. Counts what it did in its memory."""

    def _count(self, key):
        self.memory[key] = self.memory.get(key, 0) + 1

    def orders(self, view, orders):
        self._count("plans")
        orders.extend(ai.builtin.orders(view, skip=["patrol"]))
        ships = view.my.ships
        if ships:
            self.note(ships[0], "turn " + str(self.turn))

    def colony_type(self, view, question):
        self._count("colonies")
        choices = question.choices
        return choices[question.planet_id % len(choices)] if choices else None

    def enter_sector(self, view, question):
        return True

    def battle_round(self, battle, orders):
        self._count("rounds")
        for piece in battle.my_pieces:
            target = battle.weakest_enemy_in_range(piece)
            if target is not None:
                orders.fire(piece, target)
        orders.add(tactical.auto_phase())

    def end_session(self):
        self._count("sessions")
