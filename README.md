<div align="center">

# 🛒 SnaccMart

**A catalog of free, open-source Nintendo Switch homebrew.**

Tools · Games · Open-source emulators · Engine ports — browsable on the web and on the console itself.

[![validate](https://github.com/RetroVentures-Hayden/SnaccMart/actions/workflows/validate.yml/badge.svg)](https://github.com/RetroVentures-Hayden/SnaccMart/actions/workflows/validate.yml)
[![client](https://github.com/RetroVentures-Hayden/SnaccMart/actions/workflows/client.yml/badge.svg)](https://github.com/RetroVentures-Hayden/SnaccMart/actions/workflows/client.yml)
![license](https://img.shields.io/badge/license-MIT-blue)

</div>

---

> ### ⚠️ What SnaccMart is — and isn't
> SnaccMart distributes **free, open-source homebrew** that runs on a Switch which
> is **already** homebrew-enabled (or on an emulator). It is **not**:
> - a jailbreak, custom firmware, bootloader, or exploit — it doesn't *open* a console, and it won't host the software that does;
> - a source of copyrighted ROMs, BIOS images, game assets, or firmware;
> - a home for commercial or closed-source apps.
>
> An emulator is welcome; the games that run on it are **not** bundled — you bring your own legally-owned data. See [`CONTRIBUTING.md`](CONTRIBUTING.md).

> ⚠️ **Disclaimer:** The software needs testing, and it might not work properly!
> If that's the case, please [create an Issue on the repo](https://github.com/RetroVentures-Hayden/SnaccMart/issues/new)
> and I will fix it.

---

## What's in the box

SnaccMart is built around one simple idea: a **machine-readable catalog** that many
front-ends can read.

- **The catalog** (`catalog/index.json`) — a list of apps, each with its
  open-source repo, a downloadable `.nro`/`.zip`, license, category, and an
  optional checksum. Validated against a strict [JSON Schema](schema/package.schema.json).
- **A web catalog** (`index.html`) — a dependency-free page that renders the
  catalog with search, category filters, and light/dark themes. Host it on GitHub
  Pages or anywhere static.
- **An on-console client** (`client/`) — `homebrew-store.nro`, a libnx app that
  browses the catalog on the Switch itself.
- **CI** — every change to the catalog is schema-validated, and the client binary
  is built automatically.

## Repository layout

```
SnaccMart/
├── catalog/
│   ├── index.json          # the store catalog (the source of truth)
│   └── example-entry.json  # copy-paste templates for new apps
├── schema/
│   └── package.schema.json # JSON Schema every app entry must satisfy
├── scripts/
│   ├── validate.py         # catalog validator (run in CI)
│   └── gen_romfs_catalog.py# index.json -> client/romfs/catalog.tsv
├── client/                 # on-console libnx client (homebrew-store.nro)
│   ├── source/main.cpp
│   ├── Makefile
│   └── icon.jpg
├── index.html              # web catalog front-end
├── CONTRIBUTING.md         # submission rules + safety review
└── .github/workflows/      # validate.yml, client.yml
```

## The catalog format

Each app is one object in `catalog/index.json`'s `apps` array. Required fields:
`id`, `name`, `author`, `category`, `license`, `source`, `version`, `download`,
`filetype`. Open-source only — a public `source` repo and an SPDX `license` are
**mandatory**. Optional but encouraged: `sha256` (verified on install),
`icon`, `screenshots`, `requires_assets` (for emulators/ports that need
user-supplied data). Full field list: [`schema/package.schema.json`](schema/package.schema.json).

## Adding an app

1. Fork the repo.
2. Copy an object from [`catalog/example-entry.json`](catalog/example-entry.json)
   into `catalog/index.json`'s `apps` array and fill it in.
3. Validate locally: `pip install jsonschema && python3 scripts/validate.py`.
4. Open a PR. CI validates automatically; a maintainer runs the
   [safety review](CONTRIBUTING.md#safety-review-malware--virus-policy) before merging.

## The web catalog

Host the repo on **GitHub Pages** (Settings → Pages → deploy from `main`, root)
and open the site — `index.html` fetches `catalog/index.json` and renders it.
Locally, serve from the repo root (`python3 -m http.server`) rather than opening
the file directly (browsers block `fetch()` from `file://`).

## The on-console client

`client/` builds `homebrew-store.nro`. The catalog is converted from
`index.json` into a compact `romfs/catalog.tsv` at build time
(`scripts/gen_romfs_catalog.py`) and bundled, so the app needs no JSON parser.

```bash
python3 scripts/gen_romfs_catalog.py   # refresh the bundled catalog
cd client && make                      # -> client/homebrew-store.nro
```

| Version | Status |
|---------|--------|
| v0.1 | **Browse** the catalog + per-app detail view ✅ |
| v0.2 | **Download + install to SD**, **live catalog fetch**, **sha256 verify**, **.zip extract** over HTTPS ✅ (compiles; on-device test pending) |
| later | richer UI · screenshots · categories in the client |

The networked client (built by the **`client-net`** workflow, which adds the
`switch-curl` portlib + CA bundle) fetches the catalog live from
`catalog/catalog.tsv` at startup and falls back to the bundled copy when offline.
You can override the source with an **`https://`** URL in
`sdmc:/switch/homebrew-store/catalog_url.txt` (non-HTTPS overrides are ignored —
see below). The default `client` build stays dependency-free (browse-only) so
it's always green.

## Safety & malware policy

The goal is "every non-malware homebrew app," backed by a real review, not a
promise:

- **Open-source & buildable** — public `source` required; no binary-only drops.
- **Prefer CI-built binaries** — a download from the project's own public CI is
  preferred, so the binary provably matches the source.
- **Checksums** — provide `sha256`; it's verified on install (v0.2+).
- **No obfuscation / no remote code loading** — entries whose source hides
  payloads, runs fake-activity bots, or says "download and run this binary" are
  rejected.
- **Reporting** — open an Issue to flag an app; flagged apps are pulled pending review.

This is best-effort human review, not a guarantee. Install at your own risk.

## Self-hosting the catalog

The catalog is plain static JSON, so you can serve your own copy. The on-console
client only accepts an **HTTPS** catalog URL (via
`sdmc:/switch/homebrew-store/catalog_url.txt`); a plain-HTTP source is ignored
and it falls back to the default. This is deliberate: an unauthenticated catalog
can be swapped in transit — `sha256` hashes included, since they travel inside
the catalog — so self-hosting must be behind TLS to be safe. (An earlier
plain-HTTP mirror container was removed for this reason.)

## Roadmap

- [x] Catalog format + JSON Schema
- [x] Submission policy + safety review
- [x] Web catalog front-end
- [x] CI schema validation
- [x] On-console client — browse
- [x] On-console download + install to SD (v0.2)
- [x] Live catalog fetch over the network (HTTPS only)
- [x] `sha256` verification on install (rejects tampered downloads)
- [x] `.zip` package support (extract to install path, CRC-checked, traversal-guarded)

## License

MIT — see [`LICENSE`](LICENSE). Each listed app keeps its own license; SnaccMart
only indexes and links to them.
