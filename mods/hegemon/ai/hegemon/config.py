"""Hegemon's switches and weights in one place (mods/hegemon/README.md, "Tuning").
The evaluation turns features on and off here to measure what each one is worth."""

FEATURES = {
    # On: each measured better than without it, or no worse (README, "Evaluation").
    "late_surge": True,       # more of the income to the fleet late in the game
    "wide_expansion": True,   # settle small and far planets too
    "platform_all": True,     # a weapon platform on every colony
    "target_shares": True,    # prices aim at an output mix per phase
    "overflow_ships": True,   # overflowing stores pay for warships beyond the upkeep share
    "troop_research": True,   # research troops in arm and war phases
    "raids": True,            # strikes prefer colonies our loaded troops can take
    "convert": True,          # convert an overflowing resource into a short one
    # Off: measured worse, or no better.
    "price_designs": False,   # the designer weighs costs by how scarce each resource is
    "yard_demand": False,     # yards (facilities and bases) as unmet ship demand asks
    "colonize_value": False,  # value colonization research by every planet it opens
    "protect": False,         # new facilities leave room for the warships we pay for
    "storage_low": False,     # storage facilities are worth little
    "platform_two": False,    # two weapon platforms on every colony
    "more_colony_ships": False,  # more colony ships under way at once
    "expand_military": False,    # a little more upkeep for the fleet while expanding
}


def on(name):
    return FEATURES.get(name, False)
