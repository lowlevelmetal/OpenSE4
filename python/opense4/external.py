"""External bots: a computer player that runs as its own program, on CPython 3.10 or
newer with any library, and plays through a connection to the game
(docs/sdk/bots-and-arena.md; the messages are docs/sdk/ai-protocol.md, section 10).

The same `opense4.ai.Player` runs here and in the game. A bot hands its player to `run`,
which connects, answers the game's requests until the game ends, and returns:

    from opense4 import external
    from admiral import Admiral

    external.run(Admiral, port=6722, token="3f2a...")

or, from the command line, `python -m opense4.bot admiral:Admiral --port 6722 --token
3f2a...`. Where to connect comes from the arguments, else from the environment the game's
tools give the bots they start: OPENSE4_BOT_HOST, OPENSE4_BOT_PORT, OPENSE4_BOT_TOKEN and
OPENSE4_BOT_SLOT.

`Connection` is what a connection provides; `SocketConnection` is the game's own (newline
separated JSON over TCP), and `serve(connection, Player)` answers requests on any
connection, such as a test's. The module imports in the game's runtime too (for
`serve` and `Connection`); only connecting needs CPython.
"""

from __future__ import annotations

import sys
from abc import ABC, abstractmethod
from typing import Any, Callable, Dict, List, Optional

from ._engine import Dispatcher
from .ai import Player
from .services import ConnectionServices, ServiceError

API = 1
DEFAULT_PORT = 6722
"""Where the game listens for bots when it names no port of its own."""


class Connection(ABC):
    """A bot's connection to the game."""

    @abstractmethod
    def receive(self) -> Optional[Dict[str, Any]]:
        """The next request (docs/sdk/ai-protocol.md, section 3), or None when the game is over."""

    @abstractmethod
    def send(self, response: Dict[str, Any]) -> None:
        """Answers the request received last (section 4)."""

    @abstractmethod
    def service(self, name: str, args: Dict[str, Any]) -> Any:
        """Asks the game a service during a request (section 6) and returns its result.
        A refusal raises the error type the game names (ValueError, RuntimeError...) or
        opense4.services.ServiceError."""


try:
    _ConnectionError = ConnectionError
except NameError:   # the game's runtime has OSError only
    _ConnectionError = OSError


class Refused(_ConnectionError):
    """The game refused the bot: a wrong token, a slot that is not the game's or is busy."""


class RequestAbandoned(Exception):
    """The game stopped waiting for the request being handled: it ran out of time, or the
    game ended. The player's call ends with this; the game has already decided."""


# The errors a refused service raises, by the name the game gives them.
_ERRORS = {
    "ValueError": ValueError,
    "TypeError": TypeError,
    "RuntimeError": RuntimeError,
    "KeyError": KeyError,
    "LookupError": LookupError,
    "IndexError": IndexError,
}


def _service_error(error: Any) -> Exception:
    if not isinstance(error, dict):
        return ServiceError(str(error))
    kind = str(error.get("type") or "RuntimeError")
    message = str(error.get("message") or "")
    cls = _ERRORS.get(kind)
    return cls(message) if cls is not None else ServiceError(message, kind)


class SocketConnection(Connection):
    """The game's connection: one line of JSON per message over TCP (docs/sdk/ai-protocol.md,
    section 10). Connecting says hello with the token and is welcomed or refused."""

    def __init__(self, host: str = "127.0.0.1", port: int = DEFAULT_PORT, token: str = "", slot: Optional[int] = None,
                 name: str = "", timeout: float = 30.0) -> None:
        import socket   # CPython only: the game's runtime has no sockets
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._file = self.sock.makefile("rb")
        self._pending: List[Dict[str, Any]] = []   # what arrived while a service was awaited
        self._id: Any = None                       # the request being handled
        self.goodbye: Optional[Dict[str, Any]] = None   # the game's last word ({"reason", ...})
        self._send({"hello": {"api": API, "token": token, "slot": slot, "name": name}})
        first = self._read()
        self.sock.settimeout(None)
        if first is None:
            self.close()
            raise ConnectionError("the game closed the connection without a welcome")
        if "refused" in first:
            self.close()
            refused = first["refused"]
            raise Refused(refused.get("message", "refused") if isinstance(refused, dict) else str(refused))
        if "welcome" not in first:
            self.close()
            raise ConnectionError("the game's first message is not a welcome: " + repr(first)[:200])
        self.welcome: Dict[str, Any] = first["welcome"]
        self.slot: int = self.welcome.get("slot", 0)
        self.game: str = self.welcome.get("game", "")

    # ---- the wire ----

    def _send(self, message: Dict[str, Any]) -> None:
        import json
        data = json.dumps(message, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
        self.sock.sendall(data + b"\n")

    def _read(self) -> Optional[Dict[str, Any]]:
        import json
        while True:
            try:
                line = self._file.readline()
            except (OSError, ValueError):
                return None
            if not line:
                return None
            line = line.strip()
            if not line:
                continue
            message = json.loads(line.decode("utf-8"))
            if isinstance(message, dict):
                return message

    def close(self) -> None:
        try:
            self._file.close()
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass

    # ---- Connection ----

    def receive(self) -> Optional[Dict[str, Any]]:
        while True:
            message = self._pending.pop(0) if self._pending else self._read()
            if message is None:
                self.close()
                return None
            if "request" in message:
                self._id = message.get("id")
                return message["request"]
            if "bye" in message:
                bye = message["bye"]
                self.goodbye = bye if isinstance(bye, dict) else {"reason": str(bye)}
                self.close()
                return None
            # Anything else belongs to a request the game gave up on: dropped.

    def send(self, response: Dict[str, Any]) -> None:
        message: Dict[str, Any] = {"response": response}
        if self._id is not None:
            message["id"] = self._id
        self._send(message)

    def service(self, name: str, args: Dict[str, Any]) -> Any:
        message: Dict[str, Any] = {"service": {"name": name, "args": args}}
        if self._id is not None:
            message["id"] = self._id
        self._send(message)
        while True:
            reply = self._read()
            if reply is None:
                raise ConnectionError("the game closed the connection")
            if "request" in reply or "bye" in reply:
                # The game moved on: this request ran out of time.
                self._pending.append(reply)
                raise RequestAbandoned("the game stopped waiting for this request")
            if self._id is not None and reply.get("id", self._id) != self._id:
                continue
            if "result" in reply:
                return reply["result"]
            if "service_error" in reply:
                raise _service_error(reply["service_error"])

    def say_goodbye(self, reason: str = "the bot is done") -> None:
        """Tells the game the bot leaves, and closes the connection."""
        try:
            self._send({"bye": {"reason": reason}})
        except OSError:
            pass
        self.close()


def value_problem(value: Any, where: str = "the response") -> Optional[str]:
    """What keeps `value` from reaching the game as the protocol's JSON (whole numbers within
    64 bits, text keys, no floats), or None. The game would refuse it the same way."""
    if value is None or isinstance(value, (bool, str)):
        return None
    if isinstance(value, int):
        if -(1 << 63) <= value < (1 << 63):
            return None
        return where + " is a whole number beyond 64 bits"
    if isinstance(value, float):
        return where + " is a float (the game takes whole numbers only)"
    if isinstance(value, dict):
        for k, v in value.items():
            if not isinstance(k, str):
                return where + " has a key that is not text: " + repr(k)
            p = value_problem(v, where + "[" + repr(k) + "]")
            if p:
                return p
        return None
    if isinstance(value, (list, tuple)):
        for i, v in enumerate(value):
            p = value_problem(v, where + "[" + str(i) + "]")
            if p:
                return p
        return None
    return where + " is a " + type(value).__name__ + ", which the game cannot take"


def serve(connection: Connection, player: Callable[[], Player]) -> int:
    """Answers the game's requests on `connection` with players made by `player` (a Player
    subclass or any function that makes one) until the connection ends; returns the
    requests answered."""
    dispatcher = Dispatcher(ConnectionServices(connection), factory=player)
    answered = 0
    while True:
        request = connection.receive()
        if request is None:
            return answered
        response = dispatcher.handle(request)
        problem = value_problem(response)
        if problem:
            # As in the game, where such a value fails the request.
            response = {"commands": [], "answer": None, "memory": None, "notes": [], "log": [],
                        "error": {"type": "ValueError", "message": problem, "traceback": ""}}
        try:
            connection.send(response)
        except OSError:
            return answered
        answered += 1


def _setting(value: Any, variable: str, default: Any = None) -> Any:
    import os
    if value is not None:
        return value
    text = os.environ.get(variable)
    return text if text not in (None, "") else default


def connect(host: Optional[str] = None, port: Optional[int] = None, token: Optional[str] = None, slot: Optional[int] = None,
            name: str = "", wait: float = 30.0) -> SocketConnection:
    """Connects to the game, trying again for `wait` seconds while nothing listens yet."""
    import time
    host = _setting(host, "OPENSE4_BOT_HOST", "127.0.0.1")
    port = int(_setting(port, "OPENSE4_BOT_PORT", DEFAULT_PORT))
    token = _setting(token, "OPENSE4_BOT_TOKEN", "")
    slot_text = _setting(slot, "OPENSE4_BOT_SLOT")
    slot = None if slot_text is None else int(slot_text)
    until = time.monotonic() + wait
    while True:
        try:
            return SocketConnection(host, port, token, slot, name)
        except Refused:
            raise
        except OSError:
            if time.monotonic() >= until:
                raise
            time.sleep(0.2)


def run(player: Any, host: Any = None, port: Optional[int] = None, token: Optional[str] = None, slot: Optional[int] = None,
        name: Optional[str] = None, reconnect: bool = False, wait: float = 30.0,
        report: Optional[Callable[[str], None]] = None) -> int:
    """Plays `player` (a Player subclass, or a function that makes one) in the game at
    host:port, presenting `token`, for the empires of `slot` (None: the first free one).
    Returns the requests answered once the game says goodbye or the connection ends.
    `reconnect`: connect again after that, as for a host that runs once per turn (play by
    e-mail); stop with Ctrl+C.

    `run(connection, Player)` answers on a connection already made, as `serve` does."""
    if isinstance(player, Connection):
        return serve(player, host)
    if name is None:
        name = getattr(player, "__name__", "bot")
    say = report if report is not None else (lambda text: print(text, file=sys.stderr, flush=True))
    answered = 0
    while True:
        try:
            conn = connect(host, port, token, slot, name, wait=wait if not reconnect else 365 * 24 * 3600.0)
        except Refused as e:
            say("opense4 bot: the game refused the bot: " + str(e))
            raise
        say("opense4 bot: {} plays slot {} of {}".format(name, conn.slot, conn.game or "the game"))
        now = serve(conn, player)
        answered += now
        bye = conn.goodbye
        say("opense4 bot: {} ({} requests answered)".format(str(bye.get("reason", "goodbye")) if bye else "the connection ended", now))
        if not reconnect:
            return answered
