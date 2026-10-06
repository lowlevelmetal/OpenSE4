"""External bots on the bot's side (opense4.external, opense4.bot, opense4.env), under CPython
only: a host written here speaks the connection of docs/sdk/ai-protocol.md, section 10.

The training environment's tests need the engine: OPENSE4_SDK (opense4-sdk) and
OPENSE4_ENV_DATA (a game folder) name them, else they are skipped. run_external_tests.py
runs these tests; so does pytest.
"""

import json
import os
import socket
import sys
import tempfile
import threading

import support
from opense4 import ai, bot, external, testing


class FakeHost:
    """A host on a free port of this computer: `script(host)` plays the game's side of
    the first connection, reading with `read()` and writing with `write()`."""

    def __init__(self, script):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.port = self.server.getsockname()[1]
        self.received = []
        self.error = None
        self.thread = threading.Thread(target=self._run, args=(script,), daemon=True)
        self.thread.start()

    def _run(self, script):
        try:
            self.server.settimeout(20)
            conn, _ = self.server.accept()
            conn.settimeout(20)
            self.conn = conn
            self.file = conn.makefile("rb")
            try:
                script(self)
            finally:
                conn.close()
        except BaseException as e:   # reported by join()
            self.error = e
        finally:
            self.server.close()

    def read(self):
        line = self.file.readline()
        if not line:
            return None
        message = json.loads(line.decode("utf-8"))
        self.received.append(message)
        return message

    def write(self, message):
        self.conn.sendall((json.dumps(message) + "\n").encode("utf-8"))

    def welcome(self, slot=0):
        hello = self.read()["hello"]
        self.write({"welcome": {"api": 1, "slot": slot, "game": "Test game", "timeout_ms": 1000}})
        return hello

    def join(self):
        self.thread.join(30)
        assert not self.thread.is_alive(), "the host's script did not end"
        if self.error is not None:
            raise self.error


def request(call, id, view=None, start=False, args=None):
    r = testing.request(call, view=view, empire=1, turn=4, seed=7, args=args, start=start, player={"slot": 0} if start else None)
    return {"request": r, "id": id}


class Asker(ai.Player):
    """Asks the classic AI, then a query the host refuses; remembers both."""

    def politics(self, view, orders):
        orders.extend(ai.builtin.politics(view))
        try:
            self.query("path", vehicle=1, destination=2)
        except ValueError as e:
            self.memory["refused"] = str(e)

    def orders(self, view, orders):
        self.memory["orders"] = self.memory.get("orders", 0) + 1
        ai.builtin.orders(view)   # the host gives up on this request meanwhile

    def end_session(self):
        self.memory["ended"] = True


def test_hello_names_the_token_slot_and_api_and_bye_ends_the_connection():
    def script(host):
        hello = host.welcome(slot=3)
        assert hello == {"api": 1, "token": "tok", "slot": 3, "name": "Tester"}, hello
        host.write({"bye": {"reason": "the game is over", "winner": 1}})

    host = FakeHost(script)
    conn = external.SocketConnection("127.0.0.1", host.port, "tok", 3, "Tester")
    assert conn.slot == 3 and conn.game == "Test game"
    assert conn.receive() is None
    assert conn.goodbye == {"reason": "the game is over", "winner": 1}
    host.join()


def test_a_refused_bot_learns_why():
    def script(host):
        host.read()
        host.write({"refused": {"message": "wrong token"}})

    host = FakeHost(script)
    try:
        external.SocketConnection("127.0.0.1", host.port, "nope", None, "")
        assert False, "not refused"
    except external.Refused as e:
        assert str(e) == "wrong token"
    host.join()


def test_requests_services_their_refusals_and_a_request_the_game_gave_up_on():
    def script(host):
        host.welcome()
        host.write(request("politics", 1, view={}, start=True))
        service = host.read()
        assert service == {"service": {"name": "builtin", "args": {"call": "politics", "ministers": None, "skip": []}}, "id": 1}, service
        host.write({"result": [{"kind": "end_turn"}], "id": 1})
        query = host.read()
        assert query["service"]["name"] == "query" and query["service"]["args"] == {"name": "path", "args": {"vehicle": 1, "destination": 2}}
        host.write({"service_error": {"type": "ValueError", "message": "vehicle: no such vehicle in view"}, "id": 1})
        response = host.read()
        assert response["id"] == 1 and "error" not in response["response"], response
        assert response["response"]["commands"] == [{"kind": "end_turn"}]
        assert response["response"]["memory"]["refused"] == "vehicle: no such vehicle in view"
        # The game gives up on request 2 while the bot waits for a service: it goes on to request 3.
        host.write(request("orders", 2, view={}))
        assert host.read()["service"]["name"] == "builtin"
        host.write(request("end_session", 3))
        late = host.read()
        assert late["id"] == 2 and late["response"]["error"]["type"] == "RequestAbandoned", late
        last = host.read()
        assert last["id"] == 3 and "error" not in last["response"], last
        assert last["response"]["memory"]["ended"] is True and last["response"]["memory"]["orders"] == 1
        host.write({"bye": {"reason": "done"}})

    host = FakeHost(script)
    assert external.run(Asker, port=host.port, token="tok", slot=0, report=lambda text: None) == 3
    host.join()


def test_values_the_game_cannot_take_are_named_and_answered_as_an_error():
    assert external.value_problem({"a": [1, "x", None, True, {"b": -5}]}) is None
    assert external.value_problem({"a": [1, 2.5]}) == "the response['a'][1] is a float (the game takes whole numbers only)"
    assert "beyond 64 bits" in external.value_problem([1 << 64])
    assert "not text" in external.value_problem({1: 2})
    assert "set" in external.value_problem({"s": {1}}, "memory")

    class Floaty(ai.Player):
        def end_session(self):
            self.memory["ratio"] = 0.5

    class Recorder(external.Connection):
        def __init__(self):
            self.requests = [testing.request("end_session", start=True, player={"slot": 0})]
            self.sent = []

        def receive(self):
            return self.requests.pop(0) if self.requests else None

        def send(self, response):
            self.sent.append(response)

        def service(self, name, args):
            raise AssertionError("no service expected")

    conn = Recorder()
    assert external.serve(conn, Floaty) == 1
    error = conn.sent[0]["error"]
    assert error["type"] == "ValueError" and "['memory']['ratio'] is a float" in error["message"], error
    assert conn.sent[0]["memory"] is None


def test_where_to_connect_comes_from_the_environment():
    def script(host):
        hello = host.welcome(slot=2)
        assert hello["token"] == "from-env" and hello["slot"] == 2, hello
        host.write({"bye": {"reason": "done"}})

    host = FakeHost(script)
    saved = {k: os.environ.get(k) for k in ("OPENSE4_BOT_HOST", "OPENSE4_BOT_PORT", "OPENSE4_BOT_TOKEN", "OPENSE4_BOT_SLOT")}
    os.environ.update({"OPENSE4_BOT_HOST": "127.0.0.1", "OPENSE4_BOT_PORT": str(host.port), "OPENSE4_BOT_TOKEN": "from-env",
                       "OPENSE4_BOT_SLOT": "2"})
    try:
        assert external.run(Asker, report=lambda text: None) == 0
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    host.join()


def test_the_bot_command_imports_the_player_and_plays():
    folder = tempfile.mkdtemp(prefix="opense4_bot_")
    with open(os.path.join(folder, "mybot.py"), "w", encoding="utf-8") as f:
        f.write("from opense4 import ai\n\nclass Mine(ai.Player):\n    def end_session(self):\n        self.memory['n'] = 1\n\n"
                "class NotAPlayer:\n    pass\n")

    def script(host):
        host.welcome()
        host.write(request("end_session", 1, start=True))
        answer = host.read()
        assert answer["response"]["memory"] == {"n": 1}, answer
        host.write({"bye": {"reason": "done"}})

    host = FakeHost(script)
    code = bot.main(["mybot:Mine", "--path", folder, "--port", str(host.port), "--token", "tok"])
    host.join()
    assert code == 0
    for wrong in ("mybot", "mybot:Missing", "mybot:NotAPlayer"):
        try:
            bot.load_player(wrong, [folder])
            assert False, wrong
        except ValueError:
            pass
    assert bot.main(["mybot:NotAPlayer", "--path", folder, "--port", "1"]) == 2


# ---- The training environment (opense4.env), with the engine ----

def _engine():
    sdk = os.environ.get("OPENSE4_SDK")
    data = os.environ.get("OPENSE4_ENV_DATA")
    if not sdk or not data:
        support.skip("OPENSE4_SDK and OPENSE4_ENV_DATA name the engine and a game folder")
    return sdk, data


def _episode(game, seed, policy):
    """Every step of one game: (the view as JSON, reward, done, info)."""
    view = game.reset(seed=seed)
    steps = [(json.dumps(view.raw, sort_keys=True), 0, False, {})]
    done = False
    while not done:
        view, reward, done, info = game.step(policy(game, view))
        steps.append((json.dumps(view.raw, sort_keys=True), reward, done, info))
    return steps


def test_env_reset_and_step_are_the_same_for_a_seed():
    from opense4 import env
    sdk, data = _engine()
    log = os.path.join(tempfile.mkdtemp(prefix="opense4_env_"), "engine.log")
    with env.Game(seed=5, turns=4, data=data, sdk=sdk, log=log, builtin=("economy",)) as game:
        classic = lambda g, v: g.builtin_commands("orders")   # noqa: E731: the classic orders, asked during the step
        first = _episode(game, 5, classic)
        again = _episode(game, 5, classic)
        idle = _episode(game, 5, lambda g, v: [])
    assert len(first) == 5, [s[1:] for s in first]
    assert first == again
    assert first[0][0] == idle[0][0]          # the same game for the seed
    assert first[-1][2] is True and all(not s[2] for s in first[:-1])
    last_info = first[-1][3]
    assert {"turn", "game_over", "winner", "scores"} <= set(last_info), last_info
    assert any(s["empire"] == 0 for s in last_info["scores"])
    assert all(isinstance(s[3].get("refused", []), list) for s in first[1:])


def test_env_answers_queries_and_the_rules_during_a_step_and_ends_when_closed():
    from opense4 import env
    sdk, data = _engine()
    game = env.Game(seed=2, turns=3, data=data, sdk=sdk)
    view = game.reset()
    assert view.me.id == 0 and view.game.turn == 0
    assert len(view.rules.components) >= 1
    home = view.my.colonies[0]
    report = game.query("abilities", planet=home)
    assert report.raw is not None
    try:
        game.query("abilities", planet=10 ** 6)
        assert False, "a query about a planet that is not there"
    except ValueError:
        pass
    view, reward, done, info = game.step([])
    assert not done and view.game.turn == 1 and isinstance(reward, int)
    game.close()
    try:
        game.step([])
        assert False, "a step after close"
    except env.EnvError:
        pass
