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
# Two outputs from the same data:
#   - client/romfs/catalog.tsv : bundled into the .nro (offline fallback)
#   - catalog/catalog.tsv      : committed + published at a stable URL for the
#                                client to fetch live (kept in sync by CI)
OUT_ROMFS = ROOT / "client" / "romfs" / "catalog.tsv"
OUT_PUBLISHED = ROOT / "catalog" / "catalog.tsv"


def clean(v):
    return str(v if v is not None else "").replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()


def main():
    index = json.loads((ROOT / "catalog" / "index.json").read_text(encoding="utf-8"))
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
    body = "\n".join(rows) + ("\n" if rows else "")
    for out in (OUT_ROMFS, OUT_PUBLISHED):
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(body, encoding="utf-8")
    print(f"wrote catalog.tsv to romfs + published ({len(rows)} app(s))")


if __name__ == "__main__":
    main()
