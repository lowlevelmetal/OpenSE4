# The engine's entry point, written to the protocol (docs/sdk/ai-protocol.md):
# dispatch(request) -> response. The player named in an empire's first request
# of the session is made then and kept for the session; it handles every
# request with handle(request) and keeps its memory in `memory`.

_players = {}


def _make(spec):
    module = __import__(spec['module'])
    for part in spec['module'].split('.')[1:]:
        module = getattr(module, part)
    return getattr(module, spec['class'])()


def dispatch(request):
    empire = request['empire']
    if 'player' in request:
        player = _make(request['player'])
        player.memory = request.get('memory')
        _players[empire] = player
    player = _players.get(empire)
    if player is None:
        return {'error': {'type': 'LookupError', 'message': 'no player for empire %d' % empire, 'traceback': ''}}
    try:
        response = player.handle(request)
    except Exception as e:
        return {'error': {'type': type(e).__name__, 'message': str(e), 'traceback': ''}}
    if not isinstance(response, dict):
        return response
    response['memory'] = player.memory
    return response
