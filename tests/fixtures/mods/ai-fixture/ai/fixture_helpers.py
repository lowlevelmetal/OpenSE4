# Shared by the fixture players: a module of the mod's own, imported by name.


def empty_response():
    return {'commands': [], 'answer': None, 'notes': [], 'log': []}


def enemies_of(empire, pieces):
    out = []
    for p in pieces:
        if p['alive'] and p['owner'] is not None and p['owner'] != empire and p['kind'] not in ('seeker', 'obstacle'):
            out.append(p)
    return out
