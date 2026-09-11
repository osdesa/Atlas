"""Content-addressed pack management. Inspection executes only the Atlas inspector."""

from __future__ import annotations

import json
import os
import platform
import re
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path

from PySide6.QtCore import QSettings, QStandardPaths

from ..models.documents import JsonObject
from .launch import discover_executable

TRUST_WARNING = (
    "Native code runs with your user privileges. Process isolation does not restrict files or network. "
    "CPU tasks may hang or crash; GPU tasks may hang or lose the Vulkan device. "
    "Validation does not make hostile code safe. Trust only code you know for this exact digest."
)


def inspect_pack(path: Path) -> JsonObject:
    """Read bounded serialized library descriptors in a child process, without native loading."""
    executable = discover_executable("ATLAS_STUDIO_RUNNER", "apps/atlas_studio_runner/atlas_studio_runner")
    if executable is None:
        raise RuntimeError("Build atlas_studio_runner or set ATLAS_STUDIO_RUNNER before managing packs")
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        result = subprocess.run(
            [str(executable), "--inspect-task-pack", str(path)],
            stdout=output,
            stderr=errors,
            timeout=60,
            check=False,
        )
        output.seek(0)
        raw = output.read(16 * 1024 * 1024 + 1)
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError("Pack inspection output exceeds 16 MiB")
    record = json.loads(raw)
    if result.returncode:
        raise ValueError(record.get("message", "Pack inspection failed"))
    if record.get("inspection_schema_version") != 1 or not re.fullmatch(
        "[0-9a-f]{64}", record.get("digest", "")
    ):
        raise ValueError("Invalid pack inspection response")
    return record


class PackStore:
    """Own installed descriptors and digest trust; active references prevent removal."""

    def __init__(self, root: Path | None = None, settings: QSettings | None = None) -> None:
        self.root = (
            root or Path(QStandardPaths.writableLocation(QStandardPaths.AppDataLocation)) / "task-packs"
        )
        self.settings = settings or QSettings("Atlas", "Atlas Studio")
        self.installed: dict[str, JsonObject] = {}
        self.errors: list[str] = []
        self.active: set[str] = set()

    def refresh(self) -> None:
        """Reinspect installed content, retaining actionable errors for damaged entries."""
        self.installed.clear()
        self.errors.clear()
        if not self.root.exists():
            return
        for path in sorted(self.root.iterdir()):
            if not re.fullmatch("[0-9a-f]{64}", path.name):
                continue
            try:
                record = inspect_pack(path)
                if record["digest"] != path.name:
                    raise ValueError("Installed content changed; import again and trust its new digest")
                self.installed[path.name] = record
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
                self.errors.append(f"{path.name}: {error}")

    def import_directory(self, source: Path) -> JsonObject:
        """Inspect before copying referenced regular files, then verify before atomic installation."""
        record = inspect_pack(source)
        self.root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".import-", dir=self.root) as temporary:
            staging = Path(temporary)
            total = 0
            for name in set(record["files"]):
                relative = Path(name)
                if relative.is_absolute() or ".." in relative.parts or "\\" in name:
                    raise ValueError("Unsafe inspection file path")
                path = source
                for component in relative.parts:
                    path /= component
                    if path.is_symlink():
                        raise ValueError("Pack source became a symlink")
                target = staging / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
                with os.fdopen(fd, "rb") as reader, target.open("wb") as writer:
                    if not stat.S_ISREG(os.fstat(reader.fileno()).st_mode):
                        raise ValueError("Pack source is not a regular file")
                    while chunk := reader.read(65536):
                        total += len(chunk)
                        if total > 256 * 1024 * 1024:
                            raise ValueError("Pack exceeds 256 MiB")
                        writer.write(chunk)
            copied = inspect_pack(staging)
            if copied["digest"] != record["digest"]:
                raise ValueError("Pack changed during import")
            destination = self.root / record["digest"]
            if destination.exists():
                if inspect_pack(destination)["digest"] != record["digest"]:
                    raise ValueError("Installed pack is damaged; remove it before importing again")
            else:
                staging.rename(destination)
        self.installed[record["digest"]] = record
        return record

    def trusted(self, digest: str) -> bool:
        self.settings.sync()
        return self.settings.value(f"task-pack-trust/{digest}", False, type=bool)

    def set_trusted(self, digest: str, trusted: bool) -> None:
        """Persist an explicit UI decision for exactly one installed digest."""
        if digest not in self.installed:
            raise ValueError("Pack is not installed")
        self.settings.setValue(f"task-pack-trust/{digest}", trusted)
        self.settings.sync()

    def remove(self, digest: str) -> None:
        if digest in self.active:
            raise ValueError("Pack is referenced by an active launch")
        if not re.fullmatch("[0-9a-f]{64}", digest):
            raise ValueError("Invalid digest")
        shutil.rmtree(self.root / digest)
        self.installed.pop(digest, None)
        self.settings.remove(f"task-pack-trust/{digest}")
        self.settings.sync()

    @staticmethod
    def available(record: JsonObject) -> bool:
        architecture = {"amd64": "x86_64", "arm64": "aarch64"}.get(
            platform.machine().lower(), platform.machine().lower()
        )
        return {"platform": platform.system().lower(), "architecture": architecture} in record["platforms"]

    def problems(self, document: JsonObject) -> list[str]:
        problems = []
        for requested in document["packs"]:
            identity = f"{requested['pack_id']} {requested['version']} digest {requested['digest']}"
            pack = self.installed.get(requested["digest"])
            if not pack or any(pack[key] != requested[key] for key in ("pack_id", "version")):
                problems.append(f"Missing exact pack: {identity}")
            elif not self.available(pack):
                problems.append(f"Unavailable on {platform.system()}/{platform.machine()}: {identity}")
            elif not self.trusted(requested["digest"]):
                problems.append(f"Untrusted pack: {identity}")
        return problems

    def launch_directories(self, document: JsonObject) -> tuple[Path, ...]:
        """Resolve selected digests and recheck trust; the runner verifies content before loading."""
        problems = self.problems(document)
        if problems:
            raise ValueError("\n".join(problems))
        self.active = {pack["digest"] for pack in document["packs"]}
        return tuple(self.root / pack["digest"] for pack in document["packs"])
