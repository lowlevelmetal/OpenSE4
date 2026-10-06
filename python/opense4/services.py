"""What a player may ask the engine while a request is being handled
(docs/sdk/ai-protocol.md, section 6): queries, the rules view, the classic AI's
commands and answers, and applying a command at once.

A backend carries the services:

- `NativeServices`: in the game, the native module `_opense4`, one function per
  service, each taking the service's argument map and returning its result;
- `ConnectionServices`: an external bot's, as messages on its connection
  (opense4.external);
- `opense4.testing.FakeServices`: prepared answers, for tests.

The dispatcher (opense4._engine) makes the backend current for the length of each
request; `current()` is that request's context. A refused service raises an
exception: the type the engine gave (ValueError for a bad argument, with the path
to it), or ServiceError.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from . import _records
from ._values import plain


class ServiceError(Exception):
    """The engine refused a service. `type` is the kind of error it gave."""

    def __init__(self, message: str, type: str = "RuntimeError") -> None:
        super().__init__(message)
        self.type = type


class Services:
    """A backend: `call(service, args)` runs one service and returns its result."""

    def call(self, service: str, args: Dict[str, Any]) -> Any:
        raise NotImplementedError("Services.call")

    def query(self, name: str, args: Dict[str, Any]) -> Any:
        return self.call("query", {"name": name, "args": args})

    def rules(self) -> Dict[str, Any]:
        return self.call("rules", {})

    def builtin(self, call: str, ministers: Optional[List[str]], skip: List[str]) -> List[Dict[str, Any]]:
        return self.call("builtin", {"call": call, "ministers": ministers, "skip": skip})

    def builtin_answer(self, call: str, args: Dict[str, Any]) -> Any:
        return self.call("builtin_answer", {"call": call, "args": args})

    def apply(self, command: Dict[str, Any]) -> Dict[str, Any]:
        return self.call("apply", {"command": command})


SERVICES = ("query", "rules", "builtin", "builtin_answer", "apply")


class NativeServices(Services):
    """In the game: the engine's native module `_opense4`."""

    def __init__(self) -> None:
        self._native: Any = None

    def call(self, service: str, args: Dict[str, Any]) -> Any:
        if service not in SERVICES:
            raise ServiceError("no service " + repr(service), "ValueError")
        if self._native is None:
            import _opense4  # the engine's, in the game only
            self._native = _opense4
        return getattr(self._native, service)(args)


class ConnectionServices(Services):
    """An external bot's services: messages on its connection (opense4.external.Connection)."""

    def __init__(self, connection: Any) -> None:
        self._connection = connection

    def call(self, service: str, args: Dict[str, Any]) -> Any:
        if service not in SERVICES:
            raise ServiceError("no service " + repr(service), "ValueError")
        return self._connection.service(service, args)


class Context:
    """The request being handled: its backend, the request and the player's session."""

    def __init__(self, services: Services, request: Dict[str, Any], session: Any) -> None:
        self.services = services
        self.request = request
        self.session = session

    @property
    def call(self) -> str:
        return self.request["call"]

    @property
    def args(self) -> Dict[str, Any]:
        a = self.request.get("args")
        return {} if a is None else a

    @property
    def empire(self) -> int:
        return self.request["empire"]


_current: Optional[Context] = None


def current() -> Context:
    """The request being handled. Services exist only while the engine waits for an answer."""
    if _current is None:
        raise RuntimeError("no request is being handled: the engine's services answer only during a call to the player")
    return _current


def rules() -> Any:
    """The rules view of the game being played (opense4.rules.Rules), asked once per session."""
    return current().session.rules(current().services)


def query(name: str, **args: Any) -> Any:
    """Runs a query of docs/sdk/view.md ("Queries") now: `query("path", vehicle=v, destination=s)`.

    Arguments may be the view's objects (a Vehicle for a vehicle id, a System for a
    system id). The result is a record of the query's result type, its ids resolved in
    the session's view."""
    spec = _records.QUERIES.get(name)
    if spec is None:
        raise ValueError("no query " + repr(name) + "; the queries are " + ", ".join(_records.QUERIES))
    names, results = spec
    for key in args:
        if key not in names:
            raise TypeError(name + ": no argument " + repr(key) + "; it takes " + ", ".join(names))
    ctx = current()
    result = ctx.services.query(name, {k: plain(v) for k, v in args.items()})
    kind = results[0] if results else None
    if len(results) > 1:
        for r in results:
            for key in args:
                if r.startswith(key + "_"):
                    kind = r
    if kind is None or not isinstance(result, dict):
        return result
    from .view import record
    return record(kind, result, ctx.session.view)
