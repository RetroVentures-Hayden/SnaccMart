#!/bin/sh
# Clone SnaccMart, serve it over HTTP, and git-pull on an interval so the
# catalog stays current. Point the on-console client at:
#   http://<host>:<PORT>/catalog/catalog.tsv
set -eu

REPO_URL="${REPO_URL:-https://github.com/RetroVentures-Hayden/SnaccMart}"
BRANCH="${BRANCH:-main}"
INTERVAL="${INTERVAL:-300}"   # seconds between pulls
PORT="${PORT:-8080}"
DIR=/srv/snaccmart

if [ ! -d "$DIR/.git" ]; then
  echo "[mirror] cloning $REPO_URL ($BRANCH)"
  git clone --depth 1 -b "$BRANCH" "$REPO_URL" "$DIR"
else
  echo "[mirror] existing checkout at $DIR"
fi

echo "[mirror] serving $DIR on :$PORT (catalog at /catalog/catalog.tsv)"
busybox httpd -f -p "$PORT" -h "$DIR" &
HTTPD=$!
trap 'kill "$HTTPD" 2>/dev/null || true' INT TERM

while true; do
  sleep "$INTERVAL"
  # Keep httpd alive if it died.
  if ! kill -0 "$HTTPD" 2>/dev/null; then
    busybox httpd -f -p "$PORT" -h "$DIR" &
    HTTPD=$!
  fi
  if git -C "$DIR" pull --ff-only origin "$BRANCH" >/dev/null 2>&1; then
    echo "[mirror] $(date -u +%FT%TZ) synced"
  else
    echo "[mirror] $(date -u +%FT%TZ) pull failed (will retry)"
  fi
done
