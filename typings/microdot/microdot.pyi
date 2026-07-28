# Host IDE stubs — runtime: external/microdot/src/microdot/microdot.py
from typing import Any, Awaitable, Callable, Mapping, Sequence

class NoCaseDict(dict[str, Any]):
    def get(self, key: str, default: Any = None) -> Any: ...

class Request:
    max_content_length: int
    max_body_length: int
    max_readline: int
    app: Any
    client_addr: tuple[str, int]
    method: str
    url: str
    path: str
    headers: Mapping[str, str] | NoCaseDict
    def __init__(
        self,
        app: Any,
        client_addr: tuple[str, int],
        method: str,
        url: str,
        http_version: str,
        headers: Mapping[str, str] | NoCaseDict,
        body: bytes | None = None,
        stream: Any = None,
        sock: Any = None,
        url_prefix: str = "",
        subapp: Any = None,
        scheme: str | None = None,
        route: Any = None,
    ) -> None: ...

class Response:
    default_content_type: str
    already_handled: Response | None
    status_code: int
    headers: NoCaseDict
    body: Any
    def __init__(
        self,
        body: Any = b"",
        status_code: int = 200,
        headers: Mapping[str, str] | None = None,
        reason: str | None = None,
    ) -> None: ...

class Microdot:
    def __init__(self) -> None: ...
    def route(
        self, url_pattern: str, methods: Sequence[str] | None = None
    ) -> Callable[[Callable[..., Any]], Callable[..., Any]]: ...
    def get(
        self, url_pattern: str
    ) -> Callable[[Callable[..., Any]], Callable[..., Any]]: ...
    def post(
        self, url_pattern: str
    ) -> Callable[[Callable[..., Any]], Callable[..., Any]]: ...
    async def dispatch_request(self, req: Request) -> Response | None: ...
