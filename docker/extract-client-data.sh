#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 /path/to/WoW-3.3.5a-client"
  echo
  echo "Set SKIP_MMAPS=1 to extract only dbc/maps/vmaps for a faster first run."
  exit 1
fi

CLIENT_DIR="$(realpath "$1")"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="$ROOT_DIR/docker/data"
IMAGE="${TRINITY_IMAGE:-trinitycore/trinitycore:3.3.5}"

if [[ ! -d "$CLIENT_DIR/Data" ]]; then
  echo "Client Data directory not found: $CLIENT_DIR/Data"
  exit 1
fi

mkdir -p "$DATA_DIR"

docker run --rm \
  --user root \
  --entrypoint bash \
  -e SKIP_MMAPS="${SKIP_MMAPS:-0}" \
  -e HOST_UID="$(id -u)" \
  -e HOST_GID="$(id -g)" \
  -v "$CLIENT_DIR:/client:ro" \
  -v "$DATA_DIR:/data" \
  "$IMAGE" \
  -lc '
    set -euo pipefail

    cd /data
    rm -f Data
    ln -s /client/Data Data

    echo "Extracting dbc and maps..."
    mapextractor

    echo "Extracting raw vmap buildings..."
    vmap4extractor

    echo "Assembling vmaps..."
    mkdir -p vmaps
    vmap4assembler Buildings vmaps

    if [[ "${SKIP_MMAPS:-0}" != "1" ]]; then
      echo "Generating mmaps. This can take a long time."
      mkdir -p mmaps
      mmaps_generator --silent
    else
      echo "Skipping mmaps because SKIP_MMAPS=1."
    fi

    rm -f Data
    rm -rf Buildings
    chown -R "$HOST_UID:$HOST_GID" /data
  '

echo "Extracted data in $DATA_DIR"
