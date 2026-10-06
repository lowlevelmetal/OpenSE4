"""The interface fixture's computed values (docs/sdk/interface.md, "Computed values")."""

from opense4 import ui


@ui.value("design_name")
def design_name(view, ship):
    return ship.design.name if ship.design is not None else None


@ui.value("population_text")
def population_text(view, colony):
    return str(colony.total_population) + "M"


@ui.value("beacon_bonus")
def beacon_bonus(view, empire):
    for option in view.game.options.mod_options:
        if option.mod == "test.ui-fixture" and option.name == "beacon_bonus":
            return option.value
    return None


@ui.value("always_fails")
def always_fails(view, system):
    raise ValueError("the fixture's value always fails")
