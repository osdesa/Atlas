"""Bounded byte-to-line framing for supervised process channels."""

from __future__ import annotations


class BoundedLineBuffer:
    """Accumulate process bytes and return complete bounded lines."""

    def __init__(self, maximum_line_bytes: int) -> None:
        self._maximum_line_bytes = maximum_line_bytes
        self._buffer = bytearray()

    def clear(self) -> None:
        self._buffer.clear()

    def feed(self, data: bytes) -> list[bytes]:
        self._buffer.extend(data)
        lines: list[bytes] = []
        while b"\n" in self._buffer:
            line, _, remainder = self._buffer.partition(b"\n")
            self._buffer = bytearray(remainder)
            line = line.removesuffix(b"\r")
            if len(line) > self._maximum_line_bytes:
                raise ValueError("process output line exceeds 2 MiB")
            lines.append(bytes(line))
        if len(self._buffer) > self._maximum_line_bytes:
            raise ValueError("process output contains an unterminated line exceeding 2 MiB")
        return lines

    def finish(self) -> bytes:
        line = bytes(self._buffer).removesuffix(b"\r")
        self._buffer.clear()
        return line
