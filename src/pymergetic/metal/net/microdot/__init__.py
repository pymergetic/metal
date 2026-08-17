"""pymergetic.metal.net.microdot — optional host Microdot face."""

from collections.abc import Callable
from typing import Any


class Microdot:
    def __init__(self) -> None:
        self.routes: list[tuple[list[str], str, Callable[..., Any]]] = []
        self.asgi: Any = None

    def get(self, path: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
        return self.route(path, methods=["GET"])

    def route(
        self, path: str, methods: list[str] | None = None
    ) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
        verbs = list(methods) if methods else ["GET"]

        def deco(fn: Callable[..., Any]) -> Callable[..., Any]:
            self.routes.append((verbs, path, fn))
            return fn

        return deco

    def attach_asgi(self) -> Any:
        """Same listen as inspect C route_fn. Does not open a second socket."""
        import pymergetic.metal.net.http.asgi as asgi

        self.asgi = asgi
        return asgi
