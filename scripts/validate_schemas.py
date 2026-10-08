#!/usr/bin/env python3
"""Validate the checked-in SimRV release manifest against its JSON schema."""

import json
import pathlib

from jsonschema import Draft202012Validator

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOCUMENTS = (
    (ROOT / "release/schemas/release-manifest.schema.json", ROOT / "release/release-manifest.json"),
)


def validate(schema_path: pathlib.Path, document_path: pathlib.Path) -> None:
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    document = json.loads(document_path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(document)
    print(f"validated {document_path.relative_to(ROOT)}")


def main() -> None:
    for schema_path, document_path in DOCUMENTS:
        validate(schema_path, document_path)


if __name__ == "__main__":
    main()
