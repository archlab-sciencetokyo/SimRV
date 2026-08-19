#!/usr/bin/env python3
"""Create a deterministic SimRV research-companion archive and checksum."""

import argparse
import gzip
import hashlib
import json
import pathlib
import tarfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / "release/release-manifest.json").read_text())
STATIC = ["CITATION.cff", "LICENSE", "README.md", "SECURITY.md", "docs/RELEASE.md",
          "docs/RISCV_COMPLIANCE.md", "repro/README.md", "repro/experiment-manifest.json"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    output = args.output or ROOT / f"SimRV-repro-v{MANIFEST['version']}.tar.gz"
    members = [(ROOT / path, pathlib.Path(path)) for path in STATIC]
    members += [(path, path.relative_to(ROOT)) for path in sorted((ROOT / "release/schemas").glob("*.json"))]
    members += [(path, path.relative_to(ROOT)) for path in sorted((ROOT / "scripts").glob("*.py"))]
    if args.results.is_dir():
        members += [(path, pathlib.Path("results") / path.relative_to(args.results))
                    for path in sorted(args.results.rglob("*")) if path.is_file()]
    missing = [str(path) for path, _ in members if not path.is_file()]
    if missing:
        raise SystemExit(f"missing reproduction inputs: {missing}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for path, arcname in sorted(set(members), key=lambda item: str(item[1])):
                    info = archive.gettarinfo(str(path), arcname=str(arcname))
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = 0
                    with path.open("rb") as source:
                        archive.addfile(info, source)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(output.suffix + ".sha256").write_text(f"{digest}  {output.name}\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
