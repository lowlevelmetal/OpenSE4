"""A computer player that gives the rules fixture's bounty order every turn."""

from opense4 import ai, cmd


class BountyHunter(ai.Player):
    def orders(self, view, orders):
        orders.extend(ai.builtin.orders(view))
        orders.add(cmd.mod_command(mod="test.rules-fixture", name="bounty", args={"times": 2}))
        self.memory["orders"] = self.memory.get("orders", 0) + 1
