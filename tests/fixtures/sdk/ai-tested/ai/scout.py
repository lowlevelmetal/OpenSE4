# A computer player written for OpenSE4's tests of opense4-sdk test and run, with the
# opense4 package (docs/sdk/python-api.md).

from opense4 import ai


class Scout(ai.Player):
    """The classic ministers, research colonies where the empire has the type, and a count
    of the turns it planned."""

    def orders(self, view, orders):
        self.memory["turns"] = self.memory.get("turns", 0) + 1
        orders.extend(ai.builtin.orders(view))
        colonies = view.my.colonies
        if colonies:
            self.note(colonies[0], "home, turn " + str(self.turn))

    def colony_type(self, view, question):
        return "Research" if "Research" in question.choices else None
