# SnaccMart self-host mirror

A tiny container that keeps a **live copy of the SnaccMart catalog** on your own
server (e.g. ZimaOS) and serves it over HTTP. It clones the repo, serves it, and
`git pull`s on an interval, so edits you push (or make on a fork) show up without
rebuilding anything.

## What it serves

- **Web catalog:** `http://<host>:8080/`
- **Machine feed (for the on-console client):** `http://<host>:8080/catalog/catalog.tsv`

> It mirrors the **catalog** (the list of apps), which is the part you'll iterate
> on when testing new entries. App **binaries** still download from each app's own
> `download` URL — the mirror doesn't rehost them.

## Deploy on ZimaOS

Add a custom app from `docker-compose.yml`, or from a shell:

```bash
docker compose -f mirror/docker-compose.yml up -d --build
```

Configure via environment variables (in the compose file):

| Var | Default | Meaning |
|-----|---------|---------|
| `REPO_URL` | this repo | Git repo to mirror (point at your fork if you like) |
| `BRANCH` | `main` | Branch to track |
| `INTERVAL` | `300` | Seconds between `git pull`s |
| `PORT` | `8080` | HTTP port inside the container |

## Point the console client at it

On the Switch SD card, create:

```
sdmc:/switch/homebrew-store/catalog_url.txt
```

containing one line:

```
http://<your-server-ip>:8080/catalog/catalog.tsv
```

The networked client reads that URL at startup instead of the GitHub default, so
your console pulls the catalog from your own mirror. If the mirror is
unreachable, the client falls back to the catalog bundled in the `.nro`.

## Security note (read this)

The mirror serves plain **HTTP**. That is fine on a **trusted LAN**, but be aware:

- **`sha256` does not protect a swapped catalog.** The per-app checksums live
  *inside* the catalog, so anyone who can modify the HTTP response (someone on
  your network, a rogue DHCP/DNS, an open Wi-Fi) can replace both an app's
  download URL **and** its hash. The checksum only protects against accidental
  corruption and a tampered binary at a *trusted* URL — not against a tampered
  catalog.
- **The default (GitHub raw over HTTPS) is safe** precisely because the
  transport is authenticated. A mirror trades that away for locality.
- If you expose the mirror beyond your LAN, **put it behind a reverse proxy with
  TLS** (Caddy/Traefik/nginx) and use the `https://` URL on the console.

Everything downloaded still comes from each app's own (HTTPS) `download` URL
unless the catalog points elsewhere — so keep your mirror's network trusted.

## Notes

- The container keeps its checkout in a named volume, so restarts are fast and
  survive redeploys.
