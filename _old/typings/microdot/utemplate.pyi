# Host IDE stubs — runtime: external/microdot/src/microdot/utemplate.py
from typing import Any, Callable

class Template:
    name: str
    @classmethod
    def initialize(
        cls,
        template_dir: str = "templates",
        loader_class: Callable[..., Any] | type[Any] = ...,
    ) -> None: ...
    def __init__(self, template: str) -> None: ...
    def render(self, *args: Any, **kwargs: Any) -> str: ...
    async def render_async(self, *args: Any, **kwargs: Any) -> str: ...
