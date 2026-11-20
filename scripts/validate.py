#!/usr/bin/env python3
"""Validate the store catalog against the package schema.

Checks:
  - catalog/index.json top-level shape (schema_version, apps array)
  - every app in index.json against schema/package.schema.json
  - every example in catalog/example-entry.json against the schema
  - unique app ids

Exit code 0 on success, 1 on any problem. Run from the repo root:
    pip install jsonschema && python3 scripts/validate.py
"""
import json
import pathlib
import sys

try:
    from jsonschema import Draft202012Validator
except ImportError:
    print("error: missing dependency. Run: pip install jsonschema", file=sys.stderr)
    sys.exit(2)

ROOT = pathlib.Path(__file__).resolve().parent.parent


def load(rel):
    p = ROOT / rel
    with p.open(encoding="utf-8") as f:
        return json.load(f)


def main():
    errors = []

    schema = load("schema/package.schema.json")
    validator = Draft202012Validator(schema)

    index = load("catalog/index.json")
    if not isinstance(index.get("schema_version"), int):
        errors.append("index.json: missing/invalid integer 'schema_version'")
    apps = index.get("apps")
    if not isinstance(apps, list):
        errors.append("index.json: 'apps' must be an array")
        apps = []

    seen = {}
    for i, app in enumerate(apps):
        where = f"index.json apps[{i}]"
        if isinstance(app, dict) and "id" in app:
            where = f"index.json app '{app['id']}'"
            if app["id"] in seen:
                errors.append(f"duplicate id '{app['id']}'")
            seen[app["id"]] = True
        for e in sorted(validator.iter_errors(app), key=lambda e: e.path):
            loc = "/".join(str(p) for p in e.path)
            errors.append(f"{where}: {loc + ': ' if loc else ''}{e.message}")

        # URL scheme checks ('format: uri' isn't asserted by default). Reject
        # javascript:/data:/etc., and require https for integrity-critical URLs.
        if isinstance(app, dict):
            for key in ("download", "source"):
                v = app.get(key)
                if isinstance(v, str) and v and not v.startswith("https://"):
                    errors.append(f"{where}: '{key}' must be an https:// URL")
            for key in ("homepage", "icon"):
                v = app.get(key)
                if isinstance(v, str) and v and not (v.startswith("https://") or v.startswith("http://")):
                    errors.append(f"{where}: '{key}' must be an http(s):// URL")
            for s in app.get("screenshots") or []:
                if isinstance(s, str) and not (s.startswith("https://") or s.startswith("http://")):
                    errors.append(f"{where}: screenshot must be an http(s):// URL")

    # Templates must stay schema-valid too.
    examples = load("catalog/example-entry.json").get("examples", [])
    for i, ex in enumerate(examples):
        for e in sorted(validator.iter_errors(ex), key=lambda e: e.path):
            loc = "/".join(str(p) for p in e.path)
            errors.append(f"example-entry.json examples[{i}]: {loc + ': ' if loc else ''}{e.message}")

    if errors:
        print(f"CATALOG INVALID — {len(errors)} problem(s):", file=sys.stderr)
        for msg in errors:
            print(f"  - {msg}", file=sys.stderr)
        return 1

    print(f"catalog OK — {len(apps)} app(s), {len(examples)} template(s) validated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
