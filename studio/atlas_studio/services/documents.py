"""Filesystem services used by Atlas Studio controllers."""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

from ..models.documents import JsonObject
from ..models.protocol import parse_json


class DocumentRepository:
    """Read documents and atomically write user-authored JSON files."""

    @staticmethod
    def read_json(path: Path) -> JsonObject:
        return parse_json(path.read_text(encoding="utf-8"))

    @staticmethod
    def save_json(path: Path, document: JsonObject) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(document, stream, indent=2, allow_nan=False)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
