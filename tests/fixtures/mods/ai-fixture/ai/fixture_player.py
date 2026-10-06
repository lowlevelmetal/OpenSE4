# Computer players for OpenSE4's tests, written straight to the protocol
# (docs/sdk/ai-protocol.md): each handles the engine's requests itself, with
# the engine's services (_opense4), and without the opense4 package's classes.

import _opense4
from fixture_helpers import empty_response, enemies_of


class Steady:
    """The classic ministers, but its own research order (the areas it knows
    least first), its own colony types, its own fire in one battle round of
    three, and now and then a sector it keeps out of. Counts what it did in its
    memory."""

    def __init__(self):
        self.memory = None

    def handle(self, request):
        if self.memory is None:
            self.memory = {'sessions': 0, 'plans': 0, 'colonies': 0, 'battles': 0, 'declined': 0, 'refused': 0}
        refused = request['args'].get('refused')
        if refused:
            self.memory['refused'] += len(refused)
        return getattr(self, request['call'])(request, request['args'])

    def politics(self, request, args):
        r = empty_response()
        r['commands'] = _opense4.builtin({'call': 'politics'})
        return r

    def orders(self, request, args):
        r = empty_response()
        r['commands'] = _opense4.builtin({'call': 'orders', 'skip': ['patrol']})
        self.memory['plans'] += 1
        me = request['empire']
        mine = [v for v in request['view']['vehicles'] if v['owner'] == me]
        r['notes'] = [{'object': v['id'], 'kind': 'vehicle', 'text': 'turn %d' % request['turn']} for v in mine[:2]]
        return r

    def economy(self, request, args):
        r = empty_response()
        commands = _opense4.builtin({'call': 'economy', 'skip': ['research']})
        research = request['view']['my']['research']
        levels = research['levels']
        order = sorted(research['researchable'], key=lambda a: (levels[a], a))[:3]
        if order:
            commands.append({'kind': 'set_research', 'queue': [{'area': a} for a in order], 'evenly': True})
        r['commands'] = commands
        return r

    def colony_type(self, request, args):
        r = empty_response()
        self.memory['colonies'] += 1
        choices = args['choices']
        planet = args['planet']
        if choices and planet is not None:
            r['answer'] = choices[planet % len(choices)]
        return r

    def enter_sector(self, request, args):
        r = empty_response()
        if (request['turn'] + args['sector']['x']) % 7 == 0:
            self.memory['declined'] += 1
            r['answer'] = False
        return r

    def decloak(self, request, args):
        r = empty_response()
        r['answer'] = True
        return r

    def battle_round(self, request, args):
        r = empty_response()
        battle = args['battle']
        if battle['round'] % 3 != 1:
            return r
        self.memory['battles'] += 1
        me = request['empire']
        enemies = enemies_of(me, battle['pieces'])
        orders = []
        if enemies:
            target = min(enemies, key=lambda p: (-p['damage'], p['id']))
            for p in battle['pieces']:
                if p['owner'] == me and p['alive'] and p['kind'] != 'seeker' and any(w['ready'] > 0 for w in p['weapons']):
                    orders.append({'kind': 'fire', 'piece': p['id'], 'target': target['id'], 'weapon': -1})
        orders.append({'kind': 'auto_phase'})
        r['answer'] = {'orders': orders}
        return r

    def end_session(self, request, args):
        self.memory['sessions'] += 1
        return empty_response()


class Probe:
    """Records every request in its memory's `calls`, and does what its
    memory's `script` says for each call: raise, run forever, grow its memory
    or the heap, answer, give commands, use the services, return a response
    as given."""

    def __init__(self):
        self.memory = None

    def handle(self, request):
        if self.memory is None:
            self.memory = {}
        args = request['args']
        entry = {'call': request['call'], 'turn': request['turn'], 'seed': request['seed'], 'view': request['view'] is not None,
                 'args': sorted(args.keys())}
        if 'player' in request:
            entry['player'] = request['player']
            entry['memory'] = 'memory' in request
        if 'refused' in args:
            entry['refused'] = args['refused']
        script = self.memory.get('script', {}).get(request['call'], {})
        if script.get('keep_args'):
            entry['given'] = args
        if script.get('keep_view') and request['view'] is not None:
            entry['view_keys'] = sorted(request['view'].keys())
            entry['view_empire'] = request['view']['empire']
            entry['view_whole'] = request['view']['whole']
        self.memory.setdefault('calls', []).append(entry)
        do = script.get('do')
        if do == 'raise':
            raise ValueError('as the test asked')
        if do == 'loop':
            n = 0
            while True:
                n += 1
        if do == 'grow':
            self.memory['big'] = 'x' * script.get('size', 2000000)
        if do == 'heap':
            hoard = []
            while True:
                hoard.append('y' * 100000)
        if 'respond' in script:
            respond = script['respond']
            return dict(respond) if isinstance(respond, dict) else list(respond)
        r = {}
        if 'apply' in script:
            results = [_opense4.apply({'command': c}) for c in script['apply']]
            self.memory['applied'] = [{'ok': x['ok'], 'reason': x['reason'], 'changed': sorted(x['changed'].keys()),
                                       'removed': sorted(x['removed'].keys())} for x in results]
        if 'query' in script:
            self.memory['query'] = _opense4.query(script['query'])
        if 'builtin' in script:
            self.memory['builtin_' + script['builtin']['call']] = _opense4.builtin(script['builtin'])
        if 'builtin_answer' in script:
            self.memory['builtin_answer'] = _opense4.builtin_answer(script['builtin_answer'])
        if 'rules' in script:
            self.memory['rules'] = len(_opense4.rules()['components'])
        for key in ('commands', 'answer', 'notes', 'log'):
            if key in script:
                r[key] = script[key]
        return r


class Idle:
    """Answers every request with nothing: its empire does nothing at all."""

    def __init__(self):
        self.memory = None

    def handle(self, request):
        return {}
