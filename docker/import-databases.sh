#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPOSE_FILE="$ROOT_DIR/docker/docker-compose.yml"
DOWNLOAD_DIR="$ROOT_DIR/docker/downloads"
TDB_ARCHIVE="TDB_full_world_335.25101_2025_10_21.7z"
TDB_DIR="$DOWNLOAD_DIR/TDB_full_world_335.25101_2025_10_21"
TDB_SQL="$TDB_DIR/TDB_full_world_335.25101_2025_10_21.sql"

cd "$ROOT_DIR"

docker compose -f "$COMPOSE_FILE" up -d mysql

echo "Waiting for MySQL..."
until docker compose -f "$COMPOSE_FILE" exec -T mysql mysqladmin ping -utrinity -ptrinity --silent >/dev/null 2>&1; do
  sleep 2
done

echo "Importing auth..."
docker compose -f "$COMPOSE_FILE" exec -T mysql mysql -utrinity -ptrinity auth \
  < "$ROOT_DIR/sql/base/auth_database.sql"

echo "Importing characters..."
docker compose -f "$COMPOSE_FILE" exec -T mysql mysql -utrinity -ptrinity characters \
  < "$ROOT_DIR/sql/base/characters_database.sql"

mkdir -p "$DOWNLOAD_DIR"

if [[ ! -f "$DOWNLOAD_DIR/$TDB_ARCHIVE" ]]; then
  echo "Downloading TDB..."
  gh release download TDB335.25101 \
    --repo TrinityCore/TrinityCore \
    --pattern "$TDB_ARCHIVE" \
    --dir "$DOWNLOAD_DIR"
fi

if [[ ! -f "$TDB_SQL" ]]; then
  echo "Extracting TDB..."
  7z x -y "$DOWNLOAD_DIR/$TDB_ARCHIVE" -o"$TDB_DIR"
fi

echo "Importing world..."
docker compose -f "$COMPOSE_FILE" exec -T mysql mysql -utrinity -ptrinity world < "$TDB_SQL"

docker compose -f "$COMPOSE_FILE" exec -T mysql mysql -utrinity -ptrinity -e \
  "SELECT 'auth' db, COUNT(*) tables_count FROM information_schema.tables WHERE table_schema='auth'
   UNION ALL SELECT 'characters', COUNT(*) FROM information_schema.tables WHERE table_schema='characters'
   UNION ALL SELECT 'world', COUNT(*) FROM information_schema.tables WHERE table_schema='world';"
