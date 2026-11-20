#!/usr/bin/env python3
"""Resolve pending catalog entries to a real release asset + sha256.

Standard library only — no `gh`, no login, no install beyond Python 3. It uses
the public GitHub API (anonymous works for public repos; 60 requests/hour).

Run it anywhere with Python 3 and internet:

    python3 scripts/resolve_pending.py           # print resolved URL + sha256
    python3 scripts/resolve_pending.py --apply    # also write them into index.json

For the simplest path, just run the dry run and paste the output back to whoever
maintains the catalog — they can fill it in. With a local checkout, --apply
updates catalog/index.json directly; then:

    python3 scripts/gen_romfs_catalog.py
    python3 scripts/validate.py

Optional: set GITHUB_TOKEN in the environment to raise the API rate limit.
"""
import hashlib
import json
import os
import pathlib
import re
import sys
import urllib.request

INDEX = pathlib.Path("catalog/index.json")
RAW_INDEX = "https://raw.githubusercontent.com/RetroVentures-Hayden/SnaccMart/main/catalog/index.json"
APPLY = "--apply" in sys.argv
UA = "SnaccMart-resolver"
TOKEN = os.environ.get("GITHUB_TOKEN", "")


def _open(url, accept):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": accept})
    if TOKEN:
        req.add_header("Authorization", f"Bearer {TOKEN}")
    return urllib.request.urlopen(req)  # noqa: S310 (github over https)


def api(path):
    try:
        with _open(f"https://api.github.com/{path}", "application/vnd.github+json") as r:
            return json.load(r)
    except Exception:
        return None


def load_index():
    if INDEX.exists():
        return json.loads(INDEX.read_text(encoding="utf-8")), True
    with _open(RAW_INDEX, "*/*") as r:
        return json.load(r), False


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
    with _open(url, "*/*") as r:
        for chunk in iter(lambda: r.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    data, local = load_index()
    apply = APPLY and local
    if APPLY and not local:
        print("note: --apply needs a local checkout; printing results only.\n")

    changed = 0
    for app in data.get("apps", []):
        if not app.get("pending"):
            continue
        aid = app["id"]
        owner, repo = repo_from_source(app.get("source"))
        if not owner:
            print(f"SKIP   {aid}: can't parse source repo")
            continue
        rel = api(f"repos/{owner}/{repo}/releases/latest")
        if not rel or "assets" not in rel:
            print(f"SKIP   {aid}: no latest release for {owner}/{repo}")
            continue
        asset, how = pick_asset(aid, rel["assets"])
        if not asset:
            print(f"MANUAL {aid}: {how} ({len(rel['assets'])} assets in {rel.get('tag_name')}) "
                  f"- pick one at {rel.get('html_url')}")
            continue
        url = asset["browser_download_url"]
        try:
            digest = sha256_url(url)
        except Exception as e:  # noqa: BLE001
            print(f"ERROR  {aid}: download failed: {e}")
            continue
        ftype = "zip" if asset["name"].lower().endswith(".zip") else "nro"
        print(f"OK     {aid}: {asset['name']} ({how})")
        print(f"        download: {url}")
        print(f"        sha256:   {digest}")
        if apply:
            app["download"] = url
            app["filetype"] = ftype
            app["sha256"] = digest
            app.pop("pending", None)
            if ftype != "zip":
                app.pop("binary", None)
            changed += 1

    if apply and changed:
        INDEX.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"\nUpdated {changed} entr(y/ies). Next: "
              f"python3 scripts/gen_romfs_catalog.py && python3 scripts/validate.py")


if __name__ == "__main__":
    main()
