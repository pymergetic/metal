from typing import Any, TextIO


class Compiler:
    def __init__(
        self,
        file_in: TextIO,
        file_out: TextIO,
        indent: int = 0,
        seq: int = 0,
        loader: Any = None,
    ) -> None: ...
    def compile(self) -> int: ...
