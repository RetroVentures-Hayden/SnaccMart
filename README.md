# Switch Homebrew Store

A catalog of **free, open-source Switch homebrew** — tools, games, open-source
emulators and engine ports. It's a store for apps that run **on an
already-homebrew-enabled system (or an emulator)**, not a way to open a console.

> **Not included, by policy:** copyrighted ROMs/BIOS/game assets/firmware,
> commercial apps, and any CFW / bootloader / exploit payloads. See
> [`CONTRIBUTING.md`](CONTRIBUTING.md).

## How it's structured

```
switch-appstore/
├── catalog/index.json        # the store index (list of apps)
├── schema/package.schema.json# JSON Schema for one app entry
├── index.html                # web catalog front-end (reads catalog/index.json)
├── CONTRIBUTING.md           # submission rules + safety/malware review
└── README.md
```

The **index** is the heart of it: a machine-readable list any client can read —
the web page here, or an on-console client app. Each entry links to the app's
open-source repo and a downloadable `.nro`/`.zip`, with an optional `sha256` for
install-time verification.

## Viewing the catalog

- **Web:** host the repo on GitHub Pages (Settings → Pages → deploy from
  `main`, root) and open the site. The page fetches `catalog/index.json` and
  renders it.
- **Locally:** run a static server from the repo root (e.g.
  `python3 -m http.server`) and open `http://localhost:8000/` — opening
  `index.html` directly from disk won't fetch the JSON due to browser rules.

## Adding apps

Open a PR adding an object to `catalog/index.json` (copy a template; validate
against the schema). Every app must be open-source with a public `source` repo.
See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the full rules and the safety
review each submission gets.

## Roadmap

- [x] Index format + schema
- [x] Submission policy + safety review rules
- [x] Web catalog front-end
- [ ] JSON-schema validation in CI (reject malformed PRs automatically)
- [ ] On-console client app (libnx): browse + download + install to SD
- [ ] `sha256` verification on install
