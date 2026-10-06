"""The Scholar: the classic computer player with a research planner of its own.

mod.toml declares it ([[ai.players]]: module "scholar", class "Scholar"). It overrides
one decision, the economy call, and keeps the classic AI's answer for everything else:
politics, orders, colony types, battles. Of the classic economy it keeps the designs,
intelligence and construction, and replaces the research queue with its own plan
(ai/planner.py).
"""

from opense4 import ai, cmd

import planner


def requirements(record):
    """A rules record's technology needs as (tech index, level) pairs."""
    return [(q.area, q.level) for q in record.requirements]


# What grows an empire: abilities whose components and facilities are worth more than
# the planner's default (planner.WORTH). Ability names are the data files' own
# (docs/sdk/guide/data/abilities.md); every data set has these.
GROWTH = {
    "Colonize Planet - Rock": 20, "Colonize Planet - Ice": 20, "Colonize Planet - Gas": 20,
    "Resource Generation - Minerals": 8, "Resource Generation - Organics": 6, "Resource Generation - Radioactives": 6,
    "Point Generation - Research": 8, "Space Yard": 6, "Standard Ship Movement": 5,
    "Armor": 4, "Shield Generation": 4, "Supply Storage": 3, "Cargo Storage": 2,
}


def worth(kind, record, families):
    """An item's worth to the planner: its best growth ability, else its kind's default;
    half that for a later level of a component or facility family we have already."""
    best = planner.WORTH[kind]
    for a in getattr(record, "abilities", None) or []:
        best = max(best, GROWTH.get(a.name, 0))
    if (kind, getattr(record, "family", 0)) in families:
        best = max(1, best // 2)
    return best


class Scholar(ai.Player):
    """Classic in all but research."""

    def economy(self, view, orders):
        # The classic economy: designs, research, intelligence and construction.
        classic = ai.builtin.economy(view)
        orders.extend(cmd.without(classic, "set_research"))

        queue = self.plan(view)
        if queue:
            orders.add(cmd.set_research(queue, evenly=False))
            names = [view.rules.tech(a).name for a in queue[:3]]
            self.note(view.me, "researching " + ", ".join(names))
        elif cmd.only(classic, "set_research"):
            # Nothing of ours to research (every area at its maximum): let the classic
            # minister keep whatever it wants.
            orders.extend(cmd.only(classic, "set_research"))
        self.memory["plans"] = self.memory.get("plans", 0) + 1

    def plan(self, view):
        """The research queue for this turn, from the view and the rules view."""
        rules = view.rules
        research = view.my.research
        items = self.items(rules, research.levels)
        techs = [{"max_level": t.max_level, "level_cost": t.level_cost} for t in rules.techs]
        in_progress = [e.area for e in research.queue if e.area is not None and e.progress > 0]
        return planner.plan(research.levels, research.researchable, in_progress, techs, items, view.game.options.tech_cost,
                            research.income)

    def items(self, rules, levels):
        """Everything that needs technology, as the planner takes it, with its worth.
        Made once per session (on self, never in memory: a session is one engine call,
        and the rules view is the same all game)."""
        made = getattr(self, "_items", None)
        if made is None:
            # The component and facility families we can build already.
            families = set()
            for kind, table in (("component", rules.components), ("facility", rules.facilities)):
                for record in table:
                    if record.family and all(q.area is None or levels[q.area] >= q.level for q in record.requirements):
                        families.add((kind, record.family))
            made = []
            for kind, table in (("component", rules.components), ("facility", rules.facilities), ("hull", rules.hulls),
                                ("mount", rules.mounts), ("tech", rules.techs)):
                for record in table:
                    needs = requirements(record)
                    if needs:
                        made.append((kind, needs, worth(kind, record, families)))
            self._items = made
        return made
