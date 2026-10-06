# The Frontier Charter's tests: its rules functions with stand-ins for the game and the
# effects (here and under pytest). opense4-sdk test also plays the scenario itself for
# a few turns, which fails on any error its rules raise.

import frontier


class Empire:
    def __init__(self, id):
        self.id = id
        self.mod_data = {}


class Game:
    def __init__(self, grant, empires):
        self.turn = 7
        self.grant = grant
        self.empires = empires

    def option(self, name):
        assert name == "grant"
        return self.grant


class Objective:
    def __init__(self, empire):
        self.name = "second_colony"
        self.empire = empire


class Effects:
    def __init__(self):
        self.paid = []
        self.logs = []

    def add_resources(self, empire, minerals=0, organics=0, radioactives=0):
        self.paid.append((empire.id, minerals))

    def log(self, empire, text, title="", category="misc", location=None):
        self.logs.append((empire.id, title))


def test_the_grant_is_paid_once_to_whoever_met_the_objective():
    empire = Empire(2)
    fx = Effects()
    frontier.charter_grant(Game(3000, [empire]), Objective(empire), fx)
    assert fx.paid == [(2, 3000)]
    assert fx.logs == [(2, "Charter grant")]
    assert empire.mod_data == {"granted": 7}


def test_no_grant_when_the_option_is_zero():
    empire = Empire(0)
    fx = Effects()
    frontier.charter_grant(Game(0, [empire]), Objective(empire), fx)
    assert fx.paid == [] and fx.logs == []


def test_every_empire_hears_of_the_charter():
    fx = Effects()
    frontier.announce(Game(500, [Empire(0), Empire(1), Empire(2)]), fx)
    assert [e for e, _ in fx.logs] == [0, 1, 2]
