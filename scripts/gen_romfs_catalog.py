#!/usr/bin/env python3
"""Convert catalog/index.json into a tab-separated file the on-console client
reads from romfs (romfs:/catalog.tsv), so the client needs no JSON parser.

One line per app, fields in this fixed order (tabs/newlines stripped):
  id  name  author  category  version  license  description  download  source  requires_assets(0|1)

Run from the repo root:  python3 scripts/gen_romfs_catalog.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "client" / "romfs" / "catalog.tsv"


def clean(v):
    return str(v if v is not None else "").replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()


def main():
    index = json.loads((ROOT / "catalog" / "index.json").read_text(encoding="utf-8"))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for a in index.get("apps", []):
        rows.append("\t".join([
            clean(a.get("id")),
            clean(a.get("name")),
            clean(a.get("author")),
            clean(a.get("category")),
            clean(a.get("version")),
            clean(a.get("license")),
            clean(a.get("description")),
            clean(a.get("download")),
            clean(a.get("source")),
            "1" if a.get("requires_assets") else "0",
        ]))
    OUT.write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(rows)} app(s))")


if __name__ == "__main__":
    main()
