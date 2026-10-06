"""External bots: a computer player that runs as its own program, on CPython with any
library, and plays through a connection to the game (docs/MODDING_SDK.md, section 6.3).

The same `opense4.ai.Player` runs here and in the game. A bot hands its player to
`run` with a connection:

    from opense4 import external
    from mybot import Admiral

    external.run(connection, Admiral)

The connection carries the protocol's requests and responses
(docs/sdk/ai-protocol.md) and, during a request, the player's service calls. The
transports themselves (a local connection, the network) arrive in a later step of
the SDK; `Connection` is what they implement, and opense4.testing has one for tests.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Callable, Dict, Optional

from ._engine import Dispatcher
from .ai import Player
from .services import ConnectionServices


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
        A refusal raises opense4.services.ServiceError (or the error type the game names)."""


def run(connection: Connection, player: Callable[[], Player]) -> int:
    """Answers the game's requests with players made by `player` (a Player subclass or
    any function that makes one) until the connection ends; returns the requests answered."""
    dispatcher = Dispatcher(ConnectionServices(connection), factory=player)
    answered = 0
    while True:
        request = connection.receive()
        if request is None:
            return answered
        connection.send(dispatcher.handle(request))
        answered += 1
