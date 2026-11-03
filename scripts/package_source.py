"""Create a source archive from the current working tree."""

from __future__ import annotations

import argparse
from collections.abc import Iterator
import os
from pathlib import Path
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {
    "bin",
    "debug",
    ".git",
    ".idea",
    ".vs",
    "__pycache__",
    "build",
    "coverage",
    "dist",
    "logs",
    "node_modules",
    "obj",
    "outputs",
    "release",
    "screenshots",
    "session-data",
    "videos",
    "work",
    "x64",
    "x86",
}
SKIP_SUFFIXES = {
    ".db",
    ".db-shm",
    ".db-wal",
    ".dll",
    ".exe",
    ".ilk",
    ".lib",
    ".obj",
    ".pdb",
    ".zip",
}


def source_files(output: Path) -> Iterator[Path]:
    for current, directories, filenames in os.walk(ROOT):
        current_path = Path(current)
        directories[:] = sorted(
            name for name in directories if name.lower() not in SKIP_DIRS
        )

        for filename in sorted(filenames):
            path = current_path / filename
            if path.resolve() == output:
                continue
            if filename == ".env" or (
                filename.startswith(".env.") and filename != ".env.example"
            ):
                continue
            if filename.lower().endswith(".log"):
                continue
            if path.suffix.lower() in SKIP_SUFFIXES:
                continue
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=ROOT / "dist" / "GameTransferHub-source.zip",
        help="destination path (default: dist/GameTransferHub-source.zip)",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output.parent, prefix=output.name + ".", suffix=".tmp", delete=False
        ) as temporary:
            temporary_path = Path(temporary.name)

        files = sorted(source_files(output))
        with zipfile.ZipFile(temporary_path, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in files:
                archive.write(path, path.relative_to(ROOT).as_posix())

        os.replace(temporary_path, output)
    except Exception:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
        raise

    print(f"Created {output} ({len(files)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
