# Submitting an app

Apps are added by proposing a change to `catalog/index.json` (one object per
app, validated against `schema/package.schema.json`).

## What is allowed

- **Free, open-source homebrew** — tools, games, utilities, media apps.
- **Open-source emulators** and **open-source game-engine ports / source ports**.
  These ship the *app only*. If the app needs game data to be useful, set
  `"requires_assets": true` — the **user supplies their own legally-owned data**;
  the store never distributes it.

Every entry **must** have:
- a public `source` repository (open-source only), and
- an open-source `license` (SPDX id).

## What is NOT allowed

- **Copyrighted ROMs, BIOS images, game assets, or firmware.** An emulator is
  fine; the games are not.
- **Commercial or closed-source apps.**
- **CFW, bootloaders, exploit payloads, or entry-point/"acquisition" tools.**
  This is a store for apps that run *on* an already-homebrew-enabled system — not
  for the software that opens the system.
- **Malware / anything that behaves maliciously** (see below).

Submissions that don't meet these rules are rejected.

## Safety review (malware / virus policy)

Because the goal is "every non-malware homebrew app," each submission is
checked before listing:

1. **Source must be public and buildable.** No opaque binary-only drops. A
   release binary should be reproducible from the linked `source`.
2. **Prefer CI-built binaries.** A `download` produced by the project's own
   public CI (e.g. a GitHub Actions release asset) is strongly preferred over a
   hand-uploaded binary, so the binary provably comes from the source.
3. **Checksums.** Provide `sha256` for the download; it's verified on install.
4. **No obfuscation / no remote code loading.** Entries whose source contains
   obfuscated payloads, fake-activity bots, or "download and run this binary"
   patterns are rejected. (Yes, this has already come up.)
5. **Reporting.** Anyone can open an Issue to flag an app; flagged apps are
   pulled pending review.

This is a best-effort human review, not a guarantee. Users install at their own
risk; checksums and the open-source requirement are the main protections.

## How to add an app

1. Fork the repo.
2. Copy an object from `catalog/example-entry.json` into the `apps` array in
   `catalog/index.json`.
3. Fill every required field; it is validated against `schema/package.schema.json`.
4. Open a pull request. CI validates the JSON automatically (`scripts/validate.py`);
   a maintainer runs the safety review above before merging.

Validate locally before opening the PR:

```bash
pip install jsonschema
python3 scripts/validate.py
```
