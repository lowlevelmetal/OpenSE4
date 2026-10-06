"""Pioneer: a small computer player written from scratch, to be read.

mod.toml declares it ([[ai.players]]: module "pioneer", class "Pioneer"). A turn, as the
engine asks it (docs/sdk/ai-protocol.md, section 3):

1. politics(): answer what other empires sent: accept peace and trade, refuse the rest.
2. orders(): move the ships: scouts explore, colony ships settle, warships defend
   (ai/expansion.py, ai/defence.py).
3. economy(): designs, research and construction (ai/designs.py, ai/economy.py).

It overrides those three and nothing else: a new colony's type, entering a sector with
enemies, decloaking and the battles themselves are the classic AI's (the colony type
only steers the classic ministers, which Pioneer does not use).

Its memory (self.memory, saved with the game) holds only what it cannot read again
from the view: the role of each of its designs, how many of each it has made, and which
colony ship is on its way to which planet. Everything else it works out each turn.
"""

from opense4 import ai, cmd

import defence
import designs
import economy
import expansion
import parts as p

# The treaties Pioneer accepts when another empire proposes them: anything but war
# and subjugation.
FRIENDLY = ("non_aggression", "trade_alliance", "trade_research_alliance", "military_alliance", "partnership")


class Pioneer(ai.Player):
    """Explores, colonizes, builds its economy and defends what it has."""

    # ---- Diplomacy ------------------------------------------------------------------

    def politics(self, view, orders):
        for message in view.messages:
            if message.to_empire_id != self.empire_id or message.answered or not message.delivered:
                continue
            if message.type == "propose_treaty":
                orders.add(cmd.answer_message(message=message, accept=message.treaty in FRIENDLY))
            elif message.type in ("propose_trade", "gift"):
                orders.add(cmd.answer_message(message=message, accept=True))

    # ---- The ships ------------------------------------------------------------------

    def orders(self, view, orders):
        roles = self.roles(view)
        claimed = self.memory.setdefault("claimed", {})
        expansion.forget_finished(view, claimed)

        idle = view.my.idle_vehicles
        scouts = [v for v in idle if roles.get(v.design_id) == "scout"]
        colony_ships = [v for v in idle if roles.get(v.design_id, "").startswith("colony")]
        warships = [v for v in idle if roles.get(v.design_id) == "warship"]

        orders.extend(expansion.send_scouts(self, view, scouts))
        orders.extend(expansion.send_colony_ships(self, view, colony_ships, claimed))
        orders.extend(defence.guard(self, view, warships, view.my.idle_fleets))

        # Anything else idle that we did not design (a ship the game gave us): home.
        for v in idle:
            if v.design_id not in roles and v.type == "ship":
                care = defence.needs_care(self, v)
                if care is not None:
                    orders.add(care)

    # ---- Designs, research and construction --------------------------------------

    def economy(self, view, orders):
        # Commands the game refused last time come back here: worth a line in the log
        # (opense4.log), where they show what the player got wrong.
        for refused in self.refused:
            self.log("refused {}: {}".format(refused["command"]["kind"], refused["reason"]))
        parts = p.Parts(view.rules, view.my.research.levels)
        designer = designs.Designer(self, view, parts)

        # Designs first: they are applied at once (designs.Designer uses apply), so the
        # ships below can be queued from them this same turn when the view has them.
        scout = designer.update("scout")
        warship = designer.update("warship")
        colony_ships = {}
        for surface, ability in sorted(p.COLONIZE.items()):
            if parts.best_with(ability) is not None:            # a colony module for that surface
                colony_ships[surface] = designer.update("colony:" + surface, surface)

        orders.extend(economy.research(view, parts))
        orders.extend(economy.build_facilities(self, view, parts))
        wanted = self.wanted(view, scout, warship, colony_ships)
        orders.extend(economy.build_ships(self, view, wanted))
        self.log("turn {}: {} colonies, {} ships; still wanted {}".format(
            view.game.turn, len(view.my.colonies), len(view.my.ships), [w for w in wanted if w[1] > 0]))

    def wanted(self, view, scout, warship, colony_ships):
        """How many more of each design we want, most urgent first: [(design id, more)]."""
        roles = self.roles(view)
        have = {}
        for v in view.my.vehicles:
            role = roles.get(v.design_id)
            if role is not None:
                have[role] = have.get(role, 0) + 1
        for design_id, count in economy.queued_designs(view.my.colonies).items():
            role = roles.get(design_id)
            if role is not None:
                have[role] = have.get(role, 0) + count

        wanted = []
        if view.unexplored_systems and have.get("scout", 0) < 2:
            wanted.append((scout, 2 - have.get("scout", 0)))
        # Colony ships: one for each good planet of a surface we can settle, at most
        # three of a kind at a time, and one more for every three colonies we have.
        targets = expansion.targets(view, self.memory.get("claimed", {}))
        for surface, design_id in colony_ships.items():
            role = "colony:" + surface
            want = min(3 + len(view.my.colonies) // 3, len(targets.get(surface, [])))
            if want > have.get(role, 0):
                wanted.append((design_id, want - have.get(role, 0)))
        # Warships: one for every two colonies, and two more while enemies are near.
        threat = any(v.system is not None and v.system.id in {c.system.id for c in view.my.colonies if c.system}
                     for v in view.enemy_vehicles)
        want = len(view.my.colonies) // 2 + (2 if threat else 0)
        if want > have.get("warship", 0):
            wanted.append((warship, want - have.get("warship", 0)))
        if threat:
            wanted.sort(key=lambda w: w[0] != warship)      # defence first
        return wanted

    # ---- Memory -----------------------------------------------------------------------

    def roles(self, view):
        """{design id: role} for every design we made, current or not. Memory keeps each
        role's current design; older ones of the same role are found by their name."""
        current = self.memory.get("designs", {})
        by_id = {design_id: role for role, design_id in current.items()}
        prefix = view.me.name.split()[0] + " "
        for d in view.my.designs:
            if d.id in by_id or not d.name.startswith(prefix):
                continue
            label = d.name[len(prefix):].rsplit(" ", 1)[0]
            role = {"Scout": "scout", "Warship": "warship"}.get(label)
            if role is None and label.startswith("Colony "):
                role = "colony:" + label[len("Colony "):]
            if role is not None:
                by_id[d.id] = role
        return by_id

