#!/usr/bin/env python3
"""Resolve pending catalog entries to a real release asset + sha256.

For every app in catalog/index.json marked "pending", this looks up the repo's
LATEST release, picks the .nro (or .zip) asset, downloads it, hashes it, and —
with --apply — fills in download/filetype/sha256 and clears pending.

Run it on a machine with the GitHub CLI (`gh`) authenticated and network access
(this cannot run in the restricted build sandbox). Then regenerate + commit:

    python3 scripts/resolve_pending.py           # dry run: report only
    python3 scripts/resolve_pending.py --apply    # update catalog/index.json
    python3 scripts/gen_romfs_catalog.py          # refresh catalog.tsv
    python3 scripts/validate.py

Asset pick order: exact "<id>.nro" > a single .nro > a single .zip. If a release
has several plausible assets (e.g. RetroArch's per-core builds), it is reported
as AMBIGUOUS and left pending for you to resolve by hand.
"""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
INDEX = ROOT / "catalog" / "index.json"
APPLY = "--apply" in sys.argv


def gh_json(path):
    out = subprocess.run(["gh", "api", path], capture_output=True, text=True)
    if out.returncode != 0:
        return None
    return json.loads(out.stdout)


def repo_from_source(url):
    m = re.match(r"https://github\.com/([^/]+)/([^/]+?)(?:\.git)?/?$", url or "")
    return (m.group(1), m.group(2)) if m else (None, None)


def pick_asset(app_id, assets):
    nros = [a for a in assets if a["name"].lower().endswith(".nro")]
    zips = [a for a in assets if a["name"].lower().endswith(".zip")]
    for a in nros:
        if a["name"].lower() == f"{app_id}.nro":
            return a, "exact"
    if len(nros) == 1:
        return nros[0], "nro"
    if not nros and len(zips) == 1:
        return zips[0], "zip"
    return None, ("ambiguous" if (nros or zips) else "none")


def sha256_url(url):
    h = hashlib.sha256()
    with urllib.request.urlopen(url) as r:  # noqa: S310 (trusted github URL)
        for chunk in iter(lambda: r.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    data = json.loads(INDEX.read_text(encoding="utf-8"))
    changed = 0
    for app in data["apps"]:
        if not app.get("pending"):
            continue
        aid = app["id"]
        owner, repo = repo_from_source(app.get("source"))
        if not owner:
            print(f"SKIP   {aid}: can't parse source repo")
            continue
        rel = gh_json(f"repos/{owner}/{repo}/releases/latest")
        if not rel or "assets" not in rel:
            print(f"SKIP   {aid}: no latest release for {owner}/{repo}")
            continue
        asset, how = pick_asset(aid, rel["assets"])
        if not asset:
            print(f"MANUAL {aid}: {how} — {len(rel['assets'])} assets in {rel.get('tag_name')}")
            continue
        url = asset["browser_download_url"]
        try:
            digest = sha256_url(url)
        except Exception as e:  # noqa: BLE001
            print(f"ERROR  {aid}: download failed: {e}")
            continue
        ftype = "zip" if asset["name"].lower().endswith(".zip") else "nro"
        print(f"OK     {aid}: {asset['name']} ({how})\n        {url}\n        sha256={digest}")
        if APPLY:
            app["download"] = url
            app["filetype"] = ftype
            app["sha256"] = digest
            app.pop("pending", None)
            if ftype != "zip":
                app.pop("binary", None)
            changed += 1

    if APPLY and changed:
        INDEX.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"\nUpdated {changed} entr(y/ies) in {INDEX.relative_to(ROOT)}. "
              f"Now run: python3 scripts/gen_romfs_catalog.py && python3 scripts/validate.py")
    elif APPLY:
        print("\nNothing applied.")


if __name__ == "__main__":
    main()
